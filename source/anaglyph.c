#include "anaglyph.h"
#include "apng_enc.h"
#include "camera.h"
#include "image_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t s_left_crop[CAMERA_WIDTH * CAMERA_HEIGHT];
static uint16_t s_right_crop[CAMERA_WIDTH * CAMERA_HEIGHT];
static uint8_t s_left_rgb[CAMERA_WIDTH * CAMERA_HEIGHT * 3];
static uint8_t s_right_rgb[CAMERA_WIDTH * CAMERA_HEIGHT * 3];

#define ANAGLYPH_PNG_BUF_CAP (4 * 1024 * 1024)
static uint8_t s_anaglyph_png_buf[ANAGLYPH_PNG_BUF_CAP];

static uint8_t luma_rgb(const uint8_t *p) {
    return (uint8_t)((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
}

static void prepare_eye_buffers(const uint8_t *left_rgb565, int w, int h,
                                const uint8_t *right_rgb565,
                                const EffectRecipe *recipe,
                                uint8_t *left_rgb, uint8_t *right_rgb) {
    int pixel_count = CAMERA_WIDTH * CAMERA_HEIGHT;
    crop_fill_rgb565(s_left_crop, CAMERA_WIDTH, CAMERA_HEIGHT,
                     (const uint16_t *)left_rgb565, w, h);
    crop_fill_rgb565(s_right_crop, CAMERA_WIDTH, CAMERA_HEIGHT,
                     (const uint16_t *)right_rgb565, w, h);

    rgb565_to_rgb888(left_rgb, s_left_crop, pixel_count);
    rgb565_to_rgb888(right_rgb, s_right_crop, pixel_count);
    if (pipeline_recipe_has_effects(recipe)) {
        pipeline_apply(left_rgb, CAMERA_WIDTH, CAMERA_HEIGHT, recipe, 0);
        pipeline_apply(right_rgb, CAMERA_WIDTH, CAMERA_HEIGHT, recipe, 0);
    }
}

static void compose_anaglyph_rgb888(uint8_t *dst,
                                    const uint8_t *left_rgb,
                                    const uint8_t *right_rgb,
                                    int offset_dx, int offset_dy) {
    for (int y = 0; y < CAMERA_HEIGHT; y++) {
        for (int x = 0; x < CAMERA_WIDTH; x++) {
            int rx = x - offset_dx;
            int ry = y - offset_dy;
            if (rx < 0) rx = 0;
            if (rx >= CAMERA_WIDTH) rx = CAMERA_WIDTH - 1;
            if (ry < 0) ry = 0;
            if (ry >= CAMERA_HEIGHT) ry = CAMERA_HEIGHT - 1;

            int idx = (y * CAMERA_WIDTH + x) * 3;
            int ridx = (ry * CAMERA_WIDTH + rx) * 3;
            uint8_t left_luma = luma_rgb(left_rgb + idx);
            uint8_t right_luma = luma_rgb(right_rgb + ridx);
            dst[idx + 0] = left_luma;
            dst[idx + 1] = right_luma;
            dst[idx + 2] = right_luma;
        }
    }
}

static void compose_anaglyph_rgb888_sized(uint8_t *dst,
                                          const uint8_t *left_rgb,
                                          const uint8_t *right_rgb,
                                          int width, int height,
                                          int offset_dx, int offset_dy) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int rx = x - offset_dx;
            int ry = y - offset_dy;
            if (rx < 0) rx = 0;
            if (rx >= width) rx = width - 1;
            if (ry < 0) ry = 0;
            if (ry >= height) ry = height - 1;

            int idx = (y * width + x) * 3;
            int ridx = (ry * width + rx) * 3;
            uint8_t left_luma = luma_rgb(left_rgb + idx);
            uint8_t right_luma = luma_rgb(right_rgb + ridx);
            dst[idx + 0] = left_luma;
            dst[idx + 1] = right_luma;
            dst[idx + 2] = right_luma;
        }
    }
}

void build_anaglyph_preview_frame(uint16_t *dst_rgb565,
                                  const uint8_t *left_rgb565, int w, int h,
                                  const uint8_t *right_rgb565,
                                  int offset_dx, int offset_dy,
                                  const EffectRecipe *recipe) {
    static uint8_t preview_rgb[CAMERA_WIDTH * CAMERA_HEIGHT * 3];
    prepare_eye_buffers(left_rgb565, w, h, right_rgb565, recipe,
                        s_left_rgb, s_right_rgb);
    compose_anaglyph_rgb888(preview_rgb, s_left_rgb, s_right_rgb,
                            offset_dx, offset_dy);
    rgb888_to_rgb565(dst_rgb565, preview_rgb, CAMERA_WIDTH * CAMERA_HEIGHT);
}

