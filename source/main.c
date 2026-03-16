#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <3ds.h>
#include <citro2d.h>
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

// Bottom screen dimensions
#define BOT_W  320
#define BOT_H  240

static jmp_buf exitJmp;

// ---------------------------------------------------------------------------
// Colour conversion helpers
// ---------------------------------------------------------------------------

// Camera outputs RGB565: bits 15-11 = R, bits 10-5 = G, bits 4-0 = B
static void rgb565_to_rgb888(uint8_t *dst, const uint16_t *src, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t p  = src[i];
        dst[i*3+0]  = ((p >> 11) & 0x1F) << 3;  // R
        dst[i*3+1]  = ((p >>  5) & 0x3F) << 2;  // G
        dst[i*3+2]  =  (p        & 0x1F) << 3;  // B
    }
}

// Pack RGB888 back to RGB565 for the filtered_buf intermediate
static void rgb888_to_rgb565(uint16_t *dst, const uint8_t *src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = ((uint16_t)(src[i*3+0] >> 3) << 11)
               | ((uint16_t)(src[i*3+1] >> 2) <<  5)
               |  (uint16_t)(src[i*3+2] >> 3);
    }
}

// ---------------------------------------------------------------------------
// Nearest-neighbour upscale RGB888
// ---------------------------------------------------------------------------

#define SAVE_SCALE  2   // change to 3 for 1200x720

// Static buffer: SAVE_SCALE^2 * 400 * 240 * 3 bytes
static uint8_t upscale_buf[SAVE_SCALE * WIDTH * SAVE_SCALE * HEIGHT * 3];

static void nn_upscale(uint8_t *dst, const uint8_t *src,
                       int w, int h, int scale) {
    int dw = w * scale;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *sp = src + (y * w + x) * 3;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    uint8_t *dp = dst + ((y * scale + dy) * dw + (x * scale + dx)) * 3;
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                    dp[2] = sp[2];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Framebuffer blit (BGR565 image -> column-major BGR8 top framebuffer)
// ---------------------------------------------------------------------------

static void writePictureToFramebufferRGB565(void *fb, void *img,
                                             u16 x, u16 y, u16 w, u16 h) {
    u8  *fb_8   = (u8 *)fb;
    u16 *img_16 = (u16 *)img;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            u32 v    = (y + h - j + (x + i) * h) * 3;
            u16 data = img_16[j * w + i];
            // filtered_buf is RGB565 (R=bits15-11, G=bits10-5, B=bits4-0)
            // framebuffer is BGR8 (B at byte 0, G at byte 1, R at byte 2)
            fb_8[v+0] =  (data        & 0x1F) << 3;  // B
            fb_8[v+1] = ((data >>  5) & 0x3F) << 2;  // G
            fb_8[v+2] = ((data >> 11) & 0x1F) << 3;  // R
        }
    }
}

// ---------------------------------------------------------------------------
// UI colours (RGBA)
// ---------------------------------------------------------------------------

#define CLR_BG       C2D_Color32( 18,  18,  24, 255)
#define CLR_TRACK    C2D_Color32( 55,  55,  60, 255)
#define CLR_FILL     C2D_Color32( 68, 148,  68, 255)
#define CLR_HANDLE   C2D_Color32(168, 224,  88, 255)
#define CLR_BTN      C2D_Color32( 38,  42,  50, 255)
#define CLR_BTN_SEL  C2D_Color32( 68, 148,  68, 255)
#define CLR_TEXT     C2D_Color32(210, 228, 190, 255)
#define CLR_DIM      C2D_Color32(120, 130, 110, 255)
#define CLR_DIVIDER  C2D_Color32( 50,  55,  60, 255)
#define CLR_TITLE    C2D_Color32(168, 224,  88, 255)

// ---------------------------------------------------------------------------
// Slider + button geometry
// ---------------------------------------------------------------------------

#define TRACK_X   102
#define TRACK_W   176
#define TRACK_H     6
#define HANDLE_W   12
#define HANDLE_H   20

// Continuous slider row centres (y of the track mid-line)
// 5 sliders + snap + palette need to fit in 240px.
// Title bar: 0-29. Sliders: 30-172. Palette: 178-240.
#define ROW_BRIGHT    44
#define ROW_CONTRAST  72
#define ROW_SAT      100
#define ROW_GAMMA    128

