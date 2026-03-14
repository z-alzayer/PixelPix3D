#include "filter.h"
#include <stdlib.h>
#include <math.h>

// Static scratch buffer for the downsampled image — avoids per-frame malloc.
// Sized for the smallest supported pixel_size (2), giving max (400/2)*(240/2)=200*120 pixels.
#define SMALL_BUF_MAX (200 * 120 * 3)
static uint8_t small_buf[SMALL_BUF_MAX];

// --- Palette data -----------------------------------------------------------

const PaletteDef palettes[PALETTE_COUNT] = {
    {
        "GB greens", 4,
        {{ 15,  56,  15}, { 48,  98,  48}, {139, 172,  15}, {155, 188,  15}}
    },
    {
        "GB grays", 4,
        {{  0,   0,   0}, {102, 102, 102}, {178, 178, 178}, {255, 255, 255}}
    },
    {
        "GBC greenish", 5,
        {{ 15,  31,  15}, { 31,  63,  31}, { 63, 127,  63}, {111, 159,  31}, {155, 188,  15}}
    },
    {
        "GBC shell", 6,
        {{215,  36,  36}, {136, 200,  52}, {243, 241, 242},
         {233, 213,  20}, {153, 153, 164}, { 21,  62, 146}}
    },
    {
        "GBA-like UI", 8,
        {{  0,   0,   0}, { 63,  63,  63}, {132, 132, 132}, {220, 220, 220},
         {255, 255, 255}, {224, 120,  32}, { 56, 128, 224}, { 40, 168,  72}}
    },
    {
        "DB retro", 8,
        {{ 20,  12,  28}, { 68,  36,  52}, { 48,  52, 109}, { 78,  74,  78},
         {133,  76,  48}, {143,  86,  59}, {223, 113,  38}, {217, 160, 102}}
    }
};

// --- Gamma/contrast lookup table -------------------------------------------
// Precomputed once per unique (gamma, contrast) pair — 256 powf calls instead
// of one per pixel. Critical for real-time performance on the 3DS ARM11.

static uint8_t  adjust_lut[256];
static float    lut_gamma    = -1.0f;
static float    lut_contrast = -1.0f;

static void rebuild_lut(float gamma, float contrast) {
    if (gamma == lut_gamma && contrast == lut_contrast) return;
    for (int i = 0; i < 256; i++) {
        float f = powf(i / 255.0f, 1.0f / gamma) * 255.0f;
        f = (f - 128.0f) * contrast + 128.0f;
        if (f < 0.0f)   f = 0.0f;
        if (f > 255.0f) f = 255.0f;
        adjust_lut[i] = (uint8_t)f;
    }
    lut_gamma    = gamma;
    lut_contrast = contrast;
}

// --- Bayer matrix -----------------------------------------------------------

static const uint8_t bayer4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

// --- Dither helpers ---------------------------------------------------------

static uint8_t dither_channel(uint8_t v, int x, int y, int levels) {
    int step      = 255 / (levels - 1);
    int threshold = (bayer4[y & 3][x & 3] * step / 15) - (step / 2);
    int adjusted  = (int)v + threshold;
    if (adjusted < 0)   adjusted = 0;
    if (adjusted > 255) adjusted = 255;
    int level = (adjusted * (levels - 1) + 127) / 255;
    if (level >= levels) level = levels - 1;
    return (uint8_t)(level * 255 / (levels - 1));
}

static void dither_to_palette(uint8_t *r, uint8_t *g, uint8_t *b,
                               int x, int y, const PaletteDef *pal) {
    int gray   = (77 * (int)*r + 150 * (int)*g + 29 * (int)*b) >> 8;
    int levels = pal->size;
    int step   = 255 / (levels - 1);
    int thresh = (bayer4[y & 3][x & 3] * step / 15) - (step / 2);
    int adj    = gray + thresh;
    if (adj < 0)   adj = 0;
    if (adj > 255) adj = 255;
    int level  = (adj * (levels - 1) + 127) / 255;
    if (level >= levels) level = levels - 1;
    *r = pal->colors[level][0];
    *g = pal->colors[level][1];
    *b = pal->colors[level][2];
}

