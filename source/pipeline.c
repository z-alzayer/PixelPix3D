#include "pipeline.h"
#include "lomo.h"
#include "bend.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float s_basic_lut[256];
static float s_basic_gamma = -1.0f;
static float s_basic_brightness = -1.0f;
static float s_basic_contrast = -1.0f;

static void rebuild_basic_lut(float gamma, float brightness, float contrast) {
    if (gamma == s_basic_gamma &&
        brightness == s_basic_brightness &&
        contrast == s_basic_contrast) {
        return;
    }

    float inv_gamma = 1.0f / gamma;
    for (int i = 0; i < 256; i++) {
        float v = powf(i / 255.0f, inv_gamma) * 255.0f;
        v = (v * brightness - 128.0f) * contrast + 128.0f;
        if (v < 0.0f) v = 0.0f;
        else if (v > 255.0f) v = 255.0f;
        s_basic_lut[i] = v;
    }

    s_basic_gamma = gamma;
    s_basic_brightness = brightness;
    s_basic_contrast = contrast;
}

static int clamp_strength(int strength) {
    if (strength < 0) return 0;
    if (strength > 10) return 10;
    return strength;
}

static int clamp_remap_style(int style) {
    if (style < 0) return 0;
    if (style >= REMAP_STYLE_COUNT) return REMAP_STYLE_COUNT - 1;
    return style;
}

static int clamp_remap_strength_for_style(int style, int strength) {
    if (style == REMAP_STYLE_TOON) {
        if (strength < 2) return 2;
        if (strength > 15) return 15;
        return strength;
    }
    return clamp_strength(strength);
}

static int clamp_remap_cell_size(int cell_size) {
    if (cell_size < 4) return 4;
    if (cell_size > 16) return 16;
    return cell_size;
}

static int clamp_remap_cell_for_style(int style, int cell_size) {
    if (style == REMAP_STYLE_TOON) {
        if (cell_size < 1) return 1;
        if (cell_size > 16) return 16;
        return cell_size;
    }
    return clamp_remap_cell_size(cell_size);
}

static float strength_mix(float neutral, float target, int strength) {
    float t = (float)clamp_strength(strength) / 10.0f;
    return neutral + (target - neutral) * t;
}

static void apply_basic_adjustments(uint8_t *pixels, int width, int height,
                                    float brightness, float contrast,
                                    float gamma, float saturation) {
    rebuild_basic_lut(gamma, brightness, contrast);

    for (int i = 0; i < width * height; i++) {
        int idx = i * 3;
        float r = s_basic_lut[pixels[idx + 0]];
        float g = s_basic_lut[pixels[idx + 1]];
        float b = s_basic_lut[pixels[idx + 2]];

        if (fabsf(saturation - 1.0f) > 0.001f) {
            float lum = (77.0f * r + 150.0f * g + 29.0f * b) / 256.0f;
            r = lum + saturation * (r - lum);
            g = lum + saturation * (g - lum);
            b = lum + saturation * (b - lum);
        }

        if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
        if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
        if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;

        pixels[idx + 0] = (uint8_t)(r + 0.5f);
        pixels[idx + 1] = (uint8_t)(g + 0.5f);
        pixels[idx + 2] = (uint8_t)(b + 0.5f);
    }
}

static bool has_basic_adjustments(const FilterParams *params) {
    if (!params) return false;
    return fabsf(params->brightness - 1.0f) > 0.001f ||
           fabsf(params->contrast   - 1.0f) > 0.001f ||
           fabsf(params->gamma      - 1.0f) > 0.001f ||
           fabsf(params->saturation - 1.0f) > 0.001f;
}

