#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <stdint.h>
#include "filter.h"

typedef enum {
    CAPTURE_MODE_STILL = 0,
    CAPTURE_MODE_STEREO = 1,
} CaptureMode;

typedef enum {
    STEREO_OUTPUT_WIGGLE = 0,
    STEREO_OUTPUT_ANAGLYPH = 1,
} StereoOutputMode;

typedef enum {
    PIPELINE_PANEL_GB = 0,
    PIPELINE_PANEL_WIGGLE = 1,
    PIPELINE_PANEL_LOMO = 2,
    PIPELINE_PANEL_BEND = 3,
    PIPELINE_PANEL_FX = 4,
} PipelinePanel;

#define PIPELINE_PRESET_COUNT 4

typedef struct {
    char name[24];
    bool gb_enabled;
    FilterParams gb_params;
    bool base_enabled;
    int  base_preset;
    bool bend_enabled;
    int  bend_preset;
    int  fx_mode;
    int  fx_intensity;
} PipelinePreset;

typedef struct {
    bool enabled;
    int  preset;
} BaseLookState;

typedef struct {
    bool         enabled;
    FilterParams params;
} GBStageState;

typedef struct {
    bool enabled;
    int  preset;
} BendStageState;

typedef struct {
    bool enabled;
    int  fx_mode;
    int  fx_intensity;
} PostFxStageState;

typedef struct {
    int             capture_mode;
    int             active_panel;
    bool            panel_open;
    BaseLookState   base;
    GBStageState    gb;
    BendStageState  bend;
    PostFxStageState post;
} EffectPipeline;

typedef struct {
    bool         use_base_look;
    int          lomo_preset;
    bool         use_gb;
    FilterParams gb_params;
    bool         use_bend;
    int          bend_preset;
    bool         use_post_fx;
    int          post_fx_mode;
    int          post_fx_intensity;
    int          fallback_post_fx_mode;
    int          fallback_post_fx_intensity;
} EffectRecipe;

void pipeline_state_init(EffectPipeline *pipe, const FilterParams *defaults);
void pipeline_state_sync_legacy(EffectPipeline *pipe,
                                int capture_mode,
                                bool gb_enabled,
                                const FilterParams *gb_params,
                                bool lomo_enabled, int lomo_preset,
                                bool bend_enabled, int bend_preset,
                                int post_fx_mode, int post_fx_intensity,
                                int active_panel, bool panel_open);
void pipeline_build_recipe(EffectRecipe *out, const EffectPipeline *pipe);
bool pipeline_recipe_has_effects(const EffectRecipe *recipe);
void pipeline_apply(uint8_t *rgb, int w, int h,
                    const EffectRecipe *recipe,
                    int frame_count);
void pipeline_preset_default(PipelinePreset *preset, int slot);
void pipeline_preset_capture(PipelinePreset *preset, const EffectPipeline *pipe,
                             const char *name);
void pipeline_preset_apply(EffectPipeline *pipe, const PipelinePreset *preset);

#endif
