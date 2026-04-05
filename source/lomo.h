#ifndef LOMO_H
#define LOMO_H

// ---------------------------------------------------------------------------
// Lomography filter presets — raw camera only (no GB pixel filter).
// 3 classic film looks + 3 wild/stylised effects.
// ---------------------------------------------------------------------------

#include "filter.h"

#define LOMO_PRESET_COUNT 6

typedef struct {
    const char *name;
    float brightness;
    float contrast;
    float saturation;
    float gamma;
    int   fx_mode;
    int   fx_intensity;
} LomoPreset;

static const LomoPreset lomo_presets[LOMO_PRESET_COUNT] = {
    // --- Classic film looks ---
    // LC-A: punchy shadow lift, vivid colours, tasteful vignette
    { "LC-A",    1.0f, 1.4f, 1.5f, 1.3f, FX_VIGNETTE, 6 },
    // Grain: warm shadows, lifted midtones, visible film grain
    { "Grain",   1.1f, 1.2f, 1.1f, 1.2f, FX_GRAIN,    7 },
    // Mono: desaturated, punchy contrast, heavy vignette
    { "Mono",    1.0f, 1.7f, 0.1f, 0.9f, FX_VIGNETTE, 8 },

    // --- Wild / funky effects ---
    // Drift: subtle chroma shift, oversaturated, dreamlike
    { "Drift",   1.1f, 1.3f, 1.8f, 1.1f, FX_CHROMA,   4 },
    // TV: heavy scanlines on a cool-toned low-gamma image
    { "TV",      0.9f, 1.5f, 0.8f, 0.7f, FX_SCAN_H,   7 },
    // Burn: crushed blacks, spiked grain, high contrast
    { "Burn",    0.9f, 1.8f, 1.4f, 0.6f, FX_GRAIN,    9 },
};

#endif