static uint8_t glyph5x7(char ch, int row) {
    switch (ch) {
    case ' ': { static const uint8_t g[7] = {0,0,0,0,0,0,0}; return g[row]; }
    case '.': { static const uint8_t g[7] = {0,0,0,0,0,0x0c,0x0c}; return g[row]; }
    case ',': { static const uint8_t g[7] = {0,0,0,0,0,0x0c,0x08}; return g[row]; }
    case '`': { static const uint8_t g[7] = {0x08,0x04,0,0,0,0,0}; return g[row]; }
    case ':': { static const uint8_t g[7] = {0,0x0c,0x0c,0,0x0c,0x0c,0}; return g[row]; }
    case ';': { static const uint8_t g[7] = {0,0x0c,0x0c,0,0x0c,0x08,0x10}; return g[row]; }
    case '-': { static const uint8_t g[7] = {0,0,0,0x1f,0,0,0}; return g[row]; }
    case '_': { static const uint8_t g[7] = {0,0,0,0,0,0,0x1f}; return g[row]; }
    case '=': { static const uint8_t g[7] = {0,0,0x1f,0,0x1f,0,0}; return g[row]; }
    case '+': { static const uint8_t g[7] = {0,0x04,0x04,0x1f,0x04,0x04,0}; return g[row]; }
    case '*': { static const uint8_t g[7] = {0,0x15,0x0e,0x1f,0x0e,0x15,0}; return g[row]; }
    case '/': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0,0}; return g[row]; }
    case '\\': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0,0}; return g[row]; }
    case '|': { static const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case '(': { static const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
    case ')': { static const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
    case '[': { static const uint8_t g[7] = {0x0e,0x08,0x08,0x08,0x08,0x08,0x0e}; return g[row]; }
    case ']': { static const uint8_t g[7] = {0x0e,0x02,0x02,0x02,0x02,0x02,0x0e}; return g[row]; }
    case '<': { static const uint8_t g[7] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02}; return g[row]; }
    case '>': { static const uint8_t g[7] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08}; return g[row]; }
    case '#': { static const uint8_t g[7] = {0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0}; return g[row]; }
    case '%': { static const uint8_t g[7] = {0x19,0x1a,0x04,0x08,0x13,0x03,0}; return g[row]; }
    case '@': { static const uint8_t g[7] = {0x0e,0x11,0x17,0x15,0x17,0x10,0x0e}; return g[row]; }
    case '0': { static const uint8_t g[7] = {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}; return g[row]; }
    case '1': { static const uint8_t g[7] = {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}; return g[row]; }
    case '2': { static const uint8_t g[7] = {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}; return g[row]; }
    case '3': { static const uint8_t g[7] = {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}; return g[row]; }
    case '4': { static const uint8_t g[7] = {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}; return g[row]; }
    case '5': { static const uint8_t g[7] = {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}; return g[row]; }
    case '7': { static const uint8_t g[7] = {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case 'W': { static const uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1b,0x11}; return g[row]; }
    case 'M': { static const uint8_t g[7] = {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    default: { static const uint8_t g[7] = {0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f}; return g[row]; }
    }
}

static uint8_t remap_glyph_alpha(char ch, int x, int y, int cell_w, int cell_h) {
    int gx = (x * 5) / (cell_w > 0 ? cell_w : 1);
    int gy = (y * 7) / (cell_h > 0 ? cell_h : 1);
    if (gx < 0) gx = 0; else if (gx > 4) gx = 4;
    if (gy < 0) gy = 0; else if (gy > 6) gy = 6;
    return (glyph5x7(ch, gy) & (1 << (4 - gx))) ? 255 : 0;
}

static char ascii_char_for_luma(int lum) {
    static const char ramp[] = "@#*=-. ";
    int n = (int)sizeof(ramp) - 2;
    int idx = (lum * n + 127) / 255;
    if (idx < 0) idx = 0;
    if (idx > n) idx = n;
    return ramp[idx];
}

#define REMAP_GRID_MAX_COLS 160
#define REMAP_GRID_MAX_ROWS 120
#define REMAP_GRID_MAX_CELLS (REMAP_GRID_MAX_COLS * REMAP_GRID_MAX_ROWS)
#define REMAP_TOON_MAX_PIXELS (640 * 480)
#define REMAP_TOON_MAX_CLUSTERS 15
#define REMAP_TOON_EDGE_CELL_SIZE 6

static uint8_t s_remap_luma[REMAP_GRID_MAX_CELLS];
static uint8_t s_remap_norm[REMAP_GRID_MAX_CELLS];
static uint8_t s_remap_band[REMAP_GRID_MAX_CELLS];
static uint8_t s_remap_r[REMAP_GRID_MAX_CELLS];
static uint8_t s_remap_g[REMAP_GRID_MAX_CELLS];
static uint8_t s_remap_b[REMAP_GRID_MAX_CELLS];
static uint8_t s_toon_gray[REMAP_TOON_MAX_PIXELS];
static uint8_t s_toon_blur[REMAP_TOON_MAX_PIXELS];
static uint8_t s_toon_nms[REMAP_TOON_MAX_PIXELS];
static uint8_t s_toon_edge[REMAP_TOON_MAX_PIXELS];
static uint8_t s_toon_edge_tmp[REMAP_TOON_MAX_PIXELS];
static int s_toon_center_r[REMAP_TOON_MAX_CLUSTERS];
static int s_toon_center_g[REMAP_TOON_MAX_CLUSTERS];
static int s_toon_center_b[REMAP_TOON_MAX_CLUSTERS];

static int remap_hist_percentile(const int hist[32], int target) {
    int accum = 0;
    for (int i = 0; i < 32; i++) {
        accum += hist[i];
        if (accum >= target) return i;
    }
    return 31;
}

static int remap_normalize_luma(int lum, int lo, int hi) {
    if (hi <= lo) return lum;
    int v = (lum - lo) * 255 / (hi - lo);
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static uint8_t remap_clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static int remap_abs_i(int v) {
    return v < 0 ? -v : v;
}

static int remap_toon_cluster_count(int strength) {
    if (strength < 2) return 2;
    if (strength > REMAP_TOON_MAX_CLUSTERS) return REMAP_TOON_MAX_CLUSTERS;
    return strength;
}

static int remap_toon_stroke_radius(int cell_size) {
    if (cell_size >= 16) return 3;
    if (cell_size >= 12) return 2;
    if (cell_size >= 8)  return 1;
    return 0;
}

static int remap_toon_edge_threshold(int threshold_control) {
    int threshold = 4 + (threshold_control - 1) * 4;
    if (threshold < 4) threshold = 4;
    if (threshold > 64) threshold = 64;
    return threshold;
}

static int remap_toon_distance(int r, int g, int b, int c) {
    int dr = r - s_toon_center_r[c];
    int dg = g - s_toon_center_g[c];
    int db = b - s_toon_center_b[c];
    return dr * dr + dg * dg + db * db;
}

static int remap_toon_nearest_center(int r, int g, int b, int clusters) {
    int best = 0;
    int best_dist = remap_toon_distance(r, g, b, 0);
    for (int c = 1; c < clusters; c++) {
        int dist = remap_toon_distance(r, g, b, c);
        if (dist < best_dist) {
            best = c;
            best_dist = dist;
        }
    }
    return best;
}

static int remap_toon_sample_step(int pixels) {
    if (pixels > 180000) return 3;
    if (pixels > 50000) return 2;
    return 1;
}

static void remap_toon_init_centers(const uint8_t *rgb, int w, int h, int clusters) {
    int step = remap_toon_sample_step(w * h);
    for (int c = 0; c < clusters; c++) {
        int target = clusters > 1 ? c * 255 / (clusters - 1) : 128;
        int best_delta = 999;
        int best_idx = 0;
        for (int y = 0; y < h; y += step) {
            int row = y * w * 3;
            for (int x = 0; x < w; x += step) {
                int idx = row + x * 3;
                int r = rgb[idx + 0];
                int g = rgb[idx + 1];
                int b = rgb[idx + 2];
                int lum = (77 * r + 150 * g + 29 * b) >> 8;
                int delta = remap_abs_i(lum - target);
                if (delta < best_delta) {
                    best_delta = delta;
                    best_idx = idx;
                }
            }
        }
        s_toon_center_r[c] = rgb[best_idx + 0];
        s_toon_center_g[c] = rgb[best_idx + 1];
        s_toon_center_b[c] = rgb[best_idx + 2];
    }
}

static void remap_toon_kmeans(const uint8_t *rgb, int w, int h, int clusters) {
    remap_toon_init_centers(rgb, w, h, clusters);
    int step = remap_toon_sample_step(w * h);

    for (int iter = 0; iter < 8; iter++) {
        long sum_r[REMAP_TOON_MAX_CLUSTERS] = {0};
        long sum_g[REMAP_TOON_MAX_CLUSTERS] = {0};
        long sum_b[REMAP_TOON_MAX_CLUSTERS] = {0};
        int count[REMAP_TOON_MAX_CLUSTERS] = {0};

        for (int y = 0; y < h; y += step) {
            int row = y * w * 3;
            for (int x = 0; x < w; x += step) {
                int idx = row + x * 3;
                int r = rgb[idx + 0];
                int g = rgb[idx + 1];
                int b = rgb[idx + 2];
                int c = remap_toon_nearest_center(r, g, b, clusters);
                sum_r[c] += r;
                sum_g[c] += g;
                sum_b[c] += b;
                count[c]++;
            }
        }

        int shift = 0;
        for (int c = 0; c < clusters; c++) {
            if (count[c] <= 0) continue;
            int nr = (int)(sum_r[c] / count[c]);
            int ng = (int)(sum_g[c] / count[c]);
            int nb = (int)(sum_b[c] / count[c]);
            shift += remap_abs_i(nr - s_toon_center_r[c]);
            shift += remap_abs_i(ng - s_toon_center_g[c]);
            shift += remap_abs_i(nb - s_toon_center_b[c]);
            s_toon_center_r[c] = nr;
            s_toon_center_g[c] = ng;
            s_toon_center_b[c] = nb;
        }
        if (shift <= clusters * 2) break;
    }
}

static void remap_toon_build_canny_edges(const uint8_t *rgb, int w, int h,
                                         int threshold_control) {
    int pixels = w * h;
    memset(s_toon_edge, 0, pixels);
    memset(s_toon_edge_tmp, 0, pixels);
    memset(s_toon_nms, 0, pixels);
    if (w < 3 || h < 3) return;

    for (int i = 0; i < pixels; i++) {
        int idx = i * 3;
        int r = rgb[idx + 0];
        int g = rgb[idx + 1];
        int b = rgb[idx + 2];
        s_toon_gray[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum = 0;
            int count = 0;
            for (int oy = -1; oy <= 1; oy++) {
                int sy = y + oy;
                if (sy < 0) sy = 0;
                else if (sy >= h) sy = h - 1;
                for (int ox = -1; ox <= 1; ox++) {
                    int sx = x + ox;
                    if (sx < 0) sx = 0;
                    else if (sx >= w) sx = w - 1;
                    sum += s_toon_gray[sy * w + sx];
                    count++;
                }
            }
            s_toon_blur[y * w + x] = (uint8_t)(sum / count);
        }
    }

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x;
            int tl = s_toon_blur[i - w - 1];
            int tc = s_toon_blur[i - w];
            int tr = s_toon_blur[i - w + 1];
            int ml = s_toon_blur[i - 1];
            int mr = s_toon_blur[i + 1];
            int bl = s_toon_blur[i + w - 1];
            int bc = s_toon_blur[i + w];
            int br = s_toon_blur[i + w + 1];
            int gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
            int gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
            int mag = (remap_abs_i(gx) + remap_abs_i(gy)) / 4;
            if (mag > 255) mag = 255;
            s_toon_edge_tmp[i] = (uint8_t)mag;
        }
    }

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x;
            int tl = s_toon_blur[i - w - 1];
            int tc = s_toon_blur[i - w];
            int tr = s_toon_blur[i - w + 1];
            int ml = s_toon_blur[i - 1];
            int mr = s_toon_blur[i + 1];
            int bl = s_toon_blur[i + w - 1];
            int bc = s_toon_blur[i + w];
            int br = s_toon_blur[i + w + 1];
            int gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
            int gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
            int ax = remap_abs_i(gx);
            int ay = remap_abs_i(gy);
            int mag = s_toon_edge_tmp[i];
            int n1, n2;
            if (ax > ay * 2) {
                n1 = s_toon_edge_tmp[i - 1];
                n2 = s_toon_edge_tmp[i + 1];
            } else if (ay > ax * 2) {
                n1 = s_toon_edge_tmp[i - w];
                n2 = s_toon_edge_tmp[i + w];
            } else if ((gx < 0) == (gy < 0)) {
                n1 = s_toon_edge_tmp[i - w - 1];
                n2 = s_toon_edge_tmp[i + w + 1];
            } else {
                n1 = s_toon_edge_tmp[i - w + 1];
                n2 = s_toon_edge_tmp[i + w - 1];
            }
            s_toon_nms[i] = (mag >= n1 && mag >= n2) ? (uint8_t)mag : 0;
        }
    }

    int high = remap_toon_edge_threshold(threshold_control);
    int low = high / 2;
    for (int i = 0; i < pixels; i++) {
        s_toon_edge[i] = s_toon_nms[i] >= high ? 255
                       : s_toon_nms[i] >= low  ? 128 : 0;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int y = 1; y < h - 1; y++) {
            for (int x = 1; x < w - 1; x++) {
                int i = y * w + x;
                if (s_toon_edge[i] != 128) continue;
                if (s_toon_edge[i - 1] == 255 || s_toon_edge[i + 1] == 255 ||
                    s_toon_edge[i - w] == 255 || s_toon_edge[i + w] == 255 ||
                    s_toon_edge[i - w - 1] == 255 || s_toon_edge[i - w + 1] == 255 ||
                    s_toon_edge[i + w - 1] == 255 || s_toon_edge[i + w + 1] == 255)
                    s_toon_edge[i] = 255;
            }
        }
    }
    for (int i = 0; i < pixels; i++)
        if (s_toon_edge[i] != 255) s_toon_edge[i] = 0;

    int radius = remap_toon_stroke_radius(REMAP_TOON_EDGE_CELL_SIZE);
    if (radius <= 0) return;
    memcpy(s_toon_edge_tmp, s_toon_edge, pixels);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int mark = 0;
            for (int oy = -radius; oy <= radius && !mark; oy++) {
                int sy = y + oy;
                if (sy < 0 || sy >= h) continue;
                for (int ox = -radius; ox <= radius; ox++) {
                    int sx = x + ox;
                    if (sx < 0 || sx >= w) continue;
                    if (s_toon_edge_tmp[sy * w + sx]) {
                        mark = 1;
                        break;
                    }
                }
            }
            s_toon_edge[y * w + x] = mark ? 255 : 0;
        }
    }
}

