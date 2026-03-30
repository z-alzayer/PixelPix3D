#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H

#include <stdint.h>

// Load a JPEG from path, scale to (width x height), store as RGB565 in dst.
// Returns 1 on success, 0 on failure.
int load_jpeg_to_rgb565(const char *path, uint16_t *dst, int width, int height);

// Save an RGB888 buffer as JPEG at path (quality 90).
// Returns 1 on success, 0 on failure.
int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height);

// Scan dir once to seed the save counter. Call at startup before any save.
void save_counter_init(const char *dir);

// Return the next free GB_XXXX.JPG path (O(1) after save_counter_init).
// Returns 1 if a slot was found, 0 if not.
int next_save_path(const char *dir, char *out_path, int out_len);

// Scan dir for GB_XXXX.JPG files, fill paths[] with full paths sorted descending by number.
// Returns count of photos found (capped at max).
int list_saved_photos(const char *dir, char paths[][64], int max);

// ---------------------------------------------------------------------------
// Wiggle APNG functions (full 24-bit true colour, no quantization)
// ---------------------------------------------------------------------------

// Seed the wiggle save counter. Call at startup alongside save_counter_init.
void wiggle_counter_init(const char *dir);

// Return the next free GW_XXXX.png path (O(1) after wiggle_counter_init).
int next_wiggle_path(const char *dir, char *out_path, int out_len);

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

#define SAVE_DIR "sdmc:/DCIM/GameboyCamera"

#endif