static void rotate_rgb888_quadrants(uint8_t *dst, const uint8_t *src,
                                    int src_w, int src_h, int quadrants) {
    int q = quadrants & 3;
    if (q == 0) {
        memcpy(dst, src, src_w * src_h * 3);
        return;
    }

    if (q == 1) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_h - 1 - y;
                int dst_y = x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else if (q == 3) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = y;
                int dst_y = src_w - 1 - x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_w - 1 - x;
                int dst_y = src_h - 1 - y;
                memcpy(dst + (dst_y * src_w + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    }
}

int save_anaglyph_jpeg(const char *path,
                       const uint8_t *left_rgb565, int w, int h,
                       const uint8_t *right_rgb565,
                       int offset_dx, int offset_dy,
                       int save_scale, int rotate_quadrants,
                       const EffectRecipe *recipe,
                       uint8_t *rgb_scratch,
                       uint8_t *upscale_scratch,
                       uint8_t *rotate_scratch) {
    uint8_t *left_rgb = rgb_scratch;
    uint8_t *right_rgb = upscale_scratch;
    uint8_t *anaglyph_rgb = rotate_scratch;
    prepare_eye_buffers(left_rgb565, w, h, right_rgb565, recipe,
                        left_rgb, right_rgb);
    compose_anaglyph_rgb888(anaglyph_rgb, left_rgb, right_rgb,
                            offset_dx, offset_dy);

    int out_w = CAMERA_WIDTH * save_scale;
    int out_h = CAMERA_HEIGHT * save_scale;
    nn_upscale(upscale_scratch, anaglyph_rgb, CAMERA_WIDTH, CAMERA_HEIGHT, save_scale);

    const uint8_t *save_buf = upscale_scratch;
    if (rotate_quadrants != 0) {
        rotate_rgb888_quadrants(rotate_scratch, upscale_scratch, out_w, out_h,
                                rotate_quadrants);
        save_buf = rotate_scratch;
        int tmp = out_w;
        out_w = out_h;
        out_h = tmp;
    }

    return save_jpeg(path, save_buf, out_w, out_h);
}

int save_anaglyph_png(const char *path,
                      const uint8_t *left_rgb565, int w, int h,
                      const uint8_t *right_rgb565,
                      int offset_dx, int offset_dy,
                      int rotate_quadrants,
                      const EffectRecipe *recipe) {
    int npix = w * h;
    uint8_t *left_rgb = malloc((size_t)npix * 3);
    uint8_t *right_rgb = malloc((size_t)npix * 3);
    uint8_t *ana_rgb = malloc((size_t)npix * 3);
    uint8_t *rot_rgb = NULL;
    if (!left_rgb || !right_rgb || !ana_rgb) goto fail;

    rgb565_to_rgb888(left_rgb, (const uint16_t *)left_rgb565, npix);
    rgb565_to_rgb888(right_rgb, (const uint16_t *)right_rgb565, npix);
    if (pipeline_recipe_has_effects(recipe)) {
        pipeline_apply(left_rgb, w, h, recipe, 0);
        pipeline_apply(right_rgb, w, h, recipe, 0);
    }
    compose_anaglyph_rgb888_sized(ana_rgb, left_rgb, right_rgb,
                                  w, h, offset_dx, offset_dy);

    const uint8_t *frame = ana_rgb;
    int out_w = w;
    int out_h = h;
    if (rotate_quadrants != 0) {
        rot_rgb = malloc((size_t)npix * 3);
        if (!rot_rgb) goto fail;
        rotate_rgb888_quadrants(rot_rgb, ana_rgb, w, h, rotate_quadrants);
        frame = rot_rgb;
        out_w = h;
        out_h = w;
    }

    const uint8_t *frames[1] = { frame };
    size_t png_len = apng_encode(s_anaglyph_png_buf, ANAGLYPH_PNG_BUF_CAP,
                                 frames, 1, out_w, out_h, 1, 1000);
    if (png_len == 0) goto fail;

    FILE *fp = fopen(path, "wb");
    if (!fp) goto fail;
    int ok = (fwrite(s_anaglyph_png_buf, 1, png_len, fp) == png_len);
    fclose(fp);
    free(left_rgb);
    free(right_rgb);
    free(ana_rgb);
    free(rot_rgb);
    return ok;

fail:
    free(left_rgb);
    free(right_rgb);
    free(ana_rgb);
    free(rot_rgb);
    return 0;
}
