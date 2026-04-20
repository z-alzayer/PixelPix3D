// bend.c — Circuit Bend glitch effects
// Psychedelic colour corruption that simulates broken hardware,
// JPEG overflow artefacts, and datamoshing.

#include "bend.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Corrupt — per-scanline channel bit-rotation
// Simulates reading pixel memory at the wrong byte alignment.
// ---------------------------------------------------------------------------
static void bend_corrupt(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    for (int y = 0; y < h; y++) {
        int shift_r = (y % 5) + 1;
        int shift_g = ((y + 2) % 4) + 1;
        int shift_b = ((y + 4) % 6) + 1;
        uint8_t *row = rgb + y * w * 3;
        for (int x = 0; x < w; x++) {
            uint8_t r = row[x*3+0];
            uint8_t g = row[x*3+1];
            uint8_t b = row[x*3+2];
            row[x*3+0] = (r << shift_r) | (r >> (8 - shift_r));
            row[x*3+1] = (g >> shift_g) | (g << (8 - shift_g));
            row[x*3+2] = (b << shift_b) | (b >> (8 - shift_b));
        }
    }
}

// ---------------------------------------------------------------------------
// Overflow — unsigned wrapping addition with per-channel LCG offsets.
// Simulates a broken DAC whose output register overflows, wrapping
// around to produce sudden neon jumps from what should be smooth tones.
// ---------------------------------------------------------------------------
static void bend_overflow(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    for (int y = 0; y < h; y++) {
        // Gentler bias — drifts slowly across scanlines so the top of the
        // image looks near-normal and corruption builds toward the bottom.
        uint8_t bias_r = (uint8_t)((y * 3 + 17) & 0xFF);
        uint8_t bias_g = (uint8_t)((y * 5 + 53) & 0xFF);
        uint8_t bias_b = (uint8_t)((y * 7 + 97) & 0xFF);
        uint8_t *row = rgb + y * w * 3;
        for (int x = 0; x < w; x++) {
            // Wrapping add — overflow creates colour jumps
            row[x*3+0] = (uint8_t)(row[x*3+0] + bias_r);
            row[x*3+1] = (uint8_t)(row[x*3+1] + bias_g);
            row[x*3+2] = (uint8_t)(row[x*3+2] + bias_b);
        }
    }
}

// ---------------------------------------------------------------------------
// Byteshift — treat the RGB buffer as raw bytes and shift the read pointer
// per-row, so channels bleed into each other and pixel boundaries misalign.
// Like reading a framebuffer at the wrong memory offset.
// ---------------------------------------------------------------------------
static uint8_t s_byteshift_tmp[400 * 3];  // one row scratch

static void bend_byteshift(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    int row_bytes = w * 3;
    // Byte offset grows with each scanline — top rows are nearly correct,
    // bottom rows are wildly misaligned with XOR colour contamination.
    for (int y = 0; y < h; y++) {
        int shift = (y * y / 60) % row_bytes;  // quadratic growth, wrapping
        uint8_t *row = rgb + y * row_bytes;
        memcpy(s_byteshift_tmp, row, row_bytes);
        for (int i = 0; i < row_bytes; i++) {
            uint8_t src = s_byteshift_tmp[(i + shift) % row_bytes];
            row[i] = src ^ (uint8_t)(y & 0xFF);
        }
    }
}

// ---------------------------------------------------------------------------
// Solarize — harsh multi-stage XOR/overflow corruption.
// Takes the solarization concept and pushes it into pure chaos:
// wrapping math, channel cross-contamination, and bit manipulation.
// ---------------------------------------------------------------------------
static void bend_solarize(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    int npix = w * h;
    for (int i = 0; i < npix; i++) {
        uint8_t r = rgb[i*3+0], g = rgb[i*3+1], b = rgb[i*3+2];
        // Fold channels through midpoint (solarize base)
        if (r > 128) r = 255 - r;
        if (g > 128) g = 255 - g;
        if (b > 128) b = 255 - b;
        // Aggressive cross-channel XOR contamination
        r ^= g;
        b ^= (uint8_t)((r + g) & 0xFF);
        g ^= (uint8_t)((r ^ b) & 0xFF);
        // Scale up to reclaim full dynamic range — wrapping intentional
        r = (uint8_t)(r * 3);
        g = (uint8_t)(g * 3);
        b = (uint8_t)(b * 3);
        // One more XOR pass for extra shatter
        r ^= (b >> 1);
        g ^= (r >> 1);
        b ^= (g >> 1);
        rgb[i*3+0] = r;
        rgb[i*3+1] = g;
        rgb[i*3+2] = b;
    }
}

// ---------------------------------------------------------------------------
// Scramble — reinterpret pixel data as if the bus width is wrong.
// Reads RGB triplets at a stride that doesn't align with pixel boundaries,
// producing colour-shifted ghost images layered on top of each other.
// ---------------------------------------------------------------------------
static void bend_scramble(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    int total = w * h * 3;
    // Use a stride that is coprime with 3 to maximise channel misalignment.
    // stride=7 means: pixel N's red comes from byte 7*N, which wraps through
    // R/G/B of completely different pixels.
    int stride = 7;
    for (int i = 0; i < total; i += 3) {
        int src = (i * stride) % total;
        // Don't just copy — also add the original so the image is still
        // recognisable but with wild colour overlay
        uint8_t r0 = rgb[i+0], g0 = rgb[i+1], b0 = rgb[i+2];
        rgb[i+0] = (uint8_t)(r0 + rgb[(src + 0) % total]);
        rgb[i+1] = (uint8_t)(g0 + rgb[(src + 1) % total]);
        rgb[i+2] = (uint8_t)(b0 + rgb[(src + 2) % total]);
    }
}

// ---------------------------------------------------------------------------
// Acid — multiply channels together and use wrapping to create extreme
// psychedelic rainbows. Like a broken colour LUT where every entry is wrong.
// ---------------------------------------------------------------------------
static void bend_acid(uint8_t *rgb, int w, int h, int frame)
{
    (void)frame;
    int npix = w * h;
    for (int i = 0; i < npix; i++) {
        uint8_t r = rgb[i*3+0], g = rgb[i*3+1], b = rgb[i*3+2];
        // Multiply channels together — wrapping creates harsh neon
        uint8_t nr = (uint8_t)((r * g) ^ b);
        uint8_t ng = (uint8_t)((g * b) ^ r);
        uint8_t nb = (uint8_t)((b * r) ^ g);
        // Bit-reverse for extra alien colours
        // (swap nibbles: 0xAB → 0xBA)
        nr = (nr >> 4) | (nr << 4);
        ng = (ng >> 4) | (ng << 4);
        nb = (nb >> 4) | (nb << 4);
        rgb[i*3+0] = nr;
        rgb[i*3+1] = ng;
        rgb[i*3+2] = nb;
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void apply_bend(uint8_t *rgb, int w, int h, int preset_id, int frame_count)
{
    switch (preset_id) {
    case BEND_CORRUPT:   bend_corrupt(rgb, w, h, frame_count);   break;
    case BEND_MELT:      bend_overflow(rgb, w, h, frame_count);  break;
    case BEND_SWAP:      bend_byteshift(rgb, w, h, frame_count); break;
    case BEND_SOLARIZE:  bend_solarize(rgb, w, h, frame_count);  break;
    case BEND_FEEDBACK:  bend_scramble(rgb, w, h, frame_count);  break;
    case BEND_POSTERIZE: bend_acid(rgb, w, h, frame_count);      break;
    default: break;
    }
}