static void apply_toon_remap(uint8_t *rgb, int w, int h,
                             int cell_size, int clusters) {
    int pixels = w * h;
    if (pixels <= 0 || pixels > REMAP_TOON_MAX_PIXELS) return;

    clusters = remap_toon_cluster_count(clusters);
    remap_toon_build_canny_edges(rgb, w, h, cell_size);
    remap_toon_kmeans(rgb, w, h, clusters);

    for (int i = 0; i < pixels; i++) {
        int idx = i * 3;
        if (s_toon_edge[i]) {
            rgb[idx + 0] = 0;
            rgb[idx + 1] = 0;
            rgb[idx + 2] = 0;
            continue;
        }
        int c = remap_toon_nearest_center(rgb[idx + 0], rgb[idx + 1], rgb[idx + 2],
                                          clusters);
        rgb[idx + 0] = (uint8_t)s_toon_center_r[c];
        rgb[idx + 1] = (uint8_t)s_toon_center_g[c];
        rgb[idx + 2] = (uint8_t)s_toon_center_b[c];
    }
}

static void apply_remap(uint8_t *rgb, int w, int h, int style,
                        int cell_size, int strength) {
    style = clamp_remap_style(style);
    strength = clamp_remap_strength_for_style(style, strength);
    if (strength <= 0) return;

    cell_size = clamp_remap_cell_for_style(style, cell_size);
    if (style == REMAP_STYLE_TOON) {
        apply_toon_remap(rgb, w, h, cell_size, strength);
        return;
    }

    int cols = (w + cell_size - 1) / cell_size;
    int rows = (h + cell_size - 1) / cell_size;
    if (cols <= 0 || rows <= 0 ||
        cols > REMAP_GRID_MAX_COLS || rows > REMAP_GRID_MAX_ROWS)
        return;

    int hist[32] = {0};
    int total_cells = cols * rows;

    for (int cy = 0; cy < rows; cy++) {
        int by = cy * cell_size;
        int bh = cell_size;
        if (by + bh > h) bh = h - by;
        for (int cx = 0; cx < cols; cx++) {
            int bx = cx * cell_size;
            int bw = cell_size;
            if (bx + bw > w) bw = w - bx;

            long sr = 0, sg = 0, sb = 0, sl = 0;
            int count = bw * bh;
            for (int y = 0; y < bh; y++) {
                int row = ((by + y) * w + bx) * 3;
                for (int x = 0; x < bw; x++) {
                    int idx = row + x * 3;
                    int r = rgb[idx + 0];
                    int g = rgb[idx + 1];
                    int b = rgb[idx + 2];
                    int l = (77 * r + 150 * g + 29 * b) >> 8;
                    sr += r;
                    sg += g;
                    sb += b;
                    sl += l;
                }
            }

            int i = cy * cols + cx;
            int ar = (int)(sr / count);
            int ag = (int)(sg / count);
            int ab = (int)(sb / count);
            int lum = (int)(sl / count);
            s_remap_r[i] = (uint8_t)ar;
            s_remap_g[i] = (uint8_t)ag;
            s_remap_b[i] = (uint8_t)ab;
            s_remap_luma[i] = (uint8_t)lum;
            hist[lum >> 3]++;
        }
    }

    int trim = total_cells / 50;
    int lo_bin = remap_hist_percentile(hist, trim);
    int hi_bin = remap_hist_percentile(hist, total_cells - trim);
    int lo = lo_bin * 8;
    int hi = hi_bin * 8 + 7;
    if (hi < lo + 24) {
        lo -= 24;
        hi += 24;
        if (lo < 0) lo = 0;
        if (hi > 255) hi = 255;
    }

    for (int i = 0; i < total_cells; i++) {
        int norm = remap_normalize_luma(s_remap_luma[i], lo, hi);
        s_remap_norm[i] = (uint8_t)norm;
        int band = (norm * 5) / 256;
        if (band < 0) band = 0;
        if (band > 4) band = 4;
        s_remap_band[i] = (uint8_t)band;
    }

    for (int cy = 0; cy < rows; cy++) {
        int by = cy * cell_size;
        int bh = cell_size;
        if (by + bh > h) bh = h - by;
        for (int cx = 0; cx < cols; cx++) {
            int bx = cx * cell_size;
            int bw = cell_size;
            if (bx + bw > w) bw = w - bx;

            int i = cy * cols + cx;
            int norm = s_remap_norm[i];
            int ar = s_remap_r[i];
            int ag = s_remap_g[i];
            int ab = s_remap_b[i];

            char ch = ascii_char_for_luma(norm);
            int bg_r = 238, bg_g = 236, bg_b = 222;
            int ink_r = 18, ink_g = 20, ink_b = 18;
            bool glyph_mode = true;

            if (style == REMAP_STYLE_COLOR) {
                ch = ascii_char_for_luma(norm);
                bg_r = 4; bg_g = 5; bg_b = 7;
                ink_r = ar + 36; if (ink_r > 255) ink_r = 255;
                ink_g = ag + 36; if (ink_g > 255) ink_g = 255;
                ink_b = ab + 36; if (ink_b > 255) ink_b = 255;
            } else if (style == REMAP_STYLE_MATRIX) {
                ch = (norm > 232) ? ' ' : (((cx * 3 + cy * 5 + (norm >> 5)) & 1) ? '1' : '0');
                bg_r = 0; bg_g = 12; bg_b = 8;
                int glow = 255 - norm;
                ink_r = 8 + glow / 5;
                ink_g = 96 + glow * 150 / 255;
                ink_b = 34 + glow / 4;
            } else if (style == REMAP_STYLE_PINK_WASH ||
                       style == REMAP_STYLE_CCD_TINT) {
                glyph_mode = false;
            }

            for (int y = 0; y < bh; y++) {
                int row = ((by + y) * w + bx) * 3;
                for (int x = 0; x < bw; x++) {
                    int idx = row + x * 3;
                    int src_r = rgb[idx + 0];
                    int src_g = rgb[idx + 1];
                    int src_b = rgb[idx + 2];
                    int rr, rg, rb;
                    if (glyph_mode) {
                        uint8_t alpha = remap_glyph_alpha(ch, x, y, bw, bh);
                        rr = (bg_r * (255 - alpha) + ink_r * alpha) / 255;
                        rg = (bg_g * (255 - alpha) + ink_g * alpha) / 255;
                        rb = (bg_b * (255 - alpha) + ink_b * alpha) / 255;
                    } else if (style == REMAP_STYLE_PINK_WASH) {
                        rr = remap_clamp_u8((src_r * 5) / 4 + 58);
                        rg = remap_clamp_u8((src_g * 11) / 10 + 18);
                        rb = remap_clamp_u8((src_b * 6) / 5 + 52);
                        rr = (rr * 3 + 255) / 4;
                        rg = (rg * 3 + 206) / 4;
                        rb = (rb * 3 + 238) / 4;
                    } else {
                        int lum = (77 * src_r + 150 * src_g + 29 * src_b) >> 8;
                        rr = remap_clamp_u8(lum + (src_r - lum) * 3 / 4 - 18);
                        rg = remap_clamp_u8(lum + (src_g - lum) * 4 / 5 + 14);
                        rb = remap_clamp_u8(lum + (src_b - lum) * 4 / 5 + 40);
                    }
                    rgb[idx + 0] = (uint8_t)((rgb[idx + 0] * (10 - strength) + rr * strength) / 10);
                    rgb[idx + 1] = (uint8_t)((rgb[idx + 1] * (10 - strength) + rg * strength) / 10);
                    rgb[idx + 2] = (uint8_t)((rgb[idx + 2] * (10 - strength) + rb * strength) / 10);
                }
            }
        }
    }
}

