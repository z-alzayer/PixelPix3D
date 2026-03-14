#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H

#include <stdint.h>

// Load a JPEG from path, scale to (width x height), store as BGR565 in dst.
// Returns 1 on success, 0 on failure.
int load_jpeg_to_bgr565(const char *path, uint16_t *dst, int width, int height);

// Save an RGB888 buffer as JPEG at path (quality 90).
// Returns 1 on success, 0 on failure.
int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height);

// Find the next free GB_XXXX.JPG slot in dir, write full path into out_path.
// Returns 1 if a slot was found, 0 if not.
int next_save_path(const char *dir, char *out_path, int out_len);

#define SAVE_DIR "sdmc:/DCIM/GameboyCamera"

#endif
