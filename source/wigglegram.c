#include "wigglegram.h"
#include "camera.h"
#include "filter.h"
#include "gif_enc.h"
#include "app_state.h"
#include "shoot.h"
#include "ui.h"
#include "image_load.h"
#include "pipeline.h"
#include "settings.h"
#include "sound.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void reset_wiggle_preview_phase(WiggleState *wig) {
    if (!wig) return;
    wig->preview_frame = 0;
    wig->preview_last_tick = svcGetSystemTick();
}

// ---------------------------------------------------------------------------
// wiggle_align — find global translation via block-matching on downsampled luma
// ---------------------------------------------------------------------------

#define DS_W  80
#define DS_H  48
#define GLOBAL_SEARCH_X  20
#define GLOBAL_SEARCH_Y   6

static void downsample_luma(const uint16_t *src, int w, int h,
                            uint8_t *dst, int dw, int dh)
{
    int bw = w / dw, bh = h / dh, bdiv = bw * bh;
    for (int by = 0; by < dh; by++) {
        for (int bx = 0; bx < dw; bx++) {
            int sum = 0;
            for (int dy = 0; dy < bh; dy++) {
                const uint16_t *row = src + (by * bh + dy) * w + bx * bw;
                for (int dx = 0; dx < bw; dx++) {
                    uint16_t p = row[dx];
                    sum += ((p >> 11) & 0x1f) * 2
                         + ((p >>  5) & 0x3f) * 2
                         +  (p        & 0x1f);
                }
            }
            dst[by * dw + bx] = (uint8_t)(sum / bdiv);
        }
    }
}

static int sad_region(const uint8_t *a, const uint8_t *b,
                      int x0, int y0, int dx, int dy,
                      int rw, int rh, int stride)
{
    int s = 0;
    for (int ry = 0; ry < rh; ry++) {
        const uint8_t *ra = a + (y0 + ry) * stride + x0;
        const uint8_t *rb = b + (y0 + ry + dy) * stride + x0 + dx;
        for (int rx = 0; rx < rw; rx++) {
            int d = ra[rx] - rb[rx];
            s += d < 0 ? -d : d;
        }
    }
    return s;
}

void wiggle_align(WiggleAlign *align,
                  const uint8_t *left_rgb565,
                  const uint8_t *right_rgb565,
                  int w, int h)
{
    static uint8_t luma_l[DS_W * DS_H];
    static uint8_t luma_r[DS_W * DS_H];

    downsample_luma((const uint16_t *)left_rgb565,  w, h, luma_l, DS_W, DS_H);
    downsample_luma((const uint16_t *)right_rgb565, w, h, luma_r, DS_W, DS_H);

    int margin_x = GLOBAL_SEARCH_X + 2;
    int margin_y = GLOBAL_SEARCH_Y + 2;
    int x0 = margin_x, y0 = margin_y;
    int rw = DS_W - 2 * margin_x;
    int rh = DS_H - 2 * margin_y;

    if (rw <= 0 || rh <= 0) { align->global_dx = 0; align->global_dy = 0; return; }

    int best_sad = 0x7fffffff, best_dx = 0, best_dy = 0;
    for (int dy = -GLOBAL_SEARCH_Y; dy <= GLOBAL_SEARCH_Y; dy++) {
        for (int dx = -GLOBAL_SEARCH_X; dx <= GLOBAL_SEARCH_X; dx++) {
            int s = sad_region(luma_l, luma_r, x0, y0, dx, dy, rw, rh, DS_W);
            if (s < best_sad) { best_sad = s; best_dx = dx; best_dy = dy; }
        }
    }

    // Convert DS-space offset to full-res pixels
    align->global_dx = best_dx * w / DS_W;
    align->global_dy = best_dy * h / DS_H;
}

