// This file owns both stb implementations — only define them once.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "image_load.h"
#include "camera.h"
#include "filter.h"
#include "apng_enc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

// ---------------------------------------------------------------------------
// Helper: pack RGB888 pixels → RGB565 dst with nearest-neighbour scale
// ---------------------------------------------------------------------------
static void pixels_to_rgb565(const uint8_t *pixels, int img_w, int img_h,
                              uint16_t *dst, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx  = x * img_w / width;
            int sy  = y * img_h / height;
            int idx = (sy * img_w + sx) * 3;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            dst[y * width + x] = ((uint16_t)(r >> 3) << 11)
                                | ((uint16_t)(g >> 2) <<  5)
                                |  (uint16_t)(b >> 3);
        }
    }
}

int load_jpeg_to_rgb565(const char *path, uint16_t *dst, int width, int height) {
    int img_w, img_h, channels;
    uint8_t *pixels = stbi_load(path, &img_w, &img_h, &channels, 3);
    if (!pixels) return 0;
    pixels_to_rgb565(pixels, img_w, img_h, dst, width, height);
    stbi_image_free(pixels);
    return 1;
}

// ---------------------------------------------------------------------------
// load_apng_2frames_to_rgb565
// Parse the APNG chunk stream and decode frame 0 (IDAT) and frame 1 (fdAT)
// into dst0 and dst1 respectively.  Both are scaled to (width x height).
// ---------------------------------------------------------------------------

// Read a big-endian u32 from raw bytes
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

// CRC-32 for a buffer (reuse table from apng_enc.c if already built,
// but we can compute inline here as a local helper)
static uint32_t simple_crc32(const uint8_t *data, size_t len) {
    // Standard CRC-32 poly 0xEDB88320
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return ~crc;
}

// Build a standalone PNG in out_buf (must be >= raw_len + 300 bytes):
//   PNG sig + IHDR (from src_ihdr[13]) + IDAT(data, len) + IEND
// Returns total bytes written.
static size_t build_single_png(uint8_t *out, size_t out_cap,
                                const uint8_t *ihdr13,
                                const uint8_t *idat_data, uint32_t idat_len) {
    if (out_cap < idat_len + 300) return 0;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    size_t pos = 0;
    // sig
    memcpy(out + pos, sig, 8); pos += 8;
    // IHDR chunk: len=13
    out[pos]=0; out[pos+1]=0; out[pos+2]=0; out[pos+3]=13; pos += 4;
    memcpy(out + pos, "IHDR", 4); // type
    memcpy(out + pos + 4, ihdr13, 13);
    uint32_t crc = simple_crc32(out + pos, 17);
    pos += 17;
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    // IDAT chunk
    out[pos]=(uint8_t)(idat_len>>24); out[pos+1]=(uint8_t)(idat_len>>16);
    out[pos+2]=(uint8_t)(idat_len>>8); out[pos+3]=(uint8_t)idat_len; pos += 4;
    memcpy(out + pos, "IDAT", 4);
    memcpy(out + pos + 4, idat_data, idat_len);
    crc = simple_crc32(out + pos, 4 + idat_len);
    pos += 4 + idat_len;
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    // IEND chunk
    out[pos]=0; out[pos+1]=0; out[pos+2]=0; out[pos+3]=0; pos += 4;
    memcpy(out + pos, "IEND", 4); pos += 4;
    crc = simple_crc32(out + pos - 4, 4);
    out[pos]=(uint8_t)(crc>>24); out[pos+1]=(uint8_t)(crc>>16);
    out[pos+2]=(uint8_t)(crc>>8); out[pos+3]=(uint8_t)crc; pos += 4;
    return pos;
}

