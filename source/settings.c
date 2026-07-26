#include "settings.h"
#include "camera.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void ensure_settings_dir(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/pixelpix3d", 0777);
}

#define SETTINGS_MAX_LINES 384
#define SETTINGS_LINE_LEN   64
static char s_ini_lines[SETTINGS_MAX_LINES][SETTINGS_LINE_LEN];

// ---------------------------------------------------------------------------
// Helper: update or append a key=value in the INI file (no duplicates).
// Reads the file, replaces the first matching line, writes it back.
// ---------------------------------------------------------------------------
static void ini_set_key(const char *key, const char *value) {
    ensure_settings_dir();

    int n_lines = 0;
    bool found = false;
    int key_len = (int)strlen(key);

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (f) {
        char tmp[64];
        while (fgets(tmp, sizeof(tmp), f)) {
            bool is_match = (strncmp(tmp, key, key_len) == 0 && tmp[key_len] == '=');
            if (is_match && found)
                continue;  // drop duplicate
            if (n_lines >= SETTINGS_MAX_LINES)
                continue;  // drop overflow lines
            if (is_match) {
                snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN, "%s=%s\n", key, value);
                found = true;
            } else {
                snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN, "%s", tmp);
            }
            n_lines++;
        }
        fclose(f);
    }

    if (!found && n_lines < SETTINGS_MAX_LINES) {
        snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN, "%s=%s\n", key, value);
        n_lines++;
    }

    f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n_lines; i++)
        fputs(s_ini_lines[i], f);
    fclose(f);
}

void settings_save(const FilterParams *p, int save_scale, int shutter_button) {
    char val[16];
    snprintf(val, sizeof(val), "%d", p->pixel_size);       ini_set_key("pixel_size", val);
    snprintf(val, sizeof(val), "%d", p->color_levels);     ini_set_key("color_levels", val);
    snprintf(val, sizeof(val), "%.2f", (double)p->brightness); ini_set_key("brightness", val);
    snprintf(val, sizeof(val), "%.2f", (double)p->contrast);   ini_set_key("contrast", val);
    snprintf(val, sizeof(val), "%.2f", (double)p->gamma);      ini_set_key("gamma", val);
    snprintf(val, sizeof(val), "%.2f", (double)p->saturation); ini_set_key("saturation", val);
    snprintf(val, sizeof(val), "%d", p->palette);          ini_set_key("palette", val);
    snprintf(val, sizeof(val), "%d", p->dither_mode);      ini_set_key("dither_mode", val);
    snprintf(val, sizeof(val), "%d", p->invert ? 1 : 0);   ini_set_key("invert", val);
    snprintf(val, sizeof(val), "%d", save_scale);          ini_set_key("save_scale", val);
    snprintf(val, sizeof(val), "%d", p->fx_mode);          ini_set_key("fx_mode", val);
    snprintf(val, sizeof(val), "%d", p->fx_intensity);     ini_set_key("fx_intensity", val);
    snprintf(val, sizeof(val), "%d", shutter_button);      ini_set_key("shutter_button", val);
}