// ---------------------------------------------------------------------------
// build_wiggle_preview_frames — crop both frames to the overlap (AND) region
// ---------------------------------------------------------------------------
int build_wiggle_preview_frames(uint16_t dst[][CAMERA_WIDTH * CAMERA_HEIGHT],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int src_w, int src_h, int nf,
                                const WiggleAlign *align,
                                int offset_dx, int offset_dy,
                                int *out_w, int *out_h)
{
    (void)nf;

    const uint16_t *L = (const uint16_t *)left_rgb565;
    const uint16_t *R = (const uint16_t *)right_rgb565;

    int fdx = (align ? align->global_dx : 0) + offset_dx;
    int fdy = (align ? align->global_dy : 0) + offset_dy;

    // Overlap dimensions at full capture resolution
    int ow = src_w - (fdx < 0 ? -fdx : fdx);
    int oh = src_h - (fdy < 0 ? -fdy : fdy);
    if (ow <= 0) ow = 1;
    if (oh <= 0) oh = 1;

    // Source top-left for each frame
    int lx = fdx > 0 ? fdx : 0;
    int ly = fdy > 0 ? fdy : 0;
    int rx = fdx < 0 ? -fdx : 0;
    int ry = fdy < 0 ? -fdy : 0;

    // Preview-only crop-to-fill: crop the overlap to the top-screen aspect
    // before scaling so the wiggle preview matches the live viewfinder.
    int preview_ow, preview_oh, preview_ox, preview_oy;
    if ((long long)ow * CAMERA_HEIGHT > (long long)oh * CAMERA_WIDTH) {
        preview_oh = oh;
        preview_ow = (oh * CAMERA_WIDTH) / CAMERA_HEIGHT;
        if (preview_ow < 1) preview_ow = 1;
        preview_ox = (ow - preview_ow) / 2;
        preview_oy = 0;
    } else {
        preview_ow = ow;
        preview_oh = (ow * CAMERA_HEIGHT) / CAMERA_WIDTH;
        if (preview_oh < 1) preview_oh = 1;
        preview_ox = 0;
        preview_oy = (oh - preview_oh) / 2;
    }

    int dw = CAMERA_WIDTH;
    int dh = CAMERA_HEIGHT;

    // Nearest-neighbor crop+scale from the overlap region to full preview size
    for (int py = 0; py < dh; py++) {
        int sy = preview_oy + (py * preview_oh) / dh;
        for (int px = 0; px < dw; px++) {
            int sx = preview_ox + (px * preview_ow) / dw;
            dst[0][py * dw + px] = L[(ly + sy) * src_w + (lx + sx)];
            dst[2][py * dw + px] = R[(ry + sy) * src_w + (rx + sx)];
        }
    }

    int npix = dw * dh;
    for (int i = 0; i < npix; i++) {
        uint16_t lp = dst[0][i], rp = dst[2][i];
        uint16_t r = (((lp >> 11) & 0x1f) + ((rp >> 11) & 0x1f)) >> 1;
        uint16_t g = (((lp >>  5) & 0x3f) + ((rp >>  5) & 0x3f)) >> 1;
        uint16_t b = (( lp        & 0x1f) + ( rp        & 0x1f)) >> 1;
        dst[1][i] = (r << 11) | (g << 5) | b;
    }
    memcpy(dst[3], dst[1], npix * sizeof(uint16_t));

    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;
    return 4;
}

// ---------------------------------------------------------------------------
// save_wiggle_gif
// ---------------------------------------------------------------------------

#define GIF_BUF_CAP (4 * 1024 * 1024)
static uint8_t s_gif_buf[GIF_BUF_CAP];

