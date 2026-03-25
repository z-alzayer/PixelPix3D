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

#define SAVE_DIR "sdmc:/DCIM/GameboyCamera"

#endif
