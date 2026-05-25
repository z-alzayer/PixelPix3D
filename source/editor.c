#include "editor.h"
#include "camera.h"
#include "gallery.h"
#include "sticker.h"
#include "image_load.h"
#include "settings.h"
#include "ui.h"
#include "pipeline.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint16_t s_edit_wiggle_left[CAMERA_WIDTH * CAMERA_HEIGHT];
static uint16_t s_edit_wiggle_right[CAMERA_WIDTH * CAMERA_HEIGHT];
static uint16_t s_edit_wiggle_preview[WIGGLE_PREVIEW_MAX][CAMERA_WIDTH * CAMERA_HEIGHT];

static int edit_wiggle_strip_count_from_sequence(int sequence_frames) {
    int strip = (sequence_frames + 2) / 2;
    if (strip < 2) strip = 2;
    if (strip > WIGGLE_FRAME_MAX) strip = WIGGLE_FRAME_MAX;
    return strip;
}

static int edit_wiggle_endpoint_index(int sequence_frames) {
    if (sequence_frames <= 1) return 0;
    int idx = edit_wiggle_strip_count_from_sequence(sequence_frames) - 1;
    if (idx >= sequence_frames) idx = sequence_frames - 1;
    return idx;
}

static int edit_clamp_wiggle_delay_ms(int delay_ms) {
    if (delay_ms < 50) return 50;
    if (delay_ms > 1000) return 1000;
    return (delay_ms + 5) / 10 * 10;
}

static int edit_wiggle_delay_from_slider_x(int tx, float track_x, float track_w) {
    static const int anchors[] = {50, 100, 250, 500, 750, 1000};
    int count = (int)(sizeof(anchors) / sizeof(anchors[0]));
    float t = ((float)tx - track_x) / track_w;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float pos = t * (float)(count - 1);
    int seg = (int)pos;
    if (seg >= count - 1)
        return anchors[count - 1];
    float local = pos - (float)seg;
    int a = anchors[seg];
    int b = anchors[seg + 1];
    return edit_clamp_wiggle_delay_ms(a + (int)((float)(b - a) * local + 0.5f));
}

static void edit_reset_wiggle_phase(EditState *edit) {
    edit->wiggle_preview_frame = 0;
    edit->wiggle_preview_tick = svcGetSystemTick();
}

static void edit_rebuild_wiggle_preview(EditState *edit) {
    if (!edit->wiggle_source) return;
    edit->wiggle_preview_count =
        build_wiggle_preview_frames(s_edit_wiggle_preview,
                                    (const uint8_t *)s_edit_wiggle_left,
                                    (const uint8_t *)s_edit_wiggle_right,
                                    CAMERA_WIDTH, CAMERA_HEIGHT,
                                    edit->wiggle_n_frames,
                                    edit->wiggle_offset_dx,
                                    edit->wiggle_offset_dy,
                                    NULL, NULL);
    if (edit->wiggle_preview_count < 1) edit->wiggle_preview_count = 1;
    if (edit->wiggle_preview_frame >= edit->wiggle_preview_count)
        edit->wiggle_preview_frame = 0;
}

// ---------------------------------------------------------------------------
// edit_enter_or_place — enter edit mode or pick up sticker
// ---------------------------------------------------------------------------

static void edit_reset_overlays(EditState *edit) {
    edit->placing = false;
    for (int i = 0; i < STICKER_MAX; i++) edit->placed[i].active = false;
    edit->gallery_frame = -1;
}

