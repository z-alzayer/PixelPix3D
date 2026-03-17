#include "ui.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int px_stop_x(int val) {
    // val in [1..8], map to [TRACK_X .. TRACK_X+TRACK_W]
    return TRACK_X + (val - 1) * TRACK_W / (PX_STOPS - 1);
}

float slider_val_to_x(float val, float mn, float mx) {
    float t = (val - mn) / (mx - mn);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return TRACK_X + t * TRACK_W;
}

float touch_x_to_val(int px, float mn, float mx) {
    float t = (float)(px - TRACK_X) / TRACK_W;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return mn + t * (mx - mn);
}

// ---------------------------------------------------------------------------
// Slider drawing helpers
// ---------------------------------------------------------------------------

void draw_slider(float cx, float cy, float mn, float mx, float val) {
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = slider_val_to_x(val, mn, mx);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
    (void)cx;
}

void draw_snap_slider(int px_val) {
    float cy = ROW_PXSIZE;
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = px_stop_x(px_val);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
}

// ---------------------------------------------------------------------------
// Tab bar
// ---------------------------------------------------------------------------

static void draw_tab_bar(C2D_TextBuf staticBuf, int active_tab,
                         bool selfie, bool save_flash) {
    C2D_Text t;

    // Tab 0: Camera
    C2D_DrawRectSolid(TAB_0_X, TAB_BAR_Y, 0.5f, TAB_0_W, TAB_BAR_H,
                      active_tab == 0 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Camera");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, 8.0f, 0.5f, 0.48f, 0.48f,
                 active_tab == 0 ? CLR_BG : CLR_TEXT);

    // Tab 1: Settings
    C2D_DrawRectSolid(TAB_1_X, TAB_BAR_Y, 0.5f, TAB_1_W, TAB_BAR_H,
                      active_tab == 1 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Settings");
    C2D_DrawText(&t, C2D_WithColor, 84.0f, 8.0f, 0.5f, 0.48f, 0.48f,
                 active_tab == 1 ? CLR_BG : CLR_TEXT);

    // CAM action button
    C2D_DrawRectSolid(BTN_CAM_X, BTN_CAM_Y, 0.5f, BTN_CAM_W, BTN_CAM_H,
                      selfie ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, selfie ? "Selfie" : "Outer");
    C2D_DrawText(&t, C2D_WithColor, BTN_CAM_X + 14.0f, 8.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT);

    // SAVE action button
    u32 save_clr = save_flash ? CLR_HANDLE : CLR_BTN;
    C2D_DrawRectSolid(BTN_SAVE_X, BTN_SAVE_Y, 0.5f, BTN_SAVE_W, BTN_SAVE_H, save_clr);
    C2D_TextParse(&t, staticBuf, "Save");
    C2D_DrawText(&t, C2D_WithColor, BTN_SAVE_X + 24.0f, 8.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT);
}

// ---------------------------------------------------------------------------
// Camera tab
// ---------------------------------------------------------------------------

static void draw_camera_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                             FilterParams p) {
    float sc = 0.48f;
    C2D_Text t;

    C2D_DrawRectSolid(0, 200, 0.5f, BOT_W, 1, CLR_DIVIDER);

    C2D_TextParse(&t, staticBuf, "Bright");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Contrast");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Saturate");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Gamma");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Px Size");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_PXSIZE - 9.0f, 0.5f, sc, sc, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Palette");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 185.0f, 0.5f, sc, sc, CLR_DIM);

    // Px size tick labels
    C2D_TextParse(&t, staticBuf, "1");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(1) - 2.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "4");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(4) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "8");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(8) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    // Palette buttons
    const char *pal_names[7] = {"GB","Gray","GBC","Shell","GBA","DB","Clr"};
    for (int i = 0; i < 7; i++) {
        int pal_val = (i < 6) ? i : PALETTE_NONE;
        bool sel = (p.palette == pal_val);
        float bx = PAL_BTN_X0 + i * (PAL_BTN_W + 2);
        C2D_DrawRectSolid(bx, PAL_BTN_Y, 0.5f, PAL_BTN_W, PAL_BTN_H,
                          sel ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, pal_names[i]);
        C2D_DrawText(&t, C2D_WithColor, bx + 4.0f, PAL_BTN_Y + 8.0f, 0.5f,
                     0.40f, 0.40f, sel ? CLR_BG : CLR_TEXT);
    }


    // Sliders
    draw_slider(0, ROW_BRIGHT,   0.0f, 2.0f, p.brightness);
    draw_slider(0, ROW_CONTRAST, 0.5f, 2.0f, p.contrast);
    draw_slider(0, ROW_SAT,      0.0f, 2.0f, p.saturation);
    draw_slider(0, ROW_GAMMA,    0.5f, 2.0f, p.gamma);
    draw_snap_slider(p.pixel_size);

    // Dynamic value readouts
    char buf[16];
    C2D_TextBufClear(dynBuf);

    snprintf(buf, sizeof(buf), "%.1f", p.brightness);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.contrast);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.saturation);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%.1f", p.gamma);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_DIM);

    snprintf(buf, sizeof(buf), "%d", p.pixel_size);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)ROW_PXSIZE - 9.0f, 0.5f, sc, sc, CLR_DIM);
}

