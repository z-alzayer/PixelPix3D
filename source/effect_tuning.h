#ifndef EFFECT_TUNING_H
#define EFFECT_TUNING_H

#include "bend.h"
#include "filter.h"
#include "lomo.h"

typedef struct {
    int exposure;  // 0..10, 5 neutral
    int color;     // 0..10, tint/saturation character
    int texture;   // 0..10, vignette/grain emphasis
} LomoTune;

typedef struct {
    int wave;      // 0..10, sinusoidal displacement
    int chaos;     // 0..10, random byte/channel damage
    int seed;      // 0..10, pattern variation
} BendTune;

typedef struct {
    int shape;     // 0..10, mode-specific shape/type
    int depth;     // 0..10, darkness/amount
    int scale;     // 0..10, line width/grain size/radius
} FxTune;

typedef struct {
    LomoTune lomo[LOMO_PRESET_COUNT];
    BendTune bend[BEND_PRESET_COUNT];
    FxTune   fx[7];
} EffectTuning;

extern EffectTuning g_effect_tuning;

void effect_tuning_defaults(EffectTuning *t);
LomoTune effect_tuning_lomo(int preset);
BendTune effect_tuning_bend(int preset);
FxTune effect_tuning_fx(int fx_mode);

#endif
