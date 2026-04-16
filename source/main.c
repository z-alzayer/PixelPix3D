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
#include "app_state.h"
#include "shoot.h"
#include "gallery.h"

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
    u8 *snapshot_buf  = malloc(CAMERA_SCREEN_SIZE);
    u8 *snapshot_buf2 = malloc(CAMERA_SCREEN_SIZE);
    // Wiggle preview buffers (true-colour RGB565, no filter applied)
    u8 *wiggle_left  = malloc(CAMERA_SCREEN_SIZE);
    u8 *wiggle_right = malloc(CAMERA_SCREEN_SIZE);
    if (!buf || !filtered_buf || !rgb_buf ||
        !snapshot_buf || !snapshot_buf2 ||
        !wiggle_left || !wiggle_right) longjmp(exitJmp, 1);
    memset(filtered_buf, 0, CAMERA_SCREEN_SIZE);

    // App state
    AppState app = {
        .active_tab       = 0,
        .selfie           = false,
        .cam_active       = true,
        .save_flash       = 0,
        .settings_flash   = 0,
        .frame_count      = 0,
        .save_scale       = 2,
        .settings_row     = 0,
        .params           = FILTER_DEFAULTS,
        .default_params   = FILTER_DEFAULTS,
        .ranges           = FILTER_RANGES_DEFAULTS,
        .palette_sel_pal  = 0,
        .palette_sel_color = 0,
    };

    // Shoot state
    ShootState shoot = {
        .shoot_mode       = SHOOT_MODE_GBCAM,
        .shoot_mode_open  = false,
        .timer_open       = false,
        .shoot_timer_secs = 0,
        .lomo_preset      = 0,
        .timer_active     = false,
        .timer_remaining_ms = 0,
        .timer_prev_tick  = 0,
    };

    // Wiggle state
    WiggleState wig = {
        .preview          = false,
        .n_frames         = 4,
        .delay_ms         = 250,
        .preview_frame    = 0,
        .preview_last_tick = 0,
        .has_align        = false,
        .offset_dx        = 0,
        .offset_dy        = 0,
        .rebuild          = false,
        .crop_w           = CAMERA_WIDTH,
        .crop_h           = CAMERA_HEIGHT,
        .dpad_repeat      = 0,
    };
    static uint16_t wiggle_preview_frames[WIGGLE_PREVIEW_MAX][CAMERA_WIDTH * CAMERA_HEIGHT];
    static uint16_t wiggle_compose_buf[CAMERA_WIDTH * CAMERA_HEIGHT];

    // Gallery state
    GalleryState gal = {
        .mode       = false,
        .sel        = 0,
        .scroll     = 0,
        .count      = 0,
        .loaded     = -1,
        .n_frames   = 1,
        .delay_ms   = 250,
        .anim_tick  = 0,
        .anim_frame = 0,
    };
    // Edit state
    EditState edit = {
        .active         = false,
        .tab            = 0,
        .sticker_cat    = 0,
        .sticker_sel    = 0,
        .sticker_scroll = 0,
        .gallery_frame  = -1,
        .save_flash     = 0,
        .cursor_x       = (float)CAMERA_WIDTH  / 2.0f,
        .cursor_y       = (float)CAMERA_HEIGHT / 2.0f,
        .pending_scale  = 2.0f,
        .pending_angle  = 0.0f,
        .placing        = false,
    };
    for (int i = 0; i < STICKER_MAX; i++) edit.placed[i].active = false;
    static uint8_t edit_preview_rgb888[CAMERA_WIDTH * CAMERA_HEIGHT * 3];

    u32 bufSize;
    CAMU_GetMaxBytes(&bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
    CAMU_SetTransferBytes(PORT_BOTH, bufSize, CAMERA_WIDTH, CAMERA_HEIGHT);
    CAMU_Activate(SELECT_OUT1_OUT2);

    Handle camReceiveEvent[4] = {0};
    bool captureInterrupted = false;
    s32 index = 0;

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
    CAMU_ClearBuffer(PORT_BOTH);
    CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);

    for (int i = 0; i < PALETTE_COUNT; i++) app.user_palettes[i] = palettes[i];
    settings_load(&app.params, &app.save_scale);
    settings_load_palettes(app.user_palettes);
    settings_load_ranges(&app.ranges);
    app.default_params = app.params;
    filter_set_user_palettes(app.user_palettes);
    // Clamp live params to loaded ranges
    if (app.params.brightness  < app.ranges.bright_min)   app.params.brightness  = app.ranges.bright_min;
    if (app.params.brightness  > app.ranges.bright_max)   app.params.brightness  = app.ranges.bright_max;
    if (app.params.contrast    < app.ranges.contrast_min) app.params.contrast    = app.ranges.contrast_min;
    if (app.params.contrast    > app.ranges.contrast_max) app.params.contrast    = app.ranges.contrast_max;
    if (app.params.saturation  < app.ranges.sat_min)      app.params.saturation  = app.ranges.sat_min;
    if (app.params.saturation  > app.ranges.sat_max)      app.params.saturation  = app.ranges.sat_max;
    if (app.params.gamma       < app.ranges.gamma_min)    app.params.gamma       = app.ranges.gamma_min;
    if (app.params.gamma       > app.ranges.gamma_max)    app.params.gamma       = app.ranges.gamma_max;

    // Seed shared file counter — reads INI then cross-checks against dir scan
    file_counter_init(SAVE_DIR, settings_load_file_counter());

    // Initialize and launch the background save thread on core 1
    Thread save_thread = save_thread_start(snapshot_buf, snapshot_buf2);
    if (!save_thread) longjmp(exitJmp, 1);

    while (aptMainLoop()) {

        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

        bool do_save = false;

        if (!captureInterrupted) {
            // Physical button fallbacks (skip in gallery/edit mode — buttons have different roles)
            if (!gal.mode && !edit.active) {
            if (kDown & KEY_L) {
                app.params.palette = (app.params.palette <= PALETTE_NONE)
                               ? PALETTE_COUNT - 1 : app.params.palette - 1;
            }
            if (kDown & KEY_R) {
                app.params.palette = (app.params.palette >= PALETTE_COUNT - 1)
                               ? PALETTE_NONE : app.params.palette + 1;
            }
            if (kDown & KEY_B) {
                app.params.pixel_size = (app.params.pixel_size % PX_STOPS) + 1;
            }
            } // end !gal.mode && !edit.active
            // X: cycle through main tabs (Shoot → Style → FX → More → Shoot)
            // Disabled in gallery/edit mode to avoid accidental tab switches
            if ((kDown & KEY_X) && !gal.mode && !edit.active) {
                if (app.active_tab <= TAB_MORE)
                    app.active_tab = (app.active_tab + 1) % (TAB_MORE + 1);
            }
            // D-pad: context-aware per active_tab
            if (edit.active && app.active_tab == TAB_SHOOT) {
                // Edit mode sticker tab — two-step placement flow:
                //   Picker mode:  D-pad U/D = scroll, A = "pick up" selected sticker
                //   Placing mode: Circle pad = move cursor, A = place, B = cancel
                if (edit.tab == 0) {
                    if (edit.placing) {
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
                        edit.cursor_x += dx * 3.0f / 144.0f;
                        edit.cursor_y -= dy * 3.0f / 144.0f;
                        if (edit.cursor_x < 0) edit.cursor_x = 0;
                        if (edit.cursor_x >= CAMERA_WIDTH)  edit.cursor_x = (float)(CAMERA_WIDTH  - 1);
                        if (edit.cursor_y < 0) edit.cursor_y = 0;
                        if (edit.cursor_y >= CAMERA_HEIGHT) edit.cursor_y = (float)(CAMERA_HEIGHT - 1);
                        #undef CP_DEADZONE

                        // L / R (held) — scale smaller / larger
                        if (kHeld & KEY_L) {
                            edit.pending_scale -= 0.03f;
                            if (edit.pending_scale < 0.5f) edit.pending_scale = 0.5f;
                        }
                        if (kHeld & KEY_R) {
                            edit.pending_scale += 0.03f;
                            if (edit.pending_scale > 8.0f) edit.pending_scale = 8.0f;
                        }

                        // D-pad L/R — rotate by 15°
                        if (kDown & KEY_DLEFT)  { edit.pending_angle -= 15.0f; if (edit.pending_angle <   0.0f) edit.pending_angle += 360.0f; }
                        if (kDown & KEY_DRIGHT) { edit.pending_angle += 15.0f; if (edit.pending_angle >= 360.0f) edit.pending_angle -= 360.0f; }

                        // A — confirm: place sticker centered on cursor
                        if (kDown & KEY_A) {
                            for (int si = 0; si < STICKER_MAX; si++) {
                                if (!edit.placed[si].active) {
                                    edit.placed[si].active    = true;
                                    edit.placed[si].cat_idx   = edit.sticker_cat;
                                    edit.placed[si].icon_idx  = edit.sticker_sel;
                                    edit.placed[si].x         = (int)edit.cursor_x;
                                    edit.placed[si].y         = (int)edit.cursor_y;
                                    edit.placed[si].scale     = edit.pending_scale;
                                    edit.placed[si].angle_deg = edit.pending_angle;
                                    break;
                                }
                            }
                            edit.placing = false;
                        }
                        // B — cancel placement (no sticker placed)
                        if (kDown & KEY_B) {
                            edit.placing = false;
                        }
                    } else {
                        // ---- Picker mode: browse stickers ----
                        // D-pad U/D — scroll picker rows
                        sticker_cat_load(edit.sticker_cat);
                        int total_icons = sticker_cats[edit.sticker_cat].count;
                        int total_rows  = (total_icons + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
                        int max_scroll  = total_rows - GEDIT_STICKER_ROWS;
                        if (max_scroll < 0) max_scroll = 0;
                        if (kDown & KEY_DUP)   { if (edit.sticker_scroll > 0)         edit.sticker_scroll--; }
                        if (kDown & KEY_DDOWN) { if (edit.sticker_scroll < max_scroll) edit.sticker_scroll++; }

                        // A — pick up selected sticker, reset cursor to centre
                        if (kDown & KEY_A) {
                            edit.cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                            edit.cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                            edit.placing  = true;
                        }
                    }
                }
            } else if (app.active_tab == TAB_SHOOT) {
                // On shoot screen: d-pad nudges brightness (up/down) and palette (left/right)
                // Suppress when wiggle preview is active — d-pad is used for offset adjustment there
                if (!wig.preview) {
                if (kDown & KEY_DUP)    { app.params.brightness += 0.1f; if (app.params.brightness > app.ranges.bright_max) app.params.brightness = app.ranges.bright_max; }
                if (kDown & KEY_DDOWN)  { app.params.brightness -= 0.1f; if (app.params.brightness < app.ranges.bright_min) app.params.brightness = app.ranges.bright_min; }
                if (kDown & KEY_DLEFT)  { app.params.palette = (app.params.palette <= PALETTE_NONE) ? PALETTE_COUNT - 1 : app.params.palette - 1; }
                if (kDown & KEY_DRIGHT) { app.params.palette = (app.params.palette >= PALETTE_COUNT - 1) ? PALETTE_NONE : app.params.palette + 1; }
                }
            } else if (app.active_tab == TAB_STYLE) {
                // Pixel size
                if (kDown & KEY_DLEFT)  { if (app.params.pixel_size > 1) app.params.pixel_size--; }
                if (kDown & KEY_DRIGHT) { if (app.params.pixel_size < PX_STOPS) app.params.pixel_size++; }
            } else if (app.active_tab == TAB_FX) {
                if (kDown & KEY_DUP)    { app.params.fx_mode--; if (app.params.fx_mode < 0)  app.params.fx_mode = 6; }
                if (kDown & KEY_DDOWN)  { app.params.fx_mode++; if (app.params.fx_mode > 6)  app.params.fx_mode = 0; }
                if (kDown & KEY_DLEFT)  { app.params.fx_intensity--; if (app.params.fx_intensity < 0)  app.params.fx_intensity = 0; }
                if (kDown & KEY_DRIGHT) { app.params.fx_intensity++; if (app.params.fx_intensity > 10) app.params.fx_intensity = 10; }
            } else if (app.active_tab == TAB_PALETTE_ED) {
                if (kDown & KEY_DUP)    { if (--app.palette_sel_pal   < 0)                                              app.palette_sel_pal   = PALETTE_COUNT - 1; app.palette_sel_color = 0; }
                if (kDown & KEY_DDOWN)  { if (++app.palette_sel_pal   >= PALETTE_COUNT)                                 app.palette_sel_pal   = 0;                 app.palette_sel_color = 0; }
                if (kDown & KEY_DLEFT)  { if (--app.palette_sel_color < 0)                                              app.palette_sel_color = app.user_palettes[app.palette_sel_pal].size - 1; }
                if (kDown & KEY_DRIGHT) { if (++app.palette_sel_color >= app.user_palettes[app.palette_sel_pal].size)   app.palette_sel_color = 0; }
            }

            // While on palette editor, keep the live filter synced with the selected palette
            if (app.active_tab == TAB_PALETTE_ED)
                app.params.palette = app.palette_sel_pal;

            // Touch input
            touchPosition touch;
            hidTouchRead(&touch);

            bool do_cam = false, do_defaults_save = false;
            bool do_gallery_toggle = false;
            bool do_edit_cancel = false, do_edit_savenew = false, do_edit_overwrite = false;
            bool do_edit_enter_or_place = false;
            handle_touch(touch, kDown, kHeld, &app.params, &do_cam, &do_save, &do_defaults_save,
                         &app.active_tab, &app.save_scale, &app.default_params,
                         &app.ranges, app.user_palettes, &app.palette_sel_pal, &app.palette_sel_color,
                         &do_gallery_toggle,
                         gal.mode, gal.count, &gal.sel, &gal.scroll,
                         &shoot.shoot_mode, &shoot.shoot_mode_open,
                         &shoot.shoot_timer_secs, &shoot.timer_open,
                         &wig.n_frames, &wig.delay_ms,
                         &wig.offset_dx, &wig.offset_dy, &wig.rebuild,
                         &wig.preview,
                         &shoot.lomo_preset,
                         edit.active,
                         &edit.tab, &edit.sticker_cat, &edit.sticker_sel, &edit.sticker_scroll, &edit.gallery_frame,
                         edit.placed,
                         &do_edit_cancel, &do_edit_savenew, &do_edit_overwrite,
                         &do_edit_enter_or_place);

            // Enter edit mode (from gallery view Edit button) OR pick up sticker (from info tap)
            if (do_edit_enter_or_place) {
                if (!edit.active) {
                    // Reset cursor to centre when entering edit mode
                    edit.cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                    edit.cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                    edit.pending_scale = 2.0f;
                    edit.pending_angle = 0.0f;
                    edit.placing  = false;
                    edit.sticker_cat      = 0;
                    edit.sticker_sel      = 0;
                    edit.sticker_scroll   = 0;
                    sticker_cat_load(0);
                    edit.active = true;
                } else if (edit.tab == 0) {
                    // Info area tap = pick up sticker (enter placement mode), reset cursor to centre
                    edit.cursor_x = (float)CAMERA_WIDTH  / 2.0f;
                    edit.cursor_y = (float)CAMERA_HEIGHT / 2.0f;
                    edit.placing  = true;
                }
            }

            if (do_gallery_toggle) {
                gallery_toggle(&gal, &app, &edit, camReceiveEvent, &captureInterrupted);
            }

            // Gallery edit: Cancel
            if (do_edit_cancel) {
                edit.active = false;
                edit.placing   = false;
                for (int i = 0; i < STICKER_MAX; i++) edit.placed[i].active = false;
                edit.gallery_frame = -1;
            }

            // Gallery edit: Save New or Overwrite
            if ((do_edit_savenew || do_edit_overwrite) && gal.count > 0) {
                static const char *s_frame_paths_save[FRAME_COUNT] = FRAME_PATHS_INIT;
                char out_path[80];

                // Build composite context (shared for both still and wiggle)
                EditCompositeCtx ctx = {
                    edit.placed, STICKER_MAX,
                    edit.gallery_frame,
                    (edit.gallery_frame >= 0 && edit.gallery_frame < FRAME_COUNT)
                        ? s_frame_paths_save[edit.gallery_frame] : NULL
                };

                if (gal.n_frames > 1) {
                    // Wiggle: composite onto every frame, save as APNG
                    const uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
                    for (int i = 0; i < gal.n_frames; i++)
                        fptrs[i] = gallery_thumbs[i];

                    if (do_edit_overwrite) {
                        snprintf(out_path, sizeof(out_path), "%s", gal.paths[gal.sel]);
                    } else {
                        next_wiggle_path(SAVE_DIR, out_path, sizeof(out_path));
                        settings_save_file_counter(file_counter_next());
                    }
                    save_edited_apng(out_path, fptrs,
                                     gal.n_frames, gal.delay_ms,
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
                        snprintf(out_path, sizeof(out_path), "%s", gal.paths[gal.sel]);
                    } else {
                        next_save_path(SAVE_DIR, out_path, sizeof(out_path));
                        settings_save_file_counter(file_counter_next());
                    }
                    save_jpeg(out_path, edit_preview_rgb888, CAMERA_WIDTH, CAMERA_HEIGHT);
                }

                // Refresh gallery list and exit edit mode
                gal.count  = list_saved_photos(SAVE_DIR, gal.paths, GALLERY_MAX);
                gal.loaded = -1;
                edit.active = false;
                edit.placing   = false;
                for (int i = 0; i < STICKER_MAX; i++) edit.placed[i].active = false;
                edit.gallery_frame = -1;
                edit.save_flash = 60;
            }
            if (edit.save_flash > 0) edit.save_flash--;

            gallery_load_selected(&gal);

            if (!edit.active)
                gallery_handle_dpad(&gal, kDown);

            if (do_cam || (kDown & KEY_Y)) {
                CAMU_StopCapture(PORT_BOTH);
                for (int i = 0; i < 4; i++) {
                    if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
                }
                CAMU_Activate(SELECT_NONE);

                app.selfie = !app.selfie;
                camSelect  = app.selfie ? SELECT_IN1_OUT2 : SELECT_OUT1_OUT2;

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
                if (!app.selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
                CAMU_StartCapture(PORT_BOTH);
                captureInterrupted = false;
            }

            if (do_defaults_save) {
                app.default_params = app.params;
                settings_save(&app.default_params, app.save_scale);
                settings_save_palettes(app.user_palettes);
                settings_save_ranges(&app.ranges);
                app.settings_flash = 20;
            }
        }

        // Wiggle preview: B cancels, Save button confirms and writes APNG
        // D-pad and touch buttons work unconditionally (outside captureInterrupted guard)
        if (wig.preview) {
            wiggle_preview_update(&wig, &s_save, kDown, kHeld, do_save,
                                  wiggle_left, wiggle_right, &app.save_flash);
        } else if (shoot.timer_active) {
            timer_update(&shoot, &wig, &app, kDown,
                         buf, filtered_buf, wiggle_left, wiggle_right,
                         wiggle_preview_frames);
        } else if ((do_save || (kDown & KEY_A)) && !s_save.busy && !gal.mode && !edit.active) {
            shoot_trigger(&shoot, &wig, &app,
                          buf, filtered_buf, wiggle_left, wiggle_right,
                          wiggle_preview_frames);
        }

        if (s_save.busy) app.save_flash = 20;  // pin high while thread is working
        else if (app.save_flash > 0) app.save_flash--;
        if (app.settings_flash > 0) app.settings_flash--;

        bool use3d = CONFIG_3D_SLIDERSTATE > 0.0f;
        bool comparing = (kHeld & KEY_SELECT) != 0;

        if (app.cam_active) {
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
                if (shoot.shoot_mode == SHOOT_MODE_LOMO) {
                    // Lomo: raw camera feed — apply tonal preset + FX only, no GB pixel filter
                    const LomoPreset *lp = &lomo_presets[shoot.lomo_preset];
                    FilterParams lp_params = app.params;
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
                    apply_fx(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, lp_params, app.frame_count);
                } else if (shoot.shoot_mode == SHOOT_MODE_WIGGLE) {
                    // Wiggle: true-colour preview — skip all filters
                } else {
                    apply_gameboy_filter(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, app.params);
                    apply_fx(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, app.params, app.frame_count);
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
        if (wig.preview) {
            wiggle_preview_tick(&wig, wiggle_preview_frames, wiggle_left, wiggle_right);
        }

        gallery_tick(&gal);

        // Blit camera frame to top screen raw framebuffer
        gfxSet3D(false);
        if (use3d || shoot.timer_open) {
            u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            memset(fb, 0, CAMERA_WIDTH * CAMERA_HEIGHT * 3);
        } else if (edit.active && gal.count > 0) {
            // Build composited edit preview: photo + stickers + frame
            static const char *s_frame_paths[FRAME_COUNT] = FRAME_PATHS_INIT;
            // Base photo
            rgb565_to_rgb888(edit_preview_rgb888,
                             (const uint16_t *)gallery_thumbs[gal.anim_frame],
                             CAMERA_WIDTH * CAMERA_HEIGHT);
            // Stickers
            for (int si = 0; si < STICKER_MAX; si++) {
                if (!edit.placed[si].active) continue;
                const unsigned char *px = get_sticker_pixels(edit.placed[si].cat_idx,
                                                              edit.placed[si].icon_idx);
                if (px)
                    composite_sticker_rgb888(edit_preview_rgb888,
                                             CAMERA_WIDTH, CAMERA_HEIGHT,
                                             px,
                                             edit.placed[si].x,
                                             edit.placed[si].y,
                                             edit.placed[si].scale,
                                             edit.placed[si].angle_deg);
            }
            // Frame overlay — composite from romfs path (only when active)
            if (edit.gallery_frame >= 0 && edit.gallery_frame < FRAME_COUNT)
                composite_frame_rgb888(edit_preview_rgb888,
                                       CAMERA_WIDTH, CAMERA_HEIGHT,
                                       s_frame_paths[edit.gallery_frame]);
            // Cursor crosshair: visible when placing (edit.placing == true)
            if (edit.placing && edit.tab == 0) {
                int cx = (int)edit.cursor_x;
                int cy = (int)edit.cursor_y;
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
                    const unsigned char *cpx = get_sticker_pixels(edit.sticker_cat, edit.sticker_sel);
                    if (cpx) {
                        composite_sticker_rgb888(edit_preview_rgb888,
                                                 CAMERA_WIDTH, CAMERA_HEIGHT,
                                                 cpx, cx, cy,
                                                 edit.pending_scale, edit.pending_angle);
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
            if (wig.preview) {
                // Compose cropped frame into a full 400×240 buffer (black borders),
                // then blit normally. This keeps the framebuffer column stride correct.
                int bx = (CAMERA_WIDTH  - wig.crop_w) / 2;
                int by = (CAMERA_HEIGHT - wig.crop_h) / 2;
                memset(wiggle_compose_buf, 0, sizeof(wiggle_compose_buf));
                const uint16_t *src = wiggle_preview_frames[wig.preview_frame];
                for (int row = 0; row < wig.crop_h; row++)
                    memcpy(wiggle_compose_buf + (by + row) * CAMERA_WIDTH + bx,
                           src + row * wig.crop_w,
                           wig.crop_w * sizeof(uint16_t));
                writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                                wiggle_compose_buf, 0, 0,
                                                CAMERA_WIDTH, CAMERA_HEIGHT);
            } else {
                void *blit_src;
                if (gal.mode && gal.count > 0)
                    blit_src = gallery_thumbs[gal.anim_frame];
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
        draw_ui(bot, staticBuf, dynBuf, app.params, app.selfie, app.save_flash, use3d,
                app.active_tab, app.save_scale, app.settings_flash > 0,
                app.settings_row,
                app.user_palettes, app.palette_sel_pal, app.palette_sel_color,
                &app.ranges, comparing,
                gal.mode, gal.count,
                (const char (*)[64])gal.paths, gal.sel, gal.scroll,
                shoot.shoot_mode, shoot.shoot_mode_open,
                shoot.shoot_timer_secs, shoot.timer_open,
                wig.n_frames, wig.delay_ms,
                wig.preview,
                wig.offset_dx, wig.offset_dy,
                shoot.timer_active ? (shoot.timer_remaining_ms + 999) / 1000 : -1,
                shoot.lomo_preset,
                edit.active,
                edit.tab, edit.sticker_cat, edit.sticker_sel, edit.sticker_scroll,
                edit.gallery_frame,
                edit.cursor_x, edit.cursor_y,
                edit.pending_scale, edit.pending_angle,
                edit.placing);
        C3D_FrameEnd(0);
        app.frame_count++;
    }

    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++)
        if (camReceiveEvent[i]) svcCloseHandle(camReceiveEvent[i]);
    CAMU_Activate(SELECT_NONE);

    // Gracefully stop the save thread before freeing its buffers
    save_thread_stop(save_thread);

    free(buf);
    free(filtered_buf);
    free(rgb_buf);
    free(snapshot_buf);
    free(snapshot_buf2);
    free(wiggle_left);
    free(wiggle_right);
    C2D_TextBufDelete(staticBuf);
    C2D_TextBufDelete(dynBuf);
    cleanup();
    return 0;
}