void edit_enter_or_place(EditState *edit, const GalleryState *gal,
                         const EffectPipeline *live_pipeline) {
    if (!edit->active) {
        // Reset cursor to centre when entering edit mode
        edit->wiggle_source = false;
        edit->tab           = GEDIT_TAB_LOOKS;
        edit->fx_stage      = LOOKS_STAGE_LOOK;
        edit->fx_stage_open = false;
        edit->fx_tune_open  = false;
        edit->cursor_x      = (float)CAMERA_WIDTH  / 2.0f;
        edit->cursor_y      = (float)CAMERA_HEIGHT / 2.0f;
        edit->pending_scale = 2.0f;
        edit->pending_angle = 0.0f;
        edit->sticker_cat   = 0;
        edit->sticker_sel   = 0;
        edit->sticker_scroll = 0;
        if (live_pipeline) {
            edit->pipeline = *live_pipeline;
        } else {
            FilterParams defaults = FILTER_DEFAULTS;
            pipeline_state_init(&edit->pipeline, &defaults);
        }
        edit_reset_overlays(edit);
        sticker_cat_load(0);
        int gallery_frames = gal ? gal->n_frames : 1;
        if (gallery_frames > 1) {
            int endpoint = edit_wiggle_endpoint_index(gallery_frames);
            memcpy(s_edit_wiggle_left, gallery_thumbs[0], sizeof(s_edit_wiggle_left));
            memcpy(s_edit_wiggle_right, gallery_thumbs[endpoint], sizeof(s_edit_wiggle_right));
            edit->wiggle_source = true;
            edit->wiggle_n_frames = edit_wiggle_strip_count_from_sequence(gallery_frames);
            edit->wiggle_delay_ms = edit_clamp_wiggle_delay_ms(gal ? gal->delay_ms : WIGGLE_DEFAULT_DELAY_MS);
            edit->wiggle_offset_dx = 0;
            edit->wiggle_offset_dy = 0;
            edit->wiggle_manual_align = false;
            edit->wiggle_align_dragging = false;
            edit->wiggle_align_changed = false;
            edit_reset_wiggle_phase(edit);
            edit_rebuild_wiggle_preview(edit);
        }
        edit->active = true;
    } else if (edit->tab == GEDIT_TAB_STICKERS) {
        // Info area tap = pick up sticker (enter placement mode), reset cursor to centre
        edit->cursor_x = (float)CAMERA_WIDTH  / 2.0f;
        edit->cursor_y = (float)CAMERA_HEIGHT / 2.0f;
        edit->placing  = true;
    }
}

const u8 *edit_wiggle_left_pixels(void) {
    return (const u8 *)s_edit_wiggle_left;
}

const u8 *edit_wiggle_right_pixels(void) {
    return (const u8 *)s_edit_wiggle_right;
}

// ---------------------------------------------------------------------------
// edit_cancel — discard all edits and exit edit mode
// ---------------------------------------------------------------------------

void edit_cancel(EditState *edit, GalleryState *gal) {
    edit->active = false;
    edit->wiggle_source = false;
    edit->fx_stage_open = false;
    edit->fx_tune_open = false;
    edit_reset_overlays(edit);
    if (gal) gal->loaded = -1;
}

// ---------------------------------------------------------------------------
// Composite callback for save_edited_apng
// ---------------------------------------------------------------------------

typedef struct {
    PlacedSticker *stickers;
    int            n_stickers;
    int            overlay_frame_idx;
    const char    *frame_path;
    EffectRecipe   recipe;
    bool           apply_pipeline;
    int            pipeline_frame;
} EditCompositeCtx;

typedef enum {
    EDIT_SOURCE_STILL,
    EDIT_SOURCE_WIGGLE
} EditSourceKind;

static void edit_composite_cb(uint8_t *rgb888, int w, int h, void *ud) {
    EditCompositeCtx *ctx = (EditCompositeCtx *)ud;
    if (ctx->apply_pipeline) {
        pipeline_apply(rgb888, w, h, &ctx->recipe, ctx->pipeline_frame);
        ctx->pipeline_frame++;
    }
    for (int si = 0; si < ctx->n_stickers; si++) {
        if (!ctx->stickers[si].active) continue;
        const unsigned char *px = get_sticker_pixels(ctx->stickers[si].cat_idx,
                                                      ctx->stickers[si].icon_idx);
        if (px)
            composite_sticker_rgb888(rgb888, w, h, px,
                                     ctx->stickers[si].x, ctx->stickers[si].y,
                                     ctx->stickers[si].scale, ctx->stickers[si].angle_deg);
    }
    if (ctx->overlay_frame_idx >= 0 && ctx->frame_path)
        composite_frame_rgb888(rgb888, w, h, ctx->frame_path);
}

