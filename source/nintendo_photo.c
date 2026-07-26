// Nintendo 3DS Camera compatible photo writer.
//
// The 3DS system photo picker (and therefore browser uploads, e.g. to
// 3DS Gallery) only lists photos that look like official camera output:
// 640x480 JPEGs in DCIM/xxxNINxx with a Nintendo Exif APP1, plus an MPF
// APP2 index for 3D (.MPO) shots. The segment layout is reproduced from a
// genuine photo via hni_template.h; only timestamps, the thumbnail and the
// MPF sizes/offsets are patched in (hardware-verified that the per-photo
// MakerNote ID fields may stay zero).

#include <stb_image.h>        // implementations live in image_load.c
#include <stb_image_write.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nintendo_photo.h"
#include "hni_template.h"
#include "image_load.h"

#define HNI_QUALITY       90
#define HNI_THUMB_QUALITY 60
#define HNI_THUMB_W       160
#define HNI_THUMB_H       120

// ---------------------------------------------------------------------------
// JPEG encode-to-memory accumulator (saves are serialized via s_save.busy)
// ---------------------------------------------------------------------------

#define ENC_CAP (900 * 1024)
static uint8_t s_enc_buf[ENC_CAP];
static int     s_enc_len;

static void enc_accum(void *ctx, void *data, int size) {
    (void)ctx;
    if (s_enc_len + size > ENC_CAP) return;
    memcpy(s_enc_buf + s_enc_len, data, size);
    s_enc_len += size;
}

static int encode_jpeg_mem(const uint8_t *rgb888, int w, int h, int quality) {
    s_enc_len = 0;
    stbi_write_jpg_to_func(enc_accum, NULL, w, h, 3, rgb888, quality);
    return s_enc_len;
}

// Offset of the first non-APP segment (DQT onward): drops stb's SOI + JFIF
// APP0 so the Nintendo APP1/APP2 headers can be spliced in front.
static int jpeg_body_offset(const uint8_t *j, int len) {
    int i = 2;
    while (i + 4 <= len && j[i] == 0xFF) {
        uint8_t m = j[i + 1];
        if (m != 0xE0 && m != 0xE1)
            return i;
        i += 2 + ((j[i + 2] << 8) | j[i + 3]);
    }
    return -1;
}

static void be16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// ---------------------------------------------------------------------------
// Canvas fit (portrait shots become pillarboxed 640x480)
// ---------------------------------------------------------------------------

void hni_fit_canvas(uint8_t *dst, const uint8_t *src, int w, int h) {
    if (w == HNI_WIDTH && h == HNI_HEIGHT) {
        memcpy(dst, src, HNI_WIDTH * HNI_HEIGHT * 3);
        return;
    }
    memset(dst, 0, HNI_WIDTH * HNI_HEIGHT * 3);
    int draw_w = HNI_WIDTH;
    int draw_h = HNI_HEIGHT;
    if ((long long)w * HNI_HEIGHT > (long long)h * HNI_WIDTH) {
        draw_h = (h * HNI_WIDTH) / w;
        if (draw_h < 1) draw_h = 1;
    } else {
        draw_w = (w * HNI_HEIGHT) / h;
        if (draw_w < 1) draw_w = 1;
    }
    int ox = (HNI_WIDTH - draw_w) / 2;
    int oy = (HNI_HEIGHT - draw_h) / 2;
    for (int y = 0; y < draw_h; y++) {
        int sy = (y * h) / draw_h;
        for (int x = 0; x < draw_w; x++) {
            int sx = (x * w) / draw_w;
            memcpy(dst + ((oy + y) * HNI_WIDTH + ox + x) * 3,
                   src + (sy * w + sx) * 3, 3);
        }
    }
}

// ---------------------------------------------------------------------------
// Segment builders
// ---------------------------------------------------------------------------

