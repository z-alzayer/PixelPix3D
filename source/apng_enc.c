/*
 * apng_enc.c — self-contained APNG encoder, 24-bit RGB, no external deps.
 *
 * Uses stbi_zlib_compress from stb_image_write.h for deflate compression.
 * All other PNG/APNG framing is written manually.
 *
 * APNG spec: https://wiki.mozilla.org/APNG_Specification
 * PNG spec:  https://www.w3.org/TR/PNG/
 */

#include "apng_enc.h"
#include <stdlib.h>
#include <string.h>

// stbi_zlib_compress is defined in image_load.c (which owns STB_IMAGE_WRITE_IMPLEMENTATION).
// Declare it here so we can call it without pulling in the full stb header again.
extern unsigned char *stbi_zlib_compress(unsigned char *data, int data_len,
                                         int *out_len, int quality);

// ---------------------------------------------------------------------------
// Write helpers
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    int      overflow;
} ApngBuf;

static void ab_byte(ApngBuf *ab, uint8_t b) {
    if (ab->overflow) return;
    if (ab->pos >= ab->cap) { ab->overflow = 1; return; }
    ab->buf[ab->pos++] = b;
}

static void ab_bytes(ApngBuf *ab, const uint8_t *data, size_t n) {
    if (ab->overflow) return;
    if (ab->pos + n > ab->cap) { ab->overflow = 1; return; }
    memcpy(ab->buf + ab->pos, data, n);
    ab->pos += n;
}

static void ab_u32be(ApngBuf *ab, uint32_t v) {
    ab_byte(ab, (uint8_t)(v >> 24));
    ab_byte(ab, (uint8_t)(v >> 16));
    ab_byte(ab, (uint8_t)(v >>  8));
    ab_byte(ab, (uint8_t)(v      ));
}

// ---------------------------------------------------------------------------
// CRC-32 (PNG uses standard CRC-32)
// ---------------------------------------------------------------------------

static uint32_t crc32_table[256];
static int      crc32_ready = 0;

