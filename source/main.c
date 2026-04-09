#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>

#include "camera.h"
#include "ui.h"
#include "input.h"
#include "filter.h"
#include "lomo.h"
#include "image_load.h"
#include "wigglegram.h"
#include "sticker.h"
#include "settings.h"
#include "sound.h"

// Composite callback for save_edited_apng — applies placed stickers + frame
typedef struct {
    PlacedSticker *stickers;
    int            n_stickers;
    int            frame_idx;    // gallery_frame (-1 = none)
    const char    *frame_path;
} EditCompositeCtx;

static void edit_composite_cb(uint8_t *rgb888, int w, int h, void *ud) {
    EditCompositeCtx *ctx = (EditCompositeCtx *)ud;
    for (int si = 0; si < ctx->n_stickers; si++) {
        if (!ctx->stickers[si].active) continue;
        const unsigned char *px = get_sticker_pixels(ctx->stickers[si].cat_idx,
                                                      ctx->stickers[si].icon_idx);
        if (px)
            composite_sticker_rgb888(rgb888, w, h, px,
                                     ctx->stickers[si].x, ctx->stickers[si].y,
                                     ctx->stickers[si].scale, ctx->stickers[si].angle_deg);
    }
    if (ctx->frame_idx >= 0 && ctx->frame_path)
        composite_frame_rgb888(rgb888, w, h, ctx->frame_path);
}

#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define WAIT_TIMEOUT 1000000000ULL

static jmp_buf exitJmp;

// ---------------------------------------------------------------------------
// Background save thread
// ---------------------------------------------------------------------------

#define SAVE_THREAD_STACK_SIZE (32 * 1024)

typedef struct {
    uint8_t      *snapshot_buf;    // malloc'd once, CAMERA_SCREEN_SIZE bytes (RGB565) — left/main cam
    uint8_t      *snapshot_buf2;   // malloc'd once, CAMERA_SCREEN_SIZE bytes (RGB565) — right cam (wiggle)
    char          save_path[64];
    int           save_scale;
    bool          wiggle_mode;     // true = save APNG from both cam buffers
    int           wiggle_n_frames;
    int           wiggle_delay_ms;
    WiggleAlign   wiggle_align_result;
    bool          wiggle_has_align;
    int           wiggle_offset_dx;
    int           wiggle_offset_dy;
    volatile bool busy;            // main sets true on trigger; worker clears on finish
    volatile bool quit;            // main sets true at shutdown
    LightEvent    request_event;   // RESET_ONESHOT: main signals worker to start
    LightEvent    done_event;      // RESET_ONESHOT: worker signals when finished
} SaveThreadState;

static SaveThreadState s_save;