static int round_to_int(float v) {
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static void fit_rect_for_aspect(int src_w, int src_h,
                                int dst_w, int dst_h,
                                int *out_x, int *out_y,
                                int *out_w, int *out_h) {
    int draw_w = dst_w;
    int draw_h = dst_h;
    if (src_w > 0 && src_h > 0) {
        if ((long long)src_w * dst_h > (long long)src_h * dst_w) {
            draw_h = (src_h * dst_w) / src_w;
            if (draw_h < 1) draw_h = 1;
        } else {
            draw_w = (src_w * dst_h) / src_h;
            if (draw_w < 1) draw_w = 1;
        }
    }
    if (out_x) *out_x = (dst_w - draw_w) / 2;
    if (out_y) *out_y = (dst_h - draw_h) / 2;
    if (out_w) *out_w = draw_w;
    if (out_h) *out_h = draw_h;
}

static bool ext_is_ci(const char *ext, const char *want) {
    if (!ext || !want) return false;
    while (*ext && *want) {
        char a = *ext++;
        char b = *want++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *ext == 0 && *want == 0;
}

static void remap_stickers_for_save(PlacedSticker *dst,
                                    const PlacedSticker *src,
                                    int src_w, int src_h,
                                    EditSourceKind source_kind) {
    int crop_x = 0, crop_y = 0, crop_w = src_w, crop_h = src_h;
    int display_x = 0, display_y = 0;
    int display_w = CAMERA_WIDTH, display_h = CAMERA_HEIGHT;

    if (source_kind == EDIT_SOURCE_WIGGLE || src_h <= src_w) {
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
    } else {
        if ((long long)crop_w * 4 > (long long)crop_h * 3) {
            crop_w = (crop_h * 3) / 4;
            if (crop_w < 1) crop_w = 1;
            crop_x = (src_w - crop_w) / 2;
        } else {
            crop_h = (crop_w * 4) / 3;
            if (crop_h < 1) crop_h = 1;
            crop_y = (src_h - crop_h) / 2;
        }

        display_h = CAMERA_HEIGHT;
        display_w = (display_h * 3) / 4;
        if (display_w < 1) display_w = 1;
        display_x = (CAMERA_WIDTH - display_w) / 2;
        display_y = (CAMERA_HEIGHT - display_h) / 2;
    }

    float scale_x = (float)crop_w / (float)display_w;
    float scale_y = (float)crop_h / (float)display_h;
    float sticker_scale = (scale_x + scale_y) * 0.5f;

    for (int i = 0; i < STICKER_MAX; i++) {
        dst[i] = src[i];
        if (!src[i].active) continue;
        dst[i].x = crop_x + round_to_int((float)(src[i].x - display_x) * scale_x);
        dst[i].y = crop_y + round_to_int((float)(src[i].y - display_y) * scale_y);
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
    bool src_is_png = ext_is_ci(src_ext, ".png");
    EffectRecipe recipe;
    pipeline_build_recipe(&recipe, &edit->pipeline);

    PlacedSticker remapped[STICKER_MAX];
    EditCompositeCtx ctx = {
        remapped, STICKER_MAX,
        edit->gallery_frame,
        (edit->gallery_frame >= 0 && edit->gallery_frame < FRAME_COUNT)
            ? s_frame_paths_save[edit->gallery_frame] : NULL,
        recipe,
        pipeline_recipe_has_effects(&recipe),
        0
    };
    bool saved = false;

    if (gal->n_frames > 1) {
        // Wiggle: reload native-size frames, take the endpoint pair, regenerate
        // the loop from the current alignment/timing, then run the edit stack.
        uint16_t *native_frames[GALLERY_WIGGLE_MAX_FRAMES] = {0};
        uint16_t *generated_frames[WIGGLE_PREVIEW_MAX] = {0};
        const uint16_t *fptrs[WIGGLE_PREVIEW_MAX];
        int src_w = 0, src_h = 0;
        int n_frames = 0, delay_ms_unused = WIGGLE_DEFAULT_DELAY_MS;
        int out_w = 0, out_h = 0;
        int out_frames = 0;

        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++) {
            native_frames[i] = malloc(VGA_WIDTH * VGA_HEIGHT * sizeof(uint16_t));
            if (!native_frames[i]) goto cleanup_wiggle;
        }
        for (int i = 0; i < WIGGLE_PREVIEW_MAX; i++) {
            generated_frames[i] = malloc(VGA_WIDTH * VGA_HEIGHT * sizeof(uint16_t));
            if (!generated_frames[i]) goto cleanup_wiggle;
            fptrs[i] = generated_frames[i];
        }

        if (!load_animation_rgb565_native(src_path, native_frames,
                                          GALLERY_WIGGLE_MAX_FRAMES,
                                          &n_frames, &delay_ms_unused,
                                          &src_w, &src_h)) {
            goto cleanup_wiggle;
        }

        int endpoint = edit_wiggle_endpoint_index(n_frames);
        int save_dx = edit->wiggle_offset_dx;
        int save_dy = edit->wiggle_offset_dy;
        if (src_w != CAMERA_WIDTH)
            save_dx = round_to_int((float)save_dx * (float)src_w / (float)CAMERA_WIDTH);
        if (src_h != CAMERA_HEIGHT)
            save_dy = round_to_int((float)save_dy * (float)src_h / (float)CAMERA_HEIGHT);

        out_frames = build_wiggle_native_frames(generated_frames,
                                                (const uint8_t *)native_frames[0],
                                                (const uint8_t *)native_frames[endpoint],
                                                src_w, src_h,
                                                edit->wiggle_n_frames,
                                                save_dx, save_dy,
                                                &out_w, &out_h);
        if (out_frames < 1 || out_w < 1 || out_h < 1)
            goto cleanup_wiggle;

        remap_stickers_for_save(remapped, edit->placed, out_w, out_h,
                                EDIT_SOURCE_WIGGLE);

        if (overwrite) {
            snprintf(out_path, sizeof(out_path), "%s", src_path);
        } else {
            if (!next_wiggle_path_ext(SAVE_DIR, src_ext, out_path, sizeof(out_path)))
                goto cleanup_wiggle;
            settings_save_file_counter(file_counter_next());
        }
        saved = save_edited_apng(out_path, fptrs,
                                 out_frames,
                                 edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms),
                                 out_w, out_h,
                                 edit_composite_cb, &ctx);
cleanup_wiggle:
        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++)
            free(native_frames[i]);
        for (int i = 0; i < WIGGLE_PREVIEW_MAX; i++)
            free(generated_frames[i]);
    } else {
        // Still image: reload native-size source, map preview-space edits back
        // onto it, then save at the original resolution.
        uint8_t *save_rgb888 = NULL;
        int src_w = 0, src_h = 0;
        if (!load_image_rgb888_native(src_path, &save_rgb888, &src_w, &src_h))
            return;

        remap_stickers_for_save(remapped, edit->placed, src_w, src_h,
                                EDIT_SOURCE_STILL);
        edit_composite_cb(save_rgb888, src_w, src_h, &ctx);

        if (overwrite) {
            snprintf(out_path, sizeof(out_path), "%s", src_path);
        } else {
            bool got_path = src_is_png
                          ? next_anaglyph_path(SAVE_DIR, out_path, sizeof(out_path))
                          : next_save_path(SAVE_DIR, out_path, sizeof(out_path));
            if (!got_path) {
                free_loaded_image(save_rgb888);
                return;
            }
            settings_save_file_counter(file_counter_next());
        }
        saved = src_is_png
              ? save_png(out_path, save_rgb888, src_w, src_h)
              : save_jpeg(out_path, save_rgb888, src_w, src_h);
        free_loaded_image(save_rgb888);
    }

    if (!saved) return;

    // Refresh gallery list and exit edit mode
    gal->count  = list_saved_photos(SAVE_DIR, gal->paths, GALLERY_MAX);
    gal->loaded = -1;
    edit->active = false;
    edit->wiggle_source = false;
    edit_reset_overlays(edit);
    edit->save_flash = 60;
}

