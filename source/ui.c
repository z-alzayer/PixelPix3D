#include "ui.h"
#include "filter.h"
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

void draw_range_slider(float cy, float abs_min, float abs_max,
                       float val_min, float val_max, float val_def) {
    float lx = slider_val_to_x(val_min, abs_min, abs_max);
    float rx = slider_val_to_x(val_max, abs_min, abs_max);
    float dx = slider_val_to_x(val_def, abs_min, abs_max);

    // Same z=0.5f for all — submission order determines front-to-back
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H/2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    if (rx > lx)
        C2D_DrawRectSolid(lx, cy - TRACK_H/2.0f, 0.5f, rx - lx, TRACK_H, CLR_FILL);
    C2D_DrawRectSolid(lx - RHANDLE_W/2.0f, cy - RHANDLE_H/2.0f, 0.5f,
                      RHANDLE_W, RHANDLE_H, CLR_HANDLE);
    C2D_DrawRectSolid(rx - RHANDLE_W/2.0f, cy - RHANDLE_H/2.0f, 0.5f,
                      RHANDLE_W, RHANDLE_H, CLR_HANDLE);
    // Dot submitted last — renders on top
    C2D_DrawRectSolid(dx - DOT_SZ/2.0f, cy - DOT_SZ/2.0f, 0.5f,
                      DOT_SZ, DOT_SZ, CLR_TITLE);
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

    if (active_tab == 0) {
        // Camera context: CAM toggle + Save action
        C2D_DrawRectSolid(BTN_CAM_X, BTN_CAM_Y, 0.5f, BTN_CAM_W, BTN_CAM_H,
                          selfie ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, selfie ? "Selfie" : "Outer");
        C2D_DrawText(&t, C2D_WithColor, BTN_CAM_X + 14.0f, 8.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT);

        u32 save_clr = save_flash ? CLR_HANDLE : CLR_BTN;
        C2D_DrawRectSolid(BTN_SAVE_X, BTN_SAVE_Y, 0.5f, BTN_SAVE_W, BTN_SAVE_H, save_clr);
        C2D_TextParse(&t, staticBuf, "Save");
        C2D_DrawText(&t, C2D_WithColor, BTN_SAVE_X + 24.0f, 8.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT);
    } else {
        // Settings context: Calibrate tab + Palette tab
        C2D_DrawRectSolid(BTN_CAM_X, BTN_CAM_Y, 0.5f, BTN_CAM_W, BTN_CAM_H,
                          active_tab == 3 ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, "Calibrate");
        C2D_DrawText(&t, C2D_WithColor, BTN_CAM_X + 4.0f, 8.0f, 0.5f, 0.44f, 0.44f,
                     active_tab == 3 ? CLR_BG : CLR_TEXT);

        C2D_DrawRectSolid(BTN_SAVE_X, BTN_SAVE_Y, 0.5f, BTN_SAVE_W, BTN_SAVE_H,
                          active_tab == 2 ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, "Palette");
        C2D_DrawText(&t, C2D_WithColor, BTN_SAVE_X + 8.0f, 8.0f, 0.5f, 0.48f, 0.48f,
                     active_tab == 2 ? CLR_BG : CLR_TEXT);
    }
}

// ---------------------------------------------------------------------------
// Camera tab
// ---------------------------------------------------------------------------

