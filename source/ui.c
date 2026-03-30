#include "ui.h"
#include "filter.h"
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int px_stop_x(int val) {
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
// Rounded-rect helper
//
// Citro2D has no native rounded rect, so we approximate with a cross of
// three overlapping rects (same technique used by most 2D engines without
// arc primitives).  For radius r:
//
//   ┌──────────────────┐
//   │   top stripe     │  h=r, y-aligned, inset r on each side
//   ├──┬────────────┬──┤
//   │  │  middle    │  │  full height, inset r on left/right
//   ├──┴────────────┴──┤
//   │   bottom stripe  │  h=r
//   └──────────────────┘
//
// This produces a shape with proper squared-off diagonal corners instead of
// the cut-corner look from the erase trick.  For r ≤ 6 on a 3DS screen it
// looks convincingly rounded.
// ---------------------------------------------------------------------------

static void draw_rounded_rect(float x, float y, float w, float h, float r, u32 col) {
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    if (r <= 0.5f) {
        C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
        return;
    }

    // Rasterise each corner as N horizontal scanlines approximating a quarter-circle.
    // N = (int)r gives 1px-tall slices — enough for smooth curves at 3DS resolution.
    int N = (int)(r + 0.5f);

    // Centre band: full width, inner height (skips corner rows)
    C2D_DrawRectSolid(x, y + r, 0.5f, w, h - 2.0f * r, col);

    for (int i = 0; i < N; i++) {
        // Fraction from corner centre: 0 = top of arc, 1 = side of arc
        float t  = (N - i - 0.5f) / (float)N;   // goes 1..0 as i goes 0..N-1
        float dx = r * (1.0f - sqrtf(1.0f - t * t));  // indent from left/right edge
        float span_x = x + dx;
        float span_w = w - 2.0f * dx;

        // Top row i
        C2D_DrawRectSolid(span_x, y + i, 0.5f, span_w, 1.0f, col);
        // Bottom row i (mirrored)
        C2D_DrawRectSolid(span_x, y + h - 1.0f - i, 0.5f, span_w, 1.0f, col);
    }
}

// Rounded button — ~30% of height gives a soft but not fully pill-shaped corner
static void draw_pill(float x, float y, float w, float h, u32 col) {
    draw_rounded_rect(x, y, w, h, h * 0.28f, col);
}

static void draw_rounded_rect_on_panel(float x, float y, float w, float h,
                                       float r, u32 col) {
    draw_rounded_rect(x, y, w, h, r, col);
}

// ---------------------------------------------------------------------------
// Slider drawing helpers (restyled)
// ---------------------------------------------------------------------------

void draw_slider(float cx, float cy, float mn, float mx, float val) {
    // Track
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = slider_val_to_x(val, mn, mx);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    // Handle (rounded via helper)
    draw_rounded_rect_on_panel(hx - HANDLE_W / 2.0f, cy - HANDLE_H / 2.0f,
                               HANDLE_W, HANDLE_H, 3.0f, CLR_HANDLE);
    (void)cx;
}

void draw_snap_slider(float cy, int px_val) {
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = (float)px_stop_x(px_val);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    draw_rounded_rect_on_panel(hx - HANDLE_W / 2.0f, cy - HANDLE_H / 2.0f,
                               HANDLE_W, HANDLE_H, 3.0f, CLR_HANDLE);
}

void draw_range_slider(float cy, float abs_min, float abs_max,
                       float val_min, float val_max, float val_def) {
    float lx = slider_val_to_x(val_min, abs_min, abs_max);
    float rx = slider_val_to_x(val_max, abs_min, abs_max);
    float dx = slider_val_to_x(val_def, abs_min, abs_max);

    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    if (rx > lx)
        C2D_DrawRectSolid(lx, cy - TRACK_H / 2.0f, 0.5f, rx - lx, TRACK_H, CLR_FILL);
    C2D_DrawRectSolid(lx - RHANDLE_W / 2.0f, cy - RHANDLE_H / 2.0f, 0.5f,
                      RHANDLE_W, RHANDLE_H, CLR_HANDLE);
    C2D_DrawRectSolid(rx - RHANDLE_W / 2.0f, cy - RHANDLE_H / 2.0f, 0.5f,
                      RHANDLE_W, RHANDLE_H, CLR_HANDLE);
    // Default-value dot on top
    C2D_DrawRectSolid(dx - DOT_SZ / 2.0f, cy - DOT_SZ / 2.0f, 0.4f,
                      DOT_SZ, DOT_SZ, CLR_TITLE);
}

// ---------------------------------------------------------------------------
// Bottom nav bar (always visible at y=200)
// ---------------------------------------------------------------------------

static void draw_bottom_nav(C2D_TextBuf buf, int active_tab) {
    C2D_Text t;
    const char *labels[4] = { "Shoot", "Style", "FX", "More" };

    // Nav bar background
    C2D_DrawRectSolid(0, NAV_Y, 0.5f, BOT_W, NAV_H, CLR_PANEL);
    // Top border
    C2D_DrawRectSolid(0, NAV_Y, 0.5f, BOT_W, 1, CLR_DIVIDER);

    for (int i = 0; i < 4; i++) {
        float bx = (float)(i * NAV_SEG_W);
        bool sel = (active_tab == i) || (i == TAB_MORE && active_tab >= TAB_MORE);

        // Selected tab: white pill
        if (sel) {
            draw_pill(bx + 4, NAV_Y + 4, NAV_SEG_W - 8, NAV_H - 8, CLR_WHITE);
        }

        // Label
        C2D_TextParse(&t, buf, labels[i]);
        float tw = 0, th = 0;
        (void)th;
        C2D_TextGetDimensions(&t, 0.44f, 0.44f, &tw, &th);
        float tx = bx + (NAV_SEG_W - tw) / 2.0f;
        float ty_nav = NAV_Y + (NAV_H - 12.0f) / 2.0f;
        C2D_DrawText(&t, C2D_WithColor, tx, ty_nav, 0.5f, 0.44f, 0.44f,
                     sel ? CLR_ACCENT : CLR_DIM);

        // Active indicator dot below label
        if (sel) {
            float dot_x = bx + NAV_SEG_W / 2.0f - 2.0f;
            C2D_DrawRectSolid(dot_x, (float)(NAV_Y + NAV_H - NAV_INDICATOR_H - 2),
                              0.4f, 4.0f, (float)NAV_INDICATOR_H, CLR_ACCENT);
        }

        // Vertical separator between segments
        if (i < 3)
            C2D_DrawRectSolid(bx + NAV_SEG_W - 1, NAV_Y + 6, 0.5f, 1, NAV_H - 12, CLR_DIVIDER);
    }
}

// ---------------------------------------------------------------------------
// SHOOT tab
// ---------------------------------------------------------------------------

static void draw_shoot_tab(C2D_TextBuf staticBuf,
                           bool selfie, int save_flash,
                           const PaletteDef *user_palettes,
                           int active_palette,
                           bool gallery_mode) {
    C2D_Text t;

    // Background for strip area
    C2D_DrawRectSolid(0, SHOOT_STRIP_Y, 0.5f, BOT_W, SHOOT_STRIP_H, CLR_PANEL);

    // --- Camera flip button (left) ---
    bool cam_hovered = false; // no hover state on 3DS, but keep structure
    (void)cam_hovered;
    draw_pill(SHOOT_CAM_X + 3, SHOOT_CAM_Y + 4,
                      SHOOT_CAM_W - 6, SHOOT_CAM_H - 8,
                      selfie ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, selfie ? "Selfie" : "Outer");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    float lx = SHOOT_CAM_X + 3 + ((SHOOT_CAM_W - 6) - tw) / 2.0f;
    C2D_DrawText(&t, C2D_WithColor, lx, SHOOT_CAM_Y + 14.0f, 0.5f, 0.42f, 0.42f,
                 selfie ? CLR_WHITE : CLR_TEXT);

    // --- Palette swatches (centre) ---
    const u32 none_col = C2D_Color32(150, 150, 150, 255);
    for (int i = 0; i < 7; i++) {
        int pal_val = (i < 6) ? i : PALETTE_NONE;
        bool sel = (active_palette == pal_val);
        float sx = (float)(SHOOT_SWATCH_X0 + i * (SHOOT_SWATCH_W + SHOOT_SWATCH_GAP));
        float sy = (float)SHOOT_SWATCH_Y;

        // Selection ring
        if (sel) {
            C2D_DrawRectSolid(sx - 2, sy - 2, 0.4f,
                              SHOOT_SWATCH_W + 4, SHOOT_SWATCH_H + 4, CLR_SEL);
        }

        // Swatch colour = first colour of palette, or grey for "None"
        u32 swatch_col;
        if (i < 6 && user_palettes[i].size > 0) {
            swatch_col = C2D_Color32(user_palettes[i].colors[0][0],
                                     user_palettes[i].colors[0][1],
                                     user_palettes[i].colors[0][2], 255);
        } else {
            swatch_col = none_col;
        }
        draw_rounded_rect_on_panel(sx, sy, SHOOT_SWATCH_W, SHOOT_SWATCH_H, 3.0f, swatch_col);

        // "C" label for Colour/None
        if (i == 6) {
            C2D_TextParse(&t, staticBuf, "C");
            C2D_DrawText(&t, C2D_WithColor, sx + 5.0f, sy + 8.0f, 0.5f, 0.38f, 0.38f, CLR_WHITE);
        }
    }

    // --- Gallery button (right zone) ---
    draw_pill(SHOOT_GAL_X + 3, SHOOT_GAL_Y + 4,
                      SHOOT_GAL_W - 6, SHOOT_GAL_H - 8,
                      gallery_mode ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Gallery");
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    float glx = SHOOT_GAL_X + 3 + ((SHOOT_GAL_W - 6) - tw) / 2.0f;
    C2D_DrawText(&t, C2D_WithColor, glx, SHOOT_GAL_Y + 14.0f, 0.5f, 0.42f, 0.42f,
                 gallery_mode ? CLR_WHITE : CLR_TEXT);

    // --- Future area hint (shown only when not in gallery mode) ---
    if (!gallery_mode) {
        C2D_TextParse(&t, staticBuf, "More features coming soon");
        C2D_DrawText(&t, C2D_WithColor, 74.0f, 98.0f, 0.5f, 0.40f, 0.40f, CLR_DIVIDER);
    }

    if (!gallery_mode) {
        // --- Save button ---
        u32 save_bg, save_txt;
        const char *save_label;
        if (save_flash >= 20) {
            save_bg  = CLR_ACCENT;
            save_txt = CLR_WHITE;
            save_label = "Saving...";
        } else if (save_flash > 0) {
            save_bg  = CLR_CONFIRM;
            save_txt = CLR_WHITE;
            save_label = "Saved!";
        } else {
            save_bg  = CLR_ACCENT;
            save_txt = CLR_WHITE;
            save_label = "Save";
        }
        C2D_DrawRectSolid(0, SHOOT_SAVE_Y, 0.5f, BOT_W, SHOOT_SAVE_H, save_bg);
        C2D_TextParse(&t, staticBuf, save_label);
        C2D_TextGetDimensions(&t, 0.56f, 0.56f, &tw, &th);
        float slx = (BOT_W - tw) / 2.0f;
        C2D_DrawText(&t, C2D_WithColor, slx, (float)SHOOT_SAVE_Y + 12.0f, 0.5f,
                     0.56f, 0.56f, save_txt);
    }
}

// ---------------------------------------------------------------------------
// Gallery tab (shown in place of shoot content when gallery_mode is true)
// ---------------------------------------------------------------------------

static void draw_gallery_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                              int gallery_count, const char gallery_paths[][64],
                              int gallery_sel, int gallery_scroll) {
    C2D_Text t;
    (void)dynBuf;

    if (gallery_count == 0) {
        C2D_TextParse(&t, staticBuf, "No photos yet");
        C2D_DrawText(&t, C2D_WithColor, 90.0f, 90.0f, 0.5f, 0.55f, 0.55f, CLR_DIM);
        return;
    }

    for (int row = 0; row < GALLERY_ROWS; row++) {
        for (int col = 0; col < GALLERY_COLS; col++) {
            int idx = (gallery_scroll * GALLERY_COLS) + row * GALLERY_COLS + col;
            if (idx >= gallery_count) break;

            float cx = GALLERY_GAP + col * (GALLERY_CELL_W + GALLERY_GAP);
            float cy = GALLERY_START_Y + row * GALLERY_ROW_H;

            bool sel = (idx == gallery_sel);
            draw_pill(cx, cy, GALLERY_CELL_W, GALLERY_CELL_H,
                              sel ? CLR_ACCENT : CLR_BTN);

            const char *path = gallery_paths[idx];
            const char *slash = path;
            for (const char *p = path; *p; p++) if (*p == '/') slash = p + 1;
            char label[10] = {0};
            int n = 0;
            if (sscanf(slash, "GB_%d.JPG", &n) == 1)
                snprintf(label, sizeof(label), "%04d", n);
            else
                snprintf(label, sizeof(label), "?");
            C2D_TextParse(&t, staticBuf, label);
            C2D_DrawText(&t, C2D_WithColor,
                         cx + 18.0f, cy + GALLERY_CELL_H / 2.0f - 6.0f,
                         0.5f, 0.42f, 0.42f,
                         sel ? CLR_WHITE : CLR_TEXT);
        }
    }

    int total_rows = (gallery_count + GALLERY_COLS - 1) / GALLERY_COLS;
    int max_scroll = total_rows - GALLERY_ROWS;
    if (max_scroll < 0) max_scroll = 0;

    u32 up_clr = (gallery_scroll > 0)          ? CLR_BTN : CLR_TRACK;
    u32 dn_clr = (gallery_scroll < max_scroll)  ? CLR_BTN : CLR_TRACK;
    draw_pill(BTN_GSCROLL_X, BTN_GSCROLL_UP_Y,
                      BTN_GSCROLL_W, BTN_GSCROLL_H, up_clr);
    C2D_TextParse(&t, staticBuf, "^");
    C2D_DrawText(&t, C2D_WithColor,
                 BTN_GSCROLL_X + 4.0f, BTN_GSCROLL_UP_Y + 6.0f,
                 0.5f, 0.44f, 0.44f, CLR_TEXT);
    draw_pill(BTN_GSCROLL_X, BTN_GSCROLL_DN_Y,
                      BTN_GSCROLL_W, BTN_GSCROLL_H, dn_clr);
    C2D_TextParse(&t, staticBuf, "v");
    C2D_DrawText(&t, C2D_WithColor,
                 BTN_GSCROLL_X + 5.0f, BTN_GSCROLL_DN_Y + 6.0f,
                 0.5f, 0.44f, 0.44f, CLR_TEXT);
}

// ---------------------------------------------------------------------------
// STYLE tab
// ---------------------------------------------------------------------------

static void draw_style_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                           const FilterParams *p, const FilterRanges *ranges) {
    float sc = 0.44f;
    C2D_Text t;
    (void)dynBuf;

    // Section label
    C2D_TextParse(&t, staticBuf, "Style");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)STYLE_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    // --- Palette buttons ---
    // Row 1: GB, Gray, GBC, Shell (4 buttons), Row 2: GBA, DB, Clr (3 buttons)
    const char *pal_names[7] = {"GB", "Gray", "GBC", "Shell", "GBA", "DB", "Clr"};
    static const int row_count[2] = {4, 3};
    int pal_idx = 0;
    for (int row = 0; row < 2; row++) {
        int count = row_count[row];
        float row_y = (float)STYLE_PAL_Y0 + row * (STYLE_PAL_H + STYLE_PAL_GAP);
        // Centre the buttons in the row
        float total_w = count * STYLE_PAL_W + (count - 1) * STYLE_PAL_GAP;
        float start_x = (BOT_W - total_w) / 2.0f;
        for (int col = 0; col < count; col++) {
            int pal_val = (pal_idx < 6) ? pal_idx : PALETTE_NONE;
            bool sel = (p->palette == pal_val);
            float bx = start_x + col * (STYLE_PAL_W + STYLE_PAL_GAP);
            draw_pill(bx, row_y, STYLE_PAL_W, STYLE_PAL_H,
                              sel ? CLR_ACCENT : CLR_BTN);
            C2D_TextParse(&t, staticBuf, pal_names[pal_idx]);
            float tw = 0, th = 0;
            C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
            float tlx = bx + (STYLE_PAL_W - tw) / 2.0f;
            float tly = row_y + (STYLE_PAL_H - th) / 2.0f - 1.0f;
            C2D_DrawText(&t, C2D_WithColor, tlx, tly, 0.5f, sc, sc,
                         sel ? CLR_WHITE : CLR_TEXT);
            pal_idx++;
        }
    }

    // Divider
    C2D_DrawRectSolid(8, STYLE_PX_LABEL_Y - 4, 0.5f, BOT_W - 16, 1, CLR_DIVIDER);

    // --- Pixel Size section ---
    C2D_TextParse(&t, staticBuf, "Pixel Size");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)STYLE_PX_LABEL_Y, 0.5f, sc, sc, CLR_DIM);

    // Px value readout
    char buf[8];
    snprintf(buf, sizeof(buf), "%dpx", p->pixel_size);
    C2D_TextParse(&t, staticBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 280.0f, (float)STYLE_PX_LABEL_Y, 0.5f, sc, sc, CLR_DIM);

    // Tick labels
    C2D_TextParse(&t, staticBuf, "1");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(1) - 2.0f, (float)STYLE_PX_Y + 14.0f,
                 0.5f, 0.36f, 0.36f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "4");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(4) - 3.0f, (float)STYLE_PX_Y + 14.0f,
                 0.5f, 0.36f, 0.36f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "8");
    C2D_DrawText(&t, C2D_WithColor, px_stop_x(8) - 3.0f, (float)STYLE_PX_Y + 14.0f,
                 0.5f, 0.36f, 0.36f, CLR_DIM);

    draw_snap_slider((float)STYLE_PX_Y, p->pixel_size);

    // --- Image adjustment sliders (compact, at bottom) ---
    C2D_DrawRectSolid(8, STYLE_PX_Y + 24, 0.5f, BOT_W - 16, 1, CLR_DIVIDER);

    // Removed from shoot screen, now shown here in compact form
    // Bright / Contrast / Sat / Gamma — labels left, values right, sliders
    // Slightly smaller than before: fit in remaining ~50px is too tight.
    // Show only Brightness here as a compromise; full sliders in Calibrate.
    (void)ranges;
}

