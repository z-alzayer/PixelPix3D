#include "gallery.h"
#include "camera.h"
#include "image_load.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shared gallery thumbnail buffers
// ---------------------------------------------------------------------------

uint16_t gallery_thumbs[GALLERY_WIGGLE_MAX_FRAMES][CAMERA_WIDTH * CAMERA_HEIGHT];

static bool ext_is(const char *ext, const char *want) {
    if (!ext || !want) return false;
    while (*ext && *want) {
        char a = *ext++;
        char b = *want++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *ext == 0 && *want == 0;
}

static uint32_t rd32be_gallery(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static bool png_has_actl(const char *path) {
    uint8_t hdr[8];
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return false;
    }

    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(hdr, sig, sizeof(sig)) != 0) {
        fclose(fp);
        return false;
    }

    while (true) {
        uint8_t chunk_hdr[8];
        if (fread(chunk_hdr, 1, sizeof(chunk_hdr), fp) != sizeof(chunk_hdr))
            break;
        uint32_t len = rd32be_gallery(chunk_hdr);
        if (memcmp(chunk_hdr + 4, "acTL", 4) == 0) {
            fclose(fp);
            return true;
        }
        if (memcmp(chunk_hdr + 4, "IDAT", 4) == 0 ||
            memcmp(chunk_hdr + 4, "IEND", 4) == 0)
            break;
        if (fseek(fp, (long)len + 4, SEEK_CUR) != 0)
            break;
    }

    fclose(fp);
    return false;
}

// ---------------------------------------------------------------------------
// gallery_toggle — enter or leave gallery mode
// ---------------------------------------------------------------------------

void gallery_toggle(GalleryState *gal, AppState *app, EditState *edit,
                    Handle camReceiveEvent[4], bool *captureInterrupted) {
    gal->mode = !gal->mode;
    edit->active = false;
    if (gal->mode) {
        // Stop camera — free DMA bandwidth and CPU for gallery/edit
        if (app->cam_active) {
            CAMU_StopCapture(PORT_BOTH);
            for (int i = 2; i < 4; i++) {
                if (camReceiveEvent[i]) { svcCloseHandle(camReceiveEvent[i]); camReceiveEvent[i] = 0; }
            }
            *captureInterrupted = false;
            app->cam_active = false;
        }
        gal->count  = list_saved_photos(SAVE_DIR, gal->paths, GALLERY_MAX);
        gal->sel    = 0;
        gal->scroll = 0;
        gal->loaded = -1;
    } else {
        // Restart camera
        if (!app->cam_active) {
            CAMU_ClearBuffer(PORT_BOTH);
            if (!app->selfie) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
            CAMU_StartCapture(PORT_BOTH);
            app->cam_active = true;
        }
    }
}

// ---------------------------------------------------------------------------
// gallery_load_selected — load photo into thumb buffers on selection change
// ---------------------------------------------------------------------------

void gallery_load_selected(GalleryState *gal) {
    if (!gal->mode || gal->count <= 0 || gal->loaded == gal->sel)
        return;

    const char *gpath = gal->paths[gal->sel];
    const char *ext = strrchr(gpath, '.');
    bool is_png = ext_is(ext, ".png");
    bool is_gif = ext_is(ext, ".gif");
    gal->n_frames   = 1;
    gal->delay_ms   = WIGGLE_DEFAULT_DELAY_MS;
    gal->anim_tick  = svcGetSystemTick();
    gal->anim_frame = 0;
    uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
    if (is_png || is_gif) {
        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++)
            fptrs[i] = gallery_thumbs[i];
        int ok = 0;
        if (is_gif) {
            ok = load_gif_frames_to_rgb565(gpath, fptrs, GALLERY_WIGGLE_MAX_FRAMES,
                                           &gal->n_frames, &gal->delay_ms,
                                           CAMERA_WIDTH, CAMERA_HEIGHT);
        } else {
            // PNGs can be either animated wiggles or still Ana exports. Avoid
            // the APNG parser for ordinary PNGs; it is only needed when acTL is
            // present before IDAT.
            if (png_has_actl(gpath)) {
                ok = load_apng_frames_to_rgb565(gpath, fptrs,
                                                GALLERY_WIGGLE_MAX_FRAMES,
                                                &gal->n_frames, &gal->delay_ms,
                                                CAMERA_WIDTH, CAMERA_HEIGHT);
            } else {
                ok = load_png_to_rgb565_fast(gpath, gallery_thumbs[0],
                                             CAMERA_WIDTH, CAMERA_HEIGHT);
                gal->n_frames = ok ? 1 : 0;
            }
        }
        if (!ok || gal->n_frames < 1) {
            memset(gallery_thumbs[0], 0,
                   CAMERA_WIDTH * CAMERA_HEIGHT * sizeof(uint16_t));
            gal->n_frames = 1;
        }
    } else {
        int ok = load_jpeg_to_rgb565(gpath, gallery_thumbs[0], CAMERA_WIDTH, CAMERA_HEIGHT);
        if (!ok)
            memset(gallery_thumbs[0], 0,
                   CAMERA_WIDTH * CAMERA_HEIGHT * sizeof(uint16_t));
    }
    gal->loaded = gal->sel;
}

// ---------------------------------------------------------------------------
// gallery_handle_dpad — d-pad scrolling in the 4×4 grid
// ---------------------------------------------------------------------------

void gallery_handle_dpad(GalleryState *gal, u32 kDown) {
    if (!gal->mode)
        return;

    #define GAL_GRID_COLS 4
    #define GAL_GRID_ROWS 4
    int total_rows = (gal->count + GAL_GRID_COLS - 1) / GAL_GRID_COLS;
    int max_scroll = total_rows - GAL_GRID_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (kDown & KEY_DDOWN)  { if (gal->scroll < max_scroll) gal->scroll++; }
    if (kDown & KEY_DUP)    { if (gal->scroll > 0) gal->scroll--; }
    if (kDown & KEY_DRIGHT) { gal->sel++; if (gal->sel >= gal->count) gal->sel = gal->count - 1; }
    if (kDown & KEY_DLEFT)  { gal->sel--; if (gal->sel < 0) gal->sel = 0; }
    #undef GAL_GRID_COLS
    #undef GAL_GRID_ROWS
}

// ---------------------------------------------------------------------------
// gallery_tick — advance gallery wiggle animation (clock-based)
// ---------------------------------------------------------------------------

void gallery_tick(GalleryState *gal) {
    if (!gal->mode || gal->n_frames <= 1)
        return;

    u64 now = svcGetSystemTick();
    u64 period = (u64)gal->delay_ms * SYSCLOCK_ARM11 / 1000;
    if (period == 0) period = 1;
    u64 elapsed = now - gal->anim_tick;
    if (elapsed >= period) {
        u64 steps = elapsed / period;
        if (steps > 0) {
            gal->anim_frame = (gal->anim_frame + (int)(steps % gal->n_frames)) % gal->n_frames;
            gal->anim_tick += steps * period;
        }
    }
}
