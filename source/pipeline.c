#include "pipeline.h"
#include "lomo.h"
#include "bend.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float s_basic_lut[256];
static float s_basic_gamma = -1.0f;
static float s_basic_brightness = -1.0f;
static float s_basic_contrast = -1.0f;

static void rebuild_basic_lut(float gamma, float brightness, float contrast) {
    if (gamma == s_basic_gamma &&
        brightness == s_basic_brightness &&
        contrast == s_basic_contrast) {
        return;
    }

    float inv_gamma = 1.0f / gamma;
    for (int i = 0; i < 256; i++) {
        float v = powf(i / 255.0f, inv_gamma) * 255.0f;
        v = (v * brightness - 128.0f) * contrast + 128.0f;
        if (v < 0.0f) v = 0.0f;
        else if (v > 255.0f) v = 255.0f;
        s_basic_lut[i] = v;
    }

    s_basic_gamma = gamma;
    s_basic_brightness = brightness;
    s_basic_contrast = contrast;
}

static int clamp_strength(int strength) {
    if (strength < 0) return 0;
    if (strength > 10) return 10;
    return strength;
}

static float strength_mix(float neutral, float target, int strength) {
    float t = (float)clamp_strength(strength) / 10.0f;
    return neutral + (target - neutral) * t;
}

static void apply_basic_adjustments(uint8_t *pixels, int width, int height,
                                    float brightness, float contrast,
                                    float gamma, float saturation) {
    rebuild_basic_lut(gamma, brightness, contrast);

    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        float r = s_basic_lut[pixels[idx + 0]];
        float g = s_basic_lut[pixels[idx + 1]];
        float b = s_basic_lut[pixels[idx + 2]];

        if (fabsf(saturation - 1.0f) > 0.001f) {
            float lum = (77.0f * r + 150.0f * g + 29.0f * b) / 256.0f;
            r = lum + saturation * (r - lum);
            g = lum + saturation * (g - lum);
            b = lum + saturation * (b - lum);
        }

        if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
        if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
        if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;

        pixels[idx + 0] = (uint8_t)(r + 0.5f);
        pixels[idx + 1] = (uint8_t)(g + 0.5f);
        pixels[idx + 2] = (uint8_t)(b + 0.5f);
    }
}

static bool has_basic_adjustments(const FilterParams *params) {
    if (!params) return false;
    return fabsf(params->brightness - 1.0f) > 0.001f ||
           fabsf(params->contrast   - 1.0f) > 0.001f ||
           fabsf(params->gamma      - 1.0f) > 0.001f ||
           fabsf(params->saturation - 1.0f) > 0.001f;
}

void pipeline_state_init(EffectPipeline *pipe, const FilterParams *defaults) {
    pipe->capture_mode = CAPTURE_MODE_STILL;
    pipe->active_panel = PIPELINE_PANEL_GB;
    pipe->panel_open = false;
    pipe->base.enabled = false;
    pipe->base.preset = 0;
    pipe->base.strength = 10;
    pipe->gb.enabled = false;
    pipe->gb.params = *defaults;
    pipe->bend.enabled = false;
    pipe->bend.preset = 0;
    pipe->bend.strength = 10;
    pipe->post.enabled = defaults->fx_mode != FX_NONE;
    pipe->post.fx_mode = defaults->fx_mode;
    pipe->post.fx_intensity = defaults->fx_intensity;
}

void pipeline_state_sync_legacy(EffectPipeline *pipe,
                                int capture_mode,
                                bool gb_enabled,
                                const FilterParams *gb_params,
                                bool lomo_enabled, int lomo_preset, int lomo_strength,
                                bool bend_enabled, int bend_preset, int bend_strength,
                                int post_fx_mode, int post_fx_intensity,
                                int active_panel, bool panel_open) {
    pipe->capture_mode = capture_mode;
    pipe->active_panel = active_panel;
    pipe->panel_open = panel_open;
    pipe->gb.enabled = gb_enabled;
    pipe->gb.params = *gb_params;
    pipe->base.enabled = lomo_enabled;
    pipe->base.preset = lomo_preset;
    pipe->base.strength = clamp_strength(lomo_strength);
    pipe->bend.enabled = bend_enabled;
    pipe->bend.preset = bend_preset;
    pipe->bend.strength = clamp_strength(bend_strength);
    pipe->post.enabled = post_fx_mode != FX_NONE;
    pipe->post.fx_mode = post_fx_mode;
    pipe->post.fx_intensity = post_fx_intensity;
}

void pipeline_build_recipe(EffectRecipe *out, const EffectPipeline *pipe) {
    out->use_base_look = pipe->base.enabled;
    out->lomo_preset = pipe->base.preset;
    out->lomo_strength = clamp_strength(pipe->base.strength);
    out->use_gb = pipe->gb.enabled;
    out->gb_params = pipe->gb.params;
    out->use_bend = pipe->bend.enabled;
    out->bend_preset = pipe->bend.preset;
    out->bend_strength = clamp_strength(pipe->bend.strength);
    out->use_post_fx = pipe->post.enabled;
    out->post_fx_mode = pipe->post.fx_mode;
    out->post_fx_intensity = pipe->post.fx_intensity;
    if (pipe->base.enabled && pipe->base.strength > 0) {
        out->fallback_post_fx_mode = lomo_presets[pipe->base.preset].fx_mode;
        out->fallback_post_fx_intensity =
            (lomo_presets[pipe->base.preset].fx_intensity * out->lomo_strength + 5) / 10;
    } else {
        out->fallback_post_fx_mode = FX_NONE;
        out->fallback_post_fx_intensity = 0;
    }
}