// ---------------------------------------------------------------------------
// FX tab (restyled)
// ---------------------------------------------------------------------------

static void draw_fx_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                        const FilterParams *p, bool settings_flash) {
    float sc = 0.44f;
    C2D_Text t;
    char buf_str[8];

    // Section label
    C2D_TextParse(&t, staticBuf, "Effects");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)FXTAB_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    // Mode buttons row 1: None, Scan-H, Scan-V, LCD
    static const char *mode_labels_r1[4] = { "None", "Scan-H", "Scan-V", "LCD" };
    for (int i = 0; i < 4; i++) {
        float bx = (float)(FXTAB_R1_X0 + i * (FXTAB_R1_W + FXTAB_R1_GAP));
        bool sel = (p->fx_mode == i);
        draw_pill(bx, (float)FXTAB_BTN_Y1, FXTAB_R1_W, FXTAB_BTN_H,
                          sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(&t, staticBuf, mode_labels_r1[i]);
        float tw = 0, th = 0;
        C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
        float tlx = bx + (FXTAB_R1_W - tw) / 2.0f;
        float tly = (float)FXTAB_BTN_Y1 + (FXTAB_BTN_H - th) / 2.0f - 1.0f;
        C2D_DrawText(&t, C2D_WithColor, tlx, tly, 0.5f, sc, sc,
                     sel ? CLR_WHITE : CLR_TEXT);
    }

    // Mode buttons row 2: Vignette, Chroma, Grain
    static const char *mode_labels_r2[3] = { "Vignette", "Chroma", "Grain" };
    for (int i = 0; i < 3; i++) {
        float bx = (float)(FXTAB_R2_X0 + i * (FXTAB_R2_W + FXTAB_R2_GAP));
        bool sel = (p->fx_mode == 4 + i);
        draw_pill(bx, (float)FXTAB_BTN_Y2, FXTAB_R2_W, FXTAB_BTN_H,
                          sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(&t, staticBuf, mode_labels_r2[i]);
        float tw = 0, th = 0;
        C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
        float tlx = bx + (FXTAB_R2_W - tw) / 2.0f;
        float tly = (float)FXTAB_BTN_Y2 + (FXTAB_BTN_H - th) / 2.0f - 1.0f;
        C2D_DrawText(&t, C2D_WithColor, tlx, tly, 0.5f, sc, sc,
                     sel ? CLR_WHITE : CLR_TEXT);
    }

    // Divider + Intensity
    C2D_DrawRectSolid(0, 96, 0.5f, BOT_W, 1, CLR_DIVIDER);
    u32 label_clr = (p->fx_mode == FX_NONE) ? CLR_TRACK : CLR_TEXT;
    C2D_TextParse(&t, staticBuf, "Intensity");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)FXTAB_SLIDER_Y - 18.0f,
                 0.5f, sc, sc, label_clr);

    if (p->fx_mode != FX_NONE) {
        draw_slider(0, (float)FXTAB_SLIDER_Y, 0.0f, 10.0f, (float)p->fx_intensity);
    } else {
        C2D_DrawRectSolid(TRACK_X, (float)FXTAB_SLIDER_Y - TRACK_H / 2.0f,
                          0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    }

    C2D_TextBufClear(dynBuf);
    snprintf(buf_str, sizeof(buf_str), "%d", p->fx_intensity);
    C2D_TextParse(&t, dynBuf, buf_str);
    C2D_DrawText(&t, C2D_WithColor, 284.0f, (float)FXTAB_SLIDER_Y - 18.0f,
                 0.5f, sc, sc, CLR_DIM);

    C2D_DrawRectSolid(0, 144, 0.5f, BOT_W, 1, CLR_DIVIDER);
    static const char *mode_descs[7] = {
        "No effect applied",
        "Darken every other row",
        "Darken every other column",
        "Dot-matrix grid overlay",
        "Radial edge darkening",
        "RGB channel offset",
        "Per-frame random noise"
    };
    C2D_TextParse(&t, staticBuf, mode_descs[p->fx_mode]);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)FXTAB_DESC_Y,
                 0.5f, 0.40f, 0.40f, CLR_DIM);

    (void)settings_flash;  // Save as Default moved to MORE tab
}