void settings_load(FilterParams *p, int *save_scale, int *shutter_button) {
    FilterParams defaults = FILTER_DEFAULTS;
    *p              = defaults;
    *save_scale     = 2;
    *shutter_button = 0;

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        settings_save(p, *save_scale, *shutter_button);
        return;
    }

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if      (strcmp(key, "pixel_size")   == 0) p->pixel_size   = atoi(val);
        else if (strcmp(key, "color_levels") == 0) p->color_levels = atoi(val);
        else if (strcmp(key, "brightness")   == 0) p->brightness   = strtof(val, NULL);
        else if (strcmp(key, "contrast")     == 0) p->contrast     = strtof(val, NULL);
        else if (strcmp(key, "gamma")        == 0) p->gamma        = strtof(val, NULL);
        else if (strcmp(key, "saturation")   == 0) p->saturation   = strtof(val, NULL);
        else if (strcmp(key, "palette")      == 0) p->palette      = atoi(val);
        else if (strcmp(key, "dither_mode")  == 0) p->dither_mode  = atoi(val);
        else if (strcmp(key, "invert")       == 0) p->invert       = (atoi(val) != 0);
        else if (strcmp(key, "save_scale")   == 0) *save_scale     = atoi(val);
        else if (strcmp(key, "fx_mode")      == 0) p->fx_mode      = atoi(val);
        else if (strcmp(key, "fx_intensity") == 0) p->fx_intensity = atoi(val);
        else if (strcmp(key, "shutter_button") == 0) *shutter_button = atoi(val);
    }
    fclose(f);

    // Clamp to valid ranges
    if (p->pixel_size   < 1)              p->pixel_size   = 1;
    if (p->pixel_size   > 8)              p->pixel_size   = 8;
    if (p->color_levels < 2)              p->color_levels = 2;
    if (p->color_levels > 8)              p->color_levels = 8;
    if (p->brightness   < 0.0f)           p->brightness   = 0.0f;
    if (p->brightness   > 2.0f)           p->brightness   = 2.0f;
    if (p->contrast     < 0.5f)           p->contrast     = 0.5f;
    if (p->contrast     > 2.0f)           p->contrast     = 2.0f;
    if (p->gamma        < 0.5f)           p->gamma        = 0.5f;
    if (p->gamma        > 2.0f)           p->gamma        = 2.0f;
    if (p->saturation   < 0.0f)           p->saturation   = 0.0f;
    if (p->saturation   > 2.0f)           p->saturation   = 2.0f;
    if (p->palette      < PALETTE_NONE)   p->palette      = PALETTE_NONE;
    if (p->palette      >= PALETTE_COUNT) p->palette      = PALETTE_COUNT - 1;
    if (p->dither_mode  < 0)              p->dither_mode  = 0;
    if (p->dither_mode  > 3)              p->dither_mode  = 3;
    if (*save_scale     < 1)              *save_scale     = 1;
    if (*save_scale     > MAX_SAVE_SCALE) *save_scale     = MAX_SAVE_SCALE;
    if (p->fx_mode      < 0)              p->fx_mode      = 0;
    if (p->fx_mode      > 6)              p->fx_mode      = 6;
    if (p->fx_intensity < 0)              p->fx_intensity = 0;
    if (p->fx_intensity > 10)             p->fx_intensity = 10;
    if (*shutter_button < 0)              *shutter_button = 0;
    if (*shutter_button > 1)              *shutter_button = 1;
}

void settings_save_palettes(const PaletteDef *user_palettes) {
    for (int n = 0; n < PALETTE_COUNT; n++)
        for (int m = 0; m < user_palettes[n].size; m++) {
            char key[32], val[8];
            snprintf(key, sizeof(key), "palette_%d_%d", n, m);
            snprintf(val, sizeof(val), "%02X%02X%02X",
                     user_palettes[n].colors[m][0],
                     user_palettes[n].colors[m][1],
                     user_palettes[n].colors[m][2]);
            ini_set_key(key, val);
        }
}

void settings_load_palettes(PaletteDef *user_palettes) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strncmp(key, "palette_", 8) != 0) continue;
        int n = -1, m = -1;
        if (sscanf(key + 8, "%d_%d", &n, &m) != 2) continue;
        if (n < 0 || n >= PALETTE_COUNT) continue;
        if (m < 0 || m >= user_palettes[n].size) continue;

        unsigned int rgb = 0;
        if (sscanf(val, "%06X", &rgb) != 1) continue;
        user_palettes[n].colors[m][0] = (uint8_t)((rgb >> 16) & 0xFF);
        user_palettes[n].colors[m][1] = (uint8_t)((rgb >>  8) & 0xFF);
        user_palettes[n].colors[m][2] = (uint8_t)( rgb        & 0xFF);
    }
    fclose(f);
}

