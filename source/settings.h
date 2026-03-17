#ifndef SETTINGS_H
#define SETTINGS_H

#include "filter.h"

#define SETTINGS_PATH "sdmc:/3ds/pixelpix3d/settings.ini"

// Loads settings from SETTINGS_PATH into *p and *save_scale.
// If the file is missing, writes defaults and returns.
// Unknown keys are silently ignored (forward-compatible).
void settings_load(FilterParams *p, int *save_scale);

// Saves current params and save_scale to SETTINGS_PATH.
// Creates the directory if it does not exist.
void settings_save(const FilterParams *p, int save_scale);

// Appends palette_N_M=RRGGBB lines to SETTINGS_PATH (call after settings_save).
void settings_save_palettes(const PaletteDef *user_palettes);

// Loads palette_N_M colour overrides from SETTINGS_PATH into user_palettes[].
// Silently skips missing or out-of-bounds keys.
void settings_load_palettes(PaletteDef *user_palettes);

#endif
