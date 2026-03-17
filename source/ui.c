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
// Draw UI
// ---------------------------------------------------------------------------

void draw_slider(float cx, float cy, float mn, float mx, float val) {
    // Track background
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    // Filled portion
    float hx = slider_val_to_x(val, mn, mx);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    // Handle
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
    (void)cx;
}

void draw_snap_slider(int px_val) {
    float cy = ROW_PXSIZE;
    // Track background
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    // Filled portion up to current value
    float hx = px_stop_x(px_val);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    // Handle
    C2D_DrawRectSolid(hx - HANDLE_W/2.0f, cy - HANDLE_H/2.0f, 0.5f,
                      HANDLE_W, HANDLE_H, CLR_HANDLE);
}

void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             bool save_flash, bool warn3d) {
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

    // Title bar divider
    C2D_DrawRectSolid(0, 29, 0.5f, BOT_W, 1, CLR_DIVIDER);
    // Slider area / palette divider
    C2D_DrawRectSolid(0, 200, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Static text labels ---
    float sc = 0.48f;

    C2D_Text t;
    C2D_TextBufClear(staticBuf);

    C2D_TextParse(&t, staticBuf, "PixelPix3D");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 6.0f, 0.5f, 0.52f, 0.52f, CLR_TITLE);

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

    // Px size tick labels at 1, 4, 8
    C2D_TextParse(&t, staticBuf, "1");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(1) - 2.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "4");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(4) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "8");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(8) - 3.0f, (float)ROW_PXSIZE + 7.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    // Palette button labels
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

    // CAM / SAVE buttons
    u32 cam_clr  = selfie ? CLR_BTN_SEL : CLR_BTN;
    u32 save_clr = save_flash ? CLR_HANDLE : CLR_BTN;
    C2D_DrawRectSolid(BTN_CAM_X,  BTN_CAM_Y,  0.5f, BTN_CAM_W,  BTN_CAM_H,  cam_clr);
    C2D_DrawRectSolid(BTN_SAVE_X, BTN_SAVE_Y, 0.5f, BTN_SAVE_W, BTN_SAVE_H, save_clr);

    C2D_TextParse(&t, staticBuf, selfie ? "Selfie" : "Outer");
    C2D_DrawText(&t, C2D_WithColor, BTN_CAM_X + 4.0f, BTN_CAM_Y + 5.0f, 0.5f,
                 0.40f, 0.40f, CLR_TEXT);

    C2D_TextParse(&t, staticBuf, "Save");
    C2D_DrawText(&t, C2D_WithColor, BTN_SAVE_X + 14.0f, BTN_SAVE_Y + 5.0f, 0.5f,
                 0.40f, 0.40f, CLR_TEXT);

    // --- Sliders ---
    draw_slider(0, ROW_BRIGHT,   0.0f, 2.0f, p.brightness);
    draw_slider(0, ROW_CONTRAST, 0.5f, 2.0f, p.contrast);
    draw_slider(0, ROW_SAT,      0.0f, 2.0f, p.saturation);
    draw_slider(0, ROW_GAMMA,    0.5f, 2.0f, p.gamma);
    draw_snap_slider(p.pixel_size);

    // --- Dynamic value readouts ---
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
