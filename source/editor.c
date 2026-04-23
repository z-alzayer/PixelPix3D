#include "editor.h"
#include "camera.h"
#include "gallery.h"
#include "sticker.h"
#include "image_load.h"
#include "settings.h"
#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// edit_enter_or_place — enter edit mode or pick up sticker
// ---------------------------------------------------------------------------

void edit_enter_or_place(EditState *edit) {
    if (!edit->active) {
        // Reset cursor to centre when entering edit mode
        edit->cursor_x      = (float)CAMERA_WIDTH  / 2.0f;
        edit->cursor_y      = (float)CAMERA_HEIGHT / 2.0f;
        edit->pending_scale = 2.0f;
        edit->pending_angle = 0.0f;
        edit->placing       = false;
        edit->sticker_cat   = 0;
        edit->sticker_sel   = 0;
        edit->sticker_scroll = 0;
        sticker_cat_load(0);
        edit->active = true;
    } else if (edit->tab == 0) {
        // Info area tap = pick up sticker (enter placement mode), reset cursor to centre
        edit->cursor_x = (float)CAMERA_WIDTH  / 2.0f;
        edit->cursor_y = (float)CAMERA_HEIGHT / 2.0f;
        edit->placing  = true;
    }
}

// ---------------------------------------------------------------------------
// edit_cancel — discard all edits and exit edit mode
// ---------------------------------------------------------------------------

void edit_cancel(EditState *edit) {
    edit->active = false;
    edit->placing = false;
    for (int i = 0; i < STICKER_MAX; i++) edit->placed[i].active = false;
    edit->gallery_frame = -1;
}

// ---------------------------------------------------------------------------
// Composite callback for save_edited_apng
// ---------------------------------------------------------------------------

typedef struct {
    PlacedSticker *stickers;
    int            n_stickers;
    int            frame_idx;
    const char    *frame_path;
} EditCompositeCtx;

static void edit_composite_cb(uint8_t *rgb888, int w, int h, void *ud) {
    EditCompositeCtx *ctx = (EditCompositeCtx *)ud;
    for (int si = 0; si < ctx->n_stickers; si++) {
        if (!ctx->stickers[si].active) continue;
        const unsigned char *px = get_sticker_pixels(ctx->stickers[si].cat_idx,
                                                      ctx->stickers[si].icon_idx);
        if (px)
            composite_sticker_rgb888(rgb888, w, h, px,
                                     ctx->stickers[si].x, ctx->stickers[si].y,
                                     ctx->stickers[si].scale, ctx->stickers[si].angle_deg);
    }
    if (ctx->frame_idx >= 0 && ctx->frame_path)
        composite_frame_rgb888(rgb888, w, h, ctx->frame_path);
}