static void edit_set_stage_strength(EditState *edit, int delta) {
    int *value = NULL;
    int min_v = 0, max_v = 10;

    switch (edit->fx_stage) {
    case LOOKS_STAGE_LOOK:
        value = &edit->pipeline.base.strength;
        edit->pipeline.base.enabled = true;
        break;
    case LOOKS_STAGE_STYLE:
        value = &edit->pipeline.remap.strength;
        edit->pipeline.remap.enabled = true;
        if (edit->pipeline.remap.style == REMAP_STYLE_TOON) {
            min_v = 2;
            max_v = 15;
        }
        break;
    case LOOKS_STAGE_BEND:
        value = &edit->pipeline.bend.strength;
        edit->pipeline.bend.enabled = true;
        break;
    case LOOKS_STAGE_FX:
        value = &edit->pipeline.post.fx_intensity;
        edit->pipeline.post.enabled = true;
        if (edit->pipeline.post.fx_mode == FX_NONE) edit->pipeline.post.fx_mode = FX_SCAN_H;
        break;
    default:
        return;
    }

    *value += delta;
    if (*value < min_v) *value = min_v;
    if (*value > max_v) *value = max_v;
}

static void edit_toggle_current_stage(EditState *edit) {
    switch (edit->fx_stage) {
    case LOOKS_STAGE_LOOK:
        edit->pipeline.base.enabled = !edit->pipeline.base.enabled;
        break;
    case LOOKS_STAGE_GB:
        edit->pipeline.gb.enabled = !edit->pipeline.gb.enabled;
        break;
    case LOOKS_STAGE_STYLE:
        edit->pipeline.remap.enabled = !edit->pipeline.remap.enabled;
        break;
    case LOOKS_STAGE_BEND:
        edit->pipeline.bend.enabled = !edit->pipeline.bend.enabled;
        break;
    case LOOKS_STAGE_FX:
        edit->pipeline.post.enabled = !edit->pipeline.post.enabled;
        break;
    }
}

