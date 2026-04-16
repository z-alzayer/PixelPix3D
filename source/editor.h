#ifndef EDITOR_H
#define EDITOR_H

#include <3ds.h>
#include "app_state.h"
#include "camera.h"

// Enter edit mode or pick up sticker (from gallery Edit button / info tap).
void edit_enter_or_place(EditState *edit);

// Cancel edit mode — clear all placed stickers and frame overlay.
void edit_cancel(EditState *edit);

// Save edited photo (with stickers + frame) as JPEG or APNG.
// Refreshes gallery list and exits edit mode on success.
void edit_save(EditState *edit, GalleryState *gal,
               bool overwrite);

// Handle physical button input in edit mode (sticker placement / picker).
// Called when edit.active && active_tab == TAB_SHOOT.
void edit_handle_input(EditState *edit, u32 kDown, u32 kHeld);

// Render the edit-mode composited preview to the top screen.
// Writes into the provided rgb888 scratch buffer, converts to RGB565, and blits.
void edit_render_top(const EditState *edit, const GalleryState *gal,
                     uint8_t *rgb888_buf);

#endif
