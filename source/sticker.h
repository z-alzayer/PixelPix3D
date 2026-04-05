#ifndef STICKER_HEADER_H
#define STICKER_HEADER_H

#include <stdbool.h>
#include <citro2d.h>

// ---------------------------------------------------------------------------
// Sticker system — categories of 16×16 RGBA icons from romfs/stickers/
// ---------------------------------------------------------------------------

#define STICKER_PX_W        16
#define STICKER_PX_H        16
#define STICKER_MAX          8    // max placed stickers per edit session
#define STICKER_CACHE_SIZE  16    // decoded RGBA pixels held in RAM at once

// Maximum icons per category (allocated on first scan)
#define STICKER_CAT_MAX_ICONS 200

// Maximum number of categories
#define STICKER_CAT_COUNT     2

typedef struct {
    const char *name;   // display name (e.g. "boba coffee")
    char        path[64]; // romfs path
} StickerIconDef;

// A category: name + romfs directory + lazily-populated icon list
typedef struct {
    const char    *label;        // shown in category strip (e.g. "Food")
    const char    *dir;          // romfs dir (e.g. "romfs:/stickers/food")
    StickerIconDef icons[STICKER_CAT_MAX_ICONS];
    int            count;        // populated by sticker_cat_load()
    bool           loaded;
} StickerCategory;

// Global category table (defined in sticker.c)
extern StickerCategory sticker_cats[STICKER_CAT_COUNT];

// Load a category's icon list by scanning its romfs directory.
// No-op if already loaded.
void sticker_cat_load(int cat_idx);

// Placed sticker on the current photo
typedef struct {
    bool  active;
    int   x, y;       // photo-space centre coords
    int   cat_idx;    // category index
    int   icon_idx;   // index within category
    float scale;
    float angle_deg;
} PlacedSticker;

// Return decoded RGBA pixels for icon icon_idx in category cat_idx.
// Returns NULL on load failure.
const unsigned char *get_sticker_pixels(int cat_idx, int icon_idx);

// Alpha-blend a 16×16 RGBA sticker centred at (cx, cy) into an RGB888 buffer.
void composite_sticker_rgb888(unsigned char *photo_rgb888,
                               int photo_w, int photo_h,
                               const unsigned char *sticker_rgba,
                               int cx, int cy,
                               float scale, float angle_deg);

// Draw sticker as a citro2d image at screen position (sx, sy), size dw×dh.
void draw_sticker_c2d(int cat_idx, int icon_idx,
                      float sx, float sy, float dw, float dh);

// Alpha-blend a full-screen RGBA frame PNG over photo_rgb888 in place.
void composite_frame_rgb888(unsigned char *photo_rgb888,
                             int photo_w, int photo_h,
                             const char *frame_path);

#endif
