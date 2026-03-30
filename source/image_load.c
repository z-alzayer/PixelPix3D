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

int load_jpeg_to_rgb565(const char *path, uint16_t *dst, int width, int height) {
    int img_w, img_h, channels;
    uint8_t *pixels = stbi_load(path, &img_w, &img_h, &channels, 3);
    if (!pixels) return 0;

    // Nearest-neighbour scale to target dimensions and pack as RGB565.
    // writePictureToFramebufferRGB565 expects R=bits15-11, G=bits10-5, B=bits4-0.
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

    stbi_image_free(pixels);
    return 1;
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

// Persistent counter seeded by save_counter_init(); incremented on each save.
static int s_next_n = -1;  // -1 = not yet initialised

// Call once at startup: creates the DCIM directories and scans for the
// highest existing GB_NNNN number so subsequent next_save_path calls are O(1).
void save_counter_init(const char *dir) {
    mkdir("sdmc:/DCIM", 0777);
    mkdir(dir, 0777);

    int max_n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            int n = 0;
            if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1 && n > max_n)
                max_n = n;
        }
        closedir(d);
    }
    s_next_n = max_n + 1;
}

// Returns the next free path without any filesystem I/O (O(1)).
// Falls back to a full scan if save_counter_init was never called.
// Returns 1 if a free slot was found, 0 if all 9999 slots are taken.
int next_save_path(const char *dir, char *out_path, int out_len) {
    if (s_next_n < 0) save_counter_init(dir);  // lazy fallback
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

// ---------------------------------------------------------------------------
// Wiggle APNG counter (GW_XXXX.png)
// ---------------------------------------------------------------------------

static int s_wiggle_next_n = -1;

void wiggle_counter_init(const char *dir) {
    int max_n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            int n = 0;
            if (sscanf(e->d_name, "GW_%d.png", &n) == 1 && n > max_n)
                max_n = n;
        }
        closedir(d);
    }
    s_wiggle_next_n = max_n + 1;
}

int next_wiggle_path(const char *dir, char *out_path, int out_len) {
    if (s_wiggle_next_n < 0) wiggle_counter_init(dir);
    if (s_wiggle_next_n > 9999) return 0;
    snprintf(out_path, out_len, "%s/GW_%04d.png", dir, s_wiggle_next_n);
    s_wiggle_next_n++;
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
    uint8_t *blend_rgb = malloc(npix * 3);
    if (!left_rgb || !right_rgb || !blend_rgb) {
        free(left_rgb); free(right_rgb); free(blend_rgb);
        return 0;
    }

    // Convert both buffers to RGB888 — true colour, no filter applied
    rgb565_to_rgb888(left_rgb,  (const uint16_t *)left_rgb565,  npix);
    rgb565_to_rgb888(right_rgb, (const uint16_t *)right_rgb565, npix);

    // Build frame pointer array (points into left_rgb, right_rgb, or blend_rgb)
    const uint8_t *frame_ptrs[8];

    for (int f = 0; f < n_frames; f++) {
        // Ping-pong: first half goes left->right, second half right->left
        int half  = n_frames / 2;
        int step  = (f < half) ? f : (n_frames - 1 - f);
        int alpha = (half > 0) ? (step * 255 / (half > 1 ? half - 1 : 1)) : 0;
        if (n_frames == 2) alpha = (f == 0) ? 0 : 255;

        if (alpha == 0) {
            frame_ptrs[f] = left_rgb;
        } else if (alpha >= 255) {
            frame_ptrs[f] = right_rgb;
        } else {
            // Blend into blend_rgb for this frame — store as separate alloc if needed.
            // Since we only need one blended frame at a time and apng_encode copies
            // compressed data immediately, we can reuse blend_rgb per-frame.
            // But apng_encode takes all frames up front, so we need separate buffers.
            // For simplicity: allocate per blended frame on demand.
            uint8_t *bframe = malloc(npix * 3);
            if (!bframe) {
                // Free any previously allocated blend frames
                for (int k = 0; k < f; k++) {
                    if (frame_ptrs[k] != left_rgb && frame_ptrs[k] != right_rgb)
                        free((void *)frame_ptrs[k]);
                }
                free(left_rgb); free(right_rgb); free(blend_rgb);
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
    free(left_rgb); free(right_rgb); free(blend_rgb);

    if (apng_len == 0) return 0;

    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    int ok = (fwrite(s_apng_buf, 1, apng_len, fp) == apng_len);
    fclose(fp);
    return ok;
}