void settings_save_anaglyph_colors(const uint8_t colors[2][3]) {
    ensure_settings_dir();

    char left_val[8], right_val[8];
    snprintf(left_val, sizeof(left_val), "%02X%02X%02X",
             colors[0][0], colors[0][1], colors[0][2]);
    snprintf(right_val, sizeof(right_val), "%02X%02X%02X",
             colors[1][0], colors[1][1], colors[1][2]);

    int n_lines = 0;
    bool found_left = false;
    bool found_right = false;

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (f) {
        char tmp[64];
        while (fgets(tmp, sizeof(tmp), f)) {
            bool is_left = strncmp(tmp, "anaglyph_left=", 14) == 0;
            bool is_right = strncmp(tmp, "anaglyph_right=", 15) == 0;
            if ((is_left && found_left) || (is_right && found_right))
                continue;
            if (n_lines >= SETTINGS_MAX_LINES)
                continue;
            if (is_left) {
                snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN,
                         "anaglyph_left=%s\n", left_val);
                found_left = true;
            } else if (is_right) {
                snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN,
                         "anaglyph_right=%s\n", right_val);
                found_right = true;
            } else {
                snprintf(s_ini_lines[n_lines], SETTINGS_LINE_LEN, "%s", tmp);
            }
            n_lines++;
        }
        fclose(f);
    }

    if (!found_left && n_lines < SETTINGS_MAX_LINES)
        snprintf(s_ini_lines[n_lines++], SETTINGS_LINE_LEN,
                 "anaglyph_left=%s\n", left_val);
    if (!found_right && n_lines < SETTINGS_MAX_LINES)
        snprintf(s_ini_lines[n_lines++], SETTINGS_LINE_LEN,
                 "anaglyph_right=%s\n", right_val);

    f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n_lines; i++)
        fputs(s_ini_lines[i], f);
    fclose(f);
}

void settings_load_anaglyph_colors(uint8_t colors[2][3]) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        int idx = -1;
        if (strcmp(key, "anaglyph_left") == 0) idx = 0;
        else if (strcmp(key, "anaglyph_right") == 0) idx = 1;
        if (idx < 0) continue;

        if (strlen(val) != 6) continue;
        unsigned int rgb = 0;
        char extra = '\0';
        if (sscanf(val, "%6X%c", &rgb, &extra) != 1) continue;
        colors[idx][0] = (uint8_t)((rgb >> 16) & 0xFF);
        colors[idx][1] = (uint8_t)((rgb >> 8) & 0xFF);
        colors[idx][2] = (uint8_t)(rgb & 0xFF);
    }
    fclose(f);
}

void settings_save_ranges(const FilterRanges *r) {
    char val[16];
    snprintf(val, sizeof(val), "%.2f", (double)r->bright_min);   ini_set_key("bright_min", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->bright_max);   ini_set_key("bright_max", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->bright_def);   ini_set_key("bright_def", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->contrast_min); ini_set_key("contrast_min", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->contrast_max); ini_set_key("contrast_max", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->contrast_def); ini_set_key("contrast_def", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->sat_min);      ini_set_key("sat_min", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->sat_max);      ini_set_key("sat_max", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->sat_def);      ini_set_key("sat_def", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->gamma_min);    ini_set_key("gamma_min", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->gamma_max);    ini_set_key("gamma_max", val);
    snprintf(val, sizeof(val), "%.2f", (double)r->gamma_def);    ini_set_key("gamma_def", val);
}