static void dither_pixel(uint8_t *r, uint8_t *g, uint8_t *b,
                         int x, int y, const FilterParams *p) {
    if (p->palette >= 0 && p->palette < PALETTE_COUNT)
        dither_to_palette(r, g, b, x, y, &palettes[p->palette]);
    else {
        *r = dither_channel(*r, x, y, p->color_levels);
        *g = dither_channel(*g, x, y, p->color_levels);
        *b = dither_channel(*b, x, y, p->color_levels);
    }
}

// --- Main pipeline ----------------------------------------------------------

void apply_gameboy_filter(uint8_t *pixels, int width, int height, FilterParams p) {

    // Rebuild LUT only when gamma/contrast change — 256 powf calls max
    rebuild_lut(p.gamma, p.contrast);

    // Step 1: Gamma + contrast via LUT (one table lookup per channel)
    for (int i = 0; i < width * height; i++) {
        pixels[i*3+0] = adjust_lut[pixels[i*3+0]];
        pixels[i*3+1] = adjust_lut[pixels[i*3+1]];
        pixels[i*3+2] = adjust_lut[pixels[i*3+2]];
    }

    // Step 1b: Saturation — blend each pixel toward its luminance
    if (p.saturation != 1.0f) {
        for (int i = 0; i < width * height; i++) {
            int r = pixels[i*3+0], g = pixels[i*3+1], b = pixels[i*3+2];
            int lum = (77 * r + 150 * g + 29 * b) >> 8;
            pixels[i*3+0] = (uint8_t)(lum + (int)(p.saturation * (r - lum)));
            pixels[i*3+1] = (uint8_t)(lum + (int)(p.saturation * (g - lum)));
            pixels[i*3+2] = (uint8_t)(lum + (int)(p.saturation * (b - lum)));
        }
    }

    if (p.pixel_size <= 1) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                int i = (y * width + x) * 3;
                dither_pixel(&pixels[i+0], &pixels[i+1], &pixels[i+2], x, y, &p);
            }
        return;
    }

    // Step 2: Downsample — average each block into one small pixel
    int sw = (width  + p.pixel_size - 1) / p.pixel_size;
    int sh = (height + p.pixel_size - 1) / p.pixel_size;

    uint8_t *small = small_buf;
    if (sw * sh * 3 > SMALL_BUF_MAX) return;  // safety: pixel_size must be >= 2

    for (int by = 0; by < sh; by++) {
        for (int bx = 0; bx < sw; bx++) {
            int x0 = bx * p.pixel_size, x1 = x0 + p.pixel_size < width  ? x0 + p.pixel_size : width;
            int y0 = by * p.pixel_size, y1 = y0 + p.pixel_size < height ? y0 + p.pixel_size : height;
            int count = (x1 - x0) * (y1 - y0);
            long sr = 0, sg = 0, sb = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    int i = (y * width + x) * 3;
                    sr += pixels[i+0]; sg += pixels[i+1]; sb += pixels[i+2];
                }
            int si = (by * sw + bx) * 3;
            small[si+0] = (uint8_t)(sr / count);
            small[si+1] = (uint8_t)(sg / count);
            small[si+2] = (uint8_t)(sb / count);
        }
    }

    // Step 3: Dither the small buffer
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int i = (y * sw + x) * 3;
            dither_pixel(&small[i+0], &small[i+1], &small[i+2], x, y, &p);
        }

    // Step 4: Nearest-neighbour upscale — each small pixel fills a solid block
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int si = (y / p.pixel_size * sw + x / p.pixel_size) * 3;
            int pi = (y * width + x) * 3;
            pixels[pi+0] = small[si+0];
            pixels[pi+1] = small[si+1];
            pixels[pi+2] = small[si+2];
        }

    (void)small;  // static buffer, no free needed
}
