#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <3ds.h>
#include "filter.h"
#include "ui.h"
#include "settings.h"

// Hit-test: returns true if (px, py) is inside rect [rx, ry, rw, rh]
bool hit(int px, int py, int rx, int ry, int rw, int rh);

// Process bottom-screen touch input for one frame.
// Updates *p, *active_tab, *save_scale, and *default_params as needed.
// Sets *do_cam_toggle and *do_save flags for the caller to act on.
// Returns true if any touch input was consumed.
bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  FilterParams *p,
                  bool *do_cam_toggle, bool *do_save, bool *do_defaults_save,
                  int *active_tab, int *save_scale,
                  FilterParams *default_params,
                  FilterRanges *ranges,
                  PaletteDef *user_palettes,
                  int *palette_sel_pal, int *palette_sel_color,
                  bool *do_gallery_toggle,
                  bool gallery_mode, int gallery_count, int *gallery_sel, int *gallery_scroll,
                  int *shoot_mode, bool *shoot_mode_open,
                  int *shoot_timer_secs, bool *timer_open,
                  int *wiggle_frames, int *wiggle_delay_ms,
                  int *lomo_preset);

#endif
