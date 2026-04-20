#include "render.h"
#include "camera.h"
#include "gallery.h"
#include "editor.h"
#include "wigglegram.h"
#include <string.h>

// Scratch buffers owned by this module
static uint8_t  s_edit_preview_rgb888[CAMERA_WIDTH * CAMERA_HEIGHT * 3];
static uint16_t s_wiggle_compose_buf[CAMERA_WIDTH * CAMERA_HEIGHT];
uint16_t s_raw_display_buf[CAMERA_WIDTH * CAMERA_HEIGHT];

void render_top_screen(bool use3d, bool timer_open,
                       const EditState *edit, const GalleryState *gal,
                       const WiggleState *wig,
                       uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                       bool comparing, const u8 *buf, const u8 *filtered_buf) {
    gfxSet3D(false);
    if (use3d || timer_open) {
        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        memset(fb, 0, CAMERA_WIDTH * CAMERA_HEIGHT * 3);
    } else if (edit->active && gal->count > 0) {
        edit_render_top(edit, gal, s_edit_preview_rgb888);
    } else {
        if (wig->preview) {
            // Compose cropped frame into a full 400x240 buffer (black borders),
            // then blit normally. This keeps the framebuffer column stride correct.
            int bx = (CAMERA_WIDTH  - wig->crop_w) / 2;
            int by = (CAMERA_HEIGHT - wig->crop_h) / 2;
            memset(s_wiggle_compose_buf, 0, sizeof(s_wiggle_compose_buf));
            const uint16_t *src = wiggle_preview_frames[wig->preview_frame];
            for (int row = 0; row < wig->crop_h; row++)
                memcpy(s_wiggle_compose_buf + (by + row) * CAMERA_WIDTH + bx,
                       src + row * wig->crop_w,
                       wig->crop_w * sizeof(uint16_t));
            u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            writePictureToFramebufferRGB565(fb, s_wiggle_compose_buf, 0, 0,
                                            CAMERA_WIDTH, CAMERA_HEIGHT);
            // Dim the framebuffer while the filter is being applied
            if (wiggle_filter_busy()) {
                int fb_size = CAMERA_WIDTH * CAMERA_HEIGHT * 3;
                for (int i = 0; i < fb_size; i++)
                    fb[i] >>= 1;
            }
        } else {
            void *blit_src;
            if (gal->mode && gal->count > 0)
                blit_src = gallery_thumbs[gal->anim_frame];
            else if (comparing)
                blit_src = s_raw_display_buf;  // pre-populated in main.c case 2
            else
                blit_src = (void *)filtered_buf;
            writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                            blit_src, 0, 0, CAMERA_WIDTH, CAMERA_HEIGHT);
        }
    }

    // Flush top screen before C3D takes the GPU
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_TOP, true);
}