bool pipeline_recipe_has_effects(const EffectRecipe *recipe) {
    return recipe &&
           ((recipe->use_base_look && recipe->lomo_strength > 0) || recipe->use_gb ||
            has_basic_adjustments(&recipe->gb_params) ||
            (recipe->use_bend && recipe->bend_strength > 0) || recipe->use_post_fx ||
            recipe->fallback_post_fx_mode != FX_NONE);
}

void pipeline_apply(uint8_t *rgb, int w, int h,
                    const EffectRecipe *recipe,
                    int frame_count) {
    if (!pipeline_recipe_has_effects(recipe)) return;

    if (recipe->use_base_look && recipe->lomo_strength > 0) {
        const LomoPreset *lp = &lomo_presets[recipe->lomo_preset];
        apply_basic_adjustments(rgb, w, h,
                                strength_mix(1.0f, lp->brightness, recipe->lomo_strength),
                                strength_mix(1.0f, lp->contrast, recipe->lomo_strength),
                                strength_mix(1.0f, lp->gamma, recipe->lomo_strength),
                                strength_mix(1.0f, lp->saturation, recipe->lomo_strength));
    }

    if (!recipe->use_gb && has_basic_adjustments(&recipe->gb_params)) {
        apply_basic_adjustments(rgb, w, h,
                                recipe->gb_params.brightness,
                                recipe->gb_params.contrast,
                                recipe->gb_params.gamma,
                                recipe->gb_params.saturation);
    }

    if (recipe->use_gb) {
        apply_gameboy_filter(rgb, w, h, recipe->gb_params);
    }

    if (recipe->use_bend && recipe->bend_strength > 0) {
        apply_bend(rgb, w, h, recipe->bend_preset, frame_count, recipe->bend_strength);
    }

    if (recipe->use_post_fx) {
        FilterParams post = recipe->gb_params;
        post.fx_mode = recipe->post_fx_mode;
        post.fx_intensity = recipe->post_fx_intensity;
        apply_fx(rgb, w, h, post, frame_count);
    } else if (recipe->fallback_post_fx_mode != FX_NONE) {
        FilterParams post = recipe->gb_params;
        post.fx_mode = recipe->fallback_post_fx_mode;
        post.fx_intensity = recipe->fallback_post_fx_intensity;
        apply_fx(rgb, w, h, post, frame_count);
    }
}

void pipeline_preset_default(PipelinePreset *preset, int slot) {
    FilterParams defaults = FILTER_DEFAULTS;
    memset(preset, 0, sizeof(*preset));
    snprintf(preset->name, sizeof(preset->name), "Empty Slot %d", slot + 1);
    preset->gb_enabled = false;
    preset->gb_params = defaults;
    preset->base_strength = 10;
    preset->bend_strength = 10;
}

void pipeline_preset_capture(PipelinePreset *preset, const EffectPipeline *pipe,
                             const char *name) {
    memset(preset, 0, sizeof(*preset));
    char generated[24] = {0};
    bool first = true;
    if (pipe->gb.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%sGB", first ? "" : "+");
        first = false;
    }
    if (pipe->base.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", lomo_presets[pipe->base.preset].name);
        first = false;
    }
    if (pipe->bend.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", bend_presets[pipe->bend.preset].name);
        first = false;
    }
    if (pipe->post.enabled) {
        static const char *fx_names[] = {
            "None", "ScanH", "ScanV", "LCD", "Vignette", "Chroma", "Grain"
        };
        const char *fx_name = (pipe->post.fx_mode >= 0 && pipe->post.fx_mode <= 6)
                            ? fx_names[pipe->post.fx_mode] : "FX";
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", fx_name);
        first = false;
    }
    if (first) snprintf(generated, sizeof(generated), "Raw");

    if (name && name[0] && strncmp(name, "Empty Slot", 10) != 0)
        snprintf(preset->name, sizeof(preset->name), "%s", name);
    else
        snprintf(preset->name, sizeof(preset->name), "%s", generated);
    preset->gb_enabled = pipe->gb.enabled;
    preset->gb_params = pipe->gb.params;
    preset->base_enabled = pipe->base.enabled;
    preset->base_preset = pipe->base.preset;
    preset->base_strength = clamp_strength(pipe->base.strength);
    preset->bend_enabled = pipe->bend.enabled;
    preset->bend_preset = pipe->bend.preset;
    preset->bend_strength = clamp_strength(pipe->bend.strength);
    preset->fx_mode = pipe->post.enabled ? pipe->post.fx_mode : FX_NONE;
    preset->fx_intensity = pipe->post.fx_intensity;
}

void pipeline_preset_apply(EffectPipeline *pipe, const PipelinePreset *preset) {
    pipe->gb.enabled = preset->gb_enabled;
    pipe->gb.params = preset->gb_params;
    pipe->base.enabled = preset->base_enabled;
    pipe->base.preset = preset->base_preset;
    pipe->base.strength = clamp_strength(preset->base_strength);
    pipe->bend.enabled = preset->bend_enabled;
    pipe->bend.preset = preset->bend_preset;
    pipe->bend.strength = clamp_strength(preset->bend_strength);
    pipe->post.enabled = preset->fx_mode != FX_NONE;
    pipe->post.fx_mode = preset->fx_mode;
    pipe->post.fx_intensity = preset->fx_intensity;
}
