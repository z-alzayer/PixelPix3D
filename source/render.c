#include "render.h"
#include "camera.h"
#include "gallery.h"
#include "editor.h"
#include <string.h>

void render_top_screen(bool use3d, bool timer_open,
                       const EditState *edit, const GalleryState *gal,
                       const WiggleState *wig,
                       uint8_t *edit_preview_rgb888,
                       uint16_t *wiggle_compose_buf,
                       uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                       bool comparing, const u8 *buf, const u8 *filtered_buf) {
    gfxSet3D(false);
    if (use3d || timer_open) {
        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        memset(fb, 0, CAMERA_WIDTH * CAMERA_HEIGHT * 3);
    } else if (edit->active && gal->count > 0) {
        edit_render_top(edit, gal, edit_preview_rgb888);
    } else {
        if (wig->preview) {
            // Compose cropped frame into a full 400x240 buffer (black borders),
            // then blit normally. This keeps the framebuffer column stride correct.
            int bx = (CAMERA_WIDTH  - wig->crop_w) / 2;
            int by = (CAMERA_HEIGHT - wig->crop_h) / 2;
            memset(wiggle_compose_buf, 0, CAMERA_WIDTH * CAMERA_HEIGHT * sizeof(uint16_t));
            const uint16_t *src = wiggle_preview_frames[wig->preview_frame];
            for (int row = 0; row < wig->crop_h; row++)
                memcpy(wiggle_compose_buf + (by + row) * CAMERA_WIDTH + bx,
                       src + row * wig->crop_w,
                       wig->crop_w * sizeof(uint16_t));
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            wiggle_compose_buf, 0, 0,
                                            CAMERA_WIDTH, CAMERA_HEIGHT);
        } else {
            void *blit_src;
            if (gal->mode && gal->count > 0)
                blit_src = gallery_thumbs[gal->anim_frame];
            else
                blit_src = comparing ? (void *)buf : (void *)filtered_buf;
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            blit_src, 0, 0, CAMERA_WIDTH, CAMERA_HEIGHT);
        }
    }

    // Flush top screen before C3D takes the GPU
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_TOP, true);
}