static void crc32_init(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t c = (uint32_t)i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---------------------------------------------------------------------------
// Chunk writer: length(4) + type(4) + data + CRC(4)
// ---------------------------------------------------------------------------

static void write_chunk(ApngBuf *ab, const char type[4],
                        const uint8_t *data, uint32_t len) {
    ab_u32be(ab, len);
    size_t crc_start = ab->pos;
    ab_bytes(ab, (const uint8_t *)type, 4);
    if (data && len > 0)
        ab_bytes(ab, data, len);
    // CRC covers type + data
    if (!ab->overflow) {
        uint32_t crc = crc32_update(0, ab->buf + crc_start, 4 + len);
        ab_u32be(ab, crc);
    }
}

// ---------------------------------------------------------------------------
// PNG filter: prepend 0x00 (None) to each scanline row.
// Returns heap-alloc'd filtered buffer (caller frees), or NULL on OOM.
// ---------------------------------------------------------------------------

static uint8_t *filter_rows(const uint8_t *rgb888, int width, int height) {
    int row_bytes = width * 3;
    uint8_t *out = (uint8_t *)malloc((size_t)(row_bytes + 1) * (size_t)height);
    if (!out) return NULL;
    for (int y = 0; y < height; y++) {
        out[y * (row_bytes + 1)] = 0x00;  // filter type: None
        memcpy(out + y * (row_bytes + 1) + 1,
               rgb888 + y * row_bytes,
               (size_t)row_bytes);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

size_t apng_encode(uint8_t *buf, size_t buf_cap,
                   const uint8_t * const *frames_rgb888, int n_frames,
                   int width, int height,
                   uint16_t delay_num, uint16_t delay_den)
{
    if (!buf || buf_cap == 0 || !frames_rgb888 || n_frames < 1) return 0;

    if (!crc32_ready) crc32_init();

    ApngBuf ab = { buf, buf_cap, 0, 0 };

    // PNG signature
    static const uint8_t png_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    ab_bytes(&ab, png_sig, 8);

    // IHDR
    {
        uint8_t ihdr[13];
        ihdr[ 0] = (uint8_t)((uint32_t)width  >> 24); ihdr[ 1] = (uint8_t)((uint32_t)width  >> 16);
        ihdr[ 2] = (uint8_t)((uint32_t)width  >>  8); ihdr[ 3] = (uint8_t)((uint32_t)width       );
        ihdr[ 4] = (uint8_t)((uint32_t)height >> 24); ihdr[ 5] = (uint8_t)((uint32_t)height >> 16);
        ihdr[ 6] = (uint8_t)((uint32_t)height >>  8); ihdr[ 7] = (uint8_t)((uint32_t)height      );
        ihdr[ 8] = 8;    // bit depth
        ihdr[ 9] = 2;    // colour type: RGB
        ihdr[10] = 0;    // compression method
        ihdr[11] = 0;    // filter method
        ihdr[12] = 0;    // interlace method: none
        write_chunk(&ab, "IHDR", ihdr, 13);
    }

    // acTL (animation control)
    {
        uint8_t actl[8];
        uint32_t nf = (uint32_t)n_frames;
        actl[0] = (uint8_t)(nf >> 24); actl[1] = (uint8_t)(nf >> 16);
        actl[2] = (uint8_t)(nf >>  8); actl[3] = (uint8_t)(nf      );
        actl[4] = 0; actl[5] = 0; actl[6] = 0; actl[7] = 0;  // loop forever
        write_chunk(&ab, "acTL", actl, 8);
    }

    uint32_t seq = 0;

    for (int f = 0; f < n_frames; f++) {
        // fcTL (frame control)
        {
            uint8_t fctl[26];
            // sequence number
            fctl[ 0] = (uint8_t)(seq >> 24); fctl[ 1] = (uint8_t)(seq >> 16);
            fctl[ 2] = (uint8_t)(seq >>  8); fctl[ 3] = (uint8_t)(seq      );
            seq++;
            // width
            uint32_t w = (uint32_t)width;
            fctl[ 4] = (uint8_t)(w >> 24); fctl[ 5] = (uint8_t)(w >> 16);
            fctl[ 6] = (uint8_t)(w >>  8); fctl[ 7] = (uint8_t)(w      );
            // height
            uint32_t h = (uint32_t)height;
            fctl[ 8] = (uint8_t)(h >> 24); fctl[ 9] = (uint8_t)(h >> 16);
            fctl[10] = (uint8_t)(h >>  8); fctl[11] = (uint8_t)(h      );
            // x_offset, y_offset (0)
            fctl[12]=0; fctl[13]=0; fctl[14]=0; fctl[15]=0;
            fctl[16]=0; fctl[17]=0; fctl[18]=0; fctl[19]=0;
            // delay_num, delay_den
            fctl[20] = (uint8_t)(delay_num >> 8); fctl[21] = (uint8_t)(delay_num);
            fctl[22] = (uint8_t)(delay_den >> 8); fctl[23] = (uint8_t)(delay_den);
            // dispose_op: 0 = none, blend_op: 0 = source
            fctl[24] = 0; fctl[25] = 0;
            write_chunk(&ab, "fcTL", fctl, 26);
        }

        // Compress frame pixels
        uint8_t *filtered = filter_rows(frames_rgb888[f], width, height);
        if (!filtered) { ab.overflow = 1; break; }

        int filtered_len = (width * 3 + 1) * height;
        int zlen = 0;
        unsigned char *zdata = stbi_zlib_compress(filtered, filtered_len, &zlen, 8);
        free(filtered);

        if (!zdata) { ab.overflow = 1; break; }

        if (f == 0) {
            // Frame 0 image data goes in IDAT (required by PNG spec for fallback)
            write_chunk(&ab, "IDAT", zdata, (uint32_t)zlen);
        } else {
            // Subsequent frames go in fdAT (4-byte seq number prepended)
            uint8_t *fdat = (uint8_t *)malloc((size_t)zlen + 4);
            if (!fdat) { free(zdata); ab.overflow = 1; break; }
            fdat[0] = (uint8_t)(seq >> 24); fdat[1] = (uint8_t)(seq >> 16);
            fdat[2] = (uint8_t)(seq >>  8); fdat[3] = (uint8_t)(seq      );
            seq++;
            memcpy(fdat + 4, zdata, (size_t)zlen);
            write_chunk(&ab, "fdAT", fdat, (uint32_t)(zlen + 4));
            free(fdat);
        }
        free(zdata);
    }

    // IEND
    write_chunk(&ab, "IEND", NULL, 0);

    if (ab.overflow) return 0;
    return ab.pos;
}