static int round_to_int(float v) {
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static void remap_stickers_for_save(PlacedSticker *dst,
                                    const PlacedSticker *src,
                                    int src_w, int src_h,
                                    bool wiggle_source) {
    int crop_x = 0, crop_y = 0, crop_w = src_w, crop_h = src_h;
    if (wiggle_source) {
        if ((long long)src_w * CAMERA_HEIGHT > (long long)src_h * CAMERA_WIDTH) {
            crop_h = src_h;
            crop_w = (src_h * CAMERA_WIDTH) / CAMERA_HEIGHT;
            if (crop_w < 1) crop_w = 1;
            crop_x = (src_w - crop_w) / 2;
            crop_y = 0;
        } else {
            crop_w = src_w;
            crop_h = (src_w * CAMERA_HEIGHT) / CAMERA_WIDTH;
            if (crop_h < 1) crop_h = 1;
            crop_x = 0;
            crop_y = (src_h - crop_h) / 2;
        }
    }

    float scale_x = (float)crop_w / (float)CAMERA_WIDTH;
    float scale_y = (float)crop_h / (float)CAMERA_HEIGHT;
    float sticker_scale = (scale_x + scale_y) * 0.5f;

    for (int i = 0; i < STICKER_MAX; i++) {
        dst[i] = src[i];
        if (!src[i].active) continue;
        dst[i].x = crop_x + round_to_int((float)src[i].x * scale_x);
        dst[i].y = crop_y + round_to_int((float)src[i].y * scale_y);
        dst[i].scale = src[i].scale * sticker_scale;
    }
}

// ---------------------------------------------------------------------------
// edit_save — save edited photo and refresh gallery
// ---------------------------------------------------------------------------

void edit_save(EditState *edit, GalleryState *gal,
               bool overwrite) {
    if (gal->count <= 0) return;

    static const char *s_frame_paths_save[FRAME_COUNT] = FRAME_PATHS_INIT;
    char out_path[80];
    const char *src_path = gal->paths[gal->sel];
    const char *src_ext = strrchr(src_path, '.');

    PlacedSticker remapped[STICKER_MAX];
    EditCompositeCtx ctx = {
        remapped, STICKER_MAX,
        edit->gallery_frame,
        (edit->gallery_frame >= 0 && edit->gallery_frame < FRAME_COUNT)
            ? s_frame_paths_save[edit->gallery_frame] : NULL
    };
    bool saved = false;

    if (gal->n_frames > 1) {
        // Wiggle: reload native-size frames, map preview-space edits back onto
        // the full overlap image, then save in the original animation format.
        uint16_t *native_frames[GALLERY_WIGGLE_MAX_FRAMES] = {0};
        const uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
        int src_w = 0, src_h = 0;
        int n_frames = 0, delay_ms = 250;

        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++) {
            native_frames[i] = malloc(VGA_WIDTH * VGA_HEIGHT * sizeof(uint16_t));
            if (!native_frames[i]) goto cleanup_wiggle;
            fptrs[i] = native_frames[i];
        }

        if (!load_animation_rgb565_native(src_path, native_frames,
                                          GALLERY_WIGGLE_MAX_FRAMES,
                                          &n_frames, &delay_ms,
                                          &src_w, &src_h)) {
            goto cleanup_wiggle;
        }

        remap_stickers_for_save(remapped, edit->placed, src_w, src_h, true);

        if (overwrite) {
            snprintf(out_path, sizeof(out_path), "%s", src_path);
        } else {
            if (!next_wiggle_path_ext(SAVE_DIR, src_ext, out_path, sizeof(out_path)))
                goto cleanup_wiggle;
            settings_save_file_counter(file_counter_next());
        }
        saved = save_edited_apng(out_path, fptrs,
                                 n_frames, delay_ms,
                                 src_w, src_h,
                                 edit_composite_cb, &ctx);
cleanup_wiggle:
        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++)
            free(native_frames[i]);
    } else {
        // Still image: reload native-size source, map preview-space edits back
        // onto it, then save at the original resolution.
        uint8_t *save_rgb888 = NULL;
        int src_w = 0, src_h = 0;
        if (!load_image_rgb888_native(src_path, &save_rgb888, &src_w, &src_h))
            return;

        remap_stickers_for_save(remapped, edit->placed, src_w, src_h, false);
        edit_composite_cb(save_rgb888, src_w, src_h, &ctx);

        if (overwrite) {
            snprintf(out_path, sizeof(out_path), "%s", src_path);
        } else {
            if (!next_save_path(SAVE_DIR, out_path, sizeof(out_path))) {
                free_loaded_image(save_rgb888);
                return;
            }
            settings_save_file_counter(file_counter_next());
        }
        saved = save_jpeg(out_path, save_rgb888, src_w, src_h);
        free_loaded_image(save_rgb888);
    }

    if (!saved) return;

    // Refresh gallery list and exit edit mode
    gal->count  = list_saved_photos(SAVE_DIR, gal->paths, GALLERY_MAX);
    gal->loaded = -1;
    edit->active = false;
    edit->placing = false;
    for (int i = 0; i < STICKER_MAX; i++) edit->placed[i].active = false;
    edit->gallery_frame = -1;
    edit->save_flash = 60;
}

// ---------------------------------------------------------------------------
// edit_handle_input — physical button input for sticker placement / picker
// ---------------------------------------------------------------------------

