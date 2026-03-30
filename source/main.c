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
#include "image_load.h"
#include "settings.h"
#include "sound.h"

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
                             st->wiggle_delay_ms);
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
    int  shoot_timer_secs = 5;
    int  wiggle_frames    = 4;
    int  wiggle_delay_ms  = 250;

    // Wiggle capture/preview state
    bool wiggle_preview      = false;  // true while showing captured pair before saving
    int  wiggle_preview_frame = 0;     // 0=left, 1=right (alternates for top-screen anim)
    int  wiggle_preview_ticks = 0;     // vblank ticks since last frame flip

    // Gallery state
    #define GALLERY_MAX 256
    bool gallery_mode   = false;
    int  gallery_sel    = 0;
    int  gallery_scroll = 0;
    static char gallery_paths[GALLERY_MAX][64];
    int  gallery_count  = 0;
    static uint16_t gallery_thumb[CAMERA_WIDTH * CAMERA_HEIGHT];
    int  gallery_loaded = -1;

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

    // Seed file counters once so path generation is O(1) at shutter time
    save_counter_init(SAVE_DIR);
    wiggle_counter_init(SAVE_DIR);

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
            // Physical button fallbacks
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
            // X: cycle through main tabs (Shoot → Style → FX → More → Shoot)
            if (kDown & KEY_X) {
                if (active_tab <= TAB_MORE)
                    active_tab = (active_tab + 1) % (TAB_MORE + 1);
            }
            // D-pad: context-aware per active_tab
            if (active_tab == TAB_SHOOT) {
                // On shoot screen: d-pad nudges brightness (up/down) and palette (left/right)
                if (kDown & KEY_DUP)    { params.brightness += 0.1f; if (params.brightness > ranges.bright_max) params.brightness = ranges.bright_max; }
                if (kDown & KEY_DDOWN)  { params.brightness -= 0.1f; if (params.brightness < ranges.bright_min) params.brightness = ranges.bright_min; }
                if (kDown & KEY_DLEFT)  { params.palette = (params.palette <= PALETTE_NONE) ? PALETTE_COUNT - 1 : params.palette - 1; }
                if (kDown & KEY_DRIGHT) { params.palette = (params.palette >= PALETTE_COUNT - 1) ? PALETTE_NONE : params.palette + 1; }
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
            handle_touch(touch, kDown, kHeld, &params, &do_cam, &do_save, &do_defaults_save,
                         &active_tab, &save_scale, &default_params,
                         &ranges, user_palettes, &palette_sel_pal, &palette_sel_color,
                         &do_gallery_toggle,
                         gallery_mode, gallery_count, &gallery_sel, &gallery_scroll,
                         &shoot_mode, &shoot_mode_open,
                         &shoot_timer_secs,
                         &wiggle_frames, &wiggle_delay_ms);

            if (do_gallery_toggle) {
                gallery_mode = !gallery_mode;
                if (gallery_mode) {
                    gallery_count  = list_saved_photos(SAVE_DIR, gallery_paths, GALLERY_MAX);
                    gallery_sel    = 0;
                    gallery_scroll = 0;
                    gallery_loaded = -1;
                }
            }

            // Load selected photo into thumb buffer when selection changes
            if (gallery_mode && gallery_count > 0 && gallery_loaded != gallery_sel) {
                load_jpeg_to_rgb565(gallery_paths[gallery_sel], gallery_thumb,
                                    CAMERA_WIDTH, CAMERA_HEIGHT);
                gallery_loaded = gallery_sel;
            }

            // Gallery d-pad scrolling (only when gallery is open on shoot tab)
            if (gallery_mode && active_tab == TAB_SHOOT) {
                int total_rows = (gallery_count + GALLERY_COLS - 1) / GALLERY_COLS;
                int max_scroll = total_rows - GALLERY_ROWS;
                if (max_scroll < 0) max_scroll = 0;
                if (kDown & KEY_DDOWN)  { if (gallery_scroll < max_scroll) gallery_scroll++; }
                if (kDown & KEY_DUP)    { if (gallery_scroll > 0) gallery_scroll--; }
                if (kDown & KEY_DRIGHT) { gallery_sel++; if (gallery_sel >= gallery_count) gallery_sel = gallery_count - 1; }
                if (kDown & KEY_DLEFT)  { gallery_sel--; if (gallery_sel < 0) gallery_sel = 0; }
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

        // Wiggle preview: B cancels, A/Save confirms and writes APNG
        if (wiggle_preview) {
            if (kDown & KEY_B) {
                wiggle_preview = false;
            } else if ((do_save || (kDown & KEY_A)) && !s_save.busy) {
                char apng_path[64];
                if (next_wiggle_path(SAVE_DIR, apng_path, sizeof(apng_path))) {
                    // wiggle_left/right already hold the raw RGB565 snapshots
                    memcpy(s_save.snapshot_buf,  wiggle_left,  CAMERA_SCREEN_SIZE);
                    memcpy(s_save.snapshot_buf2, wiggle_right, CAMERA_SCREEN_SIZE);
                    memcpy(s_save.save_path, apng_path, sizeof(apng_path));
                    s_save.wiggle_mode     = true;
                    s_save.wiggle_n_frames = wiggle_frames;
                    s_save.wiggle_delay_ms = wiggle_delay_ms;
                    s_save.busy = true;
                    save_flash  = 20;
                    play_shutter_click();
                    LightEvent_Signal(&s_save.request_event);
                    wiggle_preview = false;
                }
            }
        } else if ((do_save || (kDown & KEY_A)) && !s_save.busy) {
            if (shoot_mode == SHOOT_MODE_WIGGLE) {
                // First press: capture both cam buffers into wiggle preview
                memcpy(wiggle_left,  buf,                        CAMERA_SCREEN_SIZE);
                memcpy(wiggle_right, buf + CAMERA_SCREEN_SIZE,   CAMERA_SCREEN_SIZE);
                wiggle_preview       = true;
                wiggle_preview_frame = 0;
                wiggle_preview_ticks = 0;
                play_shutter_click();
            } else {
                // Normal JPEG save
                char save_path[64];
                if (next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
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

        // Always drain both camera ports to prevent buffer error interrupts
        bool use3d = CONFIG_3D_SLIDERSTATE > 0.0f;
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

        // Hold SELECT to bypass the filter and preview the raw camera feed
        bool comparing = (kHeld & KEY_SELECT) != 0;

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
            if (!use3d && !comparing && !s_save.busy) {
                rgb565_to_rgb888(rgb_buf, (const uint16_t *)buf, CAMERA_WIDTH * CAMERA_HEIGHT);
                // Wiggle mode: show true-colour preview — skip GB filter and FX
                if (shoot_mode != SHOOT_MODE_WIGGLE) {
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

        // Advance wiggle preview animation (flips at wiggle_delay_ms rate)
        if (wiggle_preview) {
            wiggle_preview_ticks++;
            int ticks_per_flip = wiggle_delay_ms * 60 / 1000;
            if (ticks_per_flip < 1) ticks_per_flip = 1;
            if (wiggle_preview_ticks >= ticks_per_flip) {
                wiggle_preview_frame = 1 - wiggle_preview_frame;
                wiggle_preview_ticks = 0;
            }
        }

        // Blit camera frame to top screen raw framebuffer
        gfxSet3D(false);
        if (use3d) {
            u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++) {
                fb[i*3+0] = 0;
                fb[i*3+1] = 0;
                fb[i*3+2] = 180;
            }
        } else {
            void *blit_src;
            if (wiggle_preview)
                blit_src = (wiggle_preview_frame == 0) ? wiggle_left : wiggle_right;
            else if (gallery_mode && gallery_count > 0)
                blit_src = gallery_thumb;
            else
                blit_src = comparing ? buf : filtered_buf;
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            blit_src, 0, 0, CAMERA_WIDTH, CAMERA_HEIGHT);
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
                shoot_timer_secs,
                wiggle_frames, wiggle_delay_ms);
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
