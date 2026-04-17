#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void ensure_settings_dir(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/pixelpix3d", 0777);
}

// ---------------------------------------------------------------------------
// Helper: update or append a key=value in the INI file (no duplicates).
// Reads the file, replaces the first matching line, writes it back.
// ---------------------------------------------------------------------------
static void ini_set_key(const char *key, const char *value) {
    ensure_settings_dir();

    char lines[128][64];
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
            if (n_lines >= 128)
                continue;  // drop overflow lines
            if (is_match) {
                snprintf(lines[n_lines], 64, "%s=%s\n", key, value);
                found = true;
            } else {
                memcpy(lines[n_lines], tmp, 64);
            }
            n_lines++;
        }
        fclose(f);
    }

    if (!found && n_lines < 128) {
        snprintf(lines[n_lines], 64, "%s=%s\n", key, value);
        n_lines++;
    }

    f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n_lines; i++)
        fputs(lines[i], f);
    fclose(f);
}

void settings_save(const FilterParams *p, int save_scale) {
    ensure_settings_dir();
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "pixel_size=%d\n",   p->pixel_size);
    fprintf(f, "color_levels=%d\n", p->color_levels);
    fprintf(f, "brightness=%.2f\n", (double)p->brightness);
    fprintf(f, "contrast=%.2f\n",   (double)p->contrast);
    fprintf(f, "gamma=%.2f\n",      (double)p->gamma);
    fprintf(f, "saturation=%.2f\n", (double)p->saturation);
    fprintf(f, "palette=%d\n",      p->palette);
    fprintf(f, "dither_mode=%d\n",  p->dither_mode);
    fprintf(f, "invert=%d\n",       p->invert ? 1 : 0);
    fprintf(f, "save_scale=%d\n",   save_scale);
    fprintf(f, "fx_mode=%d\n",      p->fx_mode);
    fprintf(f, "fx_intensity=%d\n", p->fx_intensity);
    fclose(f);
}

void settings_load(FilterParams *p, int *save_scale) {
    FilterParams defaults = FILTER_DEFAULTS;
    *p          = defaults;
    *save_scale = 2;

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        settings_save(p, *save_scale);
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
    if (*save_scale     > 4)              *save_scale     = 4;
    if (p->fx_mode      < 0)              p->fx_mode      = 0;
    if (p->fx_mode      > 6)              p->fx_mode      = 6;
    if (p->fx_intensity < 0)              p->fx_intensity = 0;
    if (p->fx_intensity > 10)             p->fx_intensity = 10;
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

int settings_load_file_counter(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return 0;
    char line[64];
    int result = 0;
    while (fgets(line, sizeof(line), f)) {
        int n = 0;
        if (sscanf(line, "next_file_n=%d", &n) == 1) { result = n; break; }
    }
    fclose(f);
    return result;
}

void settings_save_file_counter(int n) {
    char val[16];
    snprintf(val, sizeof(val), "%d", n);
    ini_set_key("next_file_n", val);
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
