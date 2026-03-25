// This file owns both stb implementations — only define them once.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "image_load.h"
#include <stdio.h>
#include <string.h>
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

// Scan dir for GB_XXXX.JPG files, fill paths[] sorted descending by number.
int list_saved_photos(const char *dir, char paths[][64], int max) {
    int nums[256];
    int count = 0;

    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        int n = 0;
        if (sscanf(e->d_name, "GB_%d.JPG", &n) == 1)
            nums[count++] = n;
    }
    closedir(d);

    // Insertion sort descending
    for (int i = 1; i < count; i++) {
        int key = nums[i], j = i - 1;
        while (j >= 0 && nums[j] < key) { nums[j+1] = nums[j]; j--; }
        nums[j+1] = key;
    }

    for (int i = 0; i < count; i++)
        snprintf(paths[i], 64, "%s/GB_%04d.JPG", dir, nums[i]);

    return count;
}
