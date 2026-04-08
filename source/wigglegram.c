#include "wigglegram.h"
#include "camera.h"
#include "apng_enc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Build blended preview frames from a left/right RGB565 stereo pair.
// Creates a smooth ping-pong L→R→L cycle across nf frames.
// Returns the actual frame count used (clamped to [2, WIGGLE_PREVIEW_MAX]).
int build_wiggle_preview_frames(uint16_t dst[][400 * 240],
                                const uint8_t *left_rgb565,
                                const uint8_t *right_rgb565,
                                int w, int h, int nf)
{
    if (nf < 2) nf = 2;
    if (nf > WIGGLE_PREVIEW_MAX) nf = WIGGLE_PREVIEW_MAX;

    int npix = w * h;
    int half = nf / 2;
    if (half < 1) half = 1;

    const uint16_t *L = (const uint16_t *)left_rgb565;
    const uint16_t *R = (const uint16_t *)right_rgb565;

    for (int f = 0; f < nf; f++) {
        int alpha = (f <= half) ? (f * 255 / half) : ((nf - f) * 255 / half);
        uint16_t *d = dst[f];
        if (alpha == 0) {
            memcpy(d, L, npix * sizeof(uint16_t));
        } else if (alpha >= 255) {
            memcpy(d, R, npix * sizeof(uint16_t));
        } else {
            for (int i = 0; i < npix; i++) {
                uint16_t pl = L[i], pr = R[i];
                int lr = (pl >> 11) & 0x1f, lg = (pl >> 5) & 0x3f, lb = pl & 0x1f;
                int rr = (pr >> 11) & 0x1f, rg = (pr >> 5) & 0x3f, rb = pr & 0x1f;
                int br = (lr * (255 - alpha) + rr * alpha) / 255;
                int bg = (lg * (255 - alpha) + rg * alpha) / 255;
                int bb = (lb * (255 - alpha) + rb * alpha) / 255;
                d[i] = (uint16_t)((br << 11) | (bg << 5) | bb);
            }
        }
    }
    return nf;
}

// ---------------------------------------------------------------------------
// Wiggle APNG encoder — full 24-bit true colour, no quantization
// ---------------------------------------------------------------------------

// Static 4 MB output buffer.
// 400x240x8 frames at ~3 bytes/pixel deflated is well under 4 MB.
// Saves are serialised through the busy flag in main.c.
#define APNG_BUF_CAP (4 * 1024 * 1024)
static uint8_t s_apng_buf[APNG_BUF_CAP];

int save_wiggle_apng(const char *path,
                     const uint8_t *left_rgb565,  int w, int h,
                     const uint8_t *right_rgb565,
                     int n_frames,
                     int delay_ms)
{
    if (n_frames < 2) n_frames = 2;
    if (n_frames > 8) n_frames = 8;

    int npix = w * h;

    uint8_t *left_rgb  = malloc(npix * 3);
    uint8_t *right_rgb = malloc(npix * 3);
    if (!left_rgb || !right_rgb) {
        free(left_rgb); free(right_rgb);
        return 0;
    }

    // Convert both buffers to RGB888 — true colour, no filter applied
    rgb565_to_rgb888(left_rgb,  (const uint16_t *)left_rgb565,  npix);
    rgb565_to_rgb888(right_rgb, (const uint16_t *)right_rgb565, npix);

    // Build frame pointer array with smooth ping-pong blending.
    // The n_frames steps cover one full L→R→L cycle.
    // Formula: treat frames as evenly spaced on a triangle wave of period n_frames.
    //   half = n_frames / 2  (integer)
    //   alpha = f <= half ? f*255/half : (n_frames-f)*255/half
    const uint8_t *frame_ptrs[8];
    int half = n_frames / 2;
    if (half < 1) half = 1;

    for (int f = 0; f < n_frames; f++) {
        int alpha = (f <= half) ? (f * 255 / half) : ((n_frames - f) * 255 / half);

        if (alpha == 0) {
            frame_ptrs[f] = left_rgb;
        } else if (alpha >= 255) {
            frame_ptrs[f] = right_rgb;
        } else {
            uint8_t *bframe = malloc(npix * 3);
            if (!bframe) {
                for (int k = 0; k < f; k++)
                    if (frame_ptrs[k] != left_rgb && frame_ptrs[k] != right_rgb)
                        free((void *)frame_ptrs[k]);
                free(left_rgb); free(right_rgb);
                return 0;
            }
            int n3 = npix * 3;
            for (int i = 0; i < n3; i++) {
                int va = left_rgb[i], vb = right_rgb[i];
                bframe[i] = (uint8_t)((va * (255 - alpha) + vb * alpha) / 255);
            }
            frame_ptrs[f] = bframe;
        }
    }

    // delay: convert ms to a fraction — use delay_ms/1000 as num/den
    uint16_t delay_num = (uint16_t)delay_ms;
    uint16_t delay_den = 1000;

    size_t apng_len = apng_encode(s_apng_buf, APNG_BUF_CAP,
                                  frame_ptrs, n_frames,
                                  w, h, delay_num, delay_den);

    // Free blended frames
    for (int f = 0; f < n_frames; f++) {
        if (frame_ptrs[f] != left_rgb && frame_ptrs[f] != right_rgb)
            free((void *)frame_ptrs[f]);
    }
    free(left_rgb); free(right_rgb);

    if (apng_len == 0) return 0;

    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_apng_buf, 1, apng_len, fp) == apng_len);
    fclose(fp);
    return ok;
}
