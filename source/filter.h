#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

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
    float contrast;      // 1.0 = neutral, >1.0 = brighter/punchier
    float gamma;         // 1.0 = neutral, >1.0 = lift midtones
    float saturation;    // 1.0 = neutral, 0.0 = greyscale, >1.0 = vivid
    int   palette;       // PALETTE_NONE or 0-(PALETTE_COUNT-1)
} FilterParams;

#define FILTER_DEFAULTS { .pixel_size = 1, .color_levels = 4, \
                          .contrast = 1.5f, .gamma = 1.2f, \
                          .saturation = 1.0f, .palette = 0 }

void apply_gameboy_filter(uint8_t *pixels, int width, int height, FilterParams p);

#endif