void edit_handle_input(EditState *edit, u32 kDown, u32 kHeld) {
    if (edit->tab != 0) return;

    if (edit->placing) {
        // ---- Placement mode: sticker is "picked up" ----
        // Circle pad moves cursor with deadzone
        circlePosition cp;
        hidCircleRead(&cp);
        #define CP_DEADZONE 12
        float dx = (cp.dx > CP_DEADZONE) ? (float)(cp.dx - CP_DEADZONE) :
                   (cp.dx < -CP_DEADZONE) ? (float)(cp.dx + CP_DEADZONE) : 0.0f;
        float dy = (cp.dy > CP_DEADZONE) ? (float)(cp.dy - CP_DEADZONE) :
                   (cp.dy < -CP_DEADZONE) ? (float)(cp.dy + CP_DEADZONE) : 0.0f;
        edit->cursor_x += dx * 3.0f / 144.0f;
        edit->cursor_y -= dy * 3.0f / 144.0f;
        if (edit->cursor_x < 0) edit->cursor_x = 0;
        if (edit->cursor_x >= CAMERA_WIDTH)  edit->cursor_x = (float)(CAMERA_WIDTH  - 1);
        if (edit->cursor_y < 0) edit->cursor_y = 0;
        if (edit->cursor_y >= CAMERA_HEIGHT) edit->cursor_y = (float)(CAMERA_HEIGHT - 1);
        #undef CP_DEADZONE

        // L / R (held) — scale smaller / larger
        if (kHeld & KEY_L) {
            edit->pending_scale -= 0.03f;
            if (edit->pending_scale < 0.5f) edit->pending_scale = 0.5f;
        }
        if (kHeld & KEY_R) {
            edit->pending_scale += 0.03f;
            if (edit->pending_scale > 8.0f) edit->pending_scale = 8.0f;
        }

        // D-pad L/R — rotate by 15 degrees
        if (kDown & KEY_DLEFT)  { edit->pending_angle -= 15.0f; if (edit->pending_angle <   0.0f) edit->pending_angle += 360.0f; }
        if (kDown & KEY_DRIGHT) { edit->pending_angle += 15.0f; if (edit->pending_angle >= 360.0f) edit->pending_angle -= 360.0f; }

        // A — confirm: place sticker centered on cursor
        if (kDown & KEY_A) {
            for (int si = 0; si < STICKER_MAX; si++) {
                if (!edit->placed[si].active) {
                    edit->placed[si].active    = true;
                    edit->placed[si].cat_idx   = edit->sticker_cat;
                    edit->placed[si].icon_idx  = edit->sticker_sel;
                    edit->placed[si].x         = (int)edit->cursor_x;
                    edit->placed[si].y         = (int)edit->cursor_y;
                    edit->placed[si].scale     = edit->pending_scale;
                    edit->placed[si].angle_deg = edit->pending_angle;
                    break;
                }
            }
            edit->placing = false;
        }
        // B — cancel placement (no sticker placed)
        if (kDown & KEY_B) {
            edit->placing = false;
        }
    } else {
        // ---- Picker mode: browse stickers ----
        sticker_cat_load(edit->sticker_cat);
        int total_icons = sticker_cats[edit->sticker_cat].count;
        int total_rows  = (total_icons + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
        int max_scroll  = total_rows - GEDIT_STICKER_ROWS;
        if (max_scroll < 0) max_scroll = 0;
        if (kDown & KEY_DUP)   { if (edit->sticker_scroll > 0)         edit->sticker_scroll--; }
        if (kDown & KEY_DDOWN) { if (edit->sticker_scroll < max_scroll) edit->sticker_scroll++; }

        // A — pick up selected sticker, reset cursor to centre
        if (kDown & KEY_A) {
            edit->cursor_x = (float)CAMERA_WIDTH  / 2.0f;
            edit->cursor_y = (float)CAMERA_HEIGHT / 2.0f;
            edit->placing  = true;
        }
    }
}

// ---------------------------------------------------------------------------
// edit_render_top — composited edit preview on top screen
// ---------------------------------------------------------------------------

void edit_render_top(const EditState *edit, const GalleryState *gal,
                     uint8_t *rgb888_buf) {
    static const char *s_frame_paths[FRAME_COUNT] = FRAME_PATHS_INIT;

    // Base photo
    rgb565_to_rgb888(rgb888_buf,
                     (const uint16_t *)gallery_thumbs[gal->anim_frame],
                     CAMERA_WIDTH * CAMERA_HEIGHT);
    // Stickers
    for (int si = 0; si < STICKER_MAX; si++) {
        if (!edit->placed[si].active) continue;
        const unsigned char *px = get_sticker_pixels(edit->placed[si].cat_idx,
                                                      edit->placed[si].icon_idx);
        if (px)
            composite_sticker_rgb888(rgb888_buf,
                                     CAMERA_WIDTH, CAMERA_HEIGHT,
                                     px,
                                     edit->placed[si].x,
                                     edit->placed[si].y,
                                     edit->placed[si].scale,
                                     edit->placed[si].angle_deg);
    }
    // Frame overlay
    if (edit->gallery_frame >= 0 && edit->gallery_frame < FRAME_COUNT)
        composite_frame_rgb888(rgb888_buf,
                               CAMERA_WIDTH, CAMERA_HEIGHT,
                               s_frame_paths[edit->gallery_frame]);
    // Cursor crosshair: visible when placing
    if (edit->placing && edit->tab == 0) {
        int cx = (int)edit->cursor_x;
        int cy = (int)edit->cursor_y;
        for (int d = -12; d <= 12; d++) {
            int px2 = cx + d, py2 = cy;
            if (px2 >= 0 && px2 < CAMERA_WIDTH && py2 >= 0 && py2 < CAMERA_HEIGHT) {
                uint8_t *p = rgb888_buf + (py2 * CAMERA_WIDTH + px2) * 3;
                p[0] = 255; p[1] = 255; p[2] = 0;
            }
            px2 = cx; py2 = cy + d;
            if (px2 >= 0 && px2 < CAMERA_WIDTH && py2 >= 0 && py2 < CAMERA_HEIGHT) {
                uint8_t *p = rgb888_buf + (py2 * CAMERA_WIDTH + px2) * 3;
                p[0] = 255; p[1] = 255; p[2] = 0;
            }
        }
        // Preview selected sticker at cursor
        const unsigned char *cpx = get_sticker_pixels(edit->sticker_cat, edit->sticker_sel);
        if (cpx) {
            composite_sticker_rgb888(rgb888_buf,
                                     CAMERA_WIDTH, CAMERA_HEIGHT,
                                     cpx, cx, cy,
                                     edit->pending_scale, edit->pending_angle);
        }
    }
    // Convert and blit
    static uint16_t edit_preview_rgb565[CAMERA_WIDTH * CAMERA_HEIGHT];
    rgb888_to_rgb565(edit_preview_rgb565, rgb888_buf,
                     CAMERA_WIDTH * CAMERA_HEIGHT);
    writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
                                    edit_preview_rgb565, 0, 0,
                                    CAMERA_WIDTH, CAMERA_HEIGHT);
}
