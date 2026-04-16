#ifndef RENDER_H
#define RENDER_H

#include <3ds.h>
#include "app_state.h"
#include "camera.h"

// Render the top screen: edit preview, wiggle preview, gallery, or live camera.
// Handles gfxSet3D, framebuffer blit, flush, and swap.
void render_top_screen(bool use3d, bool timer_open,
                       const EditState *edit, const GalleryState *gal,
                       const WiggleState *wig,
                       uint8_t *edit_preview_rgb888,
                       uint16_t *wiggle_compose_buf,
                       uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                       bool comparing, const u8 *buf, const u8 *filtered_buf);

#endif