static void draw_camera_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                             FilterParams p, const FilterRanges *ranges) {
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


    // Sliders — bounds driven by calibrated ranges
    draw_slider(0, ROW_BRIGHT,   ranges->bright_min,   ranges->bright_max,   p.brightness);
    draw_slider(0, ROW_CONTRAST, ranges->contrast_min, ranges->contrast_max, p.contrast);
    draw_slider(0, ROW_SAT,      ranges->sat_min,      ranges->sat_max,      p.saturation);
    draw_slider(0, ROW_GAMMA,    ranges->gamma_min,    ranges->gamma_max,    p.gamma);
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
                               int save_scale, bool settings_flash,
                               int settings_row) {
    float sc = 0.48f;
    C2D_Text t;

    // Row cursor highlight (drawn at z=0.4f, behind buttons at z=0.5f)
    if (settings_row == 0)
        C2D_DrawRectSolid(0, SROW_SAVE_SCALE - STOG_H/2 - 2, 0.4f, 190, STOG_H + 4, CLR_ROW_CURSOR);
    else if (settings_row == 1)
        C2D_DrawRectSolid(0, SROW_DITHER - STOG_H/2 - 2, 0.4f, 190, STOG_H + 4, CLR_ROW_CURSOR);
    else if (settings_row == 2)
        C2D_DrawRectSolid(0, SROW_INVERT - STOG_H/2 - 2, 0.4f, 190, STOG_H + 4, CLR_ROW_CURSOR);

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

    // Row 2: Dither mode (4 buttons: Bayer / Cluster / Atkinson / Floyd-Steinberg)
    {
        static const char *dith_labels[4] = { "Bayr", "Clus", "Atk", "F-S" };
        static const float dith_off[4]    = { 5.0f,   5.0f,   8.0f,  8.0f };
        C2D_TextParse(&t, staticBuf, "Dither");
        C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)SROW_DITHER - 8.0f,
                     0.5f, sc, sc, CLR_TEXT);
        for (int dm = 0; dm < 4; dm++) {
            float bx = (float)(SDITH_X0 + dm * (SDITH_W + SDITH_GAP));
            bool sel = (p->dither_mode == dm);
            C2D_DrawRectSolid(bx, SROW_DITHER - STOG_H/2, 0.5f, SDITH_W, STOG_H,
                              sel ? CLR_BTN_SEL : CLR_BTN);
            C2D_TextParse(&t, staticBuf, dith_labels[dm]);
            C2D_DrawText(&t, C2D_WithColor, bx + dith_off[dm], SROW_DITHER - 8.0f,
                         0.5f, sc, sc, sel ? CLR_BG : CLR_TEXT);
        }
    }

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

    // Single "Save as Default" button — flashes green on tap
    u32 def_clr = settings_flash ? CLR_BTN_SEL : CLR_BTN;
    u32 def_txt = settings_flash ? CLR_BG      : CLR_TEXT;
    C2D_DrawRectSolid(SWBTN_X, SROW_SAVE_DEF - SWBTN_H/2, 0.5f, SWBTN_W, SWBTN_H, def_clr);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    C2D_DrawText(&t, C2D_WithColor, SWBTN_X + 36.0f, SROW_SAVE_DEF - 8.0f,
                 0.5f, sc, sc, def_txt);
}

// ---------------------------------------------------------------------------
// Palette tab
// ---------------------------------------------------------------------------