void pipeline_state_init(EffectPipeline *pipe, const FilterParams *defaults) {
    pipe->capture_mode = CAPTURE_MODE_STILL;
    pipe->active_panel = PIPELINE_PANEL_GB;
    pipe->panel_open = false;
    pipe->base.enabled = false;
    pipe->base.preset = 0;
    pipe->base.strength = 10;
    pipe->gb.enabled = false;
    pipe->gb.params = *defaults;
    pipe->remap.enabled = false;
    pipe->remap.style = REMAP_STYLE_ASCII;
    pipe->remap.cell_size = 8;
    pipe->remap.strength = 10;
    pipe->bend.enabled = false;
    pipe->bend.preset = 0;
    pipe->bend.strength = 10;
    pipe->post.enabled = defaults->fx_mode != FX_NONE;
    pipe->post.fx_mode = defaults->fx_mode;
    pipe->post.fx_intensity = defaults->fx_intensity;
}

void pipeline_state_sync_legacy(EffectPipeline *pipe,
                                int capture_mode,
                                bool gb_enabled,
                                const FilterParams *gb_params,
                                bool remap_enabled, int remap_style,
                                int remap_cell_size, int remap_strength,
                                bool lomo_enabled, int lomo_preset, int lomo_strength,
                                bool bend_enabled, int bend_preset, int bend_strength,
                                int post_fx_mode, int post_fx_intensity,
                                int active_panel, bool panel_open) {
    pipe->capture_mode = capture_mode;
    pipe->active_panel = active_panel;
    pipe->panel_open = panel_open;
    pipe->gb.enabled = gb_enabled;
    pipe->gb.params = *gb_params;
    pipe->remap.enabled = remap_enabled;
    pipe->remap.style = clamp_remap_style(remap_style);
    pipe->remap.cell_size = clamp_remap_cell_for_style(pipe->remap.style, remap_cell_size);
    pipe->remap.strength = clamp_remap_strength_for_style(pipe->remap.style, remap_strength);
    pipe->base.enabled = lomo_enabled;
    pipe->base.preset = lomo_preset;
    pipe->base.strength = clamp_strength(lomo_strength);
    pipe->bend.enabled = bend_enabled;
    pipe->bend.preset = bend_preset;
    pipe->bend.strength = clamp_strength(bend_strength);
    pipe->post.enabled = post_fx_mode != FX_NONE;
    pipe->post.fx_mode = post_fx_mode;
    pipe->post.fx_intensity = post_fx_intensity;
}