void settings_load_ranges(FilterRanges *r) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if      (strcmp(key, "bright_min")   == 0) r->bright_min   = strtof(val, NULL);
        else if (strcmp(key, "bright_max")   == 0) r->bright_max   = strtof(val, NULL);
        else if (strcmp(key, "bright_def")   == 0) r->bright_def   = strtof(val, NULL);
        else if (strcmp(key, "contrast_min") == 0) r->contrast_min = strtof(val, NULL);
        else if (strcmp(key, "contrast_max") == 0) r->contrast_max = strtof(val, NULL);
        else if (strcmp(key, "contrast_def") == 0) r->contrast_def = strtof(val, NULL);
        else if (strcmp(key, "sat_min")      == 0) r->sat_min      = strtof(val, NULL);
        else if (strcmp(key, "sat_max")      == 0) r->sat_max      = strtof(val, NULL);
        else if (strcmp(key, "sat_def")      == 0) r->sat_def      = strtof(val, NULL);
        else if (strcmp(key, "gamma_min")    == 0) r->gamma_min    = strtof(val, NULL);
        else if (strcmp(key, "gamma_max")    == 0) r->gamma_max    = strtof(val, NULL);
        else if (strcmp(key, "gamma_def")    == 0) r->gamma_def    = strtof(val, NULL);
    }
    fclose(f);

    // Clamp to absolute limits
    if (r->bright_min   < 0.0f)  r->bright_min   = 0.0f;
    if (r->bright_max   > 4.0f)  r->bright_max   = 4.0f;
    if (r->contrast_min < 0.1f)  r->contrast_min = 0.1f;
    if (r->contrast_max > 4.0f)  r->contrast_max = 4.0f;
    if (r->sat_min      < 0.0f)  r->sat_min      = 0.0f;
    if (r->sat_max      > 4.0f)  r->sat_max      = 4.0f;
    if (r->gamma_min    < 0.1f)  r->gamma_min    = 0.1f;
    if (r->gamma_max    > 4.0f)  r->gamma_max    = 4.0f;

    // Enforce min <= def <= max
    if (r->bright_min   > r->bright_max)   r->bright_min   = r->bright_max;
    if (r->bright_def   < r->bright_min)   r->bright_def   = r->bright_min;
    if (r->bright_def   > r->bright_max)   r->bright_def   = r->bright_max;
    if (r->contrast_min > r->contrast_max) r->contrast_min = r->contrast_max;
    if (r->contrast_def < r->contrast_min) r->contrast_def = r->contrast_min;
    if (r->contrast_def > r->contrast_max) r->contrast_def = r->contrast_max;
    if (r->sat_min      > r->sat_max)      r->sat_min      = r->sat_max;
    if (r->sat_def      < r->sat_min)      r->sat_def      = r->sat_min;
    if (r->sat_def      > r->sat_max)      r->sat_def      = r->sat_max;
    if (r->gamma_min    > r->gamma_max)    r->gamma_min    = r->gamma_max;
    if (r->gamma_def    < r->gamma_min)    r->gamma_def    = r->gamma_min;
    if (r->gamma_def    > r->gamma_max)    r->gamma_def    = r->gamma_max;
}

