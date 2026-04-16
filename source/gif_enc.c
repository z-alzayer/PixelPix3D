// gif_enc.c — GIF89a encoder
// Uniform 6x6x6 palette quantization (O(1) per pixel) + LZW compression.
// Fast enough for real-time use on 3DS ARM11.

#include "gif_enc.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Palette: 216-entry 6x6x6 uniform RGB cube + 40 greyscale entries = 256
// Each channel is quantized to one of {0,51,102,153,204,255} (6 levels).
// ---------------------------------------------------------------------------

#define PAL_SIZE 256

// Build the palette once into a static array.
// Indices 0-215: colour cube r*36 + g*6 + b  (r,g,b in 0..5)
// Indices 216-255: greyscale steps 0..255 (every ~6.5 steps, filling the rest)
static uint8_t s_palette[PAL_SIZE * 3];
static bool    s_palette_built = false;

static void build_palette(void)
{
    if (s_palette_built) return;
    // 6x6x6 colour cube
    static const uint8_t lev[6] = {0, 51, 102, 153, 204, 255};
    int idx = 0;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                s_palette[idx*3+0] = lev[r];
                s_palette[idx*3+1] = lev[g];
                s_palette[idx*3+2] = lev[b];
                idx++;
            }
    // Fill remaining 40 entries with evenly-spaced greyscale
    for (int i = 0; i < 40; i++) {
        uint8_t v = (uint8_t)(i * 255 / 39);
        s_palette[idx*3+0] = v;
        s_palette[idx*3+1] = v;
        s_palette[idx*3+2] = v;
        idx++;
    }
    s_palette_built = true;
}

// 4×4 Bayer ordered dither matrix, values 0..15 scaled to 0..50 (half a palette step).
// Applied per-channel before snapping to the nearest cube level.
static const int8_t s_bayer4[16] = {
    -25, 13, -19,  19,
     19,-13,  25, -19,
    -13, 25, -19,  13,
     25,-25,  19, -13
};

// Map a single RGB888 pixel to its palette index using Bayer dithering.
static inline uint8_t quantize_pixel_dither(uint8_t r, uint8_t g, uint8_t b, int dither)
{
    int rv = (int)r + dither; if (rv < 0) rv = 0; if (rv > 255) rv = 255;
    int gv = (int)g + dither; if (gv < 0) gv = 0; if (gv > 255) gv = 255;
    int bv = (int)b + dither; if (bv < 0) bv = 0; if (bv > 255) bv = 255;
    int ri = rv / 51; if (ri > 5) ri = 5;
    int gi = gv / 51; if (gi > 5) gi = 5;
    int bi = bv / 51; if (bi > 5) bi = 5;
    return (uint8_t)(ri * 36 + gi * 6 + bi);
}

// Map pixels to palette indices with Bayer dithering, stored in s_indexed.
static uint8_t s_indexed[400 * 240];

static void quantize_frame(const uint8_t *rgb, int n_pixels, int width)
{
    for (int i = 0; i < n_pixels; i++) {
        int x = i % width;
        int y = i / width;
        int dither = s_bayer4[(y & 3) * 4 + (x & 3)];
        s_indexed[i] = quantize_pixel_dither(rgb[i*3+0], rgb[i*3+1], rgb[i*3+2], dither);
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
    if (width * height > 400 * 240) return 0;

    build_palette();

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

        // Quantize to global palette with Bayer dithering
        quantize_frame(frames_rgb888[f], n_pixels, width);

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