// 4x4 box downscale 640x480 -> 160x120, encode, strip APP0, pad to even
// length like the official app. Returns thumbnail byte count (0 on failure).
static int make_thumb(uint8_t *out, int cap, const uint8_t *rgb888) {
    static uint8_t small[HNI_THUMB_W * HNI_THUMB_H * 3];
    for (int y = 0; y < HNI_THUMB_H; y++) {
        for (int x = 0; x < HNI_THUMB_W; x++) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++) {
                const uint8_t *row = rgb888 +
                    ((y * 4 + dy) * HNI_WIDTH + x * 4) * 3;
                for (int dx = 0; dx < 4; dx++) {
                    r += row[dx * 3 + 0];
                    g += row[dx * 3 + 1];
                    b += row[dx * 3 + 2];
                }
            }
            uint8_t *o = small + (y * HNI_THUMB_W + x) * 3;
            o[0] = (uint8_t)(r >> 4);
            o[1] = (uint8_t)(g >> 4);
            o[2] = (uint8_t)(b >> 4);
        }
    }
    int n = encode_jpeg_mem(small, HNI_THUMB_W, HNI_THUMB_H, HNI_THUMB_QUALITY);
    if (n <= 0) return 0;
    int off = jpeg_body_offset(s_enc_buf, n);
    if (off < 0) return 0;
    int len = 2 + (n - off);
    if (len + 1 > cap) return 0;
    out[0] = 0xFF;
    out[1] = 0xD8;
    memcpy(out + 2, s_enc_buf + off, n - off);
    if (len & 1)
        out[len++] = 0x00;
    return len;
}

// Assemble the Nintendo APP1 (Exif + MakerNote + thumbnail IFD) into out.
// timestamp: "YYYY:MM:DD HH:MM:SS". Returns segment byte count (0 on failure).
static int build_app1(uint8_t *out, int cap, const char *timestamp,
                      const uint8_t *thumb, int thumb_len) {
    int tiff_len = (int)sizeof(HNI_APP1_TIFF) + thumb_len;
    int seg_len = 2 + 6 + tiff_len;   // length field value
    if (2 + seg_len > cap || seg_len > 0xFFFF) return 0;

    uint8_t *p = out;
    *p++ = 0xFF;
    *p++ = 0xE1;
    be16(p, (uint32_t)seg_len);
    p += 2;
    memcpy(p, "Exif\0\0", 6);
    p += 6;

    uint8_t *tiff = p;
    memcpy(tiff, HNI_APP1_TIFF, sizeof(HNI_APP1_TIFF));
    memcpy(tiff + HNI_TS_OFFSET_0, timestamp, 19);
    memcpy(tiff + HNI_TS_OFFSET_1, timestamp, 19);
    memcpy(tiff + HNI_TS_OFFSET_2, timestamp, 19);
    be32(tiff + HNI_THUMB_LEN_FIELD, (uint32_t)thumb_len);
    memcpy(tiff + HNI_THUMB_DATA_OFF, thumb, thumb_len);
    return 2 + seg_len;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  app1[24 * 1024];
    int      app1_len;
    uint8_t *body;      // malloc'd entropy-coded data (DQT..EOI)
    int      body_len;
} HniImage;

static int prepare_image(HniImage *img, const uint8_t *rgb888,
                         const char *timestamp) {
    static uint8_t thumb[32 * 1024];
    int thumb_len = make_thumb(thumb, sizeof(thumb), rgb888);
    if (thumb_len <= 0) return 0;
    img->app1_len = build_app1(img->app1, sizeof(img->app1),
                               timestamp, thumb, thumb_len);
    if (img->app1_len <= 0) return 0;

    int n = encode_jpeg_mem(rgb888, HNI_WIDTH, HNI_HEIGHT, HNI_QUALITY);
    if (n <= 0) return 0;
    int off = jpeg_body_offset(s_enc_buf, n);
    if (off < 0) return 0;
    img->body_len = n - off;
    img->body = malloc(img->body_len);
    if (!img->body) return 0;
    memcpy(img->body, s_enc_buf + off, img->body_len);
    return 1;
}

static const uint8_t SOI[2] = { 0xFF, 0xD8 };

static int fwrite_all(FILE *f, const void *data, int len) {
    return fwrite(data, 1, (size_t)len, f) == (size_t)len;
}