void pipeline_build_recipe(EffectRecipe *out, const EffectPipeline *pipe) {
    out->use_base_look = pipe->base.enabled;
    out->lomo_preset = pipe->base.preset;
    out->lomo_strength = clamp_strength(pipe->base.strength);
    out->use_gb = pipe->gb.enabled;
    out->gb_params = pipe->gb.params;
    out->use_remap = pipe->remap.enabled;
    out->remap_style = clamp_remap_style(pipe->remap.style);
    out->remap_cell_size = clamp_remap_cell_for_style(out->remap_style, pipe->remap.cell_size);
    out->remap_strength = clamp_remap_strength_for_style(out->remap_style, pipe->remap.strength);
    out->use_bend = pipe->bend.enabled;
    out->bend_preset = pipe->bend.preset;
    out->bend_strength = clamp_strength(pipe->bend.strength);
    out->use_post_fx = pipe->post.enabled;
    out->post_fx_mode = pipe->post.fx_mode;
    out->post_fx_intensity = pipe->post.fx_intensity;
    if (pipe->base.enabled && pipe->base.strength > 0) {
        out->fallback_post_fx_mode = lomo_presets[pipe->base.preset].fx_mode;
        out->fallback_post_fx_intensity =
            (lomo_presets[pipe->base.preset].fx_intensity * out->lomo_strength + 5) / 10;
    } else {
        out->fallback_post_fx_mode = FX_NONE;
        out->fallback_post_fx_intensity = 0;
    }
}

