#ifndef NINTENDO_PHOTO_H
#define NINTENDO_PHOTO_H

#include <stdint.h>

// Nintendo 3DS Camera photos are always 640x480.
#define HNI_WIDTH  640
#define HNI_HEIGHT 480

// Write a Nintendo-3DS-Camera-compatible photo. Both buffers are RGB888 at
// exactly 640x480. right_rgb888 may be NULL (e.g. selfie shots): only the
// .JPG is written. Otherwise writes the JPG (left view, no MPF) and the MPO
// (both views + MPF index), matching what the official camera app produces.
// Returns 1 on success, 0 on failure.
int write_hni_photo(const char *jpg_path, const char *mpo_path,
                    const uint8_t *left_rgb888, const uint8_t *right_rgb888);

// Copy src (w x h RGB888) into a 640x480 canvas: pass-through when already
// 640x480, otherwise aspect-fit centered on black (used for portrait shots).
// dst must hold 640*480*3 bytes.
void hni_fit_canvas(uint8_t *dst, const uint8_t *src, int w, int h);

// Load the second (right-eye) image of an .MPO as RGB888 via the MPF index.
// The returned buffer must be released with free_loaded_image().
// Returns 1 on success, 0 on failure (missing/invalid MPF index).
int load_mpo_second_rgb888(const char *path,
                           uint8_t **out_rgb888, int *out_w, int *out_h);

#endif