int load_apng_frames_to_rgb565(const char *path,
                                uint16_t **frames, int max_frames,
                                int *out_n_frames, int *out_delay_ms,
                                int width, int height) {
    *out_n_frames = 0;
    *out_delay_ms = 250;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0 || fsz > (4 * 1024 * 1024)) { fclose(f); return 0; }

    uint8_t *raw = (uint8_t *)malloc((size_t)fsz);
    if (!raw) { fclose(f); return 0; }
    if ((long)fread(raw, 1, (size_t)fsz, f) != fsz) { free(raw); fclose(f); return 0; }
    fclose(f);

    // Collect per-frame compressed data pointers: frame 0 = IDAT, rest = fdAT
    uint8_t  ihdr13[13] = {0};
    const uint8_t *frame_data[8]; uint32_t frame_len[8];
    int n_found = 0;
    bool delay_read = false;

    size_t pos = 8;
    while (pos + 12 <= (size_t)fsz && n_found < max_frames) {
        uint32_t len  = rd32be(raw + pos);
        const uint8_t *type = raw + pos + 4;
        const uint8_t *data = raw + pos + 8;
        if (pos + 12 + len > (size_t)fsz) break;

        if (memcmp(type, "IHDR", 4) == 0 && len == 13) {
            memcpy(ihdr13, data, 13);
        } else if (memcmp(type, "fcTL", 4) == 0 && len >= 24 && !delay_read) {
            // fcTL data layout: seq(4) width(4) height(4) x_off(4) y_off(4)
            //                   delay_num(2) delay_den(2) dispose(1) blend(1)
            uint16_t num = (uint16_t)((data[20] << 8) | data[21]);
            uint16_t den = (uint16_t)((data[22] << 8) | data[23]);
            if (den == 0) den = 100;
            *out_delay_ms = (int)((uint32_t)num * 1000 / den);
            if (*out_delay_ms < 10) *out_delay_ms = 10;
            delay_read = true;
        } else if (memcmp(type, "IDAT", 4) == 0 && n_found == 0) {
            frame_data[n_found] = data;
            frame_len[n_found]  = len;
            n_found++;
        } else if (memcmp(type, "fdAT", 4) == 0 && len > 4) {
            frame_data[n_found] = data + 4;  // skip 4-byte seq number
            frame_len[n_found]  = len - 4;
            n_found++;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    // Decode each frame
    int loaded = 0;
    for (int i = 0; i < n_found; i++) {
        uint8_t *tmp = (uint8_t *)malloc(frame_len[i] + 300);
        if (!tmp) break;
        size_t sz = build_single_png(tmp, frame_len[i] + 300,
                                     ihdr13, frame_data[i], frame_len[i]);
        if (sz > 0) {
            int w, h, ch;
            uint8_t *pixels = stbi_load_from_memory(tmp, (int)sz, &w, &h, &ch, 3);
            if (pixels) {
                pixels_to_rgb565(pixels, w, h, frames[loaded], width, height);
                stbi_image_free(pixels);
                loaded++;
            }
        }
        free(tmp);
    }

    free(raw);
    *out_n_frames = loaded;
    return (loaded > 0) ? 1 : 0;
}

// Static output buffer for JPEG encoding.
// Real output is ~50–150 KB (palette-quantized images compress heavily).
// 512 KB is a 3× safety margin over the largest observed file.
// No runtime allocation during save; safe because saves are serialized via busy flag.
#define JPEG_BUF_CAP (512 * 1024)
static uint8_t s_jpeg_buf[JPEG_BUF_CAP];
static int     s_jpeg_len;

static void jpeg_accum(void *ctx, void *data, int size) {
    (void)ctx;
    if (s_jpeg_len + size > JPEG_BUF_CAP) return;
    memcpy(s_jpeg_buf + s_jpeg_len, data, size);
    s_jpeg_len += size;
}

// Encode to a static memory buffer, then write the whole file in one fwrite.
// Avoids thousands of tiny SD-card writes from stbi's 64-byte internal buffer.
int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height) {
    s_jpeg_len = 0;
    stbi_write_jpg_to_func(jpeg_accum, NULL, width, height, 3, rgb888, 90);
    if (s_jpeg_len <= 0) return 0;

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = (fwrite(s_jpeg_buf, 1, s_jpeg_len, f) == (size_t)s_jpeg_len);
    fclose(f);
    return ok;
}

// Single shared counter for all saved files (GB_ and GW_).
// Seeded by file_counter_init(); incremented on each save.
static int s_next_n = -1;  // -1 = not yet initialised

// Call once at startup. ini_val is the persisted next_file_n from settings
// (pass 0 if not present). We take max(ini_val, dir_scan+1) so we never
// reuse a number even if the INI was deleted or is stale.
void file_counter_init(const char *dir, int ini_val) {
    mkdir("sdmc:/DCIM", 0777);
    mkdir(dir, 0777);

    int max_n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            int n = 0;
            if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1 && n > max_n) max_n = n;
            if (sscanf(e->d_name, "GW_%d.png", &n) == 1 && n > max_n) max_n = n;
        }
        closedir(d);
    }
    int from_scan = max_n + 1;
    s_next_n = (ini_val > from_scan) ? ini_val : from_scan;
}