static void save_thread_func(void *arg) {
    SaveThreadState *st = (SaveThreadState *)arg;
    uint8_t *rgb_priv     = malloc(CAMERA_WIDTH * CAMERA_HEIGHT * 3);
    uint8_t *upscale_priv = malloc(SAVE_SCALE * CAMERA_WIDTH * SAVE_SCALE * CAMERA_HEIGHT * 3);
    if (!rgb_priv || !upscale_priv) {
        free(rgb_priv);
        free(upscale_priv);
        threadExit(1);
    }
    while (true) {
        LightEvent_Wait(&st->request_event);
        if (st->quit) break;

        char path[64];
        memcpy(path, st->save_path, sizeof(path));

        if (st->wiggle_mode) {
            save_wiggle_apng(path,
                             st->snapshot_buf,  CAMERA_WIDTH, CAMERA_HEIGHT,
                             st->snapshot_buf2,
                             st->wiggle_n_frames,
                             st->wiggle_delay_ms,
                             NULL,
                             st->wiggle_offset_dx,
                             st->wiggle_offset_dy);
        } else {
            int scale = st->save_scale;
            rgb565_to_rgb888(rgb_priv, (const uint16_t *)st->snapshot_buf,
                             CAMERA_WIDTH * CAMERA_HEIGHT);
            nn_upscale(upscale_priv, rgb_priv, CAMERA_WIDTH, CAMERA_HEIGHT, scale);
            save_jpeg(path, upscale_priv, CAMERA_WIDTH * scale, CAMERA_HEIGHT * scale);
        }

        st->busy = false;
        LightEvent_Signal(&st->done_event);
    }
    free(rgb_priv);
    free(upscale_priv);
    threadExit(0);
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void cleanup(void) {
    sound_exit();
    C2D_Fini();
    C3D_Fini();
    camExit();
    gfxExit();
    acExit();
    romfsExit();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    romfsInit();
    acInit();
    sound_init();  // gracefully no-ops if csnd unavailable
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP,    true);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    C2D_TextBuf staticBuf = C2D_TextBufNew(512);
    C2D_TextBuf dynBuf    = C2D_TextBufNew(64);

    if (setjmp(exitJmp)) { cleanup(); return 0; }

    // Camera init
    u32 camSelect = SELECT_OUT1_OUT2;
    bool selfie = false;

    camInit();
    CAMU_SetSize(camSelect, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(camSelect, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(camSelect, FRAME_RATE_30);
    CAMU_SetNoiseFilter(camSelect, true);
    CAMU_SetAutoExposure(camSelect, true);
    CAMU_SetAutoWhiteBalance(camSelect, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_SetTrimming(PORT_CAM2, false);

    // Buffers
    u8 *buf          = malloc(CAMERA_BUF_SIZE);
    u8 *filtered_buf = malloc(CAMERA_SCREEN_SIZE);
    u8 *rgb_buf      = malloc(CAMERA_WIDTH * CAMERA_HEIGHT * 3);
    s_save.snapshot_buf  = malloc(CAMERA_SCREEN_SIZE);
    s_save.snapshot_buf2 = malloc(CAMERA_SCREEN_SIZE);
    // Wiggle preview buffers (true-colour RGB565, no filter applied)
    u8 *wiggle_left  = malloc(CAMERA_SCREEN_SIZE);
    u8 *wiggle_right = malloc(CAMERA_SCREEN_SIZE);
    if (!buf || !filtered_buf || !rgb_buf ||
        !s_save.snapshot_buf || !s_save.snapshot_buf2 ||
        !wiggle_left || !wiggle_right) longjmp(exitJmp, 1);
    memset(filtered_buf, 0, CAMERA_SCREEN_SIZE);

    // Filter params
    FilterParams params         = FILTER_DEFAULTS;
    FilterParams default_params = FILTER_DEFAULTS;
    int          save_scale     = 2;
    int          active_tab     = 0;

    // Mutable palette copies + palette tab state
    PaletteDef   user_palettes[PALETTE_COUNT];
    FilterRanges ranges          = FILTER_RANGES_DEFAULTS;
    int          settings_row    = 0;   // 0=Save Scale, 1=Dither, 2=Invert
    int          palette_sel_pal   = 0;
    int          palette_sel_color = 0;

    // Shoot mode state
    int  shoot_mode       = SHOOT_MODE_GBCAM;
    bool shoot_mode_open  = false;
    bool timer_open       = false;
    int  shoot_timer_secs = 0;  // 0 = disabled
    int  wiggle_frames    = 4;
    int  wiggle_delay_ms  = 250;
    int  lomo_preset      = 0;  // index into lomo_presets[]

    // Timer countdown state
    bool timer_active       = false;
    int  timer_remaining_ms = 0;
    u64  timer_prev_tick    = 0;

    // Wiggle capture/preview state
    bool wiggle_preview      = false;  // true while showing captured pair before saving
    int  wiggle_preview_frame = 0;     // current frame index cycling through preview frames
    u64  wiggle_preview_last_tick = 0; // svcGetSystemTick() at last frame advance
    static uint16_t wiggle_preview_frames[WIGGLE_PREVIEW_MAX][CAMERA_WIDTH * CAMERA_HEIGHT];
    static uint16_t wiggle_compose_buf[CAMERA_WIDTH * CAMERA_HEIGHT]; // full-size blit target
    static WiggleAlign wiggle_align_res; // auto-detected alignment
    bool wiggle_has_align = false;       // true once wiggle_align has run for current capture
    int  wiggle_offset_dx = 0;         // user H adjustment (pixels, added on top of auto)
    int  wiggle_offset_dy = 0;         // user V adjustment (pixels)
    bool wiggle_rebuild   = false;     // set when offsets change mid-preview
    int  wiggle_crop_w    = CAMERA_WIDTH;   // actual frame width after overlap crop
    int  wiggle_crop_h    = CAMERA_HEIGHT;  // actual frame height after overlap crop
    int  wiggle_dpad_repeat = 0;       // frame counter for d-pad repeat delay

    // Gallery state
    #define GALLERY_MAX 256
    bool gallery_mode   = false;
    int  gallery_sel    = 0;
    int  gallery_scroll = 0;
    static char gallery_paths[GALLERY_MAX][64];
    int  gallery_count  = 0;
    #define GALLERY_WIGGLE_MAX_FRAMES 8
    static uint16_t gallery_thumbs[GALLERY_WIGGLE_MAX_FRAMES][CAMERA_WIDTH * CAMERA_HEIGHT];
    int  gallery_n_frames   = 1;   // 1 = still image, >1 = wiggle animation
    int  gallery_delay_ms   = 250;
    u64  gallery_anim_tick  = 0;   // svcGetSystemTick() at last frame advance
    int  gallery_anim_frame = 0;
    int  gallery_loaded = -1;

    // Gallery edit mode state
    bool gallery_edit_mode = false;
    int  edit_tab          = 0;     // 0 = stickers, 1 = frames
    int  sticker_cat       = 0;     // active category index
    int  sticker_sel       = 0;     // currently highlighted icon in active category
    int  sticker_scroll    = 0;     // row scroll offset in picker grid
    int  gallery_frame     = -1;    // active frame index (-1 = none)
    PlacedSticker placed_stickers[STICKER_MAX];
    for (int i = 0; i < STICKER_MAX; i++) placed_stickers[i].active = false;
    static uint8_t edit_preview_rgb888[CAMERA_WIDTH * CAMERA_HEIGHT * 3];
    int  edit_save_flash   = 0;     // countdown for "Saved!" display
    // Placement cursor: position for the next sticker to be placed
    float sticker_cursor_x   = (float)CAMERA_WIDTH  / 2.0f;
    float sticker_cursor_y   = (float)CAMERA_HEIGHT / 2.0f;
    float sticker_pending_scale = 2.0f;   // default 2x (32px displayed on 400px photo)
    float sticker_pending_angle = 0.0f;   // degrees
    bool  sticker_placing    = false;     // true = sticker "picked up", A confirms, B cancels

    u32 bufSize;
    CAMU_GetMaxBytes(&bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
    CAMU_SetTransferBytes(PORT_BOTH, bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
    CAMU_Activate(SELECT_OUT1_OUT2);

    Handle camReceiveEvent[4] = {0};
    bool captureInterrupted = false;
    bool cam_active = true;   // false when camera is stopped for gallery/edit
    s32 index = 0;

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
    CAMU_ClearBuffer(PORT_BOTH);
    CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);


    int save_flash     = 0;
    int settings_flash = 0;
    int frame_count    = 0;

    for (int i = 0; i < PALETTE_COUNT; i++) user_palettes[i] = palettes[i];
    settings_load(&params, &save_scale);
    settings_load_palettes(user_palettes);
    settings_load_ranges(&ranges);
    default_params = params;
    filter_set_user_palettes(user_palettes);
    // Clamp live params to loaded ranges
    if (params.brightness  < ranges.bright_min)   params.brightness  = ranges.bright_min;
    if (params.brightness  > ranges.bright_max)   params.brightness  = ranges.bright_max;
    if (params.contrast    < ranges.contrast_min) params.contrast    = ranges.contrast_min;
    if (params.contrast    > ranges.contrast_max) params.contrast    = ranges.contrast_max;
    if (params.saturation  < ranges.sat_min)      params.saturation  = ranges.sat_min;
    if (params.saturation  > ranges.sat_max)      params.saturation  = ranges.sat_max;
    if (params.gamma       < ranges.gamma_min)    params.gamma       = ranges.gamma_min;
    if (params.gamma       > ranges.gamma_max)    params.gamma       = ranges.gamma_max;

    // Seed shared file counter — reads INI then cross-checks against dir scan
    file_counter_init(SAVE_DIR, settings_load_file_counter());

    // Initialize and launch the background save thread on core 1
    LightEvent_Init(&s_save.request_event, RESET_ONESHOT);
    LightEvent_Init(&s_save.done_event,    RESET_ONESHOT);
    s_save.busy = false;
    s_save.quit = false;
    APT_SetAppCpuTimeLimit(30);  // unlock core 1; keep low to avoid cache pressure on camera DMA
    Thread save_thread = threadCreate(save_thread_func, &s_save,
                                      SAVE_THREAD_STACK_SIZE, 0x3F, 1, false);
    if (!save_thread) longjmp(exitJmp, 1);

    while (aptMainLoop()) {

        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

        bool do_save = false;

        if (!captureInterrupted) {
            // Physical button fallbacks (skip in gallery/edit mode — buttons have different roles)
            if (!gallery_mode && !gallery_edit_mode) {
            if (kDown & KEY_L) {
                params.palette = (params.palette <= PALETTE_NONE)
                               ? PALETTE_COUNT - 1 : params.palette - 1;
            }
            if (kDown & KEY_R) {
                params.palette = (params.palette >= PALETTE_COUNT - 1)
                               ? PALETTE_NONE : params.palette + 1;
            }
            if (kDown & KEY_B) {
                params.pixel_size = (params.pixel_size % PX_STOPS) + 1;
            }
            } // end !gallery_mode && !gallery_edit_mode
            // X: cycle through main tabs (Shoot → Style → FX → More → Shoot)
            // Disabled in gallery/edit mode to avoid accidental tab switches
            if ((kDown & KEY_X) && !gallery_mode && !gallery_edit_mode) {
                if (active_tab <= TAB_MORE)
                    active_tab = (active_tab + 1) % (TAB_MORE + 1);
            }
            // D-pad: context-aware per active_tab
            if (gallery_edit_mode && active_tab == TAB_SHOOT) {
                // Edit mode sticker tab — two-step placement flow:
                //   Picker mode:  D-pad U/D = scroll, A = "pick up" selected sticker
                //   Placing mode: Circle pad = move cursor, A = place, B = cancel
                if (edit_tab == 0) {
                    if (sticker_placing) {
                        // ---- Placement mode: sticker is "picked up" ----
                        // Circle pad moves cursor with deadzone
                        circlePosition cp;
                        hidCircleRead(&cp);
                        #define CP_DEADZONE 12
                        float dx = (cp.dx > CP_DEADZONE) ? (float)(cp.dx - CP_DEADZONE) :
                                   (cp.dx < -CP_DEADZONE) ? (float)(cp.dx + CP_DEADZONE) : 0.0f;
                        float dy = (cp.dy > CP_DEADZONE) ? (float)(cp.dy - CP_DEADZONE) :
                                   (cp.dy < -CP_DEADZONE) ? (float)(cp.dy + CP_DEADZONE) : 0.0f;
                        // Speed: ~3 px/frame at full deflection (crosses 400px in ~2s at 60fps)
                        // dx max after deadzone = 156-12 = 144; target = 3px/frame
                        // factor = 3 / (144 * CAMERA_WIDTH) but expressed simply:
                        sticker_cursor_x += dx * 3.0f / 144.0f;
                        sticker_cursor_y -= dy * 3.0f / 144.0f;
                        if (sticker_cursor_x < 0) sticker_cursor_x = 0;
                        if (sticker_cursor_x >= CAMERA_WIDTH)  sticker_cursor_x = (float)(CAMERA_WIDTH  - 1);
                        if (sticker_cursor_y < 0) sticker_cursor_y = 0;
                        if (sticker_cursor_y >= CAMERA_HEIGHT) sticker_cursor_y = (float)(CAMERA_HEIGHT - 1);
                        #undef CP_DEADZONE

                        // L / R (held) — scale smaller / larger
                        if (kHeld & KEY_L) {
                            sticker_pending_scale -= 0.03f;
                            if (sticker_pending_scale < 0.5f) sticker_pending_scale = 0.5f;
                        }
                        if (kHeld & KEY_R) {
                            sticker_pending_scale += 0.03f;
                            if (sticker_pending_scale > 8.0f) sticker_pending_scale = 8.0f;
                        }

                        // D-pad L/R — rotate by 15°
                        if (kDown & KEY_DLEFT)  { sticker_pending_angle -= 15.0f; if (sticker_pending_angle <   0.0f) sticker_pending_angle += 360.0f; }
                        if (kDown & KEY_DRIGHT) { sticker_pending_angle += 15.0f; if (sticker_pending_angle >= 360.0f) sticker_pending_angle -= 360.0f; }

                        // A — confirm: place sticker centered on cursor
                        if (kDown & KEY_A) {
                            for (int si = 0; si < STICKER_MAX; si++) {
                                if (!placed_stickers[si].active) {
                                    placed_stickers[si].active    = true;
                                    placed_stickers[si].cat_idx   = sticker_cat;
                                    placed_stickers[si].icon_idx  = sticker_sel;
                                    placed_stickers[si].x         = (int)sticker_cursor_x;
                                    placed_stickers[si].y         = (int)sticker_cursor_y;
                                    placed_stickers[si].scale     = sticker_pending_scale;
                                    placed_stickers[si].angle_deg = sticker_pending_angle;
                                    break;
                                }
                            }
                            sticker_placing = false;
                        }
                        // B — cancel placement (no sticker placed)
                        if (kDown & KEY_B) {
                            sticker_placing = false;
                        }
                    } else {
                        // ---- Picker mode: browse stickers ----
                        // D-pad U/D — scroll picker rows
                        sticker_cat_load(sticker_cat);
                        int total_icons = sticker_cats[sticker_cat].count;
                        int total_rows  = (total_icons + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
                        int max_scroll  = total_rows - GEDIT_STICKER_ROWS;
                        if (max_scroll < 0) max_scroll = 0;
                        if (kDown & KEY_DUP)   { if (sticker_scroll > 0)         sticker_scroll--; }
                        if (kDown & KEY_DDOWN) { if (sticker_scroll < max_scroll) sticker_scroll++; }

                        // A — pick up selected sticker, reset cursor to centre
                        if (kDown & KEY_A) {
                            sticker_cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                            sticker_cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                            sticker_placing  = true;
                        }
                    }
                }
            } else if (active_tab == TAB_SHOOT) {
                // On shoot screen: d-pad nudges brightness (up/down) and palette (left/right)
                // Suppress when wiggle preview is active — d-pad is used for offset adjustment there
                if (!wiggle_preview) {
                if (kDown & KEY_DUP)    { params.brightness += 0.1f; if (params.brightness > ranges.bright_max) params.brightness = ranges.bright_max; }
                if (kDown & KEY_DDOWN)  { params.brightness -= 0.1f; if (params.brightness < ranges.bright_min) params.brightness = ranges.bright_min; }
                if (kDown & KEY_DLEFT)  { params.palette = (params.palette <= PALETTE_NONE) ? PALETTE_COUNT - 1 : params.palette - 1; }
                if (kDown & KEY_DRIGHT) { params.palette = (params.palette >= PALETTE_COUNT - 1) ? PALETTE_NONE : params.palette + 1; }
                }
            } else if (active_tab == TAB_STYLE) {
                // Pixel size
                if (kDown & KEY_DLEFT)  { if (params.pixel_size > 1) params.pixel_size--; }
                if (kDown & KEY_DRIGHT) { if (params.pixel_size < PX_STOPS) params.pixel_size++; }
            } else if (active_tab == TAB_FX) {
                if (kDown & KEY_DUP)    { params.fx_mode--; if (params.fx_mode < 0)  params.fx_mode = 6; }
                if (kDown & KEY_DDOWN)  { params.fx_mode++; if (params.fx_mode > 6)  params.fx_mode = 0; }
                if (kDown & KEY_DLEFT)  { params.fx_intensity--; if (params.fx_intensity < 0)  params.fx_intensity = 0; }
                if (kDown & KEY_DRIGHT) { params.fx_intensity++; if (params.fx_intensity > 10) params.fx_intensity = 10; }
            } else if (active_tab == TAB_PALETTE_ED) {
                if (kDown & KEY_DUP)    { if (--palette_sel_pal   < 0)                                    palette_sel_pal   = PALETTE_COUNT - 1; palette_sel_color = 0; }
                if (kDown & KEY_DDOWN)  { if (++palette_sel_pal   >= PALETTE_COUNT)                       palette_sel_pal   = 0;                 palette_sel_color = 0; }
                if (kDown & KEY_DLEFT)  { if (--palette_sel_color < 0)                                    palette_sel_color = user_palettes[palette_sel_pal].size - 1; }
                if (kDown & KEY_DRIGHT) { if (++palette_sel_color >= user_palettes[palette_sel_pal].size) palette_sel_color = 0; }
            }

            // While on palette editor, keep the live filter synced with the selected palette
            if (active_tab == TAB_PALETTE_ED)
                params.palette = palette_sel_pal;

            // Touch input
            touchPosition touch;
            hidTouchRead(&touch);

            bool do_cam = false, do_defaults_save = false;
            bool do_gallery_toggle = false;
            bool do_edit_cancel = false, do_edit_savenew = false, do_edit_overwrite = false;
            bool do_edit_enter_or_place = false;
            handle_touch(touch, kDown, kHeld, &params, &do_cam, &do_save, &do_defaults_save,
                         &active_tab, &save_scale, &default_params,
                         &ranges, user_palettes, &palette_sel_pal, &palette_sel_color,
                         &do_gallery_toggle,
                         gallery_mode, gallery_count, &gallery_sel, &gallery_scroll,
                         &shoot_mode, &shoot_mode_open,
                         &shoot_timer_secs, &timer_open,
                         &wiggle_frames, &wiggle_delay_ms,
                         &wiggle_offset_dx, &wiggle_offset_dy, &wiggle_rebuild,
                         &wiggle_preview,
                         &lomo_preset,
                         gallery_edit_mode,
                         &edit_tab, &sticker_cat, &sticker_sel, &sticker_scroll, &gallery_frame,
                         placed_stickers,
                         &do_edit_cancel, &do_edit_savenew, &do_edit_overwrite,
                         &do_edit_enter_or_place);

            // Enter edit mode (from gallery view Edit button) OR pick up sticker (from info tap)
            if (do_edit_enter_or_place) {
                if (!gallery_edit_mode) {
                    // Reset cursor to centre when entering edit mode
                    sticker_cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                    sticker_cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                    sticker_pending_scale = 2.0f;
                    sticker_pending_angle = 0.0f;
                    sticker_placing  = false;
                    sticker_cat      = 0;
                    sticker_sel      = 0;
                    sticker_scroll   = 0;
                    sticker_cat_load(0);
                    gallery_edit_mode = true;
                } else if (edit_tab == 0) {
                    // Info area tap = pick up sticker (enter placement mode), reset cursor to centre
                    sticker_cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                    sticker_cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                    sticker_placing  = true;
                }
            }

            if (do_gallery_toggle) {
                gallery_mode = !gallery_mode;
                gallery_edit_mode = false;
                if (gallery_mode) {
                    // Stop camera — free DMA bandwidth and CPU for gallery/edit
                    if (cam_active) {
                        CAMU_StopCapture(PORT_BOTH);
                        for (int i = 2; i < 4; i++) {
                            if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
                        }
                        captureInterrupted = false;
                        cam_active = false;
                    }
                    gallery_count  = list_saved_photos(SAVE_DIR, gallery_paths, GALLERY_MAX);
                    gallery_sel    = 0;
                    gallery_scroll = 0;
                    gallery_loaded = -1;
                } else {
                    // Restart camera
                    if (!cam_active) {
                        CAMU_ClearBuffer(PORT_BOTH);
                        if (!selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
                        CAMU_StartCapture(PORT_BOTH);
                        cam_active = true;
                    }
                }
            }

            // Gallery edit: Cancel
            if (do_edit_cancel) {
                gallery_edit_mode = false;
                sticker_placing   = false;
                for (int i = 0; i < STICKER_MAX; i++) placed_stickers[i].active = false;
                gallery_frame = -1;
            }

            // Gallery edit: Save New or Overwrite
            if ((do_edit_savenew || do_edit_overwrite) && gallery_count > 0) {
                static const char *s_frame_paths_save[FRAME_COUNT] = FRAME_PATHS_INIT;
                char out_path[80];

                // Build composite context (shared for both still and wiggle)
                EditCompositeCtx ctx = {
                    placed_stickers, STICKER_MAX,
                    gallery_frame,
                    (gallery_frame >= 0 && gallery_frame < FRAME_COUNT)
                        ? s_frame_paths_save[gallery_frame] : NULL
                };

                if (gallery_n_frames > 1) {
                    // Wiggle: composite onto every frame, save as APNG
                    const uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
                    for (int i = 0; i < gallery_n_frames; i++)
                        fptrs[i] = gallery_thumbs[i];

                    if (do_edit_overwrite) {
                        snprintf(out_path, sizeof(out_path), "%s", gallery_paths[gallery_sel]);
                    } else {
                        next_wiggle_path(SAVE_DIR, out_path, sizeof(out_path));
                        settings_save_file_counter(file_counter_next());
                    }
                    save_edited_apng(out_path, fptrs,
                                     gallery_n_frames, gallery_delay_ms,
                                     CAMERA_WIDTH, CAMERA_HEIGHT,
                                     edit_composite_cb, &ctx);
                } else {
                    // Still image: composite once, save as JPEG
                    rgb565_to_rgb888(edit_preview_rgb888,
                                     (const uint16_t *)gallery_thumbs[0],
                                     CAMERA_WIDTH * CAMERA_HEIGHT);
                    edit_composite_cb(edit_preview_rgb888,
                                      CAMERA_WIDTH, CAMERA_HEIGHT, &ctx);

                    if (do_edit_overwrite) {
                        snprintf(out_path, sizeof(out_path), "%s", gallery_paths[gallery_sel]);
                    } else {
                        next_save_path(SAVE_DIR, out_path, sizeof(out_path));
                        settings_save_file_counter(file_counter_next());
                    }
                    save_jpeg(out_path, edit_preview_rgb888, CAMERA_WIDTH, CAMERA_HEIGHT);
                }

                // Refresh gallery list and exit edit mode
                gallery_count  = list_saved_photos(SAVE_DIR, gallery_paths, GALLERY_MAX);
                gallery_loaded = -1;
                gallery_edit_mode = false;
                sticker_placing   = false;
                for (int i = 0; i < STICKER_MAX; i++) placed_stickers[i].active = false;
                gallery_frame = -1;
                edit_save_flash = 60;
            }
            if (edit_save_flash > 0) edit_save_flash--;

            // Load selected photo into thumb buffers when selection changes
            if (gallery_mode && gallery_count > 0 && gallery_loaded != gallery_sel) {
                const char *gpath = gallery_paths[gallery_sel];
                const char *ext = gpath + strlen(gpath) - 4;
                gallery_n_frames   = 1;
                gallery_delay_ms   = 250;
                gallery_anim_tick  = svcGetSystemTick();
                gallery_anim_frame = 0;
                if (ext > gpath && strcmp(ext, ".png") == 0) {
                    uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
                    for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++)
                        fptrs[i] = gallery_thumbs[i];
                    load_apng_frames_to_rgb565(gpath, fptrs, GALLERY_WIGGLE_MAX_FRAMES,
                                               &gallery_n_frames, &gallery_delay_ms,
                                               CAMERA_WIDTH, CAMERA_HEIGHT);
                    if (gallery_n_frames < 1) {
                        load_jpeg_to_rgb565(gpath, gallery_thumbs[0], CAMERA_WIDTH, CAMERA_HEIGHT);
                        gallery_n_frames = 1;
                    }
                } else {
                    load_jpeg_to_rgb565(gpath, gallery_thumbs[0], CAMERA_WIDTH, CAMERA_HEIGHT);
                }
                gallery_loaded = gallery_sel;
            }

            // Gallery d-pad scrolling (full-screen gallery context, 4×4 grid)
            if (gallery_mode && !gallery_edit_mode) {
                #define GAL_GRID_COLS 4
                #define GAL_GRID_ROWS 4
                int total_rows = (gallery_count + GAL_GRID_COLS - 1) / GAL_GRID_COLS;
                int max_scroll = total_rows - GAL_GRID_ROWS;
                if (max_scroll < 0) max_scroll = 0;
                if (kDown & KEY_DDOWN)  { if (gallery_scroll < max_scroll) gallery_scroll++; }
                if (kDown & KEY_DUP)    { if (gallery_scroll > 0) gallery_scroll--; }
                if (kDown & KEY_DRIGHT) { gallery_sel++; if (gallery_sel >= gallery_count) gallery_sel = gallery_count - 1; }
                if (kDown & KEY_DLEFT)  { gallery_sel--; if (gallery_sel < 0) gallery_sel = 0; }
                #undef GAL_GRID_COLS
                #undef GAL_GRID_ROWS
            }

            if (do_cam || (kDown & KEY_Y)) {
                CAMU_StopCapture(PORT_BOTH);
                for (int i = 0; i < 4; i++) {
                    if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
                }
                CAMU_Activate(SELECT_NONE);

                selfie    = !selfie;
                camSelect = selfie ? SELECT_IN1_OUT2 : SELECT_OUT1_OUT2;

                CAMU_SetSize(camSelect, SIZE_CTR_TOP_LCD, CONTEXT_A);
                CAMU_SetOutputFormat(camSelect, OUTPUT_RGB_565, CONTEXT_A);
                CAMU_SetFrameRate(camSelect, FRAME_RATE_30);
                CAMU_SetNoiseFilter(camSelect, true);
                CAMU_SetAutoExposure(camSelect, true);
                CAMU_SetAutoWhiteBalance(camSelect, true);
                CAMU_SetTrimming(PORT_CAM1, false);
                CAMU_SetTrimming(PORT_CAM2, false);

                CAMU_GetMaxBytes(&bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
                CAMU_SetTransferBytes(PORT_BOTH, bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
                CAMU_Activate(camSelect);

                CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
                CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
                CAMU_ClearBuffer(PORT_BOTH);
                if (!selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
                CAMU_StartCapture(PORT_BOTH);
                captureInterrupted = false;
            }

            if (do_defaults_save) {
                default_params = params;
                settings_save(&default_params, save_scale);
                settings_save_palettes(user_palettes);
                settings_save_ranges(&ranges);
                settings_flash = 20;
            }
        }

        // Wiggle preview: B cancels, Save button confirms and writes APNG
        // D-pad and touch buttons work unconditionally (outside captureInterrupted guard)
        if (wiggle_preview) {
            // D-pad: left/right = X offset, up/down = Y offset, with hold-repeat
            u32 dpad = kHeld & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN);
            if (dpad) {
                bool fire = (kDown & dpad) || (wiggle_dpad_repeat > 20 && wiggle_dpad_repeat % 4 == 0);
                wiggle_dpad_repeat++;
                if (fire) {
                    if ((dpad & KEY_DLEFT)  && wiggle_offset_dx > -20) { wiggle_offset_dx--; wiggle_rebuild = true; }
                    if ((dpad & KEY_DRIGHT) && wiggle_offset_dx <  20) { wiggle_offset_dx++; wiggle_rebuild = true; }
                    if ((dpad & KEY_DUP)    && wiggle_offset_dy <  10) { wiggle_offset_dy++; wiggle_rebuild = true; }
                    if ((dpad & KEY_DDOWN)  && wiggle_offset_dy > -10) { wiggle_offset_dy--; wiggle_rebuild = true; }
                }
            } else {
                wiggle_dpad_repeat = 0;
            }
            // Touch buttons — re-read touch here so they work even when captureInterrupted
            // Use tapped (kDown) not held, to avoid rapid-fire on resistive screen
            {
                touchPosition wtouch;
                hidTouchRead(&wtouch);
                bool wtapped = (kDown & KEY_TOUCH) != 0;
                if (wtapped) {
                    int tx = wtouch.px, ty = wtouch.py;
                    #define WBTW  28
                    #define WBTH  22
                    #define WVALW 36
                    #define WRSTW 22
                    #define WMINX 18
                    #define WVALX (WMINX + WBTW + 2)
                    #define WPLUX (WVALX + WVALW + 2)
                    #define WRSTX (WPLUX + WBTW + 2)
                    int row_x_y = SHOOT_CONTENT_Y + 4;
                    int row_y_y = SHOOT_CONTENT_Y + 32;
                    int *val = NULL; int lo = 0, hi = 0;
                    if (tx < 158 && ty >= row_x_y && ty < row_x_y + WBTH)
                        { val = &wiggle_offset_dx; lo = -20; hi = 20; }
                    else if (tx < 158 && ty >= row_y_y && ty < row_y_y + WBTH)
                        { val = &wiggle_offset_dy; lo = -10; hi = 10; }
                    if (val) {
                        if (tx >= WMINX && tx < WMINX + WBTW) {
                            if (*val > lo) { (*val)--; wiggle_rebuild = true; }
                        } else if (tx >= WPLUX && tx < WPLUX + WBTW) {
                            if (*val < hi) { (*val)++; wiggle_rebuild = true; }
                        } else if (tx >= WRSTX && tx < WRSTX + WRSTW) {
                            *val = 0; wiggle_rebuild = true;
                        }
                    }
                    #undef WBTW
                    #undef WBTH
                    #undef WVALW
                    #undef WRSTW
                    #undef WMINX
                    #undef WVALX
                    #undef WPLUX
                    #undef WRSTX
                }
            }
            // L/R bumpers cycle through delay presets (50 → 100 → 200 → 500)
            if (kDown & KEY_L || kDown & KEY_R) {
                static const int delay_presets[] = {50, 100, 200, 500};
                int cur = 0;
                for (int i = 0; i < 4; i++) if (wiggle_delay_ms == delay_presets[i]) { cur = i; break; }
                wiggle_delay_ms = delay_presets[(cur + (kDown & KEY_L ? 3 : 1)) % 4];
            }
            if (kDown & KEY_B) {
                wiggle_preview = false;
            } else if ((do_save || (kDown & KEY_A)) && !s_save.busy) {
                char apng_path[64];
                if (next_wiggle_path(SAVE_DIR, apng_path, sizeof(apng_path))) {
                    settings_save_file_counter(file_counter_next());
                    // wiggle_left/right already hold the raw RGB565 snapshots
                    memcpy(s_save.snapshot_buf,  wiggle_left,  CAMERA_SCREEN_SIZE);
                    memcpy(s_save.snapshot_buf2, wiggle_right, CAMERA_SCREEN_SIZE);
                    memcpy(s_save.save_path, apng_path, sizeof(apng_path));
                    s_save.wiggle_mode     = true;
                    s_save.wiggle_n_frames = wiggle_frames;
                    s_save.wiggle_delay_ms = wiggle_delay_ms;
                    s_save.wiggle_has_align = wiggle_has_align;
                    s_save.wiggle_offset_dx = wiggle_offset_dx;
                    s_save.wiggle_offset_dy = wiggle_offset_dy;
                    if (wiggle_has_align) s_save.wiggle_align_result = wiggle_align_res;
                    s_save.busy = true;
                    save_flash  = 20;
                    play_shutter_click();
                    LightEvent_Signal(&s_save.request_event);
                    wiggle_preview = false;
                }
            }
        } else if (timer_active) {
            // B cancels countdown
            if (kDown & KEY_B) {
                timer_active = false;
            } else {
                // Advance countdown using wall-clock ticks
                u64 now = svcGetSystemTick();
                int elapsed_ms = (int)((now - timer_prev_tick) * 1000 / SYSCLOCK_ARM11);
                timer_prev_tick = now;
                timer_remaining_ms -= elapsed_ms;
                if (timer_remaining_ms <= 0) {
                    timer_active = false;
                    // Fire save using the mode that was active before switching to Timer
                    if (shoot_mode == SHOOT_MODE_WIGGLE) {
                        memcpy(wiggle_left,  buf,                      CAMERA_SCREEN_SIZE);
                        memcpy(wiggle_right, buf + CAMERA_SCREEN_SIZE, CAMERA_SCREEN_SIZE);
                        wiggle_has_align = false;
                        wiggle_offset_dx = 0;
                        wiggle_offset_dy = 0;
                        wiggle_frames = build_wiggle_preview_frames(wiggle_preview_frames,
                                                    wiggle_left, wiggle_right,
                                                    CAMERA_WIDTH, CAMERA_HEIGHT,
                                                    wiggle_frames, NULL,
                                                    wiggle_offset_dx, wiggle_offset_dy,
                                                    &wiggle_crop_w, &wiggle_crop_h);
                        wiggle_preview = true;
                        wiggle_preview_frame = 0;
                        wiggle_preview_last_tick = svcGetSystemTick();
                        play_shutter_click();
                    } else if (!s_save.busy) {
                        char save_path[64];
                        if (next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
                            settings_save_file_counter(file_counter_next());
                            memcpy(s_save.snapshot_buf, filtered_buf, CAMERA_SCREEN_SIZE);
                            memcpy(s_save.save_path, save_path, sizeof(save_path));
                            s_save.wiggle_mode = false;
                            s_save.save_scale  = save_scale;
                            s_save.busy = true;
                            save_flash  = 20;
                            play_shutter_click();
                            LightEvent_Signal(&s_save.request_event);
                        }
                    }
                }
            }
        } else if ((do_save || (kDown & KEY_A)) && !s_save.busy && !gallery_mode && !gallery_edit_mode) {
            if (shoot_timer_secs > 0 && !timer_active) {
                // Start countdown using current shoot_mode
                timer_remaining_ms = shoot_timer_secs * 1000;
                timer_prev_tick    = svcGetSystemTick();
                timer_active       = true;
            } else if (shoot_mode == SHOOT_MODE_WIGGLE) {
                // First press: capture both cam buffers, auto-align, build preview
                memcpy(wiggle_left,  buf,                      CAMERA_SCREEN_SIZE);
                memcpy(wiggle_right, buf + CAMERA_SCREEN_SIZE, CAMERA_SCREEN_SIZE);
                wiggle_has_align = false;
                wiggle_offset_dx = 0;
                wiggle_offset_dy = 0;
                wiggle_frames = build_wiggle_preview_frames(wiggle_preview_frames,
                                            wiggle_left, wiggle_right,
                                            CAMERA_WIDTH, CAMERA_HEIGHT,
                                            wiggle_frames, NULL,
                                            wiggle_offset_dx, wiggle_offset_dy,
                                            &wiggle_crop_w, &wiggle_crop_h);
                wiggle_preview           = true;
                wiggle_preview_frame     = 0;
                wiggle_preview_last_tick = svcGetSystemTick();
                play_shutter_click();
            } else {
                // Normal JPEG save
                char save_path[64];
                if (next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
                    settings_save_file_counter(file_counter_next());
                    memcpy(s_save.snapshot_buf, filtered_buf, CAMERA_SCREEN_SIZE);
                    memcpy(s_save.save_path, save_path, sizeof(save_path));
                    s_save.wiggle_mode = false;
                    s_save.save_scale  = save_scale;
                    s_save.busy = true;
                    save_flash  = 20;
                    play_shutter_click();
                    LightEvent_Signal(&s_save.request_event);
                }
            }
        }

        if (s_save.busy) save_flash = 20;  // pin high while thread is working
        else if (save_flash > 0) save_flash--;
        if (settings_flash > 0) settings_flash--;

        bool use3d = CONFIG_3D_SLIDERSTATE > 0.0f;
        bool comparing = (kHeld & KEY_SELECT) != 0;

        if (cam_active) {
        // Always drain both camera ports to prevent buffer error interrupts
        if (camReceiveEvent[2] == 0)
            CAMU_SetReceiving(&camReceiveEvent[2], buf,                      PORT_CAM1, CAMERA_SCREEN_SIZE, (s16)bufSize);
        if (camReceiveEvent[3] == 0)
            CAMU_SetReceiving(&camReceiveEvent[3], buf + CAMERA_SCREEN_SIZE, PORT_CAM2, CAMERA_SCREEN_SIZE, (s16)bufSize);

        if (captureInterrupted) {
            CAMU_StartCapture(PORT_BOTH);
            captureInterrupted = false;
        }

        // Block until any camera event fires
        svcWaitSynchronizationN(&index, camReceiveEvent, 4, false, WAIT_TIMEOUT);

        switch (index) {
        case 0:
            svcCloseHandle(camReceiveEvent[2]); camReceiveEvent[2] = 0;
            captureInterrupted = true;
            break;
        case 1:
            svcCloseHandle(camReceiveEvent[3]); camReceiveEvent[3] = 0;
            captureInterrupted = true;
            break;
        case 2:
            svcCloseHandle(camReceiveEvent[2]); camReceiveEvent[2] = 0;
            // Wiggle saves never read filtered_buf, so allow live view updates during them.
            // Only pause filter processing during JPEG saves (which copy snapshot_buf).
            if (!use3d && !comparing && (!s_save.busy || s_save.wiggle_mode)) {
                rgb565_to_rgb888(rgb_buf, (const uint16_t *)buf, CAMERA_WIDTH * CAMERA_HEIGHT);
                if (shoot_mode == SHOOT_MODE_LOMO) {
                    // Lomo: raw camera feed — apply tonal preset + FX only, no GB pixel filter
                    const LomoPreset *lp = &lomo_presets[lomo_preset];
                    FilterParams lp_params = params;
                    lp_params.brightness  = lp->brightness;
                    lp_params.contrast    = lp->contrast;
                    lp_params.saturation  = lp->saturation;
                    lp_params.gamma       = lp->gamma;
                    lp_params.fx_mode     = lp->fx_mode;
                    lp_params.fx_intensity = lp->fx_intensity;
                    lp_params.pixel_size  = 1;        // no pixelation
                    lp_params.palette     = PALETTE_NONE;  // no palette quantisation
                    lp_params.color_levels = 256;     // no posterisation
                    apply_gameboy_filter(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, lp_params);
                    apply_fx(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, lp_params, frame_count);
                } else if (shoot_mode == SHOOT_MODE_WIGGLE) {
                    // Wiggle: true-colour preview — skip all filters
                } else {
                    apply_gameboy_filter(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, params);
                    apply_fx(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, params, frame_count);
                }
                rgb888_to_rgb565((uint16_t *)filtered_buf, rgb_buf, CAMERA_WIDTH * CAMERA_HEIGHT);
            }
            break;
        case 3:
            svcCloseHandle(camReceiveEvent[3]); camReceiveEvent[3] = 0;
            break;
        default:
            break;
        }
        } else {
            // Camera is stopped (gallery / edit mode) — sleep to avoid busy-loop
            svcSleepThread(16000000LL);  // ~16ms = ~60fps pacing without camera
        }

        // Advance wiggle preview animation — clock-based, cycles all blended frames
        if (wiggle_preview) {
            // Rebuild frames when user has adjusted H/V offsets
            if (wiggle_rebuild) {
                wiggle_frames = build_wiggle_preview_frames(wiggle_preview_frames,
                                            wiggle_left, wiggle_right,
                                            CAMERA_WIDTH, CAMERA_HEIGHT,
                                            wiggle_frames, NULL,
                                            wiggle_offset_dx, wiggle_offset_dy,
                                            &wiggle_crop_w, &wiggle_crop_h);
                wiggle_rebuild = false;
            }
            u64 now = svcGetSystemTick();
            u64 period = (u64)wiggle_delay_ms * SYSCLOCK_ARM11 / 1000;
            if (now - wiggle_preview_last_tick >= period) {
                wiggle_preview_frame     = (wiggle_preview_frame + 1) % wiggle_frames;
                wiggle_preview_last_tick = now;
            }
        }

        // Advance gallery wiggle animation — clock-based
        if (gallery_mode && gallery_n_frames > 1) {
            u64 now = svcGetSystemTick();
            u64 period = (u64)gallery_delay_ms * SYSCLOCK_ARM11 / 1000;
            if (now - gallery_anim_tick >= period) {
                gallery_anim_frame = (gallery_anim_frame + 1) % gallery_n_frames;
                gallery_anim_tick  = (u64)now;
            }
        }

        // Blit camera frame to top screen raw framebuffer
        gfxSet3D(false);
        if (use3d || timer_open) {
            u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            memset(fb, 0, CAMERA_WIDTH * CAMERA_HEIGHT * 3);
        } else if (gallery_edit_mode && gallery_count > 0) {
            // Build composited edit preview: photo + stickers + frame
            static const char *s_frame_paths[FRAME_COUNT] = FRAME_PATHS_INIT;
            // Base photo
            rgb565_to_rgb888(edit_preview_rgb888,
                             (const uint16_t *)gallery_thumbs[gallery_anim_frame],
                             CAMERA_WIDTH * CAMERA_HEIGHT);
            // Stickers
            for (int si = 0; si < STICKER_MAX; si++) {
                if (!placed_stickers[si].active) continue;
                const unsigned char *px = get_sticker_pixels(placed_stickers[si].cat_idx,
                                                              placed_stickers[si].icon_idx);
                if (px)
                    composite_sticker_rgb888(edit_preview_rgb888,
                                             CAMERA_WIDTH, CAMERA_HEIGHT,
                                             px,
                                             placed_stickers[si].x,
                                             placed_stickers[si].y,
                                             placed_stickers[si].scale,
                                             placed_stickers[si].angle_deg);
            }
            // Frame overlay — composite from romfs path (only when active)
            if (gallery_frame >= 0 && gallery_frame < FRAME_COUNT)
                composite_frame_rgb888(edit_preview_rgb888,
                                       CAMERA_WIDTH, CAMERA_HEIGHT,
                                       s_frame_paths[gallery_frame]);
            // Cursor crosshair: visible when placing (sticker_placing == true)
            if (sticker_placing && edit_tab == 0) {
                int cx = (int)sticker_cursor_x;
                int cy = (int)sticker_cursor_y;
                for (int d = -12; d <= 12; d++) {
                    // Horizontal bar
                    int px2 = cx + d, py2 = cy;
                    if (px2 >= 0 && px2 < CAMERA_WIDTH && py2 >= 0 && py2 < CAMERA_HEIGHT) {
                        uint8_t *p = edit_preview_rgb888 + (py2 * CAMERA_WIDTH + px2) * 3;
                        p[0] = 255; p[1] = 255; p[2] = 0;
                    }
                    // Vertical bar
                    px2 = cx; py2 = cy + d;
                    if (px2 >= 0 && px2 < CAMERA_WIDTH && py2 >= 0 && py2 < CAMERA_HEIGHT) {
                        uint8_t *p = edit_preview_rgb888 + (py2 * CAMERA_WIDTH + px2) * 3;
                        p[0] = 255; p[1] = 255; p[2] = 0;
                    }
                }
                // Also preview selected sticker at cursor (shows where it will land)
                {
                    const unsigned char *cpx = get_sticker_pixels(sticker_cat, sticker_sel);
                    if (cpx) {
                        composite_sticker_rgb888(edit_preview_rgb888,
                                                 CAMERA_WIDTH, CAMERA_HEIGHT,
                                                 cpx, cx, cy,
                                                 sticker_pending_scale, sticker_pending_angle);
                    }
                }
            }
            // Convert back and blit
            static uint16_t edit_preview_rgb565[CAMERA_WIDTH * CAMERA_HEIGHT];
            rgb888_to_rgb565(edit_preview_rgb565, edit_preview_rgb888,
                             CAMERA_WIDTH * CAMERA_HEIGHT);
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            edit_preview_rgb565, 0, 0,
                                            CAMERA_WIDTH, CAMERA_HEIGHT);
        } else {
            if (wiggle_preview) {
                // Compose cropped frame into a full 400×240 buffer (black borders),
                // then blit normally. This keeps the framebuffer column stride correct.
                int bx = (CAMERA_WIDTH  - wiggle_crop_w) / 2;
                int by = (CAMERA_HEIGHT - wiggle_crop_h) / 2;
                memset(wiggle_compose_buf, 0, sizeof(wiggle_compose_buf));
                const uint16_t *src = wiggle_preview_frames[wiggle_preview_frame];
                for (int row = 0; row < wiggle_crop_h; row++)
                    memcpy(wiggle_compose_buf + (by + row) * CAMERA_WIDTH + bx,
                           src + row * wiggle_crop_w,
                           wiggle_crop_w * sizeof(uint16_t));
                writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                                wiggle_compose_buf, 0, 0,
                                                CAMERA_WIDTH, CAMERA_HEIGHT);
            } else {
                void *blit_src;
                if (gallery_mode && gallery_count > 0)
                    blit_src = gallery_thumbs[gallery_anim_frame];
                else
                    blit_src = comparing ? buf : filtered_buf;
                writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                                blit_src, 0, 0, CAMERA_WIDTH, CAMERA_HEIGHT);
            }
        }

        // Flush top screen before C3D takes the GPU
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, true);

        // Draw bottom screen UI with citro2d
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        draw_ui(bot, staticBuf, dynBuf, params, selfie, save_flash, use3d,
                active_tab, save_scale, settings_flash > 0,
                settings_row,
                user_palettes, palette_sel_pal, palette_sel_color,
                &ranges, comparing,
                gallery_mode, gallery_count,
                (const char (*)[64])gallery_paths, gallery_sel, gallery_scroll,
                shoot_mode, shoot_mode_open,
                shoot_timer_secs, timer_open,
                wiggle_frames, wiggle_delay_ms,
                wiggle_preview,
                wiggle_offset_dx, wiggle_offset_dy,
                timer_active ? (timer_remaining_ms + 999) / 1000 : -1,
                lomo_preset,
                gallery_edit_mode,
                edit_tab, sticker_cat, sticker_sel, sticker_scroll,
                gallery_frame,
                sticker_cursor_x, sticker_cursor_y,
                sticker_pending_scale, sticker_pending_angle,
                sticker_placing);
        C3D_FrameEnd(0);
        frame_count++;
    }

    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++)
        if (camReceiveEvent[i]) svcCloseHandle(camReceiveEvent[i]);
    CAMU_Activate(SELECT_NONE);

    // Gracefully stop the save thread before freeing its buffers
    s_save.quit = true;
    LightEvent_Signal(&s_save.request_event);  // unblock worker if waiting
    threadJoin(save_thread, U64_MAX);
    threadFree(save_thread);

    free(buf);
    free(filtered_buf);
    free(rgb_buf);
    free(s_save.snapshot_buf);
    free(s_save.snapshot_buf2);
    free(wiggle_left);
    free(wiggle_right);
    C2D_TextBufDelete(staticBuf);
    C2D_TextBufDelete(dynBuf);
    cleanup();
    return 0;
}
