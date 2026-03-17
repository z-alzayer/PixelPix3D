#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <stdbool.h>

// --- Palettes ---------------------------------------------------------------

#define PALETTE_NONE  -1
#define PALETTE_COUNT  6

typedef struct {
    const char *name;
    int         size;
    uint8_t     colors[8][3];  // ordered dark->light, max 8 entries
} PaletteDef;

extern const PaletteDef palettes[PALETTE_COUNT];

// --- Filter parameters ------------------------------------------------------

typedef struct {
    int   pixel_size;    // Block size (1 = off, try 2-8)
    int   color_levels;  // Shades per channel — only used when palette == PALETTE_NONE
    float brightness;    // 1.0 = neutral, >1.0 = brighter (simple multiplier)
    float contrast;      // 1.0 = neutral, >1.0 = more contrast (pivot around 128)
    float gamma;         // 1.0 = neutral, >1.0 = lift midtones/shadows
    float saturation;    // 1.0 = neutral, 0.0 = greyscale, >1.0 = vivid
    int   palette;       // PALETTE_NONE or 0-(PALETTE_COUNT-1)
    int   dither_mode;   // 0 = Bayer (default), 1 = Floyd-Steinberg
    bool  invert;        // apply 255-v per channel before dithering
} FilterParams;

#define FILTER_DEFAULTS { .pixel_size = 1, .color_levels = 4, \
                          .brightness = 1.0f, .contrast = 1.0f, \
                          .gamma = 1.0f, .saturation = 1.0f, .palette = 0, \
                          .dither_mode = 0, .invert = false }

void apply_gameboy_filter(uint8_t *pixels, int width, int height, FilterParams p);
void floyd_steinberg_dither(uint8_t *pixels, int width, int height,
                            const FilterParams *p);

#endif
