#ifndef WIGGLEGRAM_H
#define WIGGLEGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <3ds.h>
#include "camera.h"
#include "filter.h"

// Forward declarations (avoid circular include with app_state.h / shoot.h)
struct WiggleState;
struct SaveThreadState;

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
int build_wiggle_preview_frames(uint16_t dst[][CAMERA_WIDTH * CAMERA_HEIGHT],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int src_w, int src_h, int nf,
                                const WiggleAlign *align,
                                int offset_dx, int offset_dy,
                                int *out_w, int *out_h);

// ---------------------------------------------------------------------------
// Per-frame helpers extracted from the main loop
// ---------------------------------------------------------------------------

// Handle wiggle preview input (d-pad offsets, touch +/- buttons, delay cycling,
// B cancel, A/save confirm).  Called each frame when wig->preview is true.
// do_save: true if the touch-screen Save button was tapped this frame.
// save_flash: pointer to app.save_flash (set to 20 on save trigger).
void wiggle_preview_update(struct WiggleState *wig,
                           struct SaveThreadState *save,
                           u32 kDown, u32 kHeld,
                           bool do_save,
                           u8 *wiggle_left, u8 *wiggle_right,
                           int *save_flash,
                           const FilterParams *params);

// Advance the wiggle preview animation: rebuild frames if offsets changed,
// then cycle to the next frame based on wall-clock time.
// If filter is non-NULL and the wiggle state has filter_active, apply
// palette/fx to the preview frames after rebuilding.
void wiggle_preview_tick(struct WiggleState *wig,
                         uint16_t preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                         const u8 *wiggle_left, const u8 *wiggle_right,
                         const FilterParams *filter, int frame_count);

// Returns true while the wiggle preview filter is being applied frame-by-frame.
bool wiggle_filter_busy(void);

// Save a wiggle GIF from two raw RGB565 camera buffers.
// n_frames: number of animation frames (2..8).
// delay_ms: milliseconds per frame.
// align: alignment result (pass NULL to skip offset correction).
// offset_dx/dy: user alignment adjustment in pixels.
// Returns 1 on success, 0 on failure.
// filter: if non-NULL, apply palette/fx to each frame before encoding.
int save_wiggle_gif(const char *path,
                    const uint8_t *left_rgb565,  int w, int h,
                    const uint8_t *right_rgb565,
                    int n_frames, int delay_ms,
                    const WiggleAlign *align,
                    int offset_dx, int offset_dy,
                    const FilterParams *filter);

#endif