static void rotate_rgb888_quadrants(uint8_t *dst, const uint8_t *src,
                                    int src_w, int src_h, int quadrants) {
    int q = quadrants & 3;
    if (q == 0) {
        memcpy(dst, src, src_w * src_h * 3);
        return;
    }

    if (q == 1) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_h - 1 - y;
                int dst_y = x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else if (q == 3) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = y;
                int dst_y = src_w - 1 - x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_w - 1 - x;
                int dst_y = src_h - 1 - y;
                memcpy(dst + (dst_y * src_w + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    }
}

int save_wiggle_gif(const char *path,
                    const uint8_t *left_rgb565,  int w, int h,
                    const uint8_t *right_rgb565,
                    int n_frames, int delay_ms,
                    const WiggleAlign *align,
                    int offset_dx, int offset_dy,
                    int rotate_quadrants,
                    const EffectRecipe *recipe)
{
    (void)n_frames;

    int fdx = (align ? align->global_dx : 0) + offset_dx;
    int fdy = (align ? align->global_dy : 0) + offset_dy;

    // Overlap dimensions
    int ow = w - (fdx < 0 ? -fdx : fdx);
    int oh = h - (fdy < 0 ? -fdy : fdy);
    if (ow <= 0) ow = 1;
    if (oh <= 0) oh = 1;

    // Source top-left for each frame
    int lx = fdx > 0 ? fdx : 0;
    int ly = fdy > 0 ? fdy : 0;
    int rx = fdx < 0 ? -fdx : 0;
    int ry = fdy < 0 ? -fdy : 0;

    int onpix = ow * oh;
    uint8_t *left_crop  = malloc(onpix * 3);
    uint8_t *right_crop = malloc(onpix * 3);
    uint8_t *blend_crop = malloc(onpix * 3);
    if (!left_crop || !right_crop || !blend_crop) {
        free(left_crop); free(right_crop); free(blend_crop); return 0;
    }

    const uint16_t *L = (const uint16_t *)left_rgb565;
    const uint16_t *R = (const uint16_t *)right_rgb565;

    // Convert + crop in one pass (RGB565 → RGB888, overlap region only)
    // Also build blend frame as 50/50 average
    for (int py = 0; py < oh; py++) {
        for (int px = 0; px < ow; px++) {
            uint16_t lp = L[(ly + py) * w + (lx + px)];
            uint8_t *ld = left_crop  + (py * ow + px) * 3;
            ld[0] = (lp >> 11) << 3;
            ld[1] = ((lp >> 5) & 0x3f) << 2;
            ld[2] = (lp & 0x1f) << 3;

            uint16_t rp = R[(ry + py) * w + (rx + px)];
            uint8_t *rd = right_crop + (py * ow + px) * 3;
            rd[0] = (rp >> 11) << 3;
            rd[1] = ((rp >> 5) & 0x3f) << 2;
            rd[2] = (rp & 0x1f) << 3;

            uint8_t *bd = blend_crop + (py * ow + px) * 3;
            bd[0] = (ld[0] + rd[0]) >> 1;
            bd[1] = (ld[1] + rd[1]) >> 1;
            bd[2] = (ld[2] + rd[2]) >> 1;
        }
    }

    // Apply the active processing pipeline to each unique frame if requested.
    if (pipeline_recipe_has_effects(recipe)) {
        pipeline_apply(left_crop,  ow, oh, recipe, 0);
        pipeline_apply(right_crop, ow, oh, recipe, 1);
        pipeline_apply(blend_crop, ow, oh, recipe, 2);
    }

    // Sequence: L, blend, R, blend  (matches 3ds-mpo-gif animation order)
    const uint8_t *frame_ptrs[4] = { left_crop, blend_crop, right_crop, blend_crop };
    uint8_t *rot_left = NULL, *rot_right = NULL, *rot_blend = NULL;
    int enc_w = ow;
    int enc_h = oh;
    if (rotate_quadrants != 0) {
        rot_left = malloc(onpix * 3);
        rot_right = malloc(onpix * 3);
        rot_blend = malloc(onpix * 3);
        if (!rot_left || !rot_right || !rot_blend) {
            free(rot_left); free(rot_right); free(rot_blend);
            free(left_crop); free(right_crop); free(blend_crop);
            return 0;
        }
        rotate_rgb888_quadrants(rot_left, left_crop, ow, oh, rotate_quadrants);
        rotate_rgb888_quadrants(rot_right, right_crop, ow, oh, rotate_quadrants);
        rotate_rgb888_quadrants(rot_blend, blend_crop, ow, oh, rotate_quadrants);
        frame_ptrs[0] = rot_left;
        frame_ptrs[1] = rot_blend;
        frame_ptrs[2] = rot_right;
        frame_ptrs[3] = rot_blend;
        enc_w = oh;
        enc_h = ow;
    }
    size_t gif_len = gif_encode(s_gif_buf, GIF_BUF_CAP,
                                frame_ptrs, 4,
                                enc_w, enc_h, delay_ms);
    free(rot_left); free(rot_right); free(rot_blend);
    free(left_crop); free(right_crop); free(blend_crop);

    if (gif_len == 0) return 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_gif_buf, 1, gif_len, fp) == gif_len);
    fclose(fp);
    return ok;
}

