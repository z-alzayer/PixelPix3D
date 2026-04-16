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
#include "editor.h"
#include "render.h"

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
                edit_handle_input(&edit, kDown, kHeld);
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
            handle_touch(touch, kDown, kHeld,
                         &app, &shoot, &wig, &gal, &edit,
                         &do_cam, &do_save, &do_defaults_save,
                         &do_gallery_toggle,
                         &do_edit_cancel, &do_edit_savenew, &do_edit_overwrite,
                         &do_edit_enter_or_place);

            if (do_edit_enter_or_place)
                edit_enter_or_place(&edit);

            if (do_gallery_toggle) {
                gallery_toggle(&gal, &app, &edit, camReceiveEvent, &captureInterrupted);
            }

            if (do_edit_cancel)
                edit_cancel(&edit);

            if (do_edit_savenew)
                edit_save(&edit, &gal, false);
            else if (do_edit_overwrite)
                edit_save(&edit, &gal, true);
            if (edit.save_flash > 0) edit.save_flash--;

            gallery_load_selected(&gal);

            if (!edit.active)
                gallery_handle_dpad(&gal, kDown);

            if (do_cam || (kDown & KEY_Y)) {
                camera_toggle(&app.selfie, &camSelect, &bufSize,
                              camReceiveEvent, &captureInterrupted);
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
        render_top_screen(use3d, shoot.timer_open,
                          &edit, &gal, &wig,
                          edit_preview_rgb888, wiggle_compose_buf,
                          wiggle_preview_frames,
                          comparing, buf, filtered_buf);

        // Draw bottom screen UI with citro2d
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        draw_ui(bot, staticBuf, dynBuf,
                &app, &shoot, &wig, &gal, &edit,
                use3d, comparing,
                shoot.timer_active ? (shoot.timer_remaining_ms + 999) / 1000 : -1);
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
