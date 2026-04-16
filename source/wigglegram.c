#include "wigglegram.h"
#include "camera.h"
#include "apng_enc.h"
#include "app_state.h"
#include "shoot.h"
#include "ui.h"
#include "image_load.h"
#include "settings.h"
#include "sound.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
// crop_frame — copy the overlap region from src into dst (RGB565, row-major)
// lx/ly: top-left corner in src to start reading from
// out_w/out_h: dimensions of the output crop
// src_stride: row stride of src (full image width)
// ---------------------------------------------------------------------------
static void crop_frame(const uint16_t *src, int src_stride,
                       int lx, int ly, int out_w, int out_h, uint16_t *dst)
{
    for (int py = 0; py < out_h; py++) {
        const uint16_t *row = src + (ly + py) * src_stride + lx;
        memcpy(dst + py * out_w, row, out_w * sizeof(uint16_t));
    }
}

// ---------------------------------------------------------------------------
// build_wiggle_preview_frames — crop both frames to the overlap (AND) region
// ---------------------------------------------------------------------------
int build_wiggle_preview_frames(uint16_t dst[][400 * 240],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int w, int h, int nf,
                                const WiggleAlign *align,
                                int offset_dx, int offset_dy,
                                int *out_w, int *out_h)
{
    (void)nf;

    const uint16_t *L = (const uint16_t *)left_rgb565;
    const uint16_t *R = (const uint16_t *)right_rgb565;

    int fdx = (align ? align->global_dx : 0) + offset_dx;
    int fdy = (align ? align->global_dy : 0) + offset_dy;

    // Overlap dimensions
    int ow = w - (fdx < 0 ? -fdx : fdx);
    int oh = h - (fdy < 0 ? -fdy : fdy);
    if (ow <= 0) ow = 1;
    if (oh <= 0) oh = 1;

    // Source top-left for each frame
    // When fdx > 0: right image is shifted right; L's overlap starts at x=fdx, R's at x=0
    // When fdx < 0: right image is shifted left;  L's overlap starts at x=0,   R's at x=-fdx
    int lx = fdx > 0 ? fdx : 0;
    int ly = fdy > 0 ? fdy : 0;
    int rx = fdx < 0 ? -fdx : 0;
    int ry = fdy < 0 ? -fdy : 0;

    // Crop L into dst[0], R into dst[2]; build blend into dst[1] and dst[3]
    // Sequence: L(0), blend(1), R(2), blend(3)  — matches 3ds-mpo-gif order
    crop_frame(L, w, lx, ly, ow, oh, dst[0]);
    crop_frame(R, w, rx, ry, ow, oh, dst[2]);

    int npix = ow * oh;
    for (int i = 0; i < npix; i++) {
        uint16_t lp = dst[0][i], rp = dst[2][i];
        uint16_t r = (((lp >> 11) & 0x1f) + ((rp >> 11) & 0x1f)) >> 1;
        uint16_t g = (((lp >>  5) & 0x3f) + ((rp >>  5) & 0x3f)) >> 1;
        uint16_t b = (( lp        & 0x1f) + ( rp        & 0x1f)) >> 1;
        dst[1][i] = (r << 11) | (g << 5) | b;
    }
    memcpy(dst[3], dst[1], npix * sizeof(uint16_t));

    if (out_w) *out_w = ow;
    if (out_h) *out_h = oh;
    return 4;
}

// ---------------------------------------------------------------------------
// save_wiggle_apng
// ---------------------------------------------------------------------------

#define APNG_BUF_CAP (4 * 1024 * 1024)
static uint8_t s_apng_buf[APNG_BUF_CAP];

int save_wiggle_apng(const char *path,
                     const uint8_t *left_rgb565,  int w, int h,
                     const uint8_t *right_rgb565,
                     int n_frames, int delay_ms,
                     const WiggleAlign *align,
                     int offset_dx, int offset_dy)
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

    // Sequence: L, blend, R, blend  (matches 3ds-mpo-gif animation order)
    const uint8_t *frame_ptrs[4] = { left_crop, blend_crop, right_crop, blend_crop };
    uint16_t delay_num = (uint16_t)delay_ms;
    uint16_t delay_den = 1000;
    size_t apng_len = apng_encode(s_apng_buf, APNG_BUF_CAP,
                                  frame_ptrs, 4,
                                  ow, oh, delay_num, delay_den);
    free(left_crop); free(right_crop); free(blend_crop);

    if (apng_len == 0) return 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_apng_buf, 1, apng_len, fp) == apng_len);
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
                           int *save_flash) {
    // D-pad: left/right = X offset, up/down = Y offset, with hold-repeat
    u32 dpad = kHeld & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN);
    if (dpad) {
        bool fire = (kDown & dpad) || (wig->dpad_repeat > 20 && wig->dpad_repeat % 4 == 0);
        wig->dpad_repeat++;
        if (fire) {
            if ((dpad & KEY_DLEFT)  && wig->offset_dx > -20) { wig->offset_dx--; wig->rebuild = true; }
            if ((dpad & KEY_DRIGHT) && wig->offset_dx <  20) { wig->offset_dx++; wig->rebuild = true; }
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
                { val = &wig->offset_dx; lo = -20; hi = 20; }
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
    }

    if (kDown & KEY_B) {
        wig->preview = false;
    } else if ((do_save || (kDown & KEY_A)) && !save->busy) {
        char apng_path[64];
        if (next_wiggle_path(SAVE_DIR, apng_path, sizeof(apng_path))) {
            settings_save_file_counter(file_counter_next());
            // wiggle_left/right already hold the raw RGB565 snapshots
            memcpy(save->snapshot_buf,  wiggle_left,  CAMERA_SCREEN_SIZE);
            memcpy(save->snapshot_buf2, wiggle_right, CAMERA_SCREEN_SIZE);
            memcpy(save->save_path, apng_path, sizeof(apng_path));
            save->wiggle_mode      = true;
            save->wiggle_n_frames  = wig->n_frames;
            save->wiggle_delay_ms  = wig->delay_ms;
            save->wiggle_has_align = wig->has_align;
            save->wiggle_offset_dx = wig->offset_dx;
            save->wiggle_offset_dy = wig->offset_dy;
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

void wiggle_preview_tick(WiggleState *wig,
                         uint16_t preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                         const u8 *wiggle_left, const u8 *wiggle_right) {
    // Rebuild frames when user has adjusted H/V offsets
    if (wig->rebuild) {
        wig->n_frames = build_wiggle_preview_frames(preview_frames,
                                        wiggle_left, wiggle_right,
                                        CAMERA_WIDTH, CAMERA_HEIGHT,
                                        wig->n_frames, NULL,
                                        wig->offset_dx, wig->offset_dy,
                                        &wig->crop_w, &wig->crop_h);
        wig->rebuild = false;
    }
    u64 now = svcGetSystemTick();
    u64 period = (u64)wig->delay_ms * SYSCLOCK_ARM11 / 1000;
    if (now - wig->preview_last_tick >= period) {
        wig->preview_frame     = (wig->preview_frame + 1) % wig->n_frames;
        wig->preview_last_tick = now;
    }
}