bool pipeline_recipe_has_effects(const EffectRecipe *recipe) {
    return recipe &&
           ((recipe->use_base_look && recipe->lomo_strength > 0) || recipe->use_gb ||
            has_basic_adjustments(&recipe->gb_params) ||
            (recipe->use_remap && recipe->remap_strength > 0) ||
            (recipe->use_bend && recipe->bend_strength > 0) || recipe->use_post_fx ||
            recipe->fallback_post_fx_mode != FX_NONE);
}

void pipeline_apply(uint8_t *rgb, int w, int h,
                    const EffectRecipe *recipe,
                    int frame_count) {
    if (!pipeline_recipe_has_effects(recipe)) return;

    if (recipe->use_base_look && recipe->lomo_strength > 0) {
        const LomoPreset *lp = &lomo_presets[recipe->lomo_preset];
        apply_basic_adjustments(rgb, w, h,
                                strength_mix(1.0f, lp->brightness, recipe->lomo_strength),
                                strength_mix(1.0f, lp->contrast, recipe->lomo_strength),
                                strength_mix(1.0f, lp->gamma, recipe->lomo_strength),
                                strength_mix(1.0f, lp->saturation, recipe->lomo_strength));
    }

    if (!recipe->use_gb && has_basic_adjustments(&recipe->gb_params)) {
        apply_basic_adjustments(rgb, w, h,
                                recipe->gb_params.brightness,
                                recipe->gb_params.contrast,
                                recipe->gb_params.gamma,
                                recipe->gb_params.saturation);
    }

    if (recipe->use_gb) {
        apply_gameboy_filter(rgb, w, h, recipe->gb_params);
    }

    if (recipe->use_remap && recipe->remap_strength > 0) {
        apply_remap(rgb, w, h, recipe->remap_style, recipe->remap_cell_size,
                    recipe->remap_strength);
    }

    if (recipe->use_bend && recipe->bend_strength > 0) {
        apply_bend(rgb, w, h, recipe->bend_preset, frame_count, recipe->bend_strength);
    }

    if (recipe->use_post_fx) {
        FilterParams post = recipe->gb_params;
        post.fx_mode = recipe->post_fx_mode;
        post.fx_intensity = recipe->post_fx_intensity;
        apply_fx(rgb, w, h, post, frame_count);
    } else if (recipe->fallback_post_fx_mode != FX_NONE) {
        FilterParams post = recipe->gb_params;
        post.fx_mode = recipe->fallback_post_fx_mode;
        post.fx_intensity = recipe->fallback_post_fx_intensity;
        apply_fx(rgb, w, h, post, frame_count);
    }
}

