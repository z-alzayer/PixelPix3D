#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void ensure_settings_dir(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/pixelpix3d", 0777);
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
    if (p->dither_mode  > 1)              p->dither_mode  = 1;
    if (*save_scale     < 1)              *save_scale     = 1;
    if (*save_scale     > 2)              *save_scale     = 2;
}