static void draw_palette_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                              const PaletteDef *user_palettes,
                              int palette_sel_pal, int palette_sel_color,
                              bool settings_flash) {
    float sc = 0.40f;
    C2D_Text t;
    (void)dynBuf;

    // --- Palette selector strip (y=31..64) ---
    const char *short_names[PALETTE_COUNT] = {"GB","Gray","GBC","Shell","GBA","DB"};
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int bx = i * PALTAB_PALSEL_BTN_W;
        bool sel = (i == palette_sel_pal);
        C2D_DrawRectSolid(bx, PALTAB_PALSEL_Y, 0.5f,
                          PALTAB_PALSEL_BTN_W - 1, PALTAB_PALSEL_H,
                          sel ? CLR_BTN_SEL : CLR_BTN);
        C2D_TextParse(&t, staticBuf, short_names[i]);
        C2D_DrawText(&t, C2D_WithColor,
                     bx + 6.0f, PALTAB_PALSEL_Y + 10.0f,
                     0.5f, sc, sc,
                     sel ? CLR_BG : CLR_TEXT);
    }

    C2D_DrawRectSolid(0, PALTAB_PALSEL_Y + PALTAB_PALSEL_H, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Colour swatch strip (y=66..120): swatch i at x = 4 + i*40 ---
    const PaletteDef *pal = &user_palettes[palette_sel_pal];
    for (int i = 0; i < pal->size; i++) {
        int sx = 4 + i * (PALTAB_SWATCH_W + 4);
        bool sel = (i == palette_sel_color);
        u32 col = C2D_Color32(pal->colors[i][0], pal->colors[i][1], pal->colors[i][2], 255);
        C2D_DrawRectSolid(sx, PALTAB_SWATCH_Y, 0.5f, PALTAB_SWATCH_W, PALTAB_SWATCH_H, col);
        if (sel) {
            // 2px highlight border
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y - 2,             0.4f, PALTAB_SWATCH_W + 4, 2, CLR_SWATCH_SEL);
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y + PALTAB_SWATCH_H, 0.4f, PALTAB_SWATCH_W + 4, 2, CLR_SWATCH_SEL);
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y - 2,             0.4f, 2, PALTAB_SWATCH_H + 4, CLR_SWATCH_SEL);
            C2D_DrawRectSolid(sx + PALTAB_SWATCH_W, PALTAB_SWATCH_Y - 2, 0.4f, 2, PALTAB_SWATCH_H + 4, CLR_SWATCH_SEL);
        }
    }

    C2D_DrawRectSolid(0, PALTAB_SWATCH_Y + PALTAB_SWATCH_H + 2, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- 2D Hue-Saturation picker + Value strip ---
    {
        uint8_t cr = pal->colors[palette_sel_color][0];
        uint8_t cg = pal->colors[palette_sel_color][1];
        uint8_t cb = pal->colors[palette_sel_color][2];
        float cur_h, cur_s, cur_v;
        rgb_to_hsv(cr, cg, cb, &cur_h, &cur_s, &cur_v);

        // HS grid: 32 hue columns x 4 saturation rows
        #define HS_COLS 32
        #define HS_ROWS  4
        float cw = (float)PALTAB_HS_W / HS_COLS;
        float ch = (float)PALTAB_HS_H / HS_ROWS;
        for (int col = 0; col < HS_COLS; col++) {
            float hue = (col + 0.5f) / HS_COLS * 360.0f;
            for (int row = 0; row < HS_ROWS; row++) {
                float sat = 1.0f - (float)row / (HS_ROWS - 1);
                uint8_t pr, pg, pb;
                hsv_to_rgb(hue, sat, 1.0f, &pr, &pg, &pb);
                C2D_DrawRectSolid(
                    PALTAB_HS_X + col * cw, PALTAB_HS_Y + row * ch, 0.5f,
                    cw + 0.5f, ch + 0.5f,
                    C2D_Color32(pr, pg, pb, 255));
            }
        }

        // Crosshair at current H/S
        float cx = PALTAB_HS_X + cur_h / 360.0f * PALTAB_HS_W;
        float cy_hs = PALTAB_HS_Y + (1.0f - cur_s) * PALTAB_HS_H;
        C2D_DrawRectSolid(cx - 0.5f, PALTAB_HS_Y, 0.4f, 1.0f, PALTAB_HS_H, CLR_SWATCH_SEL);
        C2D_DrawRectSolid(PALTAB_HS_X, cy_hs - 0.5f, 0.4f, PALTAB_HS_W, 1.0f, CLR_SWATCH_SEL);

        // Value strip: 32 segments black→full colour
        #define VAL_SEGS 32
        float sw = (float)PALTAB_VAL_W / VAL_SEGS;
        for (int i = 0; i < VAL_SEGS; i++) {
            float val = (float)i / (VAL_SEGS - 1);
            uint8_t vr, vg, vb;
            hsv_to_rgb(cur_h, cur_s, val, &vr, &vg, &vb);
            C2D_DrawRectSolid(
                PALTAB_VAL_X + i * sw, PALTAB_VAL_Y, 0.5f,
                sw + 0.5f, PALTAB_VAL_H,
                C2D_Color32(vr, vg, vb, 255));
        }

        // Value cursor
        float vx = PALTAB_VAL_X + cur_v * PALTAB_VAL_W;
        C2D_DrawRectSolid(vx - 0.5f, PALTAB_VAL_Y, 0.4f, 1.0f, PALTAB_VAL_H, CLR_SWATCH_SEL);

        #undef HS_COLS
        #undef HS_ROWS
        #undef VAL_SEGS
    }

    // --- Reset button (left half) ---
    C2D_DrawRectSolid(PALTAB_RESET_X, PALTAB_BTN_Y, 0.5f,
                      PALTAB_RESET_W, PALTAB_BTN_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Reset Pal");
    C2D_DrawText(&t, C2D_WithColor,
                 PALTAB_RESET_X + 6.0f, PALTAB_BTN_Y + 4.0f,
                 0.5f, sc, sc, CLR_TEXT);

    // --- Save as Default button (right half) ---
    u32 save_col = settings_flash ? CLR_BTN_SEL : CLR_BTN;
    C2D_DrawRectSolid(PALTAB_SAVE_DEF_X, PALTAB_BTN_Y, 0.5f,
                      PALTAB_SAVE_DEF_W, PALTAB_BTN_H, save_col);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    C2D_DrawText(&t, C2D_WithColor,
                 PALTAB_SAVE_DEF_X + 4.0f, PALTAB_BTN_Y + 4.0f,
                 0.5f, sc, sc, settings_flash ? CLR_BG : CLR_TEXT);
}

// ---------------------------------------------------------------------------
// Calibrate tab
// ---------------------------------------------------------------------------

// Absolute bounds used for the calibrate sliders (wider than operational range)
#define CAL_BRIGHT_ABS_MIN   0.0f
#define CAL_BRIGHT_ABS_MAX   4.0f
#define CAL_CONTRAST_ABS_MIN 0.1f
#define CAL_CONTRAST_ABS_MAX 4.0f
#define CAL_SAT_ABS_MIN      0.0f
#define CAL_SAT_ABS_MAX      4.0f
#define CAL_GAMMA_ABS_MIN    0.1f
#define CAL_GAMMA_ABS_MAX    4.0f

static void draw_calibrate_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                                const FilterRanges *ranges, bool settings_flash) {
    float sc = 0.44f;
    C2D_Text t;
    char buf[16];

    C2D_TextBufClear(dynBuf);

    // Row: Brightness
    C2D_TextParse(&t, staticBuf, "Bright");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_BRIGHT, CAL_BRIGHT_ABS_MIN, CAL_BRIGHT_ABS_MAX,
                      ranges->bright_min, ranges->bright_max, ranges->bright_def);
    snprintf(buf, sizeof(buf), "%.1f|%.1f|%.1f",
             (double)ranges->bright_min, (double)ranges->bright_def, (double)ranges->bright_max);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT + 7.0f, 0.5f, 0.36f, 0.36f, CLR_DIM);

    // Row: Contrast
    C2D_TextParse(&t, staticBuf, "Contrast");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_CONTRAST, CAL_CONTRAST_ABS_MIN, CAL_CONTRAST_ABS_MAX,
                      ranges->contrast_min, ranges->contrast_max, ranges->contrast_def);
    snprintf(buf, sizeof(buf), "%.1f|%.1f|%.1f",
             (double)ranges->contrast_min, (double)ranges->contrast_def, (double)ranges->contrast_max);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST + 7.0f, 0.5f, 0.36f, 0.36f, CLR_DIM);

    // Row: Saturation
    C2D_TextParse(&t, staticBuf, "Saturate");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_SAT, CAL_SAT_ABS_MIN, CAL_SAT_ABS_MAX,
                      ranges->sat_min, ranges->sat_max, ranges->sat_def);
    snprintf(buf, sizeof(buf), "%.1f|%.1f|%.1f",
             (double)ranges->sat_min, (double)ranges->sat_def, (double)ranges->sat_max);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT + 7.0f, 0.5f, 0.36f, 0.36f, CLR_DIM);

    // Row: Gamma
    C2D_TextParse(&t, staticBuf, "Gamma");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_GAMMA, CAL_GAMMA_ABS_MIN, CAL_GAMMA_ABS_MAX,
                      ranges->gamma_min, ranges->gamma_max, ranges->gamma_def);
    snprintf(buf, sizeof(buf), "%.1f|%.1f|%.1f",
             (double)ranges->gamma_min, (double)ranges->gamma_def, (double)ranges->gamma_max);
    C2D_TextParse(&t, dynBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA + 7.0f, 0.5f, 0.36f, 0.36f, CLR_DIM);

    C2D_DrawRectSolid(0, 148, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // Save as Default button
    u32 def_clr = settings_flash ? CLR_BTN_SEL : CLR_BTN;
    u32 def_txt = settings_flash ? CLR_BG      : CLR_TEXT;
    C2D_DrawRectSolid(SWBTN_X, SROW_SAVE_DEF - SWBTN_H/2, 0.5f, SWBTN_W, SWBTN_H, def_clr);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    C2D_DrawText(&t, C2D_WithColor, SWBTN_X + 36.0f, SROW_SAVE_DEF - 8.0f,
                 0.5f, 0.48f, 0.48f, def_txt);
}

