#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>
#include <3ds.h>

// ---------------------------------------------------------------------------
// Dimensions / sizes
// ---------------------------------------------------------------------------

#define CAMERA_WIDTH       400
#define CAMERA_HEIGHT      240
#define CAMERA_SCREEN_SIZE (CAMERA_WIDTH * CAMERA_HEIGHT * 2)
#define CAMERA_BUF_SIZE    (CAMERA_SCREEN_SIZE * 2)
#define SAVE_SCALE         2
#define MAX_SAVE_SCALE     4

// VGA capture resolution (wiggle mode)
#define VGA_WIDTH          640
#define VGA_HEIGHT         480
#define VGA_SCREEN_SIZE    (VGA_WIDTH * VGA_HEIGHT * 2)
#define VGA_BUF_SIZE       (VGA_SCREEN_SIZE * 2)

// ---------------------------------------------------------------------------
// Colour conversion
// ---------------------------------------------------------------------------

void rgb565_to_rgb888(uint8_t *dst, const uint16_t *src, int count);
void rgb888_to_rgb565(uint16_t *dst, const uint8_t *src, int count);

// ---------------------------------------------------------------------------
// Upscaling
// ---------------------------------------------------------------------------

void nn_upscale(uint8_t *dst, const uint8_t *src, int w, int h, int scale);

// ---------------------------------------------------------------------------
// Framebuffer blit (RGB565 image → column-major BGR8 top framebuffer)
// ---------------------------------------------------------------------------

void writePictureToFramebufferRGB565(void *fb, void *img,
                                     u16 x, u16 y, u16 w, u16 h);

// Preview-only scaler: center-crop source to match destination aspect,
// then downscale to fill the destination without stretching.
void crop_fill_rgb565(uint16_t *dst, int dst_w, int dst_h,
                      const uint16_t *src, int src_w, int src_h);

// ---------------------------------------------------------------------------
// Camera resolution switch (SIZE_VGA ↔ SIZE_CTR_TOP_LCD)
// ---------------------------------------------------------------------------

void camera_set_resolution(int width, int height,
                           u32 camSelect, u32 *bufSize,
                           Handle camReceiveEvent[4], bool *captureInterrupted,
                           bool selfie);

// ---------------------------------------------------------------------------
// Camera toggle (swap front ↔ rear)
// ---------------------------------------------------------------------------

void camera_toggle(bool *selfie, u32 *camSelect, u32 *bufSize,
                   Handle camReceiveEvent[4], bool *captureInterrupted,
                   int cam_w, int cam_h);

#endif