// ---------------------------------------------------------------------------
// Settings tab
// ---------------------------------------------------------------------------

static void draw_settings_tab(C2D_TextBuf staticBuf, const FilterParams *p,
                               int save_scale) {
    float sc = 0.48f;
    C2D_Text t;

    // Row 1: Save Scale
    C2D_TextParse(&t, staticBuf, "Save Scale");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)SROW_SAVE_SCALE - 8.0f,
                 0.5f, sc, sc, CLR_TEXT);
    C2D_DrawRectSolid(STOG_X0, SROW_SAVE_SCALE - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      save_scale == 1 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "1x");
    C2D_DrawText(&t, C2D_WithColor, STOG_X0 + 20.0f, SROW_SAVE_SCALE - 8.0f,
                 0.5f, sc, sc, save_scale == 1 ? CLR_BG : CLR_TEXT);
    C2D_DrawRectSolid(STOG_X1, SROW_SAVE_SCALE - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      save_scale == 2 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "2x");
    C2D_DrawText(&t, C2D_WithColor, STOG_X1 + 20.0f, SROW_SAVE_SCALE - 8.0f,
                 0.5f, sc, sc, save_scale == 2 ? CLR_BG : CLR_TEXT);

    // Row 2: Dither mode
    C2D_TextParse(&t, staticBuf, "Dither");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)SROW_DITHER - 8.0f,
                 0.5f, sc, sc, CLR_TEXT);
    C2D_DrawRectSolid(STOG_X0, SROW_DITHER - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      p->dither_mode == 0 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Bayer");
    C2D_DrawText(&t, C2D_WithColor, STOG_X0 + 10.0f, SROW_DITHER - 8.0f,
                 0.5f, sc, sc, p->dither_mode == 0 ? CLR_BG : CLR_TEXT);
    C2D_DrawRectSolid(STOG_X1, SROW_DITHER - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      p->dither_mode == 1 ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "F-S");
    C2D_DrawText(&t, C2D_WithColor, STOG_X1 + 18.0f, SROW_DITHER - 8.0f,
                 0.5f, sc, sc, p->dither_mode == 1 ? CLR_BG : CLR_TEXT);

    // Row 3: Invert
    C2D_TextParse(&t, staticBuf, "Invert");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)SROW_INVERT - 8.0f,
                 0.5f, sc, sc, CLR_TEXT);
    C2D_DrawRectSolid(STOG_X0, SROW_INVERT - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      !p->invert ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Off");
    C2D_DrawText(&t, C2D_WithColor, STOG_X0 + 18.0f, SROW_INVERT - 8.0f,
                 0.5f, sc, sc, !p->invert ? CLR_BG : CLR_TEXT);
    C2D_DrawRectSolid(STOG_X1, SROW_INVERT - STOG_H/2, 0.5f, STOG_W, STOG_H,
                      p->invert ? CLR_BTN_SEL : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "On");
    C2D_DrawText(&t, C2D_WithColor, STOG_X1 + 22.0f, SROW_INVERT - 8.0f,
                 0.5f, sc, sc, p->invert ? CLR_BG : CLR_TEXT);

    C2D_DrawRectSolid(0, 148, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // Row 4: Set as Default
    C2D_DrawRectSolid(SWBTN_X, SROW_SET_DEF - SWBTN_H/2, 0.5f, SWBTN_W, SWBTN_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Set as Default");
    C2D_DrawText(&t, C2D_WithColor, SWBTN_X + 40.0f, SROW_SET_DEF - 8.0f,
                 0.5f, sc, sc, CLR_TEXT);

    // Row 5: Save Defaults
    C2D_DrawRectSolid(SWBTN_X, SROW_SAVE_DEF - SWBTN_H/2, 0.5f, SWBTN_W, SWBTN_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Save Defaults");
    C2D_DrawText(&t, C2D_WithColor, SWBTN_X + 44.0f, SROW_SAVE_DEF - 8.0f,
                 0.5f, sc, sc, CLR_TEXT);
}

// ---------------------------------------------------------------------------
// Draw UI (top-level)
// ---------------------------------------------------------------------------

void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             bool save_flash, bool warn3d,
             int active_tab, int save_scale) {
    C2D_TargetClear(bot, CLR_BG);
    C2D_SceneBegin(bot);

    if (warn3d) {
        C2D_Text t;
        C2D_TextBufClear(staticBuf);
        C2D_TextParse(&t, staticBuf, "3D slider not supported");
        C2D_DrawText(&t, C2D_WithColor, 34.0f, 108.0f, 0.5f, 0.55f, 0.55f, C2D_Color32(220, 80, 80, 255));
        C2D_TextParse(&t, staticBuf, "Please set slider to 0");
        C2D_DrawText(&t, C2D_WithColor, 38.0f, 128.0f, 0.5f, 0.48f, 0.48f, C2D_Color32(180, 60, 60, 255));
        return;
    }

    C2D_TextBufClear(staticBuf);

    draw_tab_bar(staticBuf, active_tab, selfie, save_flash);
    C2D_DrawRectSolid(0, 29, 0.5f, BOT_W, 1, CLR_DIVIDER);

    if (active_tab == 0) {
        draw_camera_tab(staticBuf, dynBuf, p);
    } else {
        draw_settings_tab(staticBuf, &p, save_scale);
    }
}