void settings_save_pipeline_presets(const PipelinePreset presets[PIPELINE_PRESET_COUNT]) {
    char key[32], val[64];
    for (int i = 0; i < PIPELINE_PRESET_COUNT; i++) {
        snprintf(key, sizeof(key), "preset_%d_name", i);
        ini_set_key(key, presets[i].name);

        snprintf(key, sizeof(key), "preset_%d_gb_enabled", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_enabled ? 1 : 0);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_remap_enabled", i);
        snprintf(val, sizeof(val), "%d", presets[i].remap_enabled ? 1 : 0);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_remap_style", i);
        snprintf(val, sizeof(val), "%d", presets[i].remap_style);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_remap_cell_size", i);
        snprintf(val, sizeof(val), "%d", presets[i].remap_cell_size);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_remap_strength", i);
        snprintf(val, sizeof(val), "%d", presets[i].remap_strength);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_base_enabled", i);
        snprintf(val, sizeof(val), "%d", presets[i].base_enabled ? 1 : 0);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_base_preset", i);
        snprintf(val, sizeof(val), "%d", presets[i].base_preset);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_base_strength", i);
        snprintf(val, sizeof(val), "%d", presets[i].base_strength);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_bend_enabled", i);
        snprintf(val, sizeof(val), "%d", presets[i].bend_enabled ? 1 : 0);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_bend_preset", i);
        snprintf(val, sizeof(val), "%d", presets[i].bend_preset);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_bend_strength", i);
        snprintf(val, sizeof(val), "%d", presets[i].bend_strength);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_fx_mode", i);
        snprintf(val, sizeof(val), "%d", presets[i].fx_mode);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_fx_intensity", i);
        snprintf(val, sizeof(val), "%d", presets[i].fx_intensity);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_pixel_size", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_params.pixel_size);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_color_levels", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_params.color_levels);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_brightness", i);
        snprintf(val, sizeof(val), "%.2f", (double)presets[i].gb_params.brightness);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_contrast", i);
        snprintf(val, sizeof(val), "%.2f", (double)presets[i].gb_params.contrast);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_gamma", i);
        snprintf(val, sizeof(val), "%.2f", (double)presets[i].gb_params.gamma);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_saturation", i);
        snprintf(val, sizeof(val), "%.2f", (double)presets[i].gb_params.saturation);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_palette", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_params.palette);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_dither_mode", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_params.dither_mode);
        ini_set_key(key, val);

        snprintf(key, sizeof(key), "preset_%d_invert", i);
        snprintf(val, sizeof(val), "%d", presets[i].gb_params.invert ? 1 : 0);
        ini_set_key(key, val);
    }
}

void settings_load_pipeline_presets(PipelinePreset presets[PIPELINE_PRESET_COUNT]) {
    for (int i = 0; i < PIPELINE_PRESET_COUNT; i++) {
        pipeline_preset_default(&presets[i], i);
    }

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        settings_save_pipeline_presets(presets);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        int idx = -1;
        char field[32];
        if (sscanf(key, "preset_%d_%31s", &idx, field) != 2) continue;
        if (idx < 0 || idx >= PIPELINE_PRESET_COUNT) continue;

        PipelinePreset *p = &presets[idx];
        if      (strcmp(field, "name") == 0)          snprintf(p->name, sizeof(p->name), "%s", val);
        else if (strcmp(field, "gb_enabled") == 0)    p->gb_enabled = atoi(val) != 0;
        else if (strcmp(field, "remap_enabled") == 0) p->remap_enabled = atoi(val) != 0;
        else if (strcmp(field, "remap_style") == 0)   p->remap_style = atoi(val);
        else if (strcmp(field, "remap_cell_size") == 0) p->remap_cell_size = atoi(val);
        else if (strcmp(field, "remap_strength") == 0) p->remap_strength = atoi(val);
        else if (strcmp(field, "base_enabled") == 0)  p->base_enabled = atoi(val) != 0;
        else if (strcmp(field, "base_preset") == 0)   p->base_preset = atoi(val);
        else if (strcmp(field, "base_strength") == 0) p->base_strength = atoi(val);
        else if (strcmp(field, "bend_enabled") == 0)  p->bend_enabled = atoi(val) != 0;
        else if (strcmp(field, "bend_preset") == 0)   p->bend_preset = atoi(val);
        else if (strcmp(field, "bend_strength") == 0) p->bend_strength = atoi(val);
        else if (strcmp(field, "fx_mode") == 0)       p->fx_mode = atoi(val);
        else if (strcmp(field, "fx_intensity") == 0)  p->fx_intensity = atoi(val);
        else if (strcmp(field, "pixel_size") == 0)    p->gb_params.pixel_size = atoi(val);
        else if (strcmp(field, "color_levels") == 0)  p->gb_params.color_levels = atoi(val);
        else if (strcmp(field, "brightness") == 0)    p->gb_params.brightness = strtof(val, NULL);
        else if (strcmp(field, "contrast") == 0)      p->gb_params.contrast = strtof(val, NULL);
        else if (strcmp(field, "gamma") == 0)         p->gb_params.gamma = strtof(val, NULL);
        else if (strcmp(field, "saturation") == 0)    p->gb_params.saturation = strtof(val, NULL);
        else if (strcmp(field, "palette") == 0)       p->gb_params.palette = atoi(val);
        else if (strcmp(field, "dither_mode") == 0)   p->gb_params.dither_mode = atoi(val);
        else if (strcmp(field, "invert") == 0)        p->gb_params.invert = atoi(val) != 0;
    }
    fclose(f);
}