void pipeline_preset_default(PipelinePreset *preset, int slot) {
    FilterParams defaults = FILTER_DEFAULTS;
    memset(preset, 0, sizeof(*preset));
    snprintf(preset->name, sizeof(preset->name), "Empty Slot %d", slot + 1);
    preset->gb_enabled = false;
    preset->gb_params = defaults;
    preset->remap_enabled = false;
    preset->remap_style = REMAP_STYLE_ASCII;
    preset->remap_cell_size = 8;
    preset->remap_strength = 10;
    preset->base_strength = 10;
    preset->bend_strength = 10;
}

void pipeline_preset_capture(PipelinePreset *preset, const EffectPipeline *pipe,
                             const char *name) {
    memset(preset, 0, sizeof(*preset));
    char generated[24] = {0};
    bool first = true;
    if (pipe->gb.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%sGB", first ? "" : "+");
        first = false;
    }
    if (pipe->base.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", lomo_presets[pipe->base.preset].name);
        first = false;
    }
    if (pipe->remap.enabled) {
        static const char *remap_names[REMAP_STYLE_COUNT] = {
            "ASCII", "Toon", "Color", "Pink", "CCD", "Matrix"
        };
        int style = clamp_remap_style(pipe->remap.style);
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", remap_names[style]);
        first = false;
    }
    if (pipe->bend.enabled) {
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", bend_presets[pipe->bend.preset].name);
        first = false;
    }
    if (pipe->post.enabled) {
        static const char *fx_names[] = {
            "None", "ScanH", "ScanV", "LCD", "Vignette", "Chroma", "Grain"
        };
        const char *fx_name = (pipe->post.fx_mode >= 0 && pipe->post.fx_mode <= 6)
                            ? fx_names[pipe->post.fx_mode] : "FX";
        snprintf(generated + strlen(generated), sizeof(generated) - strlen(generated),
                 "%s%s", first ? "" : "+", fx_name);
        first = false;
    }
    if (first) snprintf(generated, sizeof(generated), "Raw");

    if (name && name[0] && strncmp(name, "Empty Slot", 10) != 0)
        snprintf(preset->name, sizeof(preset->name), "%s", name);
    else
        snprintf(preset->name, sizeof(preset->name), "%s", generated);
    preset->gb_enabled = pipe->gb.enabled;
    preset->gb_params = pipe->gb.params;
    preset->remap_enabled = pipe->remap.enabled;
    preset->remap_style = clamp_remap_style(pipe->remap.style);
    preset->remap_cell_size = clamp_remap_cell_for_style(preset->remap_style, pipe->remap.cell_size);
    preset->remap_strength = clamp_remap_strength_for_style(preset->remap_style, pipe->remap.strength);
    preset->base_enabled = pipe->base.enabled;
    preset->base_preset = pipe->base.preset;
    preset->base_strength = clamp_strength(pipe->base.strength);
    preset->bend_enabled = pipe->bend.enabled;
    preset->bend_preset = pipe->bend.preset;
    preset->bend_strength = clamp_strength(pipe->bend.strength);
    preset->fx_mode = pipe->post.enabled ? pipe->post.fx_mode : FX_NONE;
    preset->fx_intensity = pipe->post.fx_intensity;
}

void pipeline_preset_apply(EffectPipeline *pipe, const PipelinePreset *preset) {
    pipe->gb.enabled = preset->gb_enabled;
    pipe->gb.params = preset->gb_params;
    pipe->remap.enabled = preset->remap_enabled;
    pipe->remap.style = clamp_remap_style(preset->remap_style);
    pipe->remap.cell_size = clamp_remap_cell_for_style(pipe->remap.style, preset->remap_cell_size);
    pipe->remap.strength = clamp_remap_strength_for_style(pipe->remap.style, preset->remap_strength);
    pipe->base.enabled = preset->base_enabled;
    pipe->base.preset = preset->base_preset;
    pipe->base.strength = clamp_strength(preset->base_strength);
    pipe->bend.enabled = preset->bend_enabled;
    pipe->bend.preset = preset->bend_preset;
    pipe->bend.strength = clamp_strength(preset->bend_strength);
    pipe->post.enabled = preset->fx_mode != FX_NONE;
    pipe->post.fx_mode = preset->fx_mode;
    pipe->post.fx_intensity = preset->fx_intensity;
}
