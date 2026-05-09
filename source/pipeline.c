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

static int clamp_remap_style(int style) {
    if (style < 0) return 0;
    if (style >= REMAP_STYLE_COUNT) return REMAP_STYLE_COUNT - 1;
    return style;
}

static int clamp_remap_cell_size(int cell_size) {
    if (cell_size < 4) return 4;
    if (cell_size > 16) return 16;
    return cell_size;
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

static uint8_t glyph5x7(char ch, int row) {
    switch (ch) {
    case ' ': { static const uint8_t g[7] = {0,0,0,0,0,0,0}; return g[row]; }
    case '.': { static const uint8_t g[7] = {0,0,0,0,0,0x0c,0x0c}; return g[row]; }
    case ',': { static const uint8_t g[7] = {0,0,0,0,0,0x0c,0x08}; return g[row]; }
    case '`': { static const uint8_t g[7] = {0x08,0x04,0,0,0,0,0}; return g[row]; }
    case ':': { static const uint8_t g[7] = {0,0x0c,0x0c,0,0x0c,0x0c,0}; return g[row]; }
    case ';': { static const uint8_t g[7] = {0,0x0c,0x0c,0,0x0c,0x08,0x10}; return g[row]; }
    case '-': { static const uint8_t g[7] = {0,0,0,0x1f,0,0,0}; return g[row]; }
    case '_': { static const uint8_t g[7] = {0,0,0,0,0,0,0x1f}; return g[row]; }
    case '=': { static const uint8_t g[7] = {0,0,0x1f,0,0x1f,0,0}; return g[row]; }
    case '+': { static const uint8_t g[7] = {0,0x04,0x04,0x1f,0x04,0x04,0}; return g[row]; }
    case '*': { static const uint8_t g[7] = {0,0x15,0x0e,0x1f,0x0e,0x15,0}; return g[row]; }
    case '/': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0,0}; return g[row]; }
    case '\\': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0,0}; return g[row]; }
    case '|': { static const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case '(': { static const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
    case ')': { static const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
    case '[': { static const uint8_t g[7] = {0x0e,0x08,0x08,0x08,0x08,0x08,0x0e}; return g[row]; }
    case ']': { static const uint8_t g[7] = {0x0e,0x02,0x02,0x02,0x02,0x02,0x0e}; return g[row]; }
    case '<': { static const uint8_t g[7] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02}; return g[row]; }
    case '>': { static const uint8_t g[7] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08}; return g[row]; }
    case '#': { static const uint8_t g[7] = {0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0}; return g[row]; }
    case '%': { static const uint8_t g[7] = {0x19,0x1a,0x04,0x08,0x13,0x03,0}; return g[row]; }
    case '@': { static const uint8_t g[7] = {0x0e,0x11,0x17,0x15,0x17,0x10,0x0e}; return g[row]; }
    case '0': { static const uint8_t g[7] = {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}; return g[row]; }
    case '1': { static const uint8_t g[7] = {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}; return g[row]; }
    case '2': { static const uint8_t g[7] = {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}; return g[row]; }
    case '3': { static const uint8_t g[7] = {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}; return g[row]; }
    case '4': { static const uint8_t g[7] = {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}; return g[row]; }
    case '5': { static const uint8_t g[7] = {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}; return g[row]; }
    case '7': { static const uint8_t g[7] = {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case 'W': { static const uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1b,0x11}; return g[row]; }
    case 'M': { static const uint8_t g[7] = {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    default: { static const uint8_t g[7] = {0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f}; return g[row]; }
    }
}

static uint8_t remap_glyph_alpha(char ch, int x, int y, int cell_w, int cell_h) {
    int gx = (x * 5) / (cell_w > 0 ? cell_w : 1);
    int gy = (y * 7) / (cell_h > 0 ? cell_h : 1);
    if (gx < 0) gx = 0; else if (gx > 4) gx = 4;
    if (gy < 0) gy = 0; else if (gy > 6) gy = 6;
    return (glyph5x7(ch, gy) & (1 << (4 - gx))) ? 255 : 0;
}

static char ascii_char_for_luma(int lum, bool matrix) {
    static const char ramp[] = "@#W%*+=-:,.` ";
    static const char matrix_ramp[] = "MW#@7531|:. ";
    const char *r = matrix ? matrix_ramp : ramp;
    int n = matrix ? (int)sizeof(matrix_ramp) - 2 : (int)sizeof(ramp) - 2;
    int idx = (lum * n + 127) / 255;
    if (idx < 0) idx = 0;
    if (idx > n) idx = n;
    return r[idx];
}

static void apply_remap(uint8_t *rgb, int w, int h, int style,
                        int cell_size, int strength) {
    strength = clamp_strength(strength);
    if (strength <= 0) return;

    style = clamp_remap_style(style);
    cell_size = clamp_remap_cell_size(cell_size);

    for (int by = 0; by < h; by += cell_size) {
        int bh = cell_size;
        if (by + bh > h) bh = h - by;
        for (int bx = 0; bx < w; bx += cell_size) {
            int bw = cell_size;
            if (bx + bw > w) bw = w - bx;

            long sr = 0, sg = 0, sb = 0;
            long l_left = 0, l_right = 0, l_top = 0, l_bot = 0;
            int n_left = 0, n_right = 0, n_top = 0, n_bot = 0;
            int count = bw * bh;
            for (int y = 0; y < bh; y++) {
                int row = ((by + y) * w + bx) * 3;
                for (int x = 0; x < bw; x++) {
                    int idx = row + x * 3;
                    int r = rgb[idx + 0];
                    int g = rgb[idx + 1];
                    int b = rgb[idx + 2];
                    int l = (77 * r + 150 * g + 29 * b) >> 8;
                    sr += r;
                    sg += g;
                    sb += b;
                    if (x < bw / 2) { l_left += l; n_left++; }
                    else { l_right += l; n_right++; }
                    if (y < bh / 2) { l_top += l; n_top++; }
                    else { l_bot += l; n_bot++; }
                }
            }

            int ar = (int)(sr / count);
            int ag = (int)(sg / count);
            int ab = (int)(sb / count);
            int lum = (77 * ar + 150 * ag + 29 * ab) >> 8;
            int avg_left = n_left ? (int)(l_left / n_left) : lum;
            int avg_right = n_right ? (int)(l_right / n_right) : lum;
            int avg_top = n_top ? (int)(l_top / n_top) : lum;
            int avg_bot = n_bot ? (int)(l_bot / n_bot) : lum;
            int dx = avg_right - avg_left;
            int dy = avg_bot - avg_top;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int edge = adx + ady;

            char ch = ascii_char_for_luma(lum, false);
            int bg_r = 238, bg_g = 236, bg_b = 222;
            int ink_r = 18, ink_g = 20, ink_b = 18;
            bool block_mode = false;
            int block_fill = 0;

            if (style == REMAP_STYLE_EDGE || style == REMAP_STYLE_OLDSKOOL) {
                int threshold = style == REMAP_STYLE_EDGE ? 42 : 32;
                if (edge < threshold) {
                    ch = (style == REMAP_STYLE_OLDSKOOL && lum < 86) ? '.' : ' ';
                } else if (adx > ady * 2) {
                    ch = (style == REMAP_STYLE_OLDSKOOL) ? ')' : '|';
                } else if (ady > adx * 2) {
                    ch = (style == REMAP_STYLE_OLDSKOOL) ? '_' : '-';
                } else {
                    ch = ((dx < 0) == (dy < 0)) ? '\\' : '/';
                }
                bg_r = 238; bg_g = 236; bg_b = 222;
                ink_r = 14; ink_g = 14; ink_b = 16;
            } else if (style == REMAP_STYLE_COLOR) {
                ch = ascii_char_for_luma(lum, false);
                bg_r = 4; bg_g = 5; bg_b = 7;
                ink_r = ar + 36; if (ink_r > 255) ink_r = 255;
                ink_g = ag + 36; if (ink_g > 255) ink_g = 255;
                ink_b = ab + 36; if (ink_b > 255) ink_b = 255;
            } else if (style == REMAP_STYLE_BLOCK) {
                block_mode = true;
                block_fill = 7 - (lum * 7 + 127) / 255;
                if (block_fill < 0) block_fill = 0;
                if (block_fill > 7) block_fill = 7;
                bg_r = 236; bg_g = 238; bg_b = 224;
                ink_r = 16; ink_g = 20; ink_b = 18;
            } else if (style == REMAP_STYLE_MATRIX) {
                ch = ascii_char_for_luma(lum, true);
                bg_r = 0; bg_g = 12; bg_b = 8;
                ink_r = 64; ink_g = 255; ink_b = 116;
            }

            for (int y = 0; y < bh; y++) {
                int row = ((by + y) * w + bx) * 3;
                for (int x = 0; x < bw; x++) {
                    int idx = row + x * 3;
                    int gy = (y * 7) / (bh > 0 ? bh : 1);
                    uint8_t alpha = block_mode
                                  ? (gy >= 7 - block_fill ? 255 : 0)
                                  : remap_glyph_alpha(ch, x, y, bw, bh);
                    int rr = (bg_r * (255 - alpha) + ink_r * alpha) / 255;
                    int rg = (bg_g * (255 - alpha) + ink_g * alpha) / 255;
                    int rb = (bg_b * (255 - alpha) + ink_b * alpha) / 255;
                    rgb[idx + 0] = (uint8_t)((rgb[idx + 0] * (10 - strength) + rr * strength) / 10);
                    rgb[idx + 1] = (uint8_t)((rgb[idx + 1] * (10 - strength) + rg * strength) / 10);
                    rgb[idx + 2] = (uint8_t)((rgb[idx + 2] * (10 - strength) + rb * strength) / 10);
                }
            }
        }
    }
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
    pipe->remap.enabled = false;
    pipe->remap.style = REMAP_STYLE_ASCII;
    pipe->remap.cell_size = 8;
    pipe->remap.strength = 10;
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
                                bool remap_enabled, int remap_style,
                                int remap_cell_size, int remap_strength,
                                bool lomo_enabled, int lomo_preset, int lomo_strength,
                                bool bend_enabled, int bend_preset, int bend_strength,
                                int post_fx_mode, int post_fx_intensity,
                                int active_panel, bool panel_open) {
    pipe->capture_mode = capture_mode;
    pipe->active_panel = active_panel;
    pipe->panel_open = panel_open;
    pipe->gb.enabled = gb_enabled;
    pipe->gb.params = *gb_params;
    pipe->remap.enabled = remap_enabled;
    pipe->remap.style = clamp_remap_style(remap_style);
    pipe->remap.cell_size = clamp_remap_cell_size(remap_cell_size);
    pipe->remap.strength = clamp_strength(remap_strength);
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
    out->use_remap = pipe->remap.enabled;
    out->remap_style = clamp_remap_style(pipe->remap.style);
    out->remap_cell_size = clamp_remap_cell_size(pipe->remap.cell_size);
    out->remap_strength = clamp_strength(pipe->remap.strength);
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
            (recipe->use_remap && recipe->remap_strength > 0) ||
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

    if (recipe->use_remap && recipe->remap_strength > 0) {
        apply_remap(rgb, w, h, recipe->remap_style, recipe->remap_cell_size,
                    recipe->remap_strength);
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
    preset->remap_enabled = false;
    preset->remap_style = REMAP_STYLE_ASCII;
    preset->remap_cell_size = 8;
    preset->remap_strength = 10;
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
    if (pipe->remap.enabled) {
        static const char *remap_names[REMAP_STYLE_COUNT] = {
            "ASCII", "Edge", "Color", "Block", "Old", "Matrix"
        };
        int style = clamp_remap_style(pipe->remap.style);
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", remap_names[style]);
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
    preset->remap_enabled = pipe->remap.enabled;
    preset->remap_style = clamp_remap_style(pipe->remap.style);
    preset->remap_cell_size = clamp_remap_cell_size(pipe->remap.cell_size);
    preset->remap_strength = clamp_strength(pipe->remap.strength);
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
    pipe->remap.enabled = preset->remap_enabled;
    pipe->remap.style = clamp_remap_style(preset->remap_style);
    pipe->remap.cell_size = clamp_remap_cell_size(preset->remap_cell_size);
    pipe->remap.strength = clamp_strength(preset->remap_strength);
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