// ---------------------------------------------------------------------------
// wiggle_preview_update — handle input while wiggle preview is displayed
// ---------------------------------------------------------------------------

void wiggle_preview_update(WiggleState *wig, SaveThreadState *save,
                           u32 kDown, u32 kHeld,
                           bool do_save,
                           u8 *wiggle_left, u8 *wiggle_right,
                           int *save_flash,
                           int rotate_quadrants,
                           const EffectRecipe *recipe) {
    // D-pad: left/right = X offset, up/down = Y offset, with hold-repeat
    u32 dpad = kHeld & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN);
    if (dpad) {
        bool fire = (kDown & dpad) || (wig->dpad_repeat > 20 && wig->dpad_repeat % 4 == 0);
        wig->dpad_repeat++;
        if (fire) {
            if ((dpad & KEY_DLEFT)  && wig->offset_dx > -40) { wig->offset_dx--; wig->rebuild = true; }
            if ((dpad & KEY_DRIGHT) && wig->offset_dx <  40) { wig->offset_dx++; wig->rebuild = true; }
            if ((dpad & KEY_DUP)    && wig->offset_dy <  10) { wig->offset_dy++; wig->rebuild = true; }
            if ((dpad & KEY_DDOWN)  && wig->offset_dy > -10) { wig->offset_dy--; wig->rebuild = true; }
        }
    } else {
        wig->dpad_repeat = 0;
    }

    // Touch buttons — re-read touch here so they work even when captureInterrupted
    // Use tapped (kDown) not held, to avoid rapid-fire on resistive screen
    {
        touchPosition wtouch;
        hidTouchRead(&wtouch);
        bool wtapped = (kDown & KEY_TOUCH) != 0;
        if (wtapped) {
            int tx = wtouch.px, ty = wtouch.py;
            #define WBTW  28
            #define WBTH  22
            #define WVALW 36
            #define WRSTW 22
            #define WMINX 18
            #define WVALX (WMINX + WBTW + 2)
            #define WPLUX (WVALX + WVALW + 2)
            #define WRSTX (WPLUX + WBTW + 2)
            int row_x_y = SHOOT_CONTENT_Y + 4;
            int row_y_y = SHOOT_CONTENT_Y + 32;
            int *val = NULL; int lo = 0, hi = 0;
            if (tx < 158 && ty >= row_x_y && ty < row_x_y + WBTH)
                { val = &wig->offset_dx; lo = -40; hi = 40; }
            else if (tx < 158 && ty >= row_y_y && ty < row_y_y + WBTH)
                { val = &wig->offset_dy; lo = -10; hi = 10; }
            if (val) {
                if (tx >= WMINX && tx < WMINX + WBTW) {
                    if (*val > lo) { (*val)--; wig->rebuild = true; }
                } else if (tx >= WPLUX && tx < WPLUX + WBTW) {
                    if (*val < hi) { (*val)++; wig->rebuild = true; }
                } else if (tx >= WRSTX && tx < WRSTX + WRSTW) {
                    *val = 0; wig->rebuild = true;
                }
            }

            // Delay controls live on the right half of the wiggle preview UI.
            if (tx >= 160) {
                float py0 = (float)SHOOT_CONTENT_Y + 20.0f;
                #define DPILL_W   32
                #define DPILL_H   16
                #define DPILL_GAP  3
                static const int presets[4] = {50, 100, 200, 500};
                float total_pw = 4 * DPILL_W + 3 * DPILL_GAP;
                float px0 = 160.0f + (160.0f - total_pw) * 0.5f;
                if (ty >= (int)py0 && ty < (int)(py0 + DPILL_H)) {
                    for (int i = 0; i < 4; i++) {
                        float bx = px0 + i * (DPILL_W + DPILL_GAP);
                        if (tx >= (int)bx && tx < (int)(bx + DPILL_W)) {
                            wig->delay_ms = presets[i];
                            reset_wiggle_preview_phase(wig);
                            break;
                        }
                    }
                }
                #undef DPILL_W
                #undef DPILL_H
                #undef DPILL_GAP

                float sy = (float)SHOOT_CONTENT_Y + 44.0f;
                #define DSTEP_BTN_W  22
                #define DSTEP_BTN_H  18
                #define DSTEP_VAL_W  54
                float total_sw = 2 * DSTEP_BTN_W + DSTEP_VAL_W + 4;
                float sx0 = 160.0f + (160.0f - total_sw) * 0.5f;
                if (ty >= (int)sy && ty < (int)(sy + DSTEP_BTN_H)) {
                    if (tx >= (int)sx0 && tx < (int)(sx0 + DSTEP_BTN_W)) {
                        wig->delay_ms -= 10;
                        if (wig->delay_ms < 10) wig->delay_ms = 10;
                        reset_wiggle_preview_phase(wig);
                    }
                    float px_btn = sx0 + DSTEP_BTN_W + 2 + DSTEP_VAL_W + 2;
                    if (tx >= (int)px_btn && tx < (int)(px_btn + DSTEP_BTN_W)) {
                        wig->delay_ms += 10;
                        if (wig->delay_ms > 1000) wig->delay_ms = 1000;
                        reset_wiggle_preview_phase(wig);
                    }
                }
                #undef DSTEP_BTN_W
                #undef DSTEP_BTN_H
                #undef DSTEP_VAL_W
            }

            if (ty >= SHOOT_SAVE_Y && ty < SHOOT_SAVE_Y + SHOOT_SAVE_H)
                do_save = true;

            #undef WBTW
            #undef WBTH
            #undef WVALW
            #undef WRSTW
            #undef WMINX
            #undef WVALX
            #undef WPLUX
            #undef WRSTX
        }
    }

    // L/R bumpers cycle through delay presets (50 → 100 → 200 → 500)
    if (kDown & KEY_L || kDown & KEY_R) {
        static const int delay_presets[] = {50, 100, 200, 500};
        int cur = 0;
        for (int i = 0; i < 4; i++) if (wig->delay_ms == delay_presets[i]) { cur = i; break; }
        wig->delay_ms = delay_presets[(cur + (kDown & KEY_L ? 3 : 1)) % 4];
        reset_wiggle_preview_phase(wig);
    }

    if (kDown & KEY_B) {
        wig->preview = false;
    } else if ((do_save || (kDown & KEY_A)) && !save->busy) {
        char apng_path[64];
        if (next_wiggle_path(SAVE_DIR, apng_path, sizeof(apng_path))) {
            settings_save_file_counter(file_counter_next());
            // wiggle_left/right already hold the raw RGB565 snapshots
            int cap_size = wig->capture_w * wig->capture_h * 2;
            memcpy(save->snapshot_buf,  wiggle_left,  cap_size);
            memcpy(save->snapshot_buf2, wiggle_right, cap_size);
            memcpy(save->save_path, apng_path, sizeof(apng_path));
            save->wiggle_mode      = true;
            save->wiggle_n_frames  = wig->n_frames;
            save->wiggle_delay_ms  = wig->delay_ms;
            save->wiggle_has_align = wig->has_align;
            save->wiggle_offset_dx = wig->offset_dx;
            save->wiggle_offset_dy = wig->offset_dy;
            save->wiggle_cap_w     = wig->capture_w;
            save->wiggle_cap_h     = wig->capture_h;
            save->rotate_quadrants = rotate_quadrants;
            save->wiggle_recipe = recipe ? *recipe : (EffectRecipe){0};
            if (wig->has_align) save->wiggle_align_result = wig->align_res;
            save->busy = true;
            *save_flash = 20;
            play_shutter_click();
            LightEvent_Signal(&save->request_event);
            wig->preview = false;
        }
    }
}

