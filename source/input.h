#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <3ds.h>
#include "filter.h"
#include "ui.h"

// Hit-test: returns true if (px, py) is inside rect [rx, ry, rw, rh]
bool hit(int px, int py, int rx, int ry, int rw, int rh);

// Process bottom-screen touch input for one frame.
// Updates *p with any slider/button changes.
// Sets *do_cam_toggle and *do_save flags for the caller to act on.
// Returns true if any touch input was consumed.
bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  FilterParams *p,
                  bool *do_cam_toggle, bool *do_save);

#endif
