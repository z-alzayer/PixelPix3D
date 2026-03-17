#include "camera.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Colour conversion
// ---------------------------------------------------------------------------

// Camera outputs RGB565: bits 15-11 = R, bits 10-5 = G, bits 4-0 = B
void rgb565_to_rgb888(uint8_t *dst, const uint16_t *src, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t p  = src[i];
        dst[i*3+0]  = ((p >> 11) & 0x1F) << 3;  // R
        dst[i*3+1]  = ((p >>  5) & 0x3F) << 2;  // G
        dst[i*3+2]  =  (p        & 0x1F) << 3;  // B
    }
}

// Pack RGB888 back to RGB565
void rgb888_to_rgb565(uint16_t *dst, const uint8_t *src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = ((uint16_t)(src[i*3+0] >> 3) << 11)
               | ((uint16_t)(src[i*3+1] >> 2) <<  5)
               |  (uint16_t)(src[i*3+2] >> 3);
    }
}

// ---------------------------------------------------------------------------
// Nearest-neighbour upscale RGB888
// ---------------------------------------------------------------------------

static uint8_t upscale_buf[SAVE_SCALE * CAMERA_WIDTH * SAVE_SCALE * CAMERA_HEIGHT * 3];

uint8_t *camera_get_upscale_buf(void) {
    return upscale_buf;
}

void nn_upscale(uint8_t *dst, const uint8_t *src, int w, int h, int scale) {
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
// Framebuffer blit (RGB565 image → column-major BGR8 top framebuffer)
// ---------------------------------------------------------------------------

void writePictureToFramebufferRGB565(void *fb, void *img,
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