// ---------------------------------------------------------------------------
// wiggle_preview_tick — advance animation (rebuild if offsets changed)
// ---------------------------------------------------------------------------

// Scratch buffer for RGB888 conversion during preview filter application
static uint8_t s_preview_rgb888[CAMERA_WIDTH * CAMERA_HEIGHT * 3];

// Tracks whether the current preview frames already have the filter baked in.
static bool s_filter_applied = false;
// Which frame to filter next (spread work across ticks: one frame per tick).
static int  s_filter_next = 0;
// Whether the filter is wanted but not yet done (drives the dimmed overlay).
static bool s_filter_pending = false;

bool wiggle_filter_busy(void) {
    return s_filter_pending;
}

void wiggle_preview_tick(WiggleState *wig,
                         uint16_t preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                         const u8 *wiggle_left, const u8 *wiggle_right,
                         const EffectRecipe *recipe, int frame_count) {
    bool effects_wanted = pipeline_recipe_has_effects(recipe);
    // Rebuild raw frames when user has adjusted H/V offsets
    if (wig->rebuild) {
        wig->n_frames = build_wiggle_preview_frames(preview_frames,
                                        wiggle_left, wiggle_right,
                                        wig->capture_w, wig->capture_h,
                                        wig->n_frames, NULL,
                                        wig->offset_dx, wig->offset_dy,
                                        &wig->crop_w, &wig->crop_h);
        s_filter_applied = false;
        s_filter_next = 0;
        s_filter_pending = effects_wanted;
        wig->rebuild = false;
    }

    // Deferred filter: apply one frame per tick when d-pad is idle.
    // This spreads the work across multiple frames to avoid stutter.
    if (effects_wanted && !s_filter_applied && wig->dpad_repeat == 0) {
        int npix = wig->crop_w * wig->crop_h;
        int f = s_filter_next;
        rgb565_to_rgb888(s_preview_rgb888, preview_frames[f], npix);
        pipeline_apply(s_preview_rgb888, wig->crop_w, wig->crop_h, recipe, frame_count + f);
        rgb888_to_rgb565(preview_frames[f], s_preview_rgb888, npix);
        s_filter_next++;
        if (s_filter_next >= wig->n_frames) {
            s_filter_applied = true;
            s_filter_pending = false;
        }
    } else if (!effects_wanted && s_filter_applied) {
        wig->rebuild = true;
        s_filter_applied = false;
        s_filter_next = 0;
        s_filter_pending = false;
    }
    u64 now = svcGetSystemTick();
    u64 period = (u64)wig->delay_ms * SYSCLOCK_ARM11 / 1000;
    if (period == 0) period = 1;
    u64 elapsed = now - wig->preview_last_tick;
    if (elapsed >= period) {
        u64 steps = elapsed / period;
        if (steps > 0) {
            wig->preview_frame = (wig->preview_frame + (int)(steps % wig->n_frames)) % wig->n_frames;
            wig->preview_last_tick += steps * period;
        }
    }
}
