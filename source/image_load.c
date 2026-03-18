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

int save_jpeg(const char *path, const uint8_t *rgb888, int width, int height) {
    return stbi_write_jpg(path, width, height, 3, rgb888, 90);
}

// Find the next unused filename GB_XXXX.JPG in dir, write it into out_path.
// Scans the directory once to find the highest existing GB_NNNN number,
// then uses max+1 — O(n) in directory entries, not O(n) in fopen calls.
// Returns 1 if a free slot was found, 0 if all 9999 slots are taken.
int next_save_path(const char *dir, char *out_path, int out_len) {
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

    int next = max_n + 1;
    if (next > 9999) return 0;
    snprintf(out_path, out_len, "%s/GB_%04d.JPG", dir, next);
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
