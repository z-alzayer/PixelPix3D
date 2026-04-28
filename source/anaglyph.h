#ifndef ANAGLYPH_H
#define ANAGLYPH_H

#include <stdint.h>
#include "pipeline.h"

void build_anaglyph_preview_frame(uint16_t *dst_rgb565,
                                  const uint8_t *left_rgb565, int w, int h,
                                  const uint8_t *right_rgb565,
                                  int offset_dx, int offset_dy,
                                  const EffectRecipe *recipe);

int save_anaglyph_jpeg(const char *path,
                       const uint8_t *left_rgb565, int w, int h,
                       const uint8_t *right_rgb565,
                       int offset_dx, int offset_dy,
                       int save_scale, int rotate_quadrants,
                       const EffectRecipe *recipe,
                       uint8_t *rgb_scratch,
                       uint8_t *upscale_scratch,
                       uint8_t *rotate_scratch);

int save_anaglyph_png(const char *path,
                      const uint8_t *left_rgb565, int w, int h,
                      const uint8_t *right_rgb565,
                      int offset_dx, int offset_dy,
                      int rotate_quadrants,
                      const EffectRecipe *recipe);

#endif
