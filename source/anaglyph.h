#ifndef ANAGLYPH_H
#define ANAGLYPH_H

#include <stdint.h>
#include "pipeline.h"

void anaglyph_default_colors(uint8_t colors[2][3]);
void anaglyph_sanitize_colors(uint8_t colors[2][3]);

void build_anaglyph_preview_frame(uint16_t *dst_rgb565,
                                  const uint8_t *left_rgb565, int w, int h,
                                  const uint8_t *right_rgb565,
                                  int offset_dx, int offset_dy,
                                  const EffectRecipe *recipe,
                                  const uint8_t colors[2][3]);

int save_anaglyph_png(const char *path,
                      const uint8_t *left_rgb565, int w, int h,
                      const uint8_t *right_rgb565,
                      int offset_dx, int offset_dy,
                      int rotate_quadrants,
                      const EffectRecipe *recipe,
                      const uint8_t colors[2][3]);

#endif