// ---------------------------------------------------------------------------
// MORE tab (settings overlay)
// ---------------------------------------------------------------------------

static void draw_more_tab(C2D_TextBuf staticBuf,
                          const FilterParams *p, int save_scale,
                          bool settings_flash) {
    float sc = 0.46f;
    C2D_Text t;

    // Section label
    C2D_TextParse(&t, staticBuf, "More Options");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    C2D_DrawRectSolid(0, 26, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Save Scale row ---
    C2D_TextParse(&t, staticBuf, "Save Scale");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_SCALE_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
    // 1x button
    draw_pill((float)MORE_STOG_X0, MORE_SCALE_Y - MORE_STOG_H / 2,
                      MORE_STOG_W, MORE_STOG_H,
                      save_scale == 1 ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "1x");
    C2D_DrawText(&t, C2D_WithColor, MORE_STOG_X0 + 20.0f, MORE_SCALE_Y - 8.0f,
                 0.5f, sc, sc, save_scale == 1 ? CLR_WHITE : CLR_TEXT);
    // 2x button
    draw_pill((float)MORE_STOG_X1, MORE_SCALE_Y - MORE_STOG_H / 2,
                      MORE_STOG_W, MORE_STOG_H,
                      save_scale == 2 ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "2x");
    C2D_DrawText(&t, C2D_WithColor, MORE_STOG_X1 + 20.0f, MORE_SCALE_Y - 8.0f,
                 0.5f, sc, sc, save_scale == 2 ? CLR_WHITE : CLR_TEXT);

    // --- Dither row ---
    {
        static const char *dith_labels[4] = { "Bayr", "Clus", "Atk", "F-S" };
        C2D_TextParse(&t, staticBuf, "Dither");
        C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_DITH_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
        for (int dm = 0; dm < 4; dm++) {
            float bx = (float)(MORE_SDITH_X0 + dm * (MORE_SDITH_W + MORE_SDITH_GAP));
            bool sel = (p->dither_mode == dm);
            draw_pill(bx, MORE_DITH_Y - MORE_STOG_H / 2,
                              MORE_SDITH_W, MORE_STOG_H,
                              sel ? CLR_ACCENT : CLR_BTN);
            C2D_TextParse(&t, staticBuf, dith_labels[dm]);
            float tw = 0, th = 0;
            C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
            float tlx = bx + (MORE_SDITH_W - tw) / 2.0f;
            C2D_DrawText(&t, C2D_WithColor, tlx, MORE_DITH_Y - 8.0f,
                         0.5f, 0.38f, 0.38f, sel ? CLR_WHITE : CLR_TEXT);
        }
    }

    // --- Invert row ---
    C2D_TextParse(&t, staticBuf, "Invert");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_INV_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_pill((float)MORE_INV_STOG_X0, MORE_INV_Y - MORE_STOG_H / 2,
                      MORE_STOG_W, MORE_STOG_H,
                      !p->invert ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Off");
    C2D_DrawText(&t, C2D_WithColor, MORE_INV_STOG_X0 + 18.0f, MORE_INV_Y - 8.0f,
                 0.5f, sc, sc, !p->invert ? CLR_WHITE : CLR_TEXT);
    draw_pill((float)MORE_INV_STOG_X1, MORE_INV_Y - MORE_STOG_H / 2,
                      MORE_STOG_W, MORE_STOG_H,
                      p->invert ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "On");
    C2D_DrawText(&t, C2D_WithColor, MORE_INV_STOG_X1 + 22.0f, MORE_INV_Y - 8.0f,
                 0.5f, sc, sc, p->invert ? CLR_WHITE : CLR_TEXT);

    C2D_DrawRectSolid(0, MORE_DIV_Y, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Power-user row: Palette Editor | Calibrate ---
    draw_pill((float)MORE_PALED_X, MORE_POWED_Y,
                      MORE_POWED_W, MORE_POWED_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Palette Editor");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_PALED_X + (MORE_POWED_W - tw) / 2.0f, MORE_POWED_Y + 6.0f,
                 0.5f, 0.42f, 0.42f, CLR_TEXT);

    draw_pill((float)MORE_CALIB_X, MORE_POWED_Y,
                      MORE_POWED_W, MORE_POWED_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Calibrate");
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_CALIB_X + (MORE_POWED_W - tw) / 2.0f, MORE_POWED_Y + 6.0f,
                 0.5f, 0.42f, 0.42f, CLR_TEXT);

    // --- Save as Default ---
    u32 def_col = settings_flash ? CLR_CONFIRM : CLR_BTN;
    u32 def_txt = settings_flash ? CLR_WHITE   : CLR_TEXT;
    draw_pill((float)MORE_SAVEDEF_X, MORE_SAVEDEF_Y,
                      MORE_SAVEDEF_W, MORE_SAVEDEF_H, def_col);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    C2D_TextGetDimensions(&t, 0.44f, 0.44f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_SAVEDEF_X + (MORE_SAVEDEF_W - tw) / 2.0f, MORE_SAVEDEF_Y + 5.0f,
                 0.5f, 0.44f, 0.44f, def_txt);
}

// ---------------------------------------------------------------------------
// Palette editor tab (accessed from MORE)
// ---------------------------------------------------------------------------

static void draw_palette_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                              const PaletteDef *user_palettes,
                              int palette_sel_pal, int palette_sel_color,
                              bool settings_flash) {
    float sc = 0.40f;
    C2D_Text t;
    (void)dynBuf;

    // Back hint
    C2D_TextParse(&t, staticBuf, "< More");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 4.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    // Palette selector strip
    const char *short_names[PALETTE_COUNT] = {"GB","Gray","GBC","Shell","GBA","DB"};
    for (int i = 0; i < PALETTE_COUNT; i++) {
        float bx = (float)(i * PALTAB_PALSEL_BTN_W);
        bool sel = (i == palette_sel_pal);
        draw_pill(bx, (float)PALTAB_PALSEL_Y,
                          (float)PALTAB_PALSEL_BTN_W - 1, PALTAB_PALSEL_H,
                          sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(&t, staticBuf, short_names[i]);
        C2D_DrawText(&t, C2D_WithColor,
                     bx + 6.0f, (float)PALTAB_PALSEL_Y + 10.0f,
                     0.5f, sc, sc, sel ? CLR_WHITE : CLR_TEXT);
    }

    C2D_DrawRectSolid(0, PALTAB_PALSEL_Y + PALTAB_PALSEL_H, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // Colour swatch strip
    const PaletteDef *pal = &user_palettes[palette_sel_pal];
    for (int i = 0; i < pal->size; i++) {
        int sx = 4 + i * (PALTAB_SWATCH_W + 4);
        bool sel = (i == palette_sel_color);
        u32 col = C2D_Color32(pal->colors[i][0], pal->colors[i][1], pal->colors[i][2], 255);
        draw_pill((float)sx, (float)PALTAB_SWATCH_Y,
                          (float)PALTAB_SWATCH_W, (float)PALTAB_SWATCH_H, col);
        if (sel) {
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y - 2, 0.4f, PALTAB_SWATCH_W + 4, 2, CLR_SEL);
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y + PALTAB_SWATCH_H, 0.4f, PALTAB_SWATCH_W + 4, 2, CLR_SEL);
            C2D_DrawRectSolid(sx - 2, PALTAB_SWATCH_Y - 2, 0.4f, 2, PALTAB_SWATCH_H + 4, CLR_SEL);
            C2D_DrawRectSolid(sx + PALTAB_SWATCH_W, PALTAB_SWATCH_Y - 2, 0.4f, 2, PALTAB_SWATCH_H + 4, CLR_SEL);
        }
    }

    C2D_DrawRectSolid(0, PALTAB_SWATCH_Y + PALTAB_SWATCH_H + 2, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // HS picker + Value strip
    {
        uint8_t cr = pal->colors[palette_sel_color][0];
        uint8_t cg = pal->colors[palette_sel_color][1];
        uint8_t cb = pal->colors[palette_sel_color][2];
        float cur_h, cur_s, cur_v;
        rgb_to_hsv(cr, cg, cb, &cur_h, &cur_s, &cur_v);

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
        float cx = PALTAB_HS_X + cur_h / 360.0f * PALTAB_HS_W;
        float cy_hs = PALTAB_HS_Y + (1.0f - cur_s) * PALTAB_HS_H;
        C2D_DrawRectSolid(cx - 0.5f, (float)PALTAB_HS_Y, 0.4f, 1.0f, PALTAB_HS_H, CLR_SEL);
        C2D_DrawRectSolid((float)PALTAB_HS_X, cy_hs - 0.5f, 0.4f, PALTAB_HS_W, 1.0f, CLR_SEL);

        #define VAL_SEGS 32
        float sw = (float)PALTAB_VAL_W / VAL_SEGS;
        for (int i = 0; i < VAL_SEGS; i++) {
            float val = (float)i / (VAL_SEGS - 1);
            uint8_t vr, vg, vb;
            hsv_to_rgb(cur_h, cur_s, val, &vr, &vg, &vb);
            C2D_DrawRectSolid(
                PALTAB_VAL_X + i * sw, (float)PALTAB_VAL_Y, 0.5f,
                sw + 0.5f, (float)PALTAB_VAL_H,
                C2D_Color32(vr, vg, vb, 255));
        }
        float vx = PALTAB_VAL_X + cur_v * PALTAB_VAL_W;
        C2D_DrawRectSolid(vx - 0.5f, (float)PALTAB_VAL_Y, 0.4f, 1.0f, PALTAB_VAL_H, CLR_SEL);
        #undef HS_COLS
        #undef HS_ROWS
        #undef VAL_SEGS
    }

    // Reset + Save buttons
    draw_pill((float)PALTAB_RESET_X, (float)PALTAB_BTN_Y,
                      (float)PALTAB_RESET_W, (float)PALTAB_BTN_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Reset");
    C2D_DrawText(&t, C2D_WithColor,
                 PALTAB_RESET_X + 14.0f, PALTAB_BTN_Y + 2.0f,
                 0.5f, 0.40f, 0.40f, CLR_TEXT);

    u32 save_col = settings_flash ? CLR_CONFIRM : CLR_BTN;
    draw_pill((float)PALTAB_SAVE_DEF_X, (float)PALTAB_BTN_Y,
                      (float)PALTAB_SAVE_DEF_W, (float)PALTAB_BTN_H, save_col);
    C2D_TextParse(&t, staticBuf, "Save Default");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 PALTAB_SAVE_DEF_X + (PALTAB_SAVE_DEF_W - tw) / 2.0f, PALTAB_BTN_Y + 2.0f,
                 0.5f, 0.40f, 0.40f, settings_flash ? CLR_WHITE : CLR_TEXT);
}

// ---------------------------------------------------------------------------
// Calibrate tab (accessed from MORE)
// ---------------------------------------------------------------------------

static void draw_calibrate_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                                const FilterRanges *ranges, bool settings_flash) {
    float sc = 0.44f;
    C2D_Text t;
    char buf_str[20];

    // Back hint
    C2D_TextParse(&t, staticBuf, "< More");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 4.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    C2D_TextBufClear(dynBuf);

    // Rows
    C2D_TextParse(&t, staticBuf, "Bright");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_BRIGHT, CAL_BRIGHT_ABS_MIN, CAL_BRIGHT_ABS_MAX,
                      ranges->bright_min, ranges->bright_max, ranges->bright_def);
    snprintf(buf_str, sizeof(buf_str), "%.1f|%.1f|%.1f",
             (double)ranges->bright_min, (double)ranges->bright_def, (double)ranges->bright_max);
    C2D_TextParse(&t, dynBuf, buf_str);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_BRIGHT + 7.0f, 0.5f, 0.34f, 0.34f, CLR_DIM);

    C2D_TextParse(&t, staticBuf, "Contrast");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_CONTRAST, CAL_CONTRAST_ABS_MIN, CAL_CONTRAST_ABS_MAX,
                      ranges->contrast_min, ranges->contrast_max, ranges->contrast_def);
    snprintf(buf_str, sizeof(buf_str), "%.1f|%.1f|%.1f",
             (double)ranges->contrast_min, (double)ranges->contrast_def, (double)ranges->contrast_max);
    C2D_TextParse(&t, dynBuf, buf_str);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_CONTRAST + 7.0f, 0.5f, 0.34f, 0.34f, CLR_DIM);

    C2D_TextParse(&t, staticBuf, "Saturate");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_SAT, CAL_SAT_ABS_MIN, CAL_SAT_ABS_MAX,
                      ranges->sat_min, ranges->sat_max, ranges->sat_def);
    snprintf(buf_str, sizeof(buf_str), "%.1f|%.1f|%.1f",
             (double)ranges->sat_min, (double)ranges->sat_def, (double)ranges->sat_max);
    C2D_TextParse(&t, dynBuf, buf_str);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_SAT + 7.0f, 0.5f, 0.34f, 0.34f, CLR_DIM);

    C2D_TextParse(&t, staticBuf, "Gamma");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA - 9.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_range_slider(ROW_GAMMA, CAL_GAMMA_ABS_MIN, CAL_GAMMA_ABS_MAX,
                      ranges->gamma_min, ranges->gamma_max, ranges->gamma_def);
    snprintf(buf_str, sizeof(buf_str), "%.1f|%.1f|%.1f",
             (double)ranges->gamma_min, (double)ranges->gamma_def, (double)ranges->gamma_max);
    C2D_TextParse(&t, dynBuf, buf_str);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, (float)ROW_GAMMA + 7.0f, 0.5f, 0.34f, 0.34f, CLR_DIM);

    C2D_DrawRectSolid(0, CAL_SAVEDEF_Y - 8, 0.5f, BOT_W, 1, CLR_DIVIDER);

    u32 def_clr = settings_flash ? CLR_CONFIRM : CLR_BTN;
    u32 def_txt = settings_flash ? CLR_WHITE   : CLR_TEXT;
    draw_pill((float)CAL_SAVEDEF_X, (float)CAL_SAVEDEF_Y,
                      (float)CAL_SAVEDEF_W, (float)CAL_SAVEDEF_H, def_clr);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.44f, 0.44f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 CAL_SAVEDEF_X + (CAL_SAVEDEF_W - tw) / 2.0f, CAL_SAVEDEF_Y + 4.0f,
                 0.5f, 0.44f, 0.44f, def_txt);
}