int write_hni_photo(const char *jpg_path, const char *mpo_path,
                    const uint8_t *left_rgb888, const uint8_t *right_rgb888) {
    char ts[20];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm || strftime(ts, sizeof(ts), "%Y:%m:%d %H:%M:%S", tm) != 19)
        memcpy(ts, "2000:01:01 00:00:00", sizeof(ts));

    static HniImage left, right;
    left.body = right.body = NULL;
    int ok = prepare_image(&left, left_rgb888, ts);
    if (ok && right_rgb888 && mpo_path)
        ok = prepare_image(&right, right_rgb888, ts);

    // The .JPG is the left view without the MPF APP2 segment.
    if (ok) {
        ok = 0;
        FILE *f = fopen(jpg_path, "wb");
        if (f) {
            ok = fwrite_all(f, SOI, 2) &&
                 fwrite_all(f, left.app1, left.app1_len) &&
                 fwrite_all(f, left.body, left.body_len);
            fclose(f);
            if (!ok) remove(jpg_path);
        }
    }

    if (ok && right.body) {
        uint8_t app2[sizeof(HNI_APP2_PRIMARY)];
        memcpy(app2, HNI_APP2_PRIMARY, sizeof(app2));
        uint32_t size1 = 2 + left.app1_len + sizeof(app2) + left.body_len;
        uint32_t size2 = 2 + right.app1_len + sizeof(HNI_APP2_SECONDARY)
                       + right.body_len;
        uint32_t pad = size1 & 1;   // official app pads image 1 to even length
        // MP entry offsets are relative to the MPF TIFF header (segment + 8).
        uint32_t mpf_base = 2 + left.app1_len + 8;
        be32(app2 + HNI_MP_ENTRY0 + 4, size1);
        be32(app2 + HNI_MP_ENTRY1 + 4, size2);
        be32(app2 + HNI_MP_ENTRY1 + 8, size1 + pad - mpf_base);

        ok = 0;
        FILE *f = fopen(mpo_path, "wb");
        if (f) {
            static const uint8_t zero = 0x00;
            ok = fwrite_all(f, SOI, 2) &&
                 fwrite_all(f, left.app1, left.app1_len) &&
                 fwrite_all(f, app2, sizeof(app2)) &&
                 fwrite_all(f, left.body, left.body_len) &&
                 (!pad || fwrite_all(f, &zero, 1)) &&
                 fwrite_all(f, SOI, 2) &&
                 fwrite_all(f, right.app1, right.app1_len) &&
                 fwrite_all(f, HNI_APP2_SECONDARY,
                            sizeof(HNI_APP2_SECONDARY)) &&
                 fwrite_all(f, right.body, right.body_len);
            fclose(f);
            if (!ok) remove(mpo_path);
        }
    }

    free(left.body);
    free(right.body);
    return ok;
}

// ---------------------------------------------------------------------------
// MPO second-image loader (for editing 3D pairs)
// ---------------------------------------------------------------------------

static uint32_t rd32(const uint8_t *p, int le) {
    return le ? ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16)
              | ((uint32_t)p[1] << 8) | p[0]
              : ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
              | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rd16(const uint8_t *p, int le) {
    return le ? ((uint32_t)p[1] << 8) | p[0]
              : ((uint32_t)p[0] << 8) | p[1];
}

int load_mpo_second_rgb888(const char *path,
                           uint8_t **out_rgb888, int *out_w, int *out_h) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 16 || fsize > 8 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    uint8_t *data = malloc((size_t)fsize);
    if (!data || fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);

    // Walk first image's segments for the APP2 "MPF\0" index.
    long img2_off = 0;
    long i = 2;
    while (i + 4 <= fsize && data[i] == 0xFF && data[i + 1] != 0xDA) {
        long seg = 2 + ((data[i + 2] << 8) | data[i + 3]);
        if (data[i + 1] == 0xE2 && i + 8 <= fsize &&
            memcmp(data + i + 4, "MPF\0", 4) == 0) {
            const uint8_t *base = data + i + 8;   // MPF TIFF header
            long avail = fsize - (i + 8);
            if (avail < 16) break;
            int le = (base[0] == 'I');
            uint32_t ifd = rd32(base + 4, le);
            if ((long)ifd + 2 > avail) break;
            uint32_t n = rd16(base + ifd, le);
            for (uint32_t k = 0; k < n; k++) {
                const uint8_t *e = base + ifd + 2 + 12 * k;
                if ((long)(ifd + 2 + 12 * (k + 1)) > avail) break;
                if (rd16(e, le) == 0xB002) {
                    uint32_t entries = rd32(e + 8, le);
                    // second MP entry's image offset field
                    if ((long)(entries + 32) <= avail)
                        img2_off = (i + 8) +
                                   (long)rd32(base + entries + 16 + 8, le);
                }
            }
            break;
        }
        i += seg;
    }

    int ok = 0;
    if (img2_off > 2 && img2_off + 4 < fsize &&
        data[img2_off] == 0xFF && data[img2_off + 1] == 0xD8) {
        int w = 0, h = 0, ch = 0;
        uint8_t *px = stbi_load_from_memory(data + img2_off,
                                            (int)(fsize - img2_off),
                                            &w, &h, &ch, 3);
        if (px) {
            *out_rgb888 = px;
            *out_w = w;
            *out_h = h;
            ok = 1;
        }
    }
    free(data);
    return ok;
}
