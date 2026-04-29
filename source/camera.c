#include "camera.h"
#include <stdbool.h>
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
            u32 v    = (y + h - 1 - j + (x + i) * h) * 3;
            u16 data = img_16[j * w + i];
            // filtered_buf is RGB565 (R=bits15-11, G=bits10-5, B=bits4-0)
            // framebuffer is BGR8 (B at byte 0, G at byte 1, R at byte 2)
            fb_8[v+0] =  (data        & 0x1F) << 3;  // B
            fb_8[v+1] = ((data >>  5) & 0x3F) << 2;  // G
            fb_8[v+2] = ((data >> 11) & 0x1F) << 3;  // R
        }
    }
}

void crop_fill_rgb565(uint16_t *dst, int dst_w, int dst_h,
                      const uint16_t *src, int src_w, int src_h) {
    int crop_w, crop_h, crop_x, crop_y;

    // Match destination aspect using a centered source crop.
    if ((long long)src_w * dst_h > (long long)src_h * dst_w) {
        crop_h = src_h;
        crop_w = (src_h * dst_w) / dst_h;
        if (crop_w < 1) crop_w = 1;
        crop_x = (src_w - crop_w) / 2;
        crop_y = 0;
    } else {
        crop_w = src_w;
        crop_h = (src_w * dst_h) / dst_w;
        if (crop_h < 1) crop_h = 1;
        crop_x = 0;
        crop_y = (src_h - crop_h) / 2;
    }

    for (int y = 0; y < dst_h; y++) {
        int sy = crop_y + (y * crop_h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int sx = crop_x + (x * crop_w) / dst_w;
            dst[y * dst_w + x] = src[sy * src_w + sx];
        }
    }
}

// ---------------------------------------------------------------------------
// Camera resolution switch (SIZE_VGA ↔ SIZE_CTR_TOP_LCD)
// ---------------------------------------------------------------------------

void camera_set_resolution(int width, int height,
                           u32 camSelect, u32 *bufSize,
                           Handle camReceiveEvent[4], bool *captureInterrupted,
                           bool selfie) {
    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++) {
        if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
    }

    CAMU_Size size = (width == VGA_WIDTH) ? SIZE_VGA : SIZE_CTR_TOP_LCD;
    CAMU_SetSize(camSelect, size, CONTEXT_A);
    CAMU_GetMaxBytes(bufSize, width, height);
    CAMU_SetTransferBytes(PORT_BOTH, *bufSize, width, height);

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
    CAMU_ClearBuffer(PORT_BOTH);
    if (!selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);
    *captureInterrupted = false;
}

// ---------------------------------------------------------------------------
// Camera toggle (swap front ↔ rear)
// ---------------------------------------------------------------------------

void camera_toggle(bool *selfie, u32 *camSelect, u32 *bufSize,
                   Handle camReceiveEvent[4], bool *captureInterrupted,
                   int cam_w, int cam_h) {
    CAMU_StopCapture(PORT_BOTH);
    for (int i = 0; i < 4; i++) {
        if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
    }
    CAMU_Activate(SELECT_NONE);

    *selfie    = !*selfie;
    *camSelect = *selfie ? SELECT_IN1_OUT2 : SELECT_OUT1_OUT2;

    CAMU_Size size = (cam_w == VGA_WIDTH) ? SIZE_VGA : SIZE_CTR_TOP_LCD;
    CAMU_SetSize(*camSelect, size, CONTEXT_A);
    CAMU_SetOutputFormat(*camSelect, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(*camSelect, FRAME_RATE_30);
    CAMU_SetNoiseFilter(*camSelect, true);
    CAMU_SetAutoExposure(*camSelect, true);
    CAMU_SetAutoWhiteBalance(*camSelect, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_SetTrimming(PORT_CAM2, false);

    CAMU_GetMaxBytes(bufSize, cam_w, cam_h);
    CAMU_SetTransferBytes(PORT_BOTH, *bufSize, cam_w, cam_h);
    CAMU_Activate(*camSelect);

    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[0], PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&camReceiveEvent[1], PORT_CAM2);
    CAMU_ClearBuffer(PORT_BOTH);
    if (!*selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    CAMU_StartCapture(PORT_BOTH);
    *captureInterrupted = false;
}
