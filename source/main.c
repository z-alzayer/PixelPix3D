#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <3ds.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <stdbool.h>
#include "filter.h"
#include "image_load.h"

#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define WAIT_TIMEOUT 1000000000ULL

#define WIDTH       400
#define HEIGHT      240
#define SCREEN_SIZE WIDTH * HEIGHT * 2
#define BUF_SIZE    SCREEN_SIZE * 2

static jmp_buf exitJmp;

// ---------------------------------------------------------------------------
// Colour conversion helpers
// ---------------------------------------------------------------------------

static void bgr565_to_rgb888(uint8_t *dst, const uint16_t *src, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t p  = src[i];
        dst[i*3+0]  =  (p        & 0x1F) << 3;  // R
        dst[i*3+1]  = ((p >>  5) & 0x3F) << 2;  // G
        dst[i*3+2]  = ((p >> 11) & 0x1F) << 3;  // B
    }
}

static void rgb888_to_bgr565(uint16_t *dst, const uint8_t *src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = ((uint16_t)(src[i*3+2] >> 3) << 11)
               | ((uint16_t)(src[i*3+1] >> 2) <<  5)
               |  (uint16_t)(src[i*3+0] >> 3);
    }
}

// ---------------------------------------------------------------------------
// Framebuffer blit (BGR565 image → column-major BGR8 framebuffer)
// ---------------------------------------------------------------------------

static void writePictureToFramebufferRGB565(void *fb, void *img,
                                             u16 x, u16 y, u16 w, u16 h) {
    u8  *fb_8   = (u8 *)fb;
    u16 *img_16 = (u16 *)img;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            u32 v    = (y + h - j + (x + i) * h) * 3;
            u16 data = img_16[j * w + i];
            fb_8[v+0] = ((data >> 11) & 0x1F) << 3;  // B (framebuffer is BGR8)
            fb_8[v+1] = ((data >>  5) & 0x3F) << 2;  // G
            fb_8[v+2] =  (data        & 0x1F) << 3;  // R
        }
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

static bool hud_selfie = false;

