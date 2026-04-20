#ifndef RENDER_H
#define RENDER_H

#include <3ds.h>
#include "app_state.h"
#include "camera.h"

// Raw display buffer — populated by main.c when comparing, read by render.
extern uint16_t s_raw_display_buf[];

// Render the top screen: edit preview, wiggle preview, gallery, or live camera.
// Handles gfxSet3D, framebuffer blit, flush, and swap.
void render_top_screen(bool use3d, bool timer_open,
                       const EditState *edit, const GalleryState *gal,
                       const WiggleState *wig,
                       uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                       bool comparing, const u8 *buf, const u8 *filtered_buf);

#endif