// ---------------------------------------------------------------------------
// Top-level draw_ui
// ---------------------------------------------------------------------------

void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             int save_flash, bool warn3d,
             int active_tab, int save_scale, bool settings_flash,
             int settings_row,
             const PaletteDef *user_palettes,
             int palette_sel_pal, int palette_sel_color,
             const FilterRanges *ranges,
             bool comparing,
             bool gallery_mode, int gallery_count,
             const char gallery_paths[][64], int gallery_sel, int gallery_scroll) {
    (void)settings_row;  // no longer used at top level

    C2D_TargetClear(bot, CLR_BG);
    C2D_SceneBegin(bot);

    if (warn3d) {
        C2D_Text t;
        C2D_TextBufClear(staticBuf);
        C2D_TextParse(&t, staticBuf, "3D slider not supported");
        C2D_DrawText(&t, C2D_WithColor, 34.0f, 108.0f, 0.5f, 0.55f, 0.55f,
                     C2D_Color32(200, 60, 60, 255));
        C2D_TextParse(&t, staticBuf, "Please set slider to 0");
        C2D_DrawText(&t, C2D_WithColor, 38.0f, 128.0f, 0.5f, 0.48f, 0.48f,
                     C2D_Color32(160, 40, 40, 255));
        return;
    }

    C2D_TextBufClear(staticBuf);

    // Bottom nav bar (always visible)
    draw_bottom_nav(staticBuf, active_tab);

    // Divider above nav
    C2D_DrawRectSolid(0, NAV_Y, 0.5f, BOT_W, 1, CLR_DIVIDER);

    if (comparing) {
        C2D_Text t;
        C2D_DrawRectSolid(0, 0, 0.6f, BOT_W, CONTENT_H, C2D_Color32(30, 30, 40, 180));
        C2D_TextParse(&t, staticBuf, "RAW");
        C2D_DrawText(&t, C2D_WithColor, 124.0f, 80.0f, 0.6f, 1.4f, 1.4f,
                     C2D_Color32(240, 200, 50, 255));
        C2D_TextParse(&t, staticBuf, "hold SELECT to compare");
        C2D_DrawText(&t, C2D_WithColor, 36.0f, 130.0f, 0.6f, 0.44f, 0.44f,
                     C2D_Color32(200, 210, 230, 200));
        return;
    }

    // Content area dispatch
    if (active_tab == TAB_SHOOT) {
        // Always draw the shoot strip (contains Gallery toggle button)
        draw_shoot_tab(staticBuf, selfie, save_flash, user_palettes,
                       p.palette, gallery_mode);
        if (gallery_mode) {
            draw_gallery_tab(staticBuf, dynBuf, gallery_count, gallery_paths,
                             gallery_sel, gallery_scroll);
        }
    } else if (active_tab == TAB_STYLE) {
        draw_style_tab(staticBuf, dynBuf, &p, ranges);
    } else if (active_tab == TAB_FX) {
        draw_fx_tab(staticBuf, dynBuf, &p, settings_flash);
    } else if (active_tab == TAB_MORE) {
        draw_more_tab(staticBuf, &p, save_scale, settings_flash);
    } else if (active_tab == TAB_PALETTE_ED) {
        draw_palette_tab(staticBuf, dynBuf, user_palettes,
                         palette_sel_pal, palette_sel_color, settings_flash);
    } else if (active_tab == TAB_CALIBRATE) {
        draw_calibrate_tab(staticBuf, dynBuf, ranges, settings_flash);
    }
}
