#ifndef WIGGLEGRAM_H
#define WIGGLEGRAM_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of animation frames in a wiggle preview / saved APNG.
#define WIGGLE_PREVIEW_MAX 8

// Build blended preview frames from a left/right RGB565 stereo pair.
// dst must point to an array of at least nf buffers, each CAMERA_WIDTH*CAMERA_HEIGHT uint16_t.
// nf is clamped to [2, WIGGLE_PREVIEW_MAX].
// Returns the actual frame count used.
int build_wiggle_preview_frames(uint16_t dst[][400 * 240],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int w, int h, int nf);

// Save a true-colour wiggle APNG from two raw RGB565 camera buffers.
// No filter is applied — full 24-bit RGB output.
// n_frames: number of animation frames (2..8).
// delay_ms: milliseconds per frame.
// Returns 1 on success, 0 on failure.
int save_wiggle_apng(const char *path,
                     const uint8_t *left_rgb565,  int w, int h,
                     const uint8_t *right_rgb565,
                     int n_frames,
                     int delay_ms);

#endif
