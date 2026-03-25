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

// Set a mutable user-editable palette array (call once at startup).
// Pass NULL to revert to the built-in const palettes[].
void filter_set_user_palettes(PaletteDef *user_pal);
const PaletteDef *filter_get_active_palettes(void);

// --- Filter parameters ------------------------------------------------------

typedef struct {
    int   pixel_size;    // Block size (1 = off, try 2-8)
    int   color_levels;  // Shades per channel — only used when palette == PALETTE_NONE
    float brightness;    // 1.0 = neutral, >1.0 = brighter (simple multiplier)
    float contrast;      // 1.0 = neutral, >1.0 = more contrast (pivot around 128)
    float gamma;         // 1.0 = neutral, >1.0 = lift midtones/shadows
    float saturation;    // 1.0 = neutral, 0.0 = greyscale, >1.0 = vivid
    int   palette;       // PALETTE_NONE or 0-(PALETTE_COUNT-1)
    int   dither_mode;   // 0 = Bayer, 1 = Cluster, 2 = Atkinson, 3 = Floyd-Steinberg
    bool  invert;        // apply 255-v per channel before dithering
    int   fx_mode;       // 0=None, 1=ScanH, 2=ScanV, 3=LCD, 4=Vignette, 5=Chroma, 6=Grain
    int   fx_intensity;  // 0-10 (maps to effect-specific strength range)
} FilterParams;

// FX mode identifiers
#define FX_NONE     0
#define FX_SCAN_H   1
#define FX_SCAN_V   2
#define FX_LCD      3
#define FX_VIGNETTE 4
#define FX_CHROMA   5
#define FX_GRAIN    6

#define FILTER_DEFAULTS { .pixel_size = 1, .color_levels = 4, \
                          .brightness = 1.0f, .contrast = 1.0f, \
                          .gamma = 1.0f, .saturation = 1.0f, .palette = 0, \
                          .dither_mode = 0, .invert = false, \
                          .fx_mode = 0, .fx_intensity = 5 }

// --- Per-parameter min/max ranges + default startup value -------------------

typedef struct {
    float bright_min,   bright_max,   bright_def;
    float contrast_min, contrast_max, contrast_def;
    float sat_min,      sat_max,      sat_def;
    float gamma_min,    gamma_max,    gamma_def;
} FilterRanges;

#define FILTER_RANGES_DEFAULTS { \
    .bright_min=0.0f,   .bright_max=2.0f,   .bright_def=1.0f, \
    .contrast_min=0.5f, .contrast_max=2.0f, .contrast_def=1.0f, \
    .sat_min=0.0f,      .sat_max=2.0f,      .sat_def=1.0f, \
    .gamma_min=0.5f,    .gamma_max=2.0f,    .gamma_def=1.0f  \
}

void apply_gameboy_filter(uint8_t *pixels, int width, int height, FilterParams p);
void apply_fx(uint8_t *buf, int w, int h, FilterParams p, int frame_count);
void floyd_steinberg_dither(uint8_t *pixels, int width, int height, const FilterParams *p);
void atkinson_dither(uint8_t *pixels, int width, int height, const FilterParams *p);

// --- Colour space conversions -----------------------------------------------

// rgb_to_hsv: r/g/b in [0,255] → h in [0,360), s in [0,1], v in [0,1]
void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *h, float *s, float *v);

// hsv_to_rgb: h in [0,360), s in [0,1], v in [0,1] → r/g/b in [0,255]
void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
