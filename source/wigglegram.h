#ifndef WIGGLEGRAM_H
#define WIGGLEGRAM_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of animation frames in a wiggle preview / saved APNG.
#define WIGGLE_PREVIEW_MAX 8

// Auto-detected stereo alignment (global translation only).
typedef struct {
    int global_dx;  // pixels to shift right image rightward to align with left
    int global_dy;
} WiggleAlign;

// Compute the dominant stereo offset between left and right RGB565 frames.
// Populates *align with the best global_dx / global_dy.
void wiggle_align(WiggleAlign *align,
                  const uint8_t *left_rgb565,
                  const uint8_t *right_rgb565,
                  int w, int h);

// Build preview frames from a stereo pair cropped to the overlap (AND) region.
// Output frames are (w - |fdx|) x (h - |fdy|) pixels, stored row-major in dst.
// out_w / out_h (if non-NULL) receive the actual crop dimensions.
// Returns 2 (the actual frame count used).
int build_wiggle_preview_frames(uint16_t dst[][400 * 240],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int w, int h, int nf,
                                const WiggleAlign *align,
                                int offset_dx, int offset_dy,
                                int *out_w, int *out_h);

// Save a true-colour wiggle APNG from two raw RGB565 camera buffers.
// n_frames: number of animation frames (2..8).
// delay_ms: milliseconds per frame.
// align: alignment result (pass NULL to skip offset correction).
// offset_dx/dy: user alignment adjustment in pixels.
// Returns 1 on success, 0 on failure.
int save_wiggle_apng(const char *path,
                     const uint8_t *left_rgb565,  int w, int h,
                     const uint8_t *right_rgb565,
                     int n_frames, int delay_ms,
                     const WiggleAlign *align,
                     int offset_dx, int offset_dy);

#endif