static void edit_wiggle_changed(EditState *edit) {
    edit_rebuild_wiggle_preview(edit);
    edit_reset_wiggle_phase(edit);
}

static void edit_wiggle_adjust(EditState *edit, int *value, int delta, int lo, int hi) {
    int next = *value + delta;
    if (next < lo) next = lo;
    if (next > hi) next = hi;
    if (next == *value) return;
    *value = next;
    edit_wiggle_changed(edit);
}

// ---------------------------------------------------------------------------
// edit_handle_input — physical button input for sticker placement / picker
// ---------------------------------------------------------------------------

void edit_handle_input(EditState *edit, u32 kDown, u32 kHeld) {
    if (edit->tab == GEDIT_TAB_LOOKS) {
        if (!edit->fx_stage_open) {
            if (kDown & KEY_DLEFT)  { if (edit->fx_stage > 0) edit->fx_stage--; }
            if (kDown & KEY_DRIGHT) { if (edit->fx_stage < LOOKS_STAGE_COUNT - 1) edit->fx_stage++; }
            if (kDown & KEY_A) edit->fx_stage_open = true;
            return;
        }
        if (kDown & KEY_B) {
            edit->fx_stage_open = false;
            edit->fx_tune_open = false;
            return;
        }
        if (kDown & KEY_A) edit_toggle_current_stage(edit);
        if (kDown & KEY_DUP) edit_set_stage_strength(edit, 1);
        if (kDown & KEY_DDOWN) edit_set_stage_strength(edit, -1);
        return;
    }

    if (edit->tab == GEDIT_TAB_WIGGLE && edit->wiggle_source) {
        if (edit->wiggle_manual_align) {
            bool changed = false;
            if (kDown & KEY_DLEFT)  { edit->wiggle_offset_dx--; changed = true; }
            if (kDown & KEY_DRIGHT) { edit->wiggle_offset_dx++; changed = true; }
            if (kDown & KEY_DUP)    { edit->wiggle_offset_dy++; changed = true; }
            if (kDown & KEY_DDOWN)  { edit->wiggle_offset_dy--; changed = true; }
            if (edit->wiggle_offset_dx < -WIGGLE_OFFSET_X_MAX) edit->wiggle_offset_dx = -WIGGLE_OFFSET_X_MAX;
            if (edit->wiggle_offset_dx >  WIGGLE_OFFSET_X_MAX) edit->wiggle_offset_dx =  WIGGLE_OFFSET_X_MAX;
            if (edit->wiggle_offset_dy < -WIGGLE_OFFSET_Y_MAX) edit->wiggle_offset_dy = -WIGGLE_OFFSET_Y_MAX;
            if (edit->wiggle_offset_dy >  WIGGLE_OFFSET_Y_MAX) edit->wiggle_offset_dy =  WIGGLE_OFFSET_Y_MAX;
            if (changed) {
                edit->wiggle_align_changed = false;
                edit_wiggle_changed(edit);
            }
            if (kDown & (KEY_A | KEY_B)) {
                if (edit->wiggle_align_changed)
                    edit_wiggle_changed(edit);
                edit->wiggle_manual_align = false;
                edit->wiggle_align_dragging = false;
                edit->wiggle_align_changed = false;
            }
            return;
        }

        if (kDown & KEY_DLEFT)
            edit_wiggle_adjust(edit, &edit->wiggle_offset_dx, -1,
                               -WIGGLE_OFFSET_X_MAX, WIGGLE_OFFSET_X_MAX);
        if (kDown & KEY_DRIGHT)
            edit_wiggle_adjust(edit, &edit->wiggle_offset_dx, 1,
                               -WIGGLE_OFFSET_X_MAX, WIGGLE_OFFSET_X_MAX);
        if (kDown & KEY_DUP)
            edit_wiggle_adjust(edit, &edit->wiggle_offset_dy, 1,
                               -WIGGLE_OFFSET_Y_MAX, WIGGLE_OFFSET_Y_MAX);
        if (kDown & KEY_DDOWN)
            edit_wiggle_adjust(edit, &edit->wiggle_offset_dy, -1,
                               -WIGGLE_OFFSET_Y_MAX, WIGGLE_OFFSET_Y_MAX);
        if (kDown & KEY_A) {
            edit->wiggle_manual_align = true;
            edit->wiggle_align_dragging = false;
            edit->wiggle_align_changed = false;
        }
        if (kDown & KEY_L) {
            int next = edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms - 10);
            if (next != edit->wiggle_delay_ms) {
                edit->wiggle_delay_ms = next;
                edit_reset_wiggle_phase(edit);
            }
        }
        if (kDown & KEY_R) {
            int next = edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms + 10);
            if (next != edit->wiggle_delay_ms) {
                edit->wiggle_delay_ms = next;
                edit_reset_wiggle_phase(edit);
            }
        }
        return;
    }

    if (edit->tab != GEDIT_TAB_STICKERS) return;

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

