#ifndef APNG_ENC_H
#define APNG_ENC_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Minimal self-contained APNG encoder.
// Produces a looping animated PNG at full 24-bit RGB — no quantization.
//
// Uses stbi_zlib_compress (from stb_image_write.h) for deflate.
// Output goes into a caller-supplied byte buffer.
//
// Usage:
//   size_t len = apng_encode(buf, buf_cap,
//                            frames_rgb888, n_frames,
//                            width, height, delay_num, delay_den);
//   // delay_num/delay_den: frame delay as a fraction (e.g. 1/4 = 250ms).
//   // Returns 0 on failure or overflow.
// ---------------------------------------------------------------------------

// Encode n_frames of RGB888 pixel data into an APNG.
// frames_rgb888[f] points to width*height*3 bytes for frame f.
// delay_num / delay_den: delay per frame as a fraction of a second.
// buf: output buffer (caller-supplied).
// buf_cap: capacity of buf in bytes.
// Returns total bytes written, or 0 on error/overflow.
size_t apng_encode(uint8_t *buf, size_t buf_cap,
                   const uint8_t * const *frames_rgb888, int n_frames,
                   int width, int height,
                   uint16_t delay_num, uint16_t delay_den);

#endif // APNG_ENC_H
