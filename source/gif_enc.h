#ifndef GIF_ENC_H
#define GIF_ENC_H

#include <stdint.h>
#include <stddef.h>

// Encode n_frames of RGB888 pixel data into a GIF89a (looping, local palettes).
// Each frame gets its own 256-colour palette via NeuQuant neural-net quantization.
// delay_ms: per-frame delay in milliseconds.
// buf / buf_cap: caller-supplied output buffer.
// Returns total bytes written, or 0 on error/overflow.
size_t gif_encode(uint8_t *buf, size_t buf_cap,
                  const uint8_t * const *frames_rgb888, int n_frames,
                  int width, int height, int delay_ms);

#endif