bool edit_handle_wiggle_touch(EditState *edit, int tx, int ty,
                              bool tapped, bool touched) {
    if (!edit->wiggle_source)
        return false;

    #define ALIGN_BOX_X WIGGLE_ALIGN_BOX_X
    #define ALIGN_BOX_Y WIGGLE_ALIGN_BOX_Y
    #define ALIGN_BOX_W (WIGGLE_ALIGN_THUMB_W * 2)
    #define ALIGN_BOX_H (WIGGLE_ALIGN_THUMB_H * 2)

    if (edit->wiggle_manual_align) {
        if (tapped && tx >= ALIGN_BOX_X && tx < ALIGN_BOX_X + ALIGN_BOX_W &&
            ty >= ALIGN_BOX_Y && ty < ALIGN_BOX_Y + ALIGN_BOX_H) {
            edit->wiggle_align_dragging = true;
            edit->wiggle_align_touch_start_x = tx;
            edit->wiggle_align_touch_start_y = ty;
            edit->wiggle_align_start_dx = edit->wiggle_offset_dx;
            edit->wiggle_align_start_dy = edit->wiggle_offset_dy;
            return true;
        }
        if (touched && edit->wiggle_align_dragging) {
            int dx = ((tx - edit->wiggle_align_touch_start_x) * CAMERA_WIDTH) /
                     (ALIGN_BOX_W * WIGGLE_ALIGN_DRAG_DAMPING);
            int dy = ((ty - edit->wiggle_align_touch_start_y) * CAMERA_HEIGHT) /
                     (ALIGN_BOX_H * WIGGLE_ALIGN_DRAG_DAMPING);
            int new_dx = edit->wiggle_align_start_dx + dx;
            int new_dy = edit->wiggle_align_start_dy + dy;
            if (new_dx < -WIGGLE_OFFSET_X_MAX) new_dx = -WIGGLE_OFFSET_X_MAX;
            if (new_dx >  WIGGLE_OFFSET_X_MAX) new_dx =  WIGGLE_OFFSET_X_MAX;
            if (new_dy < -WIGGLE_OFFSET_Y_MAX) new_dy = -WIGGLE_OFFSET_Y_MAX;
            if (new_dy >  WIGGLE_OFFSET_Y_MAX) new_dy =  WIGGLE_OFFSET_Y_MAX;
            if (new_dx != edit->wiggle_offset_dx || new_dy != edit->wiggle_offset_dy) {
                edit->wiggle_offset_dx = new_dx;
                edit->wiggle_offset_dy = new_dy;
                edit->wiggle_align_changed = true;
                edit_wiggle_changed(edit);
            }
            return true;
        }
        if (!touched && edit->wiggle_align_dragging) {
            edit->wiggle_align_dragging = false;
            if (edit->wiggle_align_changed) {
                edit_wiggle_changed(edit);
                edit->wiggle_align_changed = false;
            }
            return true;
        }
        if (tapped && tx >= SHOOT_CANCEL_X &&
            tx < SHOOT_CANCEL_X + SHOOT_CANCEL_W &&
            ty >= SHOOT_CLEAR_Y &&
            ty < SHOOT_CLEAR_Y + SHOOT_CLEAR_H) {
            edit->wiggle_manual_align = false;
            edit->wiggle_align_dragging = false;
            edit->wiggle_align_changed = false;
            return true;
        }
        if (tapped && tx >= SHOOT_PREVIEW_PRIMARY_X &&
            tx < SHOOT_PREVIEW_PRIMARY_X + SHOOT_PREVIEW_PRIMARY_W &&
            ty >= SHOOT_SAVE_Y &&
            ty < SHOOT_SAVE_Y + SHOOT_SAVE_H) {
            if (edit->wiggle_align_changed)
                edit_wiggle_changed(edit);
            edit->wiggle_manual_align = false;
            edit->wiggle_align_dragging = false;
            edit->wiggle_align_changed = false;
            return true;
        }
        return true;
    }

    if (!tapped && !touched)
        return false;

    #define WIG_BTN_W   28
    #define WIG_BTN_H   22
    #define WIG_VAL_W   42
    #define WIG_MINUS_X 18
    #define WIG_VAL_X   (WIG_MINUS_X + WIG_BTN_W + 2)
    #define WIG_PLUS_X  (WIG_VAL_X + WIG_VAL_W + 2)
    #define WIG_RST_X   (WIG_PLUS_X + WIG_BTN_W + 2)
    int row_x_y = SHOOT_CONTENT_Y + 4;
    int row_y_y = SHOOT_CONTENT_Y + 32;
    int row_frames_y = SHOOT_CONTENT_Y + 66;
    int *val = NULL;
    int lo = 0, hi = 0;
    if (tapped && tx < 158 && ty >= row_x_y && ty < row_x_y + WIG_BTN_H) {
        val = &edit->wiggle_offset_dx;
        lo = -WIGGLE_OFFSET_X_MAX;
        hi = WIGGLE_OFFSET_X_MAX;
    } else if (tapped && tx < 158 && ty >= row_y_y && ty < row_y_y + WIG_BTN_H) {
        val = &edit->wiggle_offset_dy;
        lo = -WIGGLE_OFFSET_Y_MAX;
        hi = WIGGLE_OFFSET_Y_MAX;
    } else if (tapped && tx < 158 && ty >= row_frames_y && ty < row_frames_y + WIG_BTN_H) {
        val = &edit->wiggle_n_frames;
        lo = 2;
        hi = WIGGLE_FRAME_MAX;
    }
    if (val) {
        if (tx >= WIG_MINUS_X && tx < WIG_MINUS_X + WIG_BTN_W)
            edit_wiggle_adjust(edit, val, -1, lo, hi);
        else if (tx >= WIG_PLUS_X && tx < WIG_PLUS_X + WIG_BTN_W)
            edit_wiggle_adjust(edit, val, 1, lo, hi);
        else if (val != &edit->wiggle_n_frames &&
                 tx >= WIG_RST_X && tx < WIG_RST_X + 22) {
            if (*val != 0) {
                *val = 0;
                edit_wiggle_changed(edit);
            }
        }
        return true;
    }

    if (tapped && tx >= SHOOT_MANUAL_ALIGN_X &&
        tx < SHOOT_MANUAL_ALIGN_X + SHOOT_MANUAL_ALIGN_W &&
        ty >= SHOOT_MANUAL_ALIGN_Y &&
        ty < SHOOT_MANUAL_ALIGN_Y + SHOOT_MANUAL_ALIGN_H) {
        edit->wiggle_manual_align = true;
        edit->wiggle_align_dragging = false;
        edit->wiggle_align_changed = false;
        return true;
    }

    if (tx >= 160) {
        if (tapped && tx >= 164 && tx < 186 &&
            ty >= SHOOT_CONTENT_Y + 24 && ty < SHOOT_CONTENT_Y + 46) {
            int next = edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms - 10);
            if (next != edit->wiggle_delay_ms) {
                edit->wiggle_delay_ms = next;
                edit_reset_wiggle_phase(edit);
            }
            return true;
        }
        if (tapped && tx >= 294 && tx < 316 &&
            ty >= SHOOT_CONTENT_Y + 24 && ty < SHOOT_CONTENT_Y + 46) {
            int next = edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms + 10);
            if (next != edit->wiggle_delay_ms) {
                edit->wiggle_delay_ms = next;
                edit_reset_wiggle_phase(edit);
            }
            return true;
        }
        if (ty >= SHOOT_CONTENT_Y + 20 && ty < SHOOT_CONTENT_Y + 50 &&
            tx >= 192 && tx < 288) {
            int next = edit_wiggle_delay_from_slider_x(tx, 192.0f, 96.0f);
            if (next != edit->wiggle_delay_ms) {
                edit->wiggle_delay_ms = next;
                edit_reset_wiggle_phase(edit);
            }
            return true;
        }
    }

    #undef WIG_BTN_W
    #undef WIG_BTN_H
    #undef WIG_VAL_W
    #undef WIG_MINUS_X
    #undef WIG_VAL_X
    #undef WIG_PLUS_X
    #undef WIG_RST_X
    #undef ALIGN_BOX_X
    #undef ALIGN_BOX_Y
    #undef ALIGN_BOX_W
    #undef ALIGN_BOX_H

    return false;
}