// Pixel-size snap stops (values 1-8, evenly spaced across the track)
#define ROW_PXSIZE   158
#define PX_STOPS     8
// px_stop_x[i] = x pixel for value (i+1), mapped onto TRACK_X..TRACK_X+TRACK_W
static int px_stop_x(int val) {
    // val in [1..8], map to [TRACK_X .. TRACK_X+TRACK_W]
    return TRACK_X + (val - 1) * TRACK_W / (PX_STOPS - 1);
}

// Palette buttons  (y=206..238)
#define PAL_BTN_Y     206
#define PAL_BTN_H      30
#define PAL_BTN_W      42
#define PAL_BTN_X0      4

// Action buttons top-right
#define BTN_CAM_X     213
#define BTN_CAM_Y       4
#define BTN_CAM_W      46
#define BTN_CAM_H      22

#define BTN_SAVE_X    264
#define BTN_SAVE_Y      4
#define BTN_SAVE_W     50
#define BTN_SAVE_H     22

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline float slider_val_to_x(float val, float mn, float mx) {
    float t = (val - mn) / (mx - mn);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return TRACK_X + t * TRACK_W;
}

static inline float touch_x_to_val(int px, float mn, float mx) {
    float t = (float)(px - TRACK_X) / TRACK_W;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return mn + t * (mx - mn);
}

// ---------------------------------------------------------------------------
// Draw UI
// ---------------------------------------------------------------------------

static void draw_slider(float cx, float cy, float mn, float mx, float val) {
    // Track background
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    // Filled portion
    float hx = slider_val_to_x(val, mn, mx);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    // Handle
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
    (void)cx;
}

static void draw_snap_slider(int px_val) {
    float cy = ROW_PXSIZE;
    // Track background
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    // Filled portion up to current value
    float hx = px_stop_x(px_val);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    // Handle
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
}

static void draw_ui(C3D_RenderTarget *bot,
                    C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                    FilterParams p, bool selfie,
                    bool save_flash, bool warn3d) {
    C2D_TargetClear(bot, CLR_BG);
    C2D_SceneBegin(bot);

    if (warn3d) {
        C2D_Text t;
        C2D_TextBufClear(staticBuf);
        C2D_TextParse(&t, staticBuf, "3D slider not supported");
        C2D_DrawText(&t, C2D_WithColor, 34.0f, 108.0f, 0.5f, 0.55f, 0.55f, C2D_Color32(220, 80, 80, 255));
        C2D_TextParse(&t, staticBuf, "Please set slider to 0");
        C2D_DrawText(&t, C2D_WithColor, 38.0f, 128.0f, 0.5f, 0.48f, 0.48f, C2D_Color32(180, 60, 60, 255));
        return;
    }

    // Title bar divider
    C2D_DrawRectSolid(0, 29, 0.5f, BOT_W, 1, CLR_DIVIDER);
    // Slider area / palette divider
    C2D_DrawRectSolid(0, 200, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Static text labels ---
    float sc = 0.48f;  // scale for system font (~14px glyphs at 0.48)

    C2D_Text t;
    C2D_TextBufClear(staticBuf);

    C2D_TextParse(&t, staticBuf, "PixelPix3D");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 6.0f, 0.5f, 0.52f, 0.52f, CLR_TITLE);

    C2D_TextParse(&t, staticBuf, "Bright");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Contrast");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Saturate");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Gamma");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Px Size");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_PXSIZE - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Palette");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 185.0f, 0.5f, sc, sc, CLR_DIM);

    // Px size tick labels at 1, 4, 8
    C2D_TextParse(&t, staticBuf, "1");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(1) - 2.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "4");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(4) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "8");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(8) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    // Palette button labels
    const char *pal_names[7] = {"GB","Gray","GBC","Shell","GBA","DB","Clr"};
    for (int i = 0; i < 7; i++) {
        int pal_val = (i < 6) ? i : PALETTE_NONE;
        bool sel = (p.palette == pal_val);
        float bx = PAL_BTN_X0 + i * (PAL_BTN_W + 2);
        C2D_DrawRectSolid(bx, PAL_BTN_Y, 0.5f, PAL_BTN_W, PAL_BTN_H,
                          sel ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, pal_names[i]);
        C2D_DrawText(&t, C2D_WithColor, bx + 4.0f, PAL_BTN_Y + 8.0f, 0.5f,
                     0.40f, 0.40f, sel ? CLR_BG : CLR_TEXT);
    }

    // CAM / SAVE buttons
    u32 cam_clr  = selfie ? CLR_BTN_SEL : CLR_BTN;
    u32 save_clr = save_flash ? CLR_HANDLE : CLR_BTN;
    C2D_DrawRectSolid(BTN_CAM_X,  BTN_CAM_Y,  0.5f, BTN_CAM_W,  BTN_CAM_H,  cam_clr);
    C2D_DrawRectSolid(BTN_SAVE_X, BTN_SAVE_Y, 0.5f, BTN_SAVE_W, BTN_SAVE_H, save_clr);

    C2D_TextParse(&t, staticBuf, selfie ? "Selfie" : "Outer");
    C2D_DrawText(&t, C2D_WithColor, BTN_CAM_X + 4.0f, BTN_CAM_Y + 5.0f, 0.5f,
                 0.40f, 0.40f, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Save");
    C2D_DrawText(&t, C2D_WithColor, BTN_SAVE_X + 14.0f, BTN_SAVE_Y + 5.0f, 0.5f,
                 0.40f, 0.40f, CLR_TEXT);

    // --- Sliders ---
    draw_slider(0, ROW_BRIGHT,   0.0f, 2.0f, p.brightness);
    draw_slider(0, ROW_CONTRAST, 0.5f, 2.0f, p.contrast);
    draw_slider(0, ROW_SAT,      0.0f, 2.0f, p.saturation);
    draw_slider(0, ROW_GAMMA,    0.5f, 2.0f, p.gamma);
    draw_snap_slider(p.pixel_size);

    // --- Dynamic value readouts ---
    char buf[16];
    C2D_TextBufClear(dynBuf);

    snprintf(buf, sizeof(buf), "%.1f", p.brightness);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.contrast);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.saturation);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.gamma);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%d", p.pixel_size);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_PXSIZE - 9.0f, 0.5f, sc, sc, CLR_DIM);
}

