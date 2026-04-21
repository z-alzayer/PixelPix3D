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
    uint32_t rng = (uint32_t)(frame * 1597334677u + 97);
    for (int y = 0; y < h; y++) {
        int shift_r = (y % 5) + 1;
        int shift_g = ((y + 2) % 4) + 1;
        int shift_b = ((y + 4) % 6) + 1;
        uint8_t *row = rgb + y * w * 3;
        for (int x = 0; x < w; x++) {
            uint8_t r = row[x*3+0];
            uint8_t g = row[x*3+1];
            uint8_t b = row[x*3+2];
            // Rotated versions
            uint8_t cr = (r << shift_r) | (r >> (8 - shift_r));
            uint8_t cg = (g >> shift_g) | (g << (8 - shift_g));
            uint8_t cb = (b << shift_b) | (b >> (8 - shift_b));
            // Blend ~55% corrupted with original to soften
            int mr = r + (((cr - r) * 140) >> 8);
            int mg = g + (((cg - g) * 140) >> 8);
            int mb = b + (((cb - b) * 140) >> 8);
            // Random colour pop — pick a channel to boost based on
            // which original channel is strongest
            rng = rng * 1664525u + 1013904223u;
            int boost = 20 + ((rng >> 12) & 0x1F); // 20..51
            if (r >= g && r >= b)      { mr += boost; mg += boost >> 3; }
            else if (g >= r && g >= b) { mg += boost; mb += boost >> 3; }
            else                       { mb += boost; mr += boost >> 3; }
            row[x*3+0] = (uint8_t)(mr & 0xFF);
            row[x*3+1] = (uint8_t)(mg & 0xFF);
            row[x*3+2] = (uint8_t)(mb & 0xFF);
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
    // LCG state — seeded per-frame so the pattern varies across captures
    uint32_t rng = (uint32_t)(frame * 2654435761u + 1);
    for (int y = 0; y < h; y++) {
        // Slow drift across scanlines — top is mild, bottom is wild
        int base_r = (y * 3 + 17) & 0xFF;
        int base_g = (y * 5 + 53) & 0xFF;
        int base_b = (y * 7 + 97) & 0xFF;
        uint8_t *row = rgb + y * w * 3;
        for (int x = 0; x < w; x++) {
            // LCG step — cheap pseudo-random jitter per pixel
            rng = rng * 1664525u + 1013904223u;
            int jitter = (int)((rng >> 16) & 0x3F) - 0x20; // -32..+31

            // Second scramble for independent per-channel spread
            uint32_t r2 = rng * 2891336453u + 1;
            int ch_spread = (int)((r2 >> 16) & 0x1F);  // 0..31

            uint8_t bias_r = (uint8_t)((base_r + jitter) & 0xFF);
            uint8_t bias_g = (uint8_t)((base_g + jitter + ch_spread) & 0xFF);
            uint8_t bias_b = (uint8_t)((base_b + jitter - ch_spread) & 0xFF);

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
    int row_bytes = w * 3;
    uint32_t rng = (uint32_t)(frame * 3266489917u + 13);
    for (int y = 0; y < h; y++) {
        int shift = (y * y / 60) % row_bytes;  // quadratic growth, wrapping
        uint8_t *row = rgb + y * row_bytes;
        memcpy(s_byteshift_tmp, row, row_bytes);
        for (int i = 0; i < row_bytes; i += 3) {
            int s0 = (i + shift) % row_bytes;
            uint8_t src_r = s_byteshift_tmp[s0];
            uint8_t src_g = s_byteshift_tmp[(s0 + 1) % row_bytes];
            uint8_t src_b = s_byteshift_tmp[(s0 + 2) % row_bytes];
            uint8_t orig_r = s_byteshift_tmp[i];
            uint8_t orig_g = s_byteshift_tmp[i + 1];
            uint8_t orig_b = s_byteshift_tmp[i + 2];
            // Softer XOR — blend shifted with original (~50/50) then
            // apply a mild XOR instead of the full-strength y mask
            uint8_t xmask = (uint8_t)((y >> 1) & 0x7F); // half strength
            int mr = ((int)orig_r + (int)src_r) >> 1;
            int mg = ((int)orig_g + (int)src_g) >> 1;
            int mb = ((int)orig_b + (int)src_b) >> 1;
            mr ^= xmask;
            mg ^= xmask;
            mb ^= xmask;
            // Random vibrant tint — varies per pixel
            rng = rng * 1664525u + 1013904223u;
            int sel = (rng >> 16) % 3;
            int pop = 15 + ((rng >> 8) & 0x1F); // 15..46
            if (sel == 0)      mr += pop;
            else if (sel == 1) mg += pop;
            else               mb += pop;
            row[i+0] = (uint8_t)(mr & 0xFF);
            row[i+1] = (uint8_t)(mg & 0xFF);
            row[i+2] = (uint8_t)(mb & 0xFF);
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
    int total = w * h * 3;
    // Use a stride coprime with 3 to maximise channel misalignment.
    int stride = 7;
    // LCG for per-pixel randomness
    uint32_t rng = (uint32_t)(frame * 2246822519u + 3266489917u);
    for (int i = 0; i < total; i += 3) {
        int src = (i * stride) % total;
        uint8_t r0 = rgb[i+0], g0 = rgb[i+1], b0 = rgb[i+2];
        uint8_t rs = rgb[(src + 0) % total];
        uint8_t gs = rgb[(src + 1) % total];
        uint8_t bs = rgb[(src + 2) % total];

        // Softer blend — mix ~40% scrambled instead of full add to tame
        // the harsh scanline banding
        int mr = r0 + ((rs * 100) >> 8);
        int mg = g0 + ((gs * 100) >> 8);
        int mb = b0 + ((bs * 100) >> 8);

        // Vibrant colour tint derived from the original subject —
        // boost the dominant channel and cross-contaminate slightly
        // so the subject's hues punch through the glitch.
        rng = rng * 1664525u + 1013904223u;
        int sel = (rng >> 16) % 3;
        int boost = 30 + ((rng >> 8) & 0x1F);  // 30..61

        if (sel == 0)      { mr += boost; mg += boost >> 2; }
        else if (sel == 1) { mg += boost; mb += boost >> 2; }
        else               { mb += boost; mr += boost >> 2; }

        rgb[i+0] = (uint8_t)(mr > 255 ? mr & 0xFF : mr);
        rgb[i+1] = (uint8_t)(mg > 255 ? mg & 0xFF : mg);
        rgb[i+2] = (uint8_t)(mb > 255 ? mb & 0xFF : mb);
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
