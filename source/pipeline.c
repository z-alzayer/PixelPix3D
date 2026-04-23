#include "pipeline.h"
#include "lomo.h"
#include "bend.h"
#include <math.h>

static void apply_basic_adjustments(uint8_t *pixels, int width, int height,
                                    float brightness, float contrast,
                                    float gamma, float saturation) {
    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        float r = powf(pixels[idx + 0] / 255.0f, 1.0f / gamma) * 255.0f;
        float g = powf(pixels[idx + 1] / 255.0f, 1.0f / gamma) * 255.0f;
        float b = powf(pixels[idx + 2] / 255.0f, 1.0f / gamma) * 255.0f;

        r = (r * brightness - 128.0f) * contrast + 128.0f;
        g = (g * brightness - 128.0f) * contrast + 128.0f;
        b = (b * brightness - 128.0f) * contrast + 128.0f;

        int lum = (77 * (int)r + 150 * (int)g + 29 * (int)b) >> 8;
        r = lum + saturation * (r - lum);
        g = lum + saturation * (g - lum);
        b = lum + saturation * (b - lum);

        if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
        if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
        if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;

        pixels[idx + 0] = (uint8_t)r;
        pixels[idx + 1] = (uint8_t)g;
        pixels[idx + 2] = (uint8_t)b;
    }
}

void pipeline_state_init(EffectPipeline *pipe, const FilterParams *defaults) {
    pipe->capture_mode = CAPTURE_MODE_STILL;
    pipe->active_panel = PIPELINE_PANEL_GB;
    pipe->panel_open = false;
    pipe->base.enabled = false;
    pipe->base.preset = 0;
    pipe->gb.enabled = true;
    pipe->gb.params = *defaults;
    pipe->bend.enabled = false;
    pipe->bend.preset = 0;
    pipe->post.enabled = defaults->fx_mode != FX_NONE;
    pipe->post.fx_mode = defaults->fx_mode;
    pipe->post.fx_intensity = defaults->fx_intensity;
}

void pipeline_state_sync_legacy(EffectPipeline *pipe,
                                int capture_mode,
                                bool gb_enabled,
                                const FilterParams *gb_params,
                                bool lomo_enabled, int lomo_preset,
                                bool bend_enabled, int bend_preset,
                                int post_fx_mode, int post_fx_intensity,
                                int active_panel, bool panel_open) {
    pipe->capture_mode = capture_mode;
    pipe->active_panel = active_panel;
    pipe->panel_open = panel_open;
    pipe->gb.enabled = gb_enabled;
    pipe->gb.params = *gb_params;
    pipe->base.enabled = lomo_enabled;
    pipe->base.preset = lomo_preset;
    pipe->bend.enabled = bend_enabled;
    pipe->bend.preset = bend_preset;
    pipe->post.enabled = post_fx_mode != FX_NONE;
    pipe->post.fx_mode = post_fx_mode;
    pipe->post.fx_intensity = post_fx_intensity;
}

void pipeline_build_recipe(EffectRecipe *out, const EffectPipeline *pipe) {
    out->use_base_look = pipe->base.enabled;
    out->lomo_preset = pipe->base.preset;
    out->use_gb = pipe->gb.enabled;
    out->gb_params = pipe->gb.params;
    out->use_bend = pipe->bend.enabled;
    out->bend_preset = pipe->bend.preset;
    out->use_post_fx = pipe->post.enabled;
    out->post_fx_mode = pipe->post.fx_mode;
    out->post_fx_intensity = pipe->post.fx_intensity;
    if (pipe->base.enabled) {
        out->fallback_post_fx_mode = lomo_presets[pipe->base.preset].fx_mode;
        out->fallback_post_fx_intensity = lomo_presets[pipe->base.preset].fx_intensity;
    } else {
        out->fallback_post_fx_mode = FX_NONE;
        out->fallback_post_fx_intensity = 0;
    }
}

bool pipeline_recipe_has_effects(const EffectRecipe *recipe) {
    return recipe &&
           (recipe->use_base_look || recipe->use_gb ||
            recipe->use_bend || recipe->use_post_fx ||
            recipe->fallback_post_fx_mode != FX_NONE);
}

void pipeline_apply(uint8_t *rgb, int w, int h,
                    const EffectRecipe *recipe,
                    int frame_count) {
    if (!pipeline_recipe_has_effects(recipe)) return;

    if (recipe->use_base_look) {
        const LomoPreset *lp = &lomo_presets[recipe->lomo_preset];
        apply_basic_adjustments(rgb, w, h,
                                lp->brightness,
                                lp->contrast,
                                lp->gamma,
                                lp->saturation);
    }

    if (recipe->use_gb) {
        apply_gameboy_filter(rgb, w, h, recipe->gb_params);
    }

    if (recipe->use_bend) {
        apply_bend(rgb, w, h, recipe->bend_preset, frame_count);
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
