#include "effect_tuning.h"
#include <string.h>

EffectTuning g_effect_tuning;

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void effect_tuning_defaults(EffectTuning *t) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < LOMO_PRESET_COUNT; i++) {
        t->lomo[i].exposure = 5;
        t->lomo[i].color = 5;
        t->lomo[i].texture = 5;
    }
    for (int i = 0; i < BEND_PRESET_COUNT; i++) {
        t->bend[i].wave = 0;
        t->bend[i].chaos = 0;
        t->bend[i].seed = 0;
    }
    for (int i = 0; i <= 6; i++) {
        t->fx[i].shape = 0;
        t->fx[i].depth = 0;
        t->fx[i].scale = 0;
    }
}

LomoTune effect_tuning_lomo(int preset) {
    if (preset < 0) preset = 0;
    if (preset >= LOMO_PRESET_COUNT) preset = LOMO_PRESET_COUNT - 1;
    LomoTune t = g_effect_tuning.lomo[preset];
    t.exposure = clamp_i(t.exposure, 0, 10);
    t.color = clamp_i(t.color, 0, 10);
    t.texture = clamp_i(t.texture, 0, 10);
    return t;
}

BendTune effect_tuning_bend(int preset) {
    if (preset < 0) preset = 0;
    if (preset >= BEND_PRESET_COUNT) preset = BEND_PRESET_COUNT - 1;
    BendTune t = g_effect_tuning.bend[preset];
    t.wave = clamp_i(t.wave, 0, 10);
    t.chaos = clamp_i(t.chaos, 0, 10);
    t.seed = clamp_i(t.seed, 0, 10);
    return t;
}

FxTune effect_tuning_fx(int fx_mode) {
    if (fx_mode < 0) fx_mode = 0;
    if (fx_mode > 6) fx_mode = 6;
    FxTune t = g_effect_tuning.fx[fx_mode];
    t.shape = clamp_i(t.shape, 0, 10);
    t.depth = clamp_i(t.depth, 0, 10);
    t.scale = clamp_i(t.scale, 0, 10);
    return t;
}