// Returns the current value of the shared counter (the next number that will
// be used). Call after file_counter_init to persist it in settings.
int file_counter_next(void) { return s_next_n; }

int next_save_path(const char *dir, char *out_path, int out_len) {
    if (s_next_n < 0) file_counter_init(dir, 0);
    if (s_next_n > 9999) return 0;
    snprintf(out_path, out_len, "%s/GB_%04d.JPG", dir, s_next_n);
    s_next_n++;
    return 1;
}

// Scan dir for GB_XXXX.JPG and GW_XXXX.GIF files, fill paths[] sorted descending by number.
// Both file types share the same 4-digit counter space so they interleave naturally.
int list_saved_photos(const char *dir, char paths[][64], int max) {
    // Pack type (0=JPG, 1=GIF) in high bit of a 32-bit slot alongside the number.
    // Sorting by value descending keeps them interleaved correctly.
    int nums[256];
    int types[256];  // 0=JPG, 1=GIF
    int count = 0;

    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        int n = 0;
        if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1) {
            nums[count]  = n;
            types[count] = 0;
            count++;
        } else if (sscanf(e->d_name, "GW_%d.png", &n) == 1) {
            nums[count]  = n;
            types[count] = 1;
            count++;
        }
    }
    closedir(d);

    // Insertion sort descending by number
    for (int i = 1; i < count; i++) {
        int kn = nums[i], kt = types[i], j = i - 1;
        while (j >= 0 && nums[j] < kn) {
            nums[j+1]  = nums[j];
            types[j+1] = types[j];
            j--;
        }
        nums[j+1]  = kn;
        types[j+1] = kt;
    }

    for (int i = 0; i < count; i++) {
        if (types[i] == 0)
            snprintf(paths[i], 64, "%s/GB_%04d.JPG", dir, nums[i]);
        else
            snprintf(paths[i], 64, "%s/GW_%04d.png", dir, nums[i]);
    }

    return count;
}

int next_wiggle_path(const char *dir, char *out_path, int out_len) {
    if (s_next_n < 0) file_counter_init(dir, 0);
    if (s_next_n > 9999) return 0;
    snprintf(out_path, out_len, "%s/GW_%04d.png", dir, s_next_n);
    s_next_n++;
    return 1;
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
    // First half: L→R (alpha 0 → 255), second half: R→L (alpha 255 → 0).
    // denom is chosen so the midpoint frame is pure right (alpha=255).
    //   n=2: L, R
    //   n=4: L, mid-L, mid-R, R  → alpha 0, 85, 170, 255 (one-way L→R, loops back)
    //        actually we split: first n/2 go 0..255, second n/2 go 255..0
    //   n=4: 0=L(0), 1=blend(170), 2=R(255), 3=blend(85) → smooth loop
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
    // Simplify common values to keep the fraction small.
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

// ---------------------------------------------------------------------------
// Save an edited wiggle APNG — composite stickers/frame onto each animation
// frame then encode as APNG, preserving the original frame count and timing.
// ---------------------------------------------------------------------------

int save_edited_apng(const char *path,
                     const uint16_t * const *frames_rgb565,
                     int n_frames, int delay_ms,
                     int w, int h,
                     composite_fn_t composite_fn, void *userdata)
{
    if (n_frames < 1 || n_frames > 8) return 0;
    int npix = w * h;

    uint8_t *rgb_bufs[8] = {0};
    const uint8_t *frame_ptrs[8];

    for (int f = 0; f < n_frames; f++) {
        rgb_bufs[f] = malloc(npix * 3);
        if (!rgb_bufs[f]) goto fail;
        rgb565_to_rgb888(rgb_bufs[f], frames_rgb565[f], npix);
        if (composite_fn)
            composite_fn(rgb_bufs[f], w, h, userdata);
        frame_ptrs[f] = rgb_bufs[f];
    }

    uint16_t delay_num = (uint16_t)delay_ms;
    uint16_t delay_den = 1000;
    size_t apng_len = apng_encode(s_apng_buf, APNG_BUF_CAP,
                                  frame_ptrs, n_frames,
                                  w, h, delay_num, delay_den);

    for (int f = 0; f < n_frames; f++) free(rgb_bufs[f]);

    if (apng_len == 0) return 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_apng_buf, 1, apng_len, fp) == apng_len);
    fclose(fp);
    return ok;

fail:
    for (int f = 0; f < n_frames; f++) free(rgb_bufs[f]);
    return 0;
}
