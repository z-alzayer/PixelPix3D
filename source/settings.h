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

#endif