// ---------------------------------------------------------------------------
// Touch handling
// ---------------------------------------------------------------------------

// Returns true if px,py is inside the rect [rx, ry, rw, rh]
static inline bool hit(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                         FilterParams *p,
                         bool *do_cam_toggle, bool *do_save) {
    *do_cam_toggle = false;
    *do_save       = false;

    bool touched = (kHeld & KEY_TOUCH) != 0;
    bool tapped  = (kDown & KEY_TOUCH) != 0;
    if (!touched) return false;

    int tx = touch.px, ty = touch.py;

    // CAM button (tap only)
    if (tapped && hit(tx, ty, BTN_CAM_X, BTN_CAM_Y, BTN_CAM_W, BTN_CAM_H)) {
        *do_cam_toggle = true;
        return true;
    }

    // SAVE button (tap only)
    if (tapped && hit(tx, ty, BTN_SAVE_X, BTN_SAVE_Y, BTN_SAVE_W, BTN_SAVE_H)) {
        *do_save = true;
        return true;
    }

    // Palette buttons (tap only)
    if (tapped && ty >= PAL_BTN_Y && ty < PAL_BTN_Y + PAL_BTN_H) {
        for (int i = 0; i < 7; i++) {
            int bx = PAL_BTN_X0 + i * (PAL_BTN_W + 2);
            if (tx >= bx && tx < bx + PAL_BTN_W) {
                p->palette = (i < 6) ? i : PALETTE_NONE;
                return true;
            }
        }
    }

    // Pixel-size slider (drag, snaps to integer 1-8)
    if (ty >= ROW_PXSIZE - 14 && ty < ROW_PXSIZE + 14 &&
        tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
        float t_val = (float)(tx - TRACK_X) / TRACK_W;
        if (t_val < 0.0f) t_val = 0.0f;
        if (t_val > 1.0f) t_val = 1.0f;
        int val = (int)(t_val * (PX_STOPS - 1) + 0.5f) + 1;
        if (val < 1) val = 1;
        if (val > PX_STOPS) val = PX_STOPS;
        p->pixel_size = val;
        return true;
    }

    // Continuous sliders (drag)
    if (tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
        if (ty >= ROW_BRIGHT - 13 && ty < ROW_BRIGHT + 13) {
            p->brightness = touch_x_to_val(tx, 0.0f, 2.0f);
            return true;
        }
        if (ty >= ROW_CONTRAST - 13 && ty < ROW_CONTRAST + 13) {
            p->contrast = touch_x_to_val(tx, 0.5f, 2.0f);
            return true;
        }
        if (ty >= ROW_SAT - 13 && ty < ROW_SAT + 13) {
            p->saturation = touch_x_to_val(tx, 0.0f, 2.0f);
            return true;
        }
        if (ty >= ROW_GAMMA - 13 && ty < ROW_GAMMA + 13) {
            p->gamma = touch_x_to_val(tx, 0.5f, 2.0f);
            return true;
        }
    }

    return false;
}

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

    // Init citro3d + citro2d for bottom screen
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
    u8 *buf = malloc(BUF_SIZE);
    u8 *filtered_buf  = malloc(SCREEN_SIZE);
    u8 *rgb_buf       = malloc(WIDTH * HEIGHT * 3);
    if (!buf || !filtered_buf || !rgb_buf) longjmp(exitJmp, 1);
    memset(filtered_buf, 0, SCREEN_SIZE);

    // Filter params
    FilterParams params = FILTER_DEFAULTS;

    u32 bufSize;
    CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT);
    CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT);
    CAMU_Activate(SELECT_OUT1_OUT2);

    Handle camReceiveEvent[4] = {0};
    bool captureInterrupted = false;
    s32 index = 0;

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
    CAMU_ClearBuffer(PORT_BOTH);
    CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);

    // save flash timer (frames to keep SAVE button highlighted)
    int save_flash = 0;

    while (aptMainLoop()) {

        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

        if (!captureInterrupted) {
            // Physical button fallbacks (kept alongside touch)
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
            if (kDown & KEY_DUP)   { params.brightness += 0.1f; if (params.brightness > 2.0f) params.brightness = 2.0f; }
            if (kDown & KEY_DDOWN) { params.brightness -= 0.1f; if (params.brightness < 0.0f) params.brightness = 0.0f; }
            if (kDown & KEY_DLEFT) { params.saturation -= 0.1f; if (params.saturation < 0.0f) params.saturation = 0.0f; }
            if (kDown & KEY_DRIGHT){ params.saturation += 0.1f; if (params.saturation > 2.0f) params.saturation = 2.0f; }

            // Touch input
            touchPosition touch;
            hidTouchRead(&touch);

            bool do_cam = false, do_save = false;
            handle_touch(touch, kDown, kHeld, &params,
                         &do_cam, &do_save);

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

                CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT);
                CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT);
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
                    rgb565_to_rgb888(rgb_buf, (const uint16_t *)filtered_buf, WIDTH * HEIGHT);
                    nn_upscale(upscale_buf, rgb_buf, WIDTH, HEIGHT, SAVE_SCALE);
                    if (save_jpeg(save_path, upscale_buf, WIDTH * SAVE_SCALE, HEIGHT * SAVE_SCALE))
                        save_flash = 20;  // highlight for 20 frames
                }
            }
        }

        if (save_flash > 0) save_flash--;

        // Queue camera receive — only CAM1 used
        bool use3d = CONFIG_3D_SLIDERSTATE > 0.0f;
        if (camReceiveEvent[2] == 0)
            CAMU_SetReceiving(&camReceiveEvent[2], buf,               PORT_CAM1, SCREEN_SIZE, (s16)bufSize);
        if (camReceiveEvent[3] == 0)
            CAMU_SetReceiving(&camReceiveEvent[3], buf + SCREEN_SIZE, PORT_CAM2, SCREEN_SIZE, (s16)bufSize);

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
                rgb565_to_rgb888(rgb_buf, (const uint16_t *)buf, WIDTH * HEIGHT);
                apply_gameboy_filter(rgb_buf, WIDTH, HEIGHT, params);
                rgb888_to_rgb565((uint16_t *)filtered_buf, rgb_buf, WIDTH * HEIGHT);
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
            // Fill top screen with dark red (BGR8: B=0, G=0, R=180)
            for (int i = 0; i < WIDTH * HEIGHT; i++) {
                fb[i*3+0] = 0;    // B
                fb[i*3+1] = 0;    // G
                fb[i*3+2] = 180;  // R
            }
        } else {
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), filtered_buf, 0, 0, WIDTH, HEIGHT);
        }

        // Flush top screen before C3D takes the GPU
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, true);

        // Draw bottom screen UI with citro2d
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        draw_ui(bot, staticBuf, dynBuf, params, selfie, save_flash > 0, use3d);
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
