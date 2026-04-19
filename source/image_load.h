#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H

#include <stdint.h>

// Load a JPEG/PNG from path, scale to (width x height), store as RGB565 in dst.
// Returns 1 on success, 0 on failure.
int load_jpeg_to_rgb565(const char *path, uint16_t *dst, int width, int height);

// Load all frames of a wiggle APNG. frames[i] must each point to a buffer of
// (width * height) uint16_t values. Up to max_frames are loaded.
// *out_n_frames: actual frame count loaded.
// *out_delay_ms: per-frame delay in milliseconds from the first fcTL chunk.
// Returns 1 on success (at least one frame), 0 on failure.
int load_apng_frames_to_rgb565(const char *path,
                                uint16_t **frames, int max_frames,
                                int *out_n_frames, int *out_delay_ms,
                                int width, int height);

// Load all frames of an animated GIF. frames[i] must each point to a buffer of
// (width * height) uint16_t values. Up to max_frames are loaded.
// *out_n_frames: actual frame count loaded.
// *out_delay_ms: per-frame delay in milliseconds from the first frame.
// Returns 1 on success (at least one frame), 0 on failure.
int load_gif_frames_to_rgb565(const char *path,
                               uint16_t **frames, int max_frames,
                               int *out_n_frames, int *out_delay_ms,
                               int width, int height);

// Save an RGB888 buffer as JPEG at path (quality 90).
// Returns 1 on success, 0 on failure.
int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height);

// Seed the shared file counter from the INI value and a dir scan.
// ini_val: value loaded from settings (0 if not present).
// Both GB_ and GW_ files draw from the same counter so numbers never collide.
void file_counter_init(const char *dir, int ini_val);

// Return the current counter value (the next number to be used).
// Persist this with settings_save_file_counter after each save.
int  file_counter_next(void);

// Return the next free GB_XXXX.JPG path (O(1) after file_counter_init).
// Returns 1 if a slot was found, 0 if not.
int next_save_path(const char *dir, char *out_path, int out_len);

// Scan dir for GB_XXXX.JPG files, fill paths[] with full paths sorted descending by number.
// Returns count of photos found (capped at max).
int list_saved_photos(const char *dir, char paths[][64], int max);

// ---------------------------------------------------------------------------
// Wiggle APNG functions (full 24-bit true colour, no quantization)
// ---------------------------------------------------------------------------

// Return the next free GW_XXXX.gif path (O(1) after file_counter_init).
int next_wiggle_path(const char *dir, char *out_path, int out_len);

// Save an edited wiggle: composites stickers/frame onto each RGB565 thumb frame,
// then encodes as APNG.  composite_fn is called once per frame with the RGB888
// scratch buffer already converted from the RGB565 source.
// composite_fn(rgb888, w, h, userdata) — apply stickers/frames in place.
// Returns 1 on success, 0 on failure.
typedef void (*composite_fn_t)(uint8_t *rgb888, int w, int h, void *userdata);
int save_edited_apng(const char *path,
                     const uint16_t * const *frames_rgb565,
                     int n_frames, int delay_ms,
                     int w, int h,
                     composite_fn_t composite_fn, void *userdata);

#define SAVE_DIR "sdmc:/DCIM/GameboyCamera"

#endif