void edit_tick(EditState *edit) {
    if (!edit->active || !edit->wiggle_source || edit->wiggle_preview_count <= 1)
        return;
    u64 now = svcGetSystemTick();
    u64 period = (u64)edit_clamp_wiggle_delay_ms(edit->wiggle_delay_ms) *
                 SYSCLOCK_ARM11 / 1000;
    if (period == 0) period = 1;
    u64 elapsed = now - edit->wiggle_preview_tick;
    if (elapsed >= period) {
        u64 steps = elapsed / period;
        edit->wiggle_preview_frame =
            (edit->wiggle_preview_frame + (int)(steps % edit->wiggle_preview_count)) %
            edit->wiggle_preview_count;
        edit->wiggle_preview_tick += steps * period;
    }
}

// ---------------------------------------------------------------------------
// edit_render_top — composited edit preview on top screen
// ---------------------------------------------------------------------------

void edit_render_top(const EditState *edit, const GalleryState *gal,
                     uint8_t *rgb888_buf) {
    static const char *s_frame_paths[FRAME_COUNT] = FRAME_PATHS_INIT;
    EffectRecipe recipe;
    pipeline_build_recipe(&recipe, &edit->pipeline);

    int base_frame = gal->anim_frame;
    const uint16_t *base_pixels = (const uint16_t *)gallery_thumbs[base_frame];
    if (edit->wiggle_source && edit->wiggle_preview_count > 0) {
        base_frame = edit->wiggle_preview_frame;
        if (base_frame < 0 || base_frame >= edit->wiggle_preview_count)
            base_frame = 0;
        base_pixels = s_edit_wiggle_preview[base_frame];
    }

    // Base photo
    rgb565_to_rgb888(rgb888_buf,
                     base_pixels,
                     CAMERA_WIDTH * CAMERA_HEIGHT);
    if (pipeline_recipe_has_effects(&recipe))
        pipeline_apply(rgb888_buf, CAMERA_WIDTH, CAMERA_HEIGHT,
                       &recipe, base_frame);
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
    if (edit->gallery_frame >= 0 && edit->gallery_frame < FRAME_COUNT) {
        if (!edit->wiggle_source && gal && gal->src_w > 0 && gal->src_h > 0) {
            int fx = 0, fy = 0, fw = CAMERA_WIDTH, fh = CAMERA_HEIGHT;
            fit_rect_for_aspect(gal->src_w, gal->src_h,
                                CAMERA_WIDTH, CAMERA_HEIGHT,
                                &fx, &fy, &fw, &fh);
            composite_frame_rgb888_region(rgb888_buf,
                                          CAMERA_WIDTH, CAMERA_HEIGHT,
                                          s_frame_paths[edit->gallery_frame],
                                          fx, fy, fw, fh,
                                          gal->src_h > gal->src_w);
        } else {
            composite_frame_rgb888(rgb888_buf,
                                   CAMERA_WIDTH, CAMERA_HEIGHT,
                                   s_frame_paths[edit->gallery_frame]);
        }
    }
    // Cursor crosshair: visible when placing
    if (edit->placing && edit->tab == GEDIT_TAB_STICKERS) {
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
