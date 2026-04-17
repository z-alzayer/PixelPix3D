// gif_enc.c — GIF89a encoder
// Median-cut palette quantization + LZW compression.
// Fast enough for real-time use on 3DS ARM11.

#include "gif_enc.h"
#include "camera.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Median-cut quantization — builds an optimal 256-colour palette from the
// actual pixel data.  Uses a 15-bit colour space (5 bits/channel = 32768
// buckets) so the working set stays small (~130KB static).
// ---------------------------------------------------------------------------

#define PAL_SIZE  256
#define HIST_SIZE 32768  // 2^15

// Histogram: count of pixels per 15-bit colour (R5G5B5).
static uint16_t s_hist[HIST_SIZE];

// Lookup table: 15-bit colour → palette index (filled after median-cut).
static uint8_t s_lut[HIST_SIZE];

// The palette itself (256 RGB triplets).
static uint8_t s_palette[PAL_SIZE * 3];

// Convert RGB888 to a 15-bit index.
static inline int rgb_to_15(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

// A "box" in colour space for median-cut.
typedef struct {
    int rmin, rmax, gmin, gmax, bmin, bmax;
    int count;  // total pixel count in this box
} MCBox;

// Compute the bounds and pixel count of a box by scanning the histogram.
static void box_shrink(MCBox *box)
{
    int rmin = 31, rmax = 0, gmin = 31, gmax = 0, bmin = 31, bmax = 0;
    int count = 0;
    for (int r = box->rmin; r <= box->rmax; r++)
        for (int g = box->gmin; g <= box->gmax; g++)
            for (int b = box->bmin; b <= box->bmax; b++) {
                int c = s_hist[(r << 10) | (g << 5) | b];
                if (c) {
                    if (r < rmin) rmin = r;
                    if (r > rmax) rmax = r;
                    if (g < gmin) gmin = g;
                    if (g > gmax) gmax = g;
                    if (b < bmin) bmin = b;
                    if (b > bmax) bmax = b;
                    count += c;
                }
            }
    box->rmin = rmin; box->rmax = rmax;
    box->gmin = gmin; box->gmax = gmax;
    box->bmin = bmin; box->bmax = bmax;
    box->count = count;
}

// Split a box along its longest axis at the median.
// Returns true if split succeeded (box had >1 distinct colour).
static bool box_split(MCBox *src, MCBox *dst)
{
    int rdim = src->rmax - src->rmin;
    int gdim = src->gmax - src->gmin;
    int bdim = src->bmax - src->bmin;
    if (rdim == 0 && gdim == 0 && bdim == 0) return false;

    // Choose longest axis
    int axis; // 0=R, 1=G, 2=B
    if (rdim >= gdim && rdim >= bdim) axis = 0;
    else if (gdim >= bdim)            axis = 1;
    else                              axis = 2;

    // Accumulate counts along the chosen axis to find the median split point
    int total = src->count;
    int half  = total / 2;
    int accum = 0;
    int split = 0;

    int lo, hi;
    if (axis == 0) { lo = src->rmin; hi = src->rmax; }
    else if (axis == 1) { lo = src->gmin; hi = src->gmax; }
    else { lo = src->bmin; hi = src->bmax; }

    for (int v = lo; v <= hi; v++) {
        int slice_count = 0;
        if (axis == 0) {
            for (int g = src->gmin; g <= src->gmax; g++)
                for (int b = src->bmin; b <= src->bmax; b++)
                    slice_count += s_hist[(v << 10) | (g << 5) | b];
        } else if (axis == 1) {
            for (int r = src->rmin; r <= src->rmax; r++)
                for (int b = src->bmin; b <= src->bmax; b++)
                    slice_count += s_hist[(r << 10) | (v << 5) | b];
        } else {
            for (int r = src->rmin; r <= src->rmax; r++)
                for (int g = src->gmin; g <= src->gmax; g++)
                    slice_count += s_hist[(r << 10) | (g << 5) | v];
        }
        accum += slice_count;
        if (accum >= half) { split = v; break; }
    }

    // Ensure we don't produce an empty box
    if (split >= hi) split = hi - 1;

    // Create the two child boxes
    *dst = *src;
    if (axis == 0) { src->rmax = split; dst->rmin = split + 1; }
    else if (axis == 1) { src->gmax = split; dst->gmin = split + 1; }
    else { src->bmax = split; dst->bmin = split + 1; }

    box_shrink(src);
    box_shrink(dst);
    return dst->count > 0;
}

// Compute the average colour of a box → palette entry, and fill the LUT.
static void box_to_palette(const MCBox *box, int pal_idx)
{
    long rsum = 0, gsum = 0, bsum = 0, total = 0;
    for (int r = box->rmin; r <= box->rmax; r++)
        for (int g = box->gmin; g <= box->gmax; g++)
            for (int b = box->bmin; b <= box->bmax; b++) {
                int c = s_hist[(r << 10) | (g << 5) | b];
                if (c) {
                    // Use centre of each 5-bit bucket (v*8+4) for accuracy
                    rsum += (long)c * (r * 8 + 4);
                    gsum += (long)c * (g * 8 + 4);
                    bsum += (long)c * (b * 8 + 4);
                    total += c;
                }
            }
    if (total == 0) total = 1;
    s_palette[pal_idx*3+0] = (uint8_t)(rsum / total);
    s_palette[pal_idx*3+1] = (uint8_t)(gsum / total);
    s_palette[pal_idx*3+2] = (uint8_t)(bsum / total);

    // Fill LUT for all 15-bit colours in this box
    for (int r = box->rmin; r <= box->rmax; r++)
        for (int g = box->gmin; g <= box->gmax; g++)
            for (int b = box->bmin; b <= box->bmax; b++)
                s_lut[(r << 10) | (g << 5) | b] = (uint8_t)pal_idx;
}

// Static box array — avoids stack usage on 3DS save thread
static MCBox s_boxes[PAL_SIZE];

// Build palette from pixel data across all frames via median-cut.
static void build_palette(const uint8_t * const *frames, int n_frames,
                          int n_pixels)
{
    // Build histogram
    memset(s_hist, 0, sizeof(s_hist));
    for (int f = 0; f < n_frames; f++) {
        const uint8_t *px = frames[f];
        for (int i = 0; i < n_pixels; i++) {
            int idx15 = rgb_to_15(px[i*3], px[i*3+1], px[i*3+2]);
            if (s_hist[idx15] < 0xFFFF) s_hist[idx15]++;
        }
    }

    // Start with one box covering the full colour space
    s_boxes[0] = (MCBox){0, 31, 0, 31, 0, 31, 0};
    box_shrink(&s_boxes[0]);
    int n_boxes = 1;

    // Split boxes until we have 256 (or can't split any more)
    while (n_boxes < PAL_SIZE) {
        // Find the box with the largest count that can be split
        int best = -1, best_count = 0;
        for (int i = 0; i < n_boxes; i++) {
            int rdim = s_boxes[i].rmax - s_boxes[i].rmin;
            int gdim = s_boxes[i].gmax - s_boxes[i].gmin;
            int bdim = s_boxes[i].bmax - s_boxes[i].bmin;
            if ((rdim > 0 || gdim > 0 || bdim > 0) && s_boxes[i].count > best_count) {
                best = i;
                best_count = s_boxes[i].count;
            }
        }
        if (best < 0) break; // no splittable boxes left

        MCBox new_box;
        if (box_split(&s_boxes[best], &new_box)) {
            s_boxes[n_boxes++] = new_box;
        } else {
            break;
        }
    }

    // Convert each box to a palette entry + fill LUT
    for (int i = 0; i < n_boxes; i++)
        box_to_palette(&s_boxes[i], i);

    // Fill any unused palette entries (if <256 distinct colours)
    for (int i = n_boxes; i < PAL_SIZE; i++) {
        s_palette[i*3+0] = 0;
        s_palette[i*3+1] = 0;
        s_palette[i*3+2] = 0;
    }
}

// Map pixels to palette indices using the 15-bit LUT.
static uint8_t s_indexed[VGA_WIDTH * VGA_HEIGHT];

static void quantize_frame(const uint8_t *rgb, int n_pixels)
{
    for (int i = 0; i < n_pixels; i++) {
        s_indexed[i] = s_lut[rgb_to_15(rgb[i*3], rgb[i*3+1], rgb[i*3+2])];
    }
}

// ---------------------------------------------------------------------------
// LZW encoder — standard GIF LZW with variable-length codes (up to 12 bits)
// ---------------------------------------------------------------------------

#define LZW_BITS  12
#define LZW_HSIZE 5003

typedef struct {
    uint8_t *out;
    size_t   out_cap;
    size_t   out_pos;
    unsigned long accum;
    int           a_bits;
    uint8_t  block[256];
    int      b_count;
    int      htab[LZW_HSIZE];
    int      codetab[LZW_HSIZE];
} LZWState;

static void lzw_put_byte(LZWState *lzw, uint8_t v)
{
    if (lzw->out_pos < lzw->out_cap) lzw->out[lzw->out_pos++] = v;
}

static void lzw_flush_block(LZWState *lzw)
{
    if (lzw->b_count > 0) {
        lzw_put_byte(lzw, (uint8_t)lzw->b_count);
        for (int i = 0; i < lzw->b_count; i++)
            lzw_put_byte(lzw, lzw->block[i]);
        lzw->b_count = 0;
    }
}

static void lzw_char_out(LZWState *lzw, uint8_t c)
{
    lzw->block[lzw->b_count++] = c;
    if (lzw->b_count >= 254) lzw_flush_block(lzw);
}

static void lzw_output(LZWState *lzw, int code, int n_bits)
{
    lzw->accum |= ((unsigned long)code) << lzw->a_bits;
    lzw->a_bits += n_bits;
    while (lzw->a_bits >= 8) {
        lzw_char_out(lzw, (uint8_t)(lzw->accum & 0xFF));
        lzw->accum >>= 8;
        lzw->a_bits -= 8;
    }
}

static void lzw_flush_bits(LZWState *lzw)
{
    while (lzw->a_bits > 0) {
        lzw_char_out(lzw, (uint8_t)(lzw->accum & 0xFF));
        lzw->accum >>= 8;
        lzw->a_bits -= 8;
    }
    lzw->a_bits = 0;
}

static void lzw_encode(LZWState *lzw, const uint8_t *pixels, int n_pixels, int min_code_size)
{
    int n_bits     = min_code_size + 1;
    int maxcode    = 1 << n_bits;
    int clear_code = 1 << min_code_size;
    int eoi_code   = clear_code + 1;
    int free_entry = eoi_code + 1;

    lzw->accum   = 0;
    lzw->a_bits  = 0;
    lzw->b_count = 0;
    for (int i = 0; i < LZW_HSIZE; i++) lzw->htab[i] = -1;

    lzw_put_byte(lzw, (uint8_t)min_code_size);
    lzw_output(lzw, clear_code, n_bits);

    int ent = pixels[0];
    for (int i = 1; i < n_pixels; i++) {
        int c     = pixels[i];
        int fcode = (c << LZW_BITS) + ent;
        int hpos  = (c << (LZW_BITS - 8)) ^ ent;

        bool found = false;
        if (lzw->htab[hpos] == fcode) {
            ent = lzw->codetab[hpos];
            found = true;
        } else if (lzw->htab[hpos] >= 0) {
            int disp = (hpos == 0) ? 1 : LZW_HSIZE - hpos;
            do {
                hpos -= disp;
                if (hpos < 0) hpos += LZW_HSIZE;
                if (lzw->htab[hpos] == fcode) {
                    ent = lzw->codetab[hpos];
                    found = true;
                    break;
                }
            } while (lzw->htab[hpos] >= 0);
        }

        if (!found) {
            lzw_output(lzw, ent, n_bits);
            ent = c;
            if (free_entry < (1 << LZW_BITS)) {
                lzw->codetab[hpos] = free_entry++;
                lzw->htab[hpos]    = fcode;
                if (free_entry > maxcode) {
                    n_bits++;
                    if (n_bits > LZW_BITS) { n_bits = LZW_BITS; maxcode = 1 << LZW_BITS; }
                    else maxcode = 1 << n_bits;
                }
            } else {
                // Emit clear code at the CURRENT bit width so the decoder can
                // read it correctly before it resets its own bit width.
                lzw_output(lzw, clear_code, n_bits);
                for (int j = 0; j < LZW_HSIZE; j++) lzw->htab[j] = -1;
                free_entry = eoi_code + 1;
                n_bits     = min_code_size + 1;
                maxcode    = 1 << n_bits;
            }
        }
    }
    lzw_output(lzw, ent, n_bits);
    lzw_output(lzw, eoi_code, n_bits);
    lzw_flush_bits(lzw);
    lzw_flush_block(lzw);
    lzw_put_byte(lzw, 0); // block terminator
}

// ---------------------------------------------------------------------------
// GIF89a output helpers
// ---------------------------------------------------------------------------

typedef struct { uint8_t *buf; size_t cap; size_t pos; } GIFOut;

static void gif_put(GIFOut *g, uint8_t v)
    { if (g->pos < g->cap) g->buf[g->pos++] = v; }
static void gif_put_le16(GIFOut *g, uint16_t v)
    { gif_put(g, v & 0xFF); gif_put(g, (v >> 8) & 0xFF); }
static void gif_write(GIFOut *g, const void *data, size_t n)
    { if (g->pos + n <= g->cap) { memcpy(g->buf + g->pos, data, n); g->pos += n; } }

// ---------------------------------------------------------------------------
// gif_encode — public entry point
// ---------------------------------------------------------------------------

// Static to avoid stack overflow on 3DS (save thread has only 32KB stack).
static LZWState s_lzw; // ~41KB — must not be on the stack

size_t gif_encode(uint8_t *buf, size_t buf_cap,
                  const uint8_t * const *frames_rgb888, int n_frames,
                  int width, int height, int delay_ms)
{
    if (!buf || buf_cap == 0 || n_frames < 1 || !frames_rgb888) return 0;
    if (width * height > VGA_WIDTH * VGA_HEIGHT) return 0;

    build_palette(frames_rgb888, n_frames, width * height);

    GIFOut g = { buf, buf_cap, 0 };

    // GIF header with global colour table (shared across all frames)
    gif_write(&g, "GIF89a", 6);
    gif_put_le16(&g, (uint16_t)width);
    gif_put_le16(&g, (uint16_t)height);
    // packed: global CT flag=1, colour res=7, no sort, gct_size=7 (256 entries)
    gif_put(&g, 0xF7);
    gif_put(&g, 0x00); // background colour index
    gif_put(&g, 0x00); // pixel aspect ratio

    // Global Colour Table
    gif_write(&g, s_palette, PAL_SIZE * 3);

    // Netscape loop extension (infinite loop)
    gif_put(&g, 0x21); gif_put(&g, 0xFF); gif_put(&g, 0x0B);
    gif_write(&g, "NETSCAPE2.0", 11);
    gif_put(&g, 0x03); gif_put(&g, 0x01);
    gif_put_le16(&g, 0x0000);
    gif_put(&g, 0x00);

    int delay_hundredths = delay_ms / 10;
    if (delay_hundredths < 1) delay_hundredths = 1;

    for (int f = 0; f < n_frames; f++) {
        int n_pixels = width * height;

        // Quantize to global palette
        quantize_frame(frames_rgb888[f], n_pixels);

        // Graphic Control Extension
        gif_put(&g, 0x21); gif_put(&g, 0xF9); gif_put(&g, 0x04);
        gif_put(&g, 0x00);
        gif_put_le16(&g, (uint16_t)delay_hundredths);
        gif_put(&g, 0x00);
        gif_put(&g, 0x00);

        // Image Descriptor — no local colour table, use global
        gif_put(&g, 0x2C);
        gif_put_le16(&g, 0); gif_put_le16(&g, 0);
        gif_put_le16(&g, (uint16_t)width);
        gif_put_le16(&g, (uint16_t)height);
        gif_put(&g, 0x00); // no local CT

        // LZW image data
        s_lzw.out     = buf;
        s_lzw.out_cap = buf_cap;
        s_lzw.out_pos = g.pos;
        lzw_encode(&s_lzw, s_indexed, n_pixels, 8);
        g.pos = s_lzw.out_pos;
    }

    gif_put(&g, 0x3B); // GIF trailer
    return g.pos;
}