void settings_save_effect_tuning(const EffectTuning *t) {
    char key[40], val[16];
    ini_set_key("effect_tuning_version", "2");
    for (int i = 0; i < LOMO_PRESET_COUNT; i++) {
        snprintf(key, sizeof(key), "lomo_%d_exposure", i);
        snprintf(val, sizeof(val), "%d", t->lomo[i].exposure);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "lomo_%d_color", i);
        snprintf(val, sizeof(val), "%d", t->lomo[i].color);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "lomo_%d_texture", i);
        snprintf(val, sizeof(val), "%d", t->lomo[i].texture);
        ini_set_key(key, val);
    }
    for (int i = 0; i < BEND_PRESET_COUNT; i++) {
        snprintf(key, sizeof(key), "bend_%d_wave", i);
        snprintf(val, sizeof(val), "%d", t->bend[i].wave);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "bend_%d_chaos", i);
        snprintf(val, sizeof(val), "%d", t->bend[i].chaos);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "bend_%d_seed", i);
        snprintf(val, sizeof(val), "%d", t->bend[i].seed);
        ini_set_key(key, val);
    }
    for (int i = 0; i <= 6; i++) {
        snprintf(key, sizeof(key), "fx_%d_shape", i);
        snprintf(val, sizeof(val), "%d", t->fx[i].shape);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "fx_%d_depth", i);
        snprintf(val, sizeof(val), "%d", t->fx[i].depth);
        ini_set_key(key, val);
        snprintf(key, sizeof(key), "fx_%d_scale", i);
        snprintf(val, sizeof(val), "%d", t->fx[i].scale);
        ini_set_key(key, val);
    }
}

void settings_load_effect_tuning(EffectTuning *t) {
    effect_tuning_defaults(t);

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        settings_save_effect_tuning(t);
        return;
    }

    char line[64];
    int version = 0;
    while (fgets(line, sizeof(line), f)) {
        int n = 0;
        if (sscanf(line, "effect_tuning_version=%d", &n) == 1) {
            version = n;
            break;
        }
    }
    if (version < 2) {
        fclose(f);
        return;
    }
    rewind(f);

    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        int v = atoi(eq + 1);
        if (v < 0) v = 0;
        if (v > 10) v = 10;

        int idx = -1;
        char field[24];
        if (sscanf(key, "lomo_%d_%23s", &idx, field) == 2 &&
            idx >= 0 && idx < LOMO_PRESET_COUNT) {
            if (strcmp(field, "exposure") == 0) t->lomo[idx].exposure = v;
            else if (strcmp(field, "color") == 0) t->lomo[idx].color = v;
            else if (strcmp(field, "texture") == 0) t->lomo[idx].texture = v;
        } else if (sscanf(key, "bend_%d_%23s", &idx, field) == 2 &&
                   idx >= 0 && idx < BEND_PRESET_COUNT) {
            if (strcmp(field, "wave") == 0) t->bend[idx].wave = v;
            else if (strcmp(field, "chaos") == 0) t->bend[idx].chaos = v;
            else if (strcmp(field, "seed") == 0) t->bend[idx].seed = v;
        } else if (sscanf(key, "fx_%d_%23s", &idx, field) == 2 &&
                   idx >= 0 && idx <= 6) {
            if (strcmp(field, "shape") == 0) t->fx[idx].shape = v;
            else if (strcmp(field, "depth") == 0) t->fx[idx].depth = v;
            else if (strcmp(field, "scale") == 0) t->fx[idx].scale = v;
        }
    }
    fclose(f);
}