static void print_params(FilterParams p) {
    printf("\x1b[1;1H");
    printf(" GameBoy Camera\n");
    printf(" --------------\n");
    if (p.palette >= 0 && p.palette < PALETTE_COUNT)
        printf(" Palette:  [%d] %s      \n", p.palette, palettes[p.palette].name);
    else
        printf(" Palette:  colour (%d lvl)   \n", p.color_levels);
    printf(" Px size:  %d    \n", p.pixel_size);
    printf(" Brightness: %.1f  \n", p.contrast);
    printf(" Saturation: %.1f  \n", p.saturation);
    printf(" Camera:   %s    \n", hud_selfie ? "selfie" : "outer");
    printf("\n");
    printf(" L/R   palette\n");
    printf(" B     px size\n");
    printf(" U/D   brightness\n");
    printf(" </> saturation\n");
    printf(" Y     camera\n");
    printf(" A     save\n");
    printf(" START exit\n");
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void cleanup(void) {
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
    consoleInit(GFX_BOTTOM, NULL);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    if (setjmp(exitJmp)) { cleanup(); return 0; }

    // Camera init
    // camSelect: SELECT_OUT1_OUT2 = dual outer, SELECT_IN1_OUT2 = selfie + right outer
    u32 camSelect = SELECT_OUT1_OUT2;
    bool selfie = false;

    printf("camInit: 0x%08X\n", (unsigned int)camInit());
    CAMU_SetSize(camSelect, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(camSelect, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(camSelect, FRAME_RATE_30);
    CAMU_SetNoiseFilter(camSelect, true);
    CAMU_SetAutoExposure(camSelect, true);
    CAMU_SetAutoWhiteBalance(camSelect, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_SetTrimming(PORT_CAM2, false);

    // Buffers
    u8 *buf = malloc(BUF_SIZE);
    if (!buf) {
        printf("malloc failed!\nPress START\n");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysHeld() & KEY_START) longjmp(exitJmp, 1); }
    }

    // filtered_buf holds the GB-filtered version of the CAM1 frame (BGR565)
    u8 *filtered_buf = malloc(SCREEN_SIZE);
    // rgb_buf is a scratch buffer for the filter pipeline
    u8 *rgb_buf      = malloc(WIDTH * HEIGHT * 3);
    if (!filtered_buf || !rgb_buf) {
        printf("malloc failed!\nPress START\n");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysHeld() & KEY_START) longjmp(exitJmp, 1); }
    }
    // Initialise filtered_buf to black so the screen isn't garbage before first frame
    memset(filtered_buf, 0, SCREEN_SIZE);

    // Filter params
    FilterParams params = FILTER_DEFAULTS;
    const int pixel_sizes[] = {1, 2, 4, 8};
    int ps_idx = 0;  // index into pixel_sizes; B cycles through

    u32 bufSize;
    printf("GetMaxBytes: 0x%08X\n",      (unsigned int)CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
    printf("SetTransferBytes: 0x%08X\n", (unsigned int)CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT));
    printf("Activate: 0x%08X\n",         (unsigned int)CAMU_Activate(SELECT_OUT1_OUT2));

    Handle camReceiveEvent[4] = {0};
    bool captureInterrupted = false;
    s32 index = 0;

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);

    CAMU_ClearBuffer(PORT_BOTH);
    CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);

    gfxFlushBuffers();
    gspWaitForVBlank();
    gfxSwapBuffers();

    print_params(params);

    while (aptMainLoop()) {

        if (!captureInterrupted) {
            hidScanInput();
            u32 kDown = hidKeysDown();

            if (kDown & KEY_START) break;

            // L/R bumpers: cycle palette
            if (kDown & KEY_L) {
                params.palette = (params.palette <= PALETTE_NONE)
                               ? PALETTE_COUNT - 1 : params.palette - 1;
                print_params(params);
            }
            if (kDown & KEY_R) {
                params.palette = (params.palette >= PALETTE_COUNT - 1)
                               ? PALETTE_NONE : params.palette + 1;
                print_params(params);
            }

            // B: cycle pixel size 1→2→4→8→1
            if (kDown & KEY_B) {
                ps_idx = (ps_idx + 1) % 4;
                params.pixel_size = pixel_sizes[ps_idx];
                print_params(params);
            }

            // D-pad up/down: brightness (contrast)
            if (kDown & KEY_DUP) {
                params.contrast += 0.1f;
                if (params.contrast > 3.0f) params.contrast = 3.0f;
                print_params(params);
            }
            if (kDown & KEY_DDOWN) {
                params.contrast -= 0.1f;
                if (params.contrast < 0.1f) params.contrast = 0.1f;
                print_params(params);
            }

            // D-pad left/right: saturation
            if (kDown & KEY_DLEFT) {
                params.saturation -= 0.1f;
                if (params.saturation < 0.0f) params.saturation = 0.0f;
                print_params(params);
            }
            if (kDown & KEY_DRIGHT) {
                params.saturation += 0.1f;
                if (params.saturation > 3.0f) params.saturation = 3.0f;
                print_params(params);
            }

            // Y: toggle outer/selfie camera
            if (kDown & KEY_Y) {
                // Tear down current capture
                CAMU_StopCapture(PORT_BOTH);
                for (int i = 0; i < 4; i++) {
                    if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
                }
                CAMU_Activate(SELECT_NONE);

                selfie = !selfie;
                hud_selfie = selfie;
                camSelect = selfie ? SELECT_IN1_OUT2 : SELECT_OUT1_OUT2;

                CAMU_SetSize(camSelect, SIZE_CTR_TOP_LCD, CONTEXT_A);
                CAMU_SetOutputFormat(camSelect, OUTPUT_RGB_565, CONTEXT_A);
                CAMU_SetFrameRate(camSelect, FRAME_RATE_30);
                CAMU_SetNoiseFilter(camSelect, true);
                CAMU_SetAutoExposure(camSelect, true);
                CAMU_SetAutoWhiteBalance(camSelect, true);
                CAMU_SetTrimming(PORT_CAM1, false);
                CAMU_SetTrimming(PORT_CAM2, false);

                CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT);
                CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT);
                CAMU_Activate(camSelect);

                CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
                CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
                CAMU_ClearBuffer(PORT_BOTH);
                // Only sync vsync timing when both sensors are outer cameras
                if (!selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
                CAMU_StartCapture(PORT_BOTH);
                captureInterrupted = false;
                print_params(params);
            }

            // Save
            if (kDown & KEY_A) {
                char save_path[64];
                if (!next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
                    printf("Save dir full!\n");
                } else {
                    // filtered_buf is BGR565 — convert to RGB888 for JPEG
                    bgr565_to_rgb888(rgb_buf, (const uint16_t *)filtered_buf, WIDTH * HEIGHT);
                    if (save_jpeg(save_path, rgb_buf, WIDTH, HEIGHT))
                        printf("Saved: %s\n", save_path);
                    else
                        printf("Save failed!\n");
                }
            }
        }

        // Queue receive for any port not already pending
        if (camReceiveEvent[2] == 0)
            CAMU_SetReceiving(&camReceiveEvent[2], buf,               PORT_CAM1, SCREEN_SIZE, (s16)bufSize);
        if (camReceiveEvent[3] == 0)
            CAMU_SetReceiving(&camReceiveEvent[3], buf + SCREEN_SIZE, PORT_CAM2, SCREEN_SIZE, (s16)bufSize);

        if (captureInterrupted) {
            CAMU_StartCapture(PORT_BOTH);
            captureInterrupted = false;
        }

        // Block until any event fires
        svcWaitSynchronizationN(&index, camReceiveEvent, 4, false, WAIT_TIMEOUT);
        switch (index) {
        case 0:  // CAM1 buffer error
            svcCloseHandle(camReceiveEvent[2]); camReceiveEvent[2] = 0;
            captureInterrupted = true;
            continue;
        case 1:  // CAM2 buffer error
            svcCloseHandle(camReceiveEvent[3]); camReceiveEvent[3] = 0;
            captureInterrupted = true;
            continue;
        case 2:  // CAM1 frame ready — apply filter
            svcCloseHandle(camReceiveEvent[2]); camReceiveEvent[2] = 0;
            bgr565_to_rgb888(rgb_buf, (const uint16_t *)buf, WIDTH * HEIGHT);
            apply_gameboy_filter(rgb_buf, WIDTH, HEIGHT, params);
            rgb888_to_bgr565((uint16_t *)filtered_buf, rgb_buf, WIDTH * HEIGHT);
            break;
        case 3:  // CAM2 frame ready (used for 3D right eye only)
            svcCloseHandle(camReceiveEvent[3]); camReceiveEvent[3] = 0;
            break;
        default:
            break;
        }

        if (CONFIG_3D_SLIDERSTATE > 0.0f) {
            gfxSet3D(true);
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT,  NULL, NULL), filtered_buf,      0, 0, WIDTH, HEIGHT);
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), buf + SCREEN_SIZE, 0, 0, WIDTH, HEIGHT);
        } else {
            gfxSet3D(false);
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT,  NULL, NULL), filtered_buf,      0, 0, WIDTH, HEIGHT);
        }

        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++)
        if (camReceiveEvent[i]) svcCloseHandle(camReceiveEvent[i]);
    CAMU_Activate(SELECT_NONE);

    free(buf);
    free(filtered_buf);
    free(rgb_buf);
    cleanup();
    return 0;
}
