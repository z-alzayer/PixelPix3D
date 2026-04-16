#include "gallery.h"
#include "camera.h"
#include "image_load.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Shared gallery thumbnail buffers
// ---------------------------------------------------------------------------

uint16_t gallery_thumbs[GALLERY_WIGGLE_MAX_FRAMES][CAMERA_WIDTH * CAMERA_HEIGHT];

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
    const char *ext = gpath + strlen(gpath) - 4;
    gal->n_frames   = 1;
    gal->delay_ms   = 250;
    gal->anim_tick  = svcGetSystemTick();
    gal->anim_frame = 0;
    if (ext > gpath && strcmp(ext, ".png") == 0) {
        uint16_t *fptrs[GALLERY_WIGGLE_MAX_FRAMES];
        for (int i = 0; i < GALLERY_WIGGLE_MAX_FRAMES; i++)
            fptrs[i] = gallery_thumbs[i];
        load_apng_frames_to_rgb565(gpath, fptrs, GALLERY_WIGGLE_MAX_FRAMES,
                                   &gal->n_frames, &gal->delay_ms,
                                   CAMERA_WIDTH, CAMERA_HEIGHT);
        if (gal->n_frames < 1) {
            load_jpeg_to_rgb565(gpath, gallery_thumbs[0], CAMERA_WIDTH, CAMERA_HEIGHT);
            gal->n_frames = 1;
        }
    } else {
        load_jpeg_to_rgb565(gpath, gallery_thumbs[0], CAMERA_WIDTH, CAMERA_HEIGHT);
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
    if (now - gal->anim_tick >= period) {
        gal->anim_frame = (gal->anim_frame + 1) % gal->n_frames;
        gal->anim_tick  = now;
    }
}
