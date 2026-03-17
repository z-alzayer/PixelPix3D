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

#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define WAIT_TIMEOUT 1000000000ULL

static jmp_buf exitJmp;

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void cleanup(void) {
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
    if (!buf || !filtered_buf || !rgb_buf) longjmp(exitJmp, 1);
    memset(filtered_buf, 0, CAMERA_SCREEN_SIZE);

    // Filter params
    FilterParams params         = FILTER_DEFAULTS;
    FilterParams default_params = FILTER_DEFAULTS;
    int          save_scale     = 2;
    int          active_tab     = 0;

    // Mutable palette copies + palette tab state
    PaletteDef user_palettes[PALETTE_COUNT];
    int        settings_row      = 0;   // 0=Save Scale, 1=Dither, 2=Invert
    int        palette_sel_pal   = 0;
    int        palette_sel_color = 0;

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

    for (int i = 0; i < PALETTE_COUNT; i++) user_palettes[i] = palettes[i];
    settings_load(&params, &save_scale);
    settings_load_palettes(user_palettes);
    default_params = params;
    filter_set_user_palettes(user_palettes);

    while (aptMainLoop()) {

        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

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
            // D-pad: context-aware per active_tab
            if (active_tab == 0) {
                if (kDown & KEY_DUP)    { params.brightness += 0.1f; if (params.brightness > 2.0f) params.brightness = 2.0f; }
                if (kDown & KEY_DDOWN)  { params.brightness -= 0.1f; if (params.brightness < 0.0f) params.brightness = 0.0f; }
                if (kDown & KEY_DLEFT)  { params.saturation -= 0.1f; if (params.saturation < 0.0f) params.saturation = 0.0f; }
                if (kDown & KEY_DRIGHT) { params.saturation += 0.1f; if (params.saturation > 2.0f) params.saturation = 2.0f; }
            } else if (active_tab == 1) {
                if (kDown & KEY_DUP)   { settings_row--; if (settings_row < 0) settings_row = 2; }
                if (kDown & KEY_DDOWN) { settings_row++; if (settings_row > 2) settings_row = 0; }
                if ((kDown & KEY_DLEFT) || (kDown & KEY_DRIGHT)) {
                    if      (settings_row == 0) save_scale         = (save_scale == 1) ? 2 : 1;
                    else if (settings_row == 1) params.dither_mode = !params.dither_mode;
                    else if (settings_row == 2) params.invert      = !params.invert;
                }
            } else if (active_tab == 2) {
                if (kDown & KEY_DUP)    { if (--palette_sel_pal   < 0)                                   palette_sel_pal   = PALETTE_COUNT - 1; palette_sel_color = 0; }
                if (kDown & KEY_DDOWN)  { if (++palette_sel_pal   >= PALETTE_COUNT)                      palette_sel_pal   = 0;                 palette_sel_color = 0; }
                if (kDown & KEY_DLEFT)  { if (--palette_sel_color < 0)                                   palette_sel_color = user_palettes[palette_sel_pal].size - 1; }
                if (kDown & KEY_DRIGHT) { if (++palette_sel_color >= user_palettes[palette_sel_pal].size) palette_sel_color = 0; }
            }

            // Touch input
            touchPosition touch;
            hidTouchRead(&touch);

            bool do_cam = false, do_save = false, do_defaults_save = false;
            handle_touch(touch, kDown, kHeld, &params, &do_cam, &do_save, &do_defaults_save,
                         &active_tab, &save_scale, &default_params,
                         user_palettes, &palette_sel_pal, &palette_sel_color);

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

            if (do_save || (kDown & KEY_A)) {
                char save_path[64];
                if (next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
                    uint8_t *upscale_buf = camera_get_upscale_buf();
                    rgb565_to_rgb888(rgb_buf, (const uint16_t *)filtered_buf, CAMERA_WIDTH * CAMERA_HEIGHT);
                    nn_upscale(upscale_buf, rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, save_scale);
                    if (save_jpeg(save_path, upscale_buf, CAMERA_WIDTH * save_scale, CAMERA_HEIGHT * save_scale))
                        save_flash = 20;
                }
            }

            if (do_defaults_save) {
                default_params = params;
                settings_save(&default_params, save_scale);
                settings_save_palettes(user_palettes);
                settings_flash = 20;
            }
        }

        if (save_flash > 0) save_flash--;
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
            if (!use3d) {
                rgb565_to_rgb888(rgb_buf, (const uint16_t *)buf, CAMERA_WIDTH * CAMERA_HEIGHT);
                apply_gameboy_filter(rgb_buf, CAMERA_WIDTH, CAMERA_HEIGHT, params);
                rgb888_to_rgb565((uint16_t *)filtered_buf, rgb_buf, CAMERA_WIDTH * CAMERA_HEIGHT);
            }
            break;
        case 3:
            svcCloseHandle(camReceiveEvent[3]); camReceiveEvent[3] = 0;
            break;
        default:
            break;
        }

        // Blit camera frame to top screen raw framebuffer
        gfxSet3D(false);
        if (use3d) {
            u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            for (int i = 0; i < CAMERA_WIDTH * CAMERA_HEIGHT; i++) {
                fb[i*3+0] = 0;    // B
                fb[i*3+1] = 0;    // G
                fb[i*3+2] = 180;  // R
            }
        } else {
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            filtered_buf, 0, 0, CAMERA_WIDTH, CAMERA_HEIGHT);
        }

        // Flush top screen before C3D takes the GPU
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, true);

        // Draw bottom screen UI with citro2d
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        draw_ui(bot, staticBuf, dynBuf, params, selfie, save_flash > 0, use3d,
                active_tab, save_scale, settings_flash > 0,
                settings_row,
                user_palettes, palette_sel_pal, palette_sel_color);
        C3D_FrameEnd(0);
    }

    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++)
        if (camReceiveEvent[i]) svcCloseHandle(camReceiveEvent[i]);
    CAMU_Activate(SELECT_NONE);

    free(buf);
    free(filtered_buf);
    free(rgb_buf);
    C2D_TextBufDelete(staticBuf);
    C2D_TextBufDelete(dynBuf);
    cleanup();
    return 0;
}
