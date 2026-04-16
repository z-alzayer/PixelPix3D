#ifndef GALLERY_H
#define GALLERY_H

#include <3ds.h>
#include "app_state.h"
#include "camera.h"

// Shared gallery thumbnail buffers (owned by gallery.c).
extern uint16_t gallery_thumbs[GALLERY_WIGGLE_MAX_FRAMES][CAMERA_WIDTH * CAMERA_HEIGHT];

// Toggle gallery mode on/off.  Stops/restarts camera capture as needed.
// camReceiveEvent: the 4-element camera event handle array.
// captureInterrupted: pointer to the capture-interrupted flag.
void gallery_toggle(GalleryState *gal, AppState *app, EditState *edit,
                    Handle camReceiveEvent[4], bool *captureInterrupted);

// Load the currently selected photo into gallery_thumbs[].
// Updates gal->n_frames, delay_ms, anim_tick, anim_frame, loaded.
void gallery_load_selected(GalleryState *gal);

// Handle d-pad scrolling in the 4x4 gallery grid.
void gallery_handle_dpad(GalleryState *gal, u32 kDown);

// Advance gallery wiggle animation (clock-based frame cycling).
void gallery_tick(GalleryState *gal);

#endif
