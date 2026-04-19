#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <3ds.h>
#include "app_state.h"
#include "ui.h"

// Hit-test: returns true if (px, py) is inside rect [rx, ry, rw, rh]
bool hit(int px, int py, int rx, int ry, int rw, int rh);

// Process bottom-screen touch input for one frame.
// Sets output flags for the caller to act on.
// Returns true if any touch input was consumed.
bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  AppState *app, ShootState *shoot, WiggleState *wig,
                  GalleryState *gal, EditState *edit,
                  bool *do_cam_toggle, bool *do_save, bool *do_defaults_save,
                  bool *do_defaults_reset,
                  bool *do_gallery_toggle,
                  bool *do_edit_cancel, bool *do_edit_savenew,
                  bool *do_edit_overwrite, bool *do_edit_enter);

#endif