// ---------------------------------------------------------------------------
// Draw UI (top-level)
// ---------------------------------------------------------------------------

void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             bool save_flash, bool warn3d,
             int active_tab, int save_scale, bool settings_flash,
             int settings_row,
             const PaletteDef *user_palettes,
             int palette_sel_pal, int palette_sel_color,
             const FilterRanges *ranges,
             bool comparing) {
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

    if (comparing) {
        C2D_Text t;
        C2D_DrawRectSolid(0, 30, 0.6f, BOT_W, BOT_H - 30, C2D_Color32(0, 0, 0, 120));
        C2D_TextParse(&t, staticBuf, "RAW");
        C2D_DrawText(&t, C2D_WithColor, 132.0f, 112.0f, 0.6f, 1.4f, 1.4f, C2D_Color32(255, 220, 50, 255));
        C2D_TextParse(&t, staticBuf, "hold SELECT to compare");
        C2D_DrawText(&t, C2D_WithColor, 36.0f, 158.0f, 0.6f, 0.44f, 0.44f, C2D_Color32(200, 200, 180, 200));
        return;
    }

    if (active_tab == 0) {
        draw_camera_tab(staticBuf, dynBuf, p, ranges);
    } else if (active_tab == 1) {
        draw_settings_tab(staticBuf, &p, save_scale, settings_flash, settings_row);
    } else if (active_tab == 2) {
        draw_palette_tab(staticBuf, dynBuf, user_palettes, palette_sel_pal, palette_sel_color, settings_flash);
    } else if (active_tab == 3) {
        draw_calibrate_tab(staticBuf, dynBuf, ranges, settings_flash);
    }
}
