#include "sticker.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>

// stb_image implementation lives in image_load.c — include header only
#include "stb_image.h"

// ---------------------------------------------------------------------------
// Category table — add new categories here only
// ---------------------------------------------------------------------------

StickerCategory sticker_cats[STICKER_CAT_COUNT] = {
    { "Food",   "romfs:/stickers/food",   {}, 0, false },
    { "Emojis", "romfs:/stickers/emojis", {}, 0, false },
};

// ---------------------------------------------------------------------------
// Directory scan — populate a category's icon list from its romfs folder
// ---------------------------------------------------------------------------

void sticker_cat_load(int cat_idx) {
    if (cat_idx < 0 || cat_idx >= STICKER_CAT_COUNT) return;
    StickerCategory *cat = &sticker_cats[cat_idx];
    if (cat->loaded) return;
    cat->loaded = true;
    cat->count  = 0;

    DIR *d = opendir(cat->dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && cat->count < STICKER_CAT_MAX_ICONS) {
        const char *n = ent->d_name;
        int len = strlen(n);
        // Only accept .png files
        if (len < 5 || strcmp(n + len - 4, ".png") != 0) continue;

        StickerIconDef *icon = &cat->icons[cat->count++];
        // Display name: strip extension, replace _ with space
        int name_len = len - 4;
        if (name_len >= (int)sizeof(icon->path)) name_len = (int)sizeof(icon->path) - 1;
        char tmp[64];
        if (name_len >= (int)sizeof(tmp)) name_len = (int)sizeof(tmp) - 1;
        memcpy(tmp, n, name_len);
        tmp[name_len] = '\0';
        for (int i = 0; tmp[i]; i++) if (tmp[i] == '_') tmp[i] = ' ';
        // Store display name inline (reuse path buffer temporarily — name points to path start)
        // We pack name + '\0' + path into the single path[64] field:
        // [name (up to 31 chars + '\0')] [path (romfs:/... + '\0')]
        // Instead, just use the filename (without extension) as the display name,
        // stored in a separate static buffer isn't possible — use path field for full path
        // and derive name on demand from last '/' + strip ext.
        snprintf(icon->path, sizeof(icon->path), "%s/%s", cat->dir, n);
        icon->name = NULL;  // derived at draw time from path
    }
    closedir(d);

    // Sort alphabetically by path for consistent ordering
    for (int i = 0; i < cat->count - 1; i++) {
        for (int j = i + 1; j < cat->count; j++) {
            if (strcmp(cat->icons[i].path, cat->icons[j].path) > 0) {
                StickerIconDef tmp = cat->icons[i];
                cat->icons[i] = cat->icons[j];
                cat->icons[j] = tmp;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LRU pixel cache — keyed by (cat_idx << 16 | icon_idx)
// ---------------------------------------------------------------------------

typedef struct {
    int   key;        // (cat_idx << 16) | icon_idx, -1 = empty
    int   lru_stamp;
    unsigned char pixels[STICKER_PX_W * STICKER_PX_H * 4];
} StickerCacheSlot;

static StickerCacheSlot s_cache[STICKER_CACHE_SIZE];
static int s_lru_counter = 0;
static bool s_cache_init = false;

static void cache_init(void) {
    for (int i = 0; i < STICKER_CACHE_SIZE; i++)
        s_cache[i].key = -1;
    s_cache_init = true;
}

const unsigned char *get_sticker_pixels(int cat_idx, int icon_idx) {
    if (!s_cache_init) cache_init();
    if (cat_idx < 0 || cat_idx >= STICKER_CAT_COUNT) return NULL;
    StickerCategory *cat = &sticker_cats[cat_idx];
    if (!cat->loaded) sticker_cat_load(cat_idx);
    if (icon_idx < 0 || icon_idx >= cat->count) return NULL;

    int key = (cat_idx << 16) | icon_idx;

    // Search cache
    for (int i = 0; i < STICKER_CACHE_SIZE; i++) {
        if (s_cache[i].key == key) {
            s_cache[i].lru_stamp = ++s_lru_counter;
            return s_cache[i].pixels;
        }
    }

    // Find LRU slot to evict
    int lru_slot = 0;
    for (int i = 1; i < STICKER_CACHE_SIZE; i++) {
        if (s_cache[i].key == -1) { lru_slot = i; break; }
        if (s_cache[i].lru_stamp < s_cache[lru_slot].lru_stamp) lru_slot = i;
    }

    // Load via stb_image
    int w, h, ch;
    unsigned char *data = stbi_load(cat->icons[icon_idx].path, &w, &h, &ch, 4);
    if (!data) return NULL;

    StickerCacheSlot *slot = &s_cache[lru_slot];
    slot->key       = key;
    slot->lru_stamp = ++s_lru_counter;

    if (w == STICKER_PX_W && h == STICKER_PX_H) {
        memcpy(slot->pixels, data, STICKER_PX_W * STICKER_PX_H * 4);
    } else {
        for (int dy = 0; dy < STICKER_PX_H; dy++) {
            int sy = dy * h / STICKER_PX_H;
            for (int dx = 0; dx < STICKER_PX_W; dx++) {
                int sx = dx * w / STICKER_PX_W;
                const unsigned char *src = data + (sy * w + sx) * 4;
                unsigned char *dst = slot->pixels + (dy * STICKER_PX_W + dx) * 4;
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
            }
        }
    }

    stbi_image_free(data);
    return slot->pixels;
}

// ---------------------------------------------------------------------------
// Composite sticker into RGB888 photo buffer
// ---------------------------------------------------------------------------

void composite_sticker_rgb888(unsigned char *photo_rgb888,
                               int photo_w, int photo_h,
                               const unsigned char *sticker_rgba,
                               int cx, int cy,
                               float scale, float angle_deg)
{
    if (scale <= 0) scale = 1.0f;

    int out_half = (int)(STICKER_PX_W * scale * 0.5f + 0.5f);
    if (out_half < 1) out_half = 1;

    float rad   = angle_deg * (3.14159265f / 180.0f);
    float cos_a = cosf(-rad);
    float sin_a = sinf(-rad);
    float src_half = STICKER_PX_W * 0.5f;

    int x0 = cx - out_half - 1, x1 = cx + out_half + 1;
    int y0 = cy - out_half - 1, y1 = cy + out_half + 1;

    for (int oy = y0; oy <= y1; oy++) {
        if (oy < 0 || oy >= photo_h) continue;
        float dy = (float)(oy - cy);
        for (int ox = x0; ox <= x1; ox++) {
            if (ox < 0 || ox >= photo_w) continue;
            float dx = (float)(ox - cx);
            float lx = (cos_a * dx - sin_a * dy) / scale + src_half;
            float ly = (sin_a * dx + cos_a * dy) / scale + src_half;
            int sx = (int)lx, sy = (int)ly;
            if (sx < 0 || sx >= STICKER_PX_W || sy < 0 || sy >= STICKER_PX_H) continue;

            const unsigned char *src = sticker_rgba + (sy * STICKER_PX_W + sx) * 4;
            unsigned char *dst = photo_rgb888 + (oy * photo_w + ox) * 3;
            unsigned int alpha = src[3];
            if (alpha == 0) continue;
            if (alpha == 255) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            } else {
                unsigned int inv = 255 - alpha;
                dst[0] = (unsigned char)((src[0] * alpha + dst[0] * inv) >> 8);
                dst[1] = (unsigned char)((src[1] * alpha + dst[1] * inv) >> 8);
                dst[2] = (unsigned char)((src[2] * alpha + dst[2] * inv) >> 8);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Frame PNG cache
// ---------------------------------------------------------------------------

#define FRAME_CACHE_SIZE 7

typedef struct {
    char path[64];
    int  w, h;
    unsigned char *data;
} FrameCacheSlot;

static FrameCacheSlot s_frame_cache[FRAME_CACHE_SIZE];
static bool s_frame_cache_init = false;

static void frame_cache_ensure_init(void) {
    if (s_frame_cache_init) return;
    for (int i = 0; i < FRAME_CACHE_SIZE; i++) s_frame_cache[i].data = NULL;
    s_frame_cache_init = true;
}

static const unsigned char *frame_cache_get(const char *path, int *out_w, int *out_h) {
    frame_cache_ensure_init();
    for (int i = 0; i < FRAME_CACHE_SIZE; i++) {
        if (s_frame_cache[i].data && strcmp(s_frame_cache[i].path, path) == 0) {
            *out_w = s_frame_cache[i].w; *out_h = s_frame_cache[i].h;
            return s_frame_cache[i].data;
        }
    }
    int slot = 0;
    for (int i = 0; i < FRAME_CACHE_SIZE; i++) {
        if (!s_frame_cache[i].data) { slot = i; goto load; }
    }
    free(s_frame_cache[0].data); s_frame_cache[0].data = NULL;
load:;
    int w, h, ch;
    unsigned char *d = stbi_load(path, &w, &h, &ch, 4);
    if (!d) return NULL;
    s_frame_cache[slot].data = d;
    s_frame_cache[slot].w = w; s_frame_cache[slot].h = h;
    int plen = (int)strlen(path); if (plen >= 64) plen = 63;
    memcpy(s_frame_cache[slot].path, path, plen);
    s_frame_cache[slot].path[plen] = '\0';
    *out_w = w; *out_h = h;
    return d;
}

void composite_frame_rgb888(unsigned char *photo_rgb888,
                             int photo_w, int photo_h,
                             const char *frame_path) {
    int w, h;
    const unsigned char *data = frame_cache_get(frame_path, &w, &h);
    if (!data) return;
    for (int y = 0; y < photo_h; y++) {
        int sy_fr = y * h / photo_h;
        for (int x = 0; x < photo_w; x++) {
            int sx_fr = x * w / photo_w;
            const unsigned char *src = data + (sy_fr * w + sx_fr) * 4;
            unsigned char *dst = photo_rgb888 + (y * photo_w + x) * 3;
            unsigned int alpha = src[3];
            if (alpha == 0) continue;
            if (alpha == 255) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            } else {
                unsigned int inv = 255 - alpha;
                dst[0] = (unsigned char)((src[0] * alpha + dst[0] * inv) >> 8);
                dst[1] = (unsigned char)((src[1] * alpha + dst[1] * inv) >> 8);
                dst[2] = (unsigned char)((src[2] * alpha + dst[2] * inv) >> 8);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Citro2d sticker renderer
// ---------------------------------------------------------------------------

void draw_sticker_c2d(int cat_idx, int icon_idx,
                      float sx, float sy, float dw, float dh) {
    const unsigned char *px = get_sticker_pixels(cat_idx, icon_idx);
    if (!px) return;

    float pw = dw / STICKER_PX_W;
    float ph = dh / STICKER_PX_H;

    for (int row = 0; row < STICKER_PX_H; row++) {
        for (int col = 0; col < STICKER_PX_W; col++) {
            const unsigned char *p = px + (row * STICKER_PX_W + col) * 4;
            if (p[3] == 0) continue;
            u32 color = C2D_Color32(p[0], p[1], p[2], p[3]);
            C2D_DrawRectSolid(sx + col * pw, sy + row * ph, 0.5f, pw + 0.5f, ph + 0.5f, color);
        }
    }
}
