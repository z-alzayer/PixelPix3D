#include "ui_draw.h"
#include "filter.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Bottom nav bar (always visible at y=200)
// ---------------------------------------------------------------------------

void draw_bottom_nav(C2D_TextBuf buf, int active_tab) {
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

// ---------------------------------------------------------------------------
// Horizontal slider helper for shoot panel (label left, track, value right)
// ---------------------------------------------------------------------------
static void draw_panel_hslider(C2D_TextBuf buf, const char *label,
                                float val, float mn, float mx,
                                float row_y) {
    C2D_Text t;
    float tw = 0, th = 0;

    // Label
    C2D_TextParse(&t, buf, label);
    C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor, 4.0f, row_y + (TRACK_H * 0.5f) - th * 0.5f + 1.0f,
                 0.5f, 0.40f, 0.40f, CLR_DIM);

    // Track
    float track_x = (float)SHOOT_HSLIDER_X;
    float track_w = (float)SHOOT_HSLIDER_W;
    float track_cy = row_y + TRACK_H * 0.5f + 1.0f;
    C2D_DrawRectSolid(track_x, track_cy - TRACK_H * 0.5f, 0.5f, track_w, TRACK_H, CLR_TRACK);
    float t_val = (val - mn) / (mx - mn);
    if (t_val < 0.0f) t_val = 0.0f;
    if (t_val > 1.0f) t_val = 1.0f;
    float hx = track_x + t_val * track_w;
    if (hx - track_x > 0)
        C2D_DrawRectSolid(track_x, track_cy - TRACK_H * 0.5f, 0.5f,
                          hx - track_x, TRACK_H, CLR_FILL);
    draw_rounded_rect(hx - HANDLE_W * 0.5f, track_cy - HANDLE_H * 0.5f,
                      HANDLE_W, HANDLE_H, 3.0f, CLR_HANDLE);

    // Value
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%.2f", (double)val);
    C2D_TextParse(&t, buf, vbuf);
    C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 BOT_W - SHOOT_HSLIDER_VAL_W - 4 + (SHOOT_HSLIDER_VAL_W - tw) * 0.5f,
                 row_y + (TRACK_H * 0.5f) - th * 0.5f + 1.0f,
                 0.5f, 0.36f, 0.36f, CLR_DIM);
}

void draw_shoot_tab(C2D_TextBuf staticBuf,
                    bool selfie, int save_flash,
                    const PaletteDef *user_palettes,
                    int active_palette,
                    bool gallery_mode,
                    const FilterParams *p, const FilterRanges *ranges,
                    int shoot_mode, bool shoot_mode_open,
                    int shoot_timer_secs,
                    int wiggle_frames, int wiggle_delay_ms) {
    C2D_Text t;

    // Background for strip area
    C2D_DrawRectSolid(0, SHOOT_STRIP_Y, 0.5f, BOT_W, SHOOT_STRIP_H, CLR_PANEL);

    // --- Camera flip button (left) ---
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

        if (sel) {
            C2D_DrawRectSolid(sx - 2, sy - 2, 0.4f,
                              SHOOT_SWATCH_W + 4, SHOOT_SWATCH_H + 4, CLR_SEL);
        }

        u32 swatch_col;
        if (i < 6 && user_palettes[i].size > 0) {
            swatch_col = C2D_Color32(user_palettes[i].colors[0][0],
                                     user_palettes[i].colors[0][1],
                                     user_palettes[i].colors[0][2], 255);
        } else {
            swatch_col = none_col;
        }
        draw_rounded_rect_on_panel(sx, sy, SHOOT_SWATCH_W, SHOOT_SWATCH_H, 3.0f, swatch_col);

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

    // Thin divider below strip
    C2D_DrawRectSolid(0, (float)SHOOT_STRIP_H, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // -----------------------------------------------------------------------
    // Middle area: mode grid OR full contextual panel
    // -----------------------------------------------------------------------
    if (!gallery_mode) {
        if (!shoot_mode_open) {
            // ---- Mode grid ----
            static const char *mode_labels[SHOOT_MODE_COUNT] = {
                "GB Cam", "Random", "Photobooth", "Timer", "Wiggle", "Lomo"
            };
            static const int mode_row_ys[2] = { SHOOT_MODE_ROW1_Y, SHOOT_MODE_ROW2_Y };

            for (int i = 0; i < SHOOT_MODE_COUNT; i++) {
                int row  = i / 3;
                int col  = i % 3;
                float bx = SHOOT_MODE_BTN_GAP + col * (SHOOT_MODE_BTN_W + SHOOT_MODE_BTN_GAP);
                float by = (float)mode_row_ys[row];
                bool sel = (shoot_mode == i);

                draw_pill(bx, by, SHOOT_MODE_BTN_W, SHOOT_MODE_ROW_H,
                          sel ? CLR_ACCENT : CLR_BTN);

                C2D_TextParse(&t, staticBuf, mode_labels[i]);
                float tw2 = 0, th2 = 0;
                C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw2, &th2);
                C2D_DrawText(&t, C2D_WithColor,
                             bx + (SHOOT_MODE_BTN_W - tw2) / 2.0f,
                             by + (SHOOT_MODE_ROW_H - th2) / 2.0f - 1.0f,
                             0.5f, 0.42f, 0.42f,
                             sel ? CLR_WHITE : CLR_TEXT);
            }
        } else {
            // ---- Full contextual panel ----

            // Back button (top-left)
            draw_pill(4.0f, (float)SHOOT_BACK_Y + 2, (float)SHOOT_BACK_W, (float)SHOOT_BACK_H - 4, CLR_BTN);
            C2D_TextParse(&t, staticBuf, "< Back");
            C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         4.0f + ((float)SHOOT_BACK_W - tw) * 0.5f,
                         (float)SHOOT_BACK_Y + ((float)SHOOT_BACK_H - th) * 0.5f - 1.0f,
                         0.5f, 0.40f, 0.40f, CLR_TEXT);

            // Mode title (right of back button)
            static const char *mode_titles[SHOOT_MODE_COUNT] = {
                "GB Cam", "Random", "Photobooth", "Timer", "Wiggle", "Lomo"
            };
            C2D_TextParse(&t, staticBuf, mode_titles[shoot_mode]);
            C2D_TextGetDimensions(&t, 0.46f, 0.46f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         (float)SHOOT_BACK_W + 8.0f + ((BOT_W - SHOOT_BACK_W - 12.0f) - tw) * 0.5f,
                         (float)SHOOT_BACK_Y + ((float)SHOOT_BACK_H - th) * 0.5f - 1.0f,
                         0.5f, 0.46f, 0.46f, CLR_ACCENT);

            // Divider
            C2D_DrawRectSolid(0, (float)(SHOOT_BACK_Y + SHOOT_BACK_H + 2), 0.5f, BOT_W, 1, CLR_DIVIDER);

            // ----- Per-mode content -----
            float cy = (float)SHOOT_CONTENT_Y;

            if (shoot_mode == SHOOT_MODE_GBCAM) {
                // 4 vertical sliders: Brt / Con / Sat / Gam
                // Each column centred in a 80px wide lane, track spans SHOOT_CONTENT_Y..SHOOT_SAVE_Y-4
                float vals[4]  = { p->brightness,  p->contrast,    p->saturation,   p->gamma };
                float mins[4]  = { ranges->bright_min,  ranges->contrast_min, ranges->sat_min,  ranges->gamma_min };
                float maxs[4]  = { ranges->bright_max,  ranges->contrast_max, ranges->sat_max,  ranges->gamma_max };
                float defs[4]  = { ranges->bright_def,  ranges->contrast_def, ranges->sat_def,  ranges->gamma_def };
                static const char *vlbls[4] = { "Brt", "Con", "Sat", "Gam" };
                #define VCOL_W   80
                #define VTRACK_W  4
                #define VHANDLE_W 14
                #define VHANDLE_H  8
                float vtrack_top = cy + 14.0f;  // leave room for label above
                float vtrack_bot = (float)SHOOT_SAVE_Y - 6.0f;
                float vtrack_h   = vtrack_bot - vtrack_top;
                for (int i = 0; i < 4; i++) {
                    float col_cx = i * VCOL_W + VCOL_W / 2.0f;
                    float tx_left = col_cx - VTRACK_W / 2.0f;
                    // Label
                    C2D_TextParse(&t, staticBuf, vlbls[i]);
                    float tw2 = 0, th2 = 0;
                    C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw2, &th2);
                    C2D_DrawText(&t, C2D_WithColor,
                                 col_cx - tw2 / 2.0f, cy,
                                 0.5f, 0.36f, 0.36f, CLR_DIM);
                    // Track
                    C2D_DrawRectSolid(tx_left, vtrack_top, 0.5f, VTRACK_W, vtrack_h, CLR_TRACK);
                    float mn = mins[i], mx = maxs[i], df = defs[i], v = vals[i];
                    float t_val = (v  - mn) / (mx - mn);
                    float t_def = (df - mn) / (mx - mn);
                    if (t_val < 0.0f) t_val = 0.0f;
                    if (t_val > 1.0f) t_val = 1.0f;
                    float hy = vtrack_top + (1.0f - t_val) * vtrack_h;
                    float dy = vtrack_top + (1.0f - t_def) * vtrack_h;
                    float fill_top = hy < dy ? hy : dy;
                    float fill_bot = hy > dy ? hy : dy;
                    if (fill_bot > fill_top)
                        C2D_DrawRectSolid(tx_left, fill_top, 0.4f, VTRACK_W, fill_bot - fill_top, CLR_FILL);
                    // Default tick
                    C2D_DrawRectSolid(col_cx - 4.0f, dy - 0.5f, 0.4f, 8.0f, 1.0f, CLR_DIM);
                    // Handle
                    draw_rounded_rect_on_panel(col_cx - VHANDLE_W / 2.0f, hy - VHANDLE_H / 2.0f,
                                               VHANDLE_W, VHANDLE_H, 2.0f, CLR_HANDLE);
                }
                #undef VCOL_W
                #undef VTRACK_W
                #undef VHANDLE_W
                #undef VHANDLE_H

            } else if (shoot_mode == SHOOT_MODE_RANDOM) {
                C2D_TextParse(&t, staticBuf, "Applies a random palette each save.");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 8.0f, 0.5f, 0.40f, 0.40f, CLR_DIM);
                C2D_TextParse(&t, staticBuf, "Current palette used as fallback.");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 26.0f, 0.5f, 0.40f, 0.40f, CLR_DIM);

            } else if (shoot_mode == SHOOT_MODE_PHOTOBOOTH ||
                       shoot_mode == SHOOT_MODE_TIMER) {
                // Timer selector: 3s / 5s / 10s / 15s
                static const int timer_vals[4] = { 3, 5, 10, 15 };
                static const char *timer_lbls[4] = { "3s", "5s", "10s", "15s" };
                float total_btn_w = 4 * SHOOT_TIMER_BTN_W + 3 * SHOOT_TIMER_BTN_GAP;
                float btn_start_x = (BOT_W - total_btn_w) * 0.5f;

                C2D_TextParse(&t, staticBuf,
                    shoot_mode == SHOOT_MODE_PHOTOBOOTH
                        ? "Timer delay between 4 shots:"
                        : "Countdown before capture:");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy, 0.5f, 0.40f, 0.40f, CLR_DIM);

                for (int i = 0; i < 4; i++) {
                    float bx = btn_start_x + i * (SHOOT_TIMER_BTN_W + SHOOT_TIMER_BTN_GAP);
                    bool sel = (shoot_timer_secs == timer_vals[i]);
                    draw_pill(bx, cy + 20.0f, (float)SHOOT_TIMER_BTN_W, (float)SHOOT_TIMER_BTN_H,
                              sel ? CLR_ACCENT : CLR_BTN);
                    C2D_TextParse(&t, staticBuf, timer_lbls[i]);
                    C2D_TextGetDimensions(&t, 0.50f, 0.50f, &tw, &th);
                    C2D_DrawText(&t, C2D_WithColor,
                                 bx + ((float)SHOOT_TIMER_BTN_W - tw) * 0.5f,
                                 cy + 20.0f + ((float)SHOOT_TIMER_BTN_H - th) * 0.5f - 1.0f,
                                 0.5f, 0.50f, 0.50f,
                                 sel ? CLR_WHITE : CLR_TEXT);
                }

                if (shoot_mode == SHOOT_MODE_PHOTOBOOTH) {
                    C2D_TextParse(&t, staticBuf, "4 shots with countdown each");
                    C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 60.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);
                }

            } else if (shoot_mode == SHOOT_MODE_WIGGLE) {
                // Wiggle: Frames slider + Delay slider
                C2D_TextParse(&t, staticBuf, "Stereo GIF — adjust capture:");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy, 0.5f, 0.40f, 0.40f, CLR_DIM);

                // Frames slider: 2..8 frames
                draw_panel_hslider(staticBuf, "Frames",
                                   (float)wiggle_frames, 2.0f, 8.0f,
                                   cy + 18.0f);
                // Delay slider: 100..1000ms
                draw_panel_hslider(staticBuf, "Delay ms",
                                   (float)wiggle_delay_ms, 100.0f, 1000.0f,
                                   cy + 18.0f + HANDLE_H + 10.0f);

            } else if (shoot_mode == SHOOT_MODE_LOMO) {
                C2D_TextParse(&t, staticBuf, "High-contrast film look.");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 8.0f, 0.5f, 0.40f, 0.40f, CLR_DIM);
                C2D_TextParse(&t, staticBuf, "Auto-boosts contrast + vignette");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 26.0f, 0.5f, 0.40f, 0.40f, CLR_DIM);
                C2D_TextParse(&t, staticBuf, "on each saved photo.");
                C2D_DrawText(&t, C2D_WithColor, 8.0f, cy + 44.0f, 0.5f, 0.40f, 0.40f, CLR_DIM);
            }
        }

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
// Gallery tab (shown below shoot strip when gallery_mode is true)
// ---------------------------------------------------------------------------

void draw_gallery_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
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

    u32 up_clr = (gallery_scroll > 0)         ? CLR_BTN : CLR_TRACK;
    u32 dn_clr = (gallery_scroll < max_scroll) ? CLR_BTN : CLR_TRACK;
    draw_pill(BTN_GSCROLL_X, BTN_GSCROLL_UP_Y, BTN_GSCROLL_W, BTN_GSCROLL_H, up_clr);
    C2D_TextParse(&t, staticBuf, "^");
    C2D_DrawText(&t, C2D_WithColor,
                 BTN_GSCROLL_X + 4.0f, BTN_GSCROLL_UP_Y + 6.0f,
                 0.5f, 0.44f, 0.44f, CLR_TEXT);
    draw_pill(BTN_GSCROLL_X, BTN_GSCROLL_DN_Y, BTN_GSCROLL_W, BTN_GSCROLL_H, dn_clr);
    C2D_TextParse(&t, staticBuf, "v");
    C2D_DrawText(&t, C2D_WithColor,
                 BTN_GSCROLL_X + 5.0f, BTN_GSCROLL_DN_Y + 6.0f,
                 0.5f, 0.44f, 0.44f, CLR_TEXT);
}

// ---------------------------------------------------------------------------
// STYLE tab
// ---------------------------------------------------------------------------

void draw_style_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                    const FilterParams *p, const FilterRanges *ranges) {
    float sc = 0.44f;
    C2D_Text t;
    (void)dynBuf;

    C2D_TextParse(&t, staticBuf, "Style");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)STYLE_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    // --- Palette buttons: 4+3 grid ---
    const char *pal_names[7] = {"GB", "Gray", "GBC", "Shell", "GBA", "DB", "Clr"};
    static const int row_count[2] = {4, 3};
    int pal_idx = 0;
    for (int row = 0; row < 2; row++) {
        int count = row_count[row];
        float row_y = (float)STYLE_PAL_Y0 + row * (STYLE_PAL_H + STYLE_PAL_GAP);
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

    C2D_DrawRectSolid(8, STYLE_PX_LABEL_Y - 4, 0.5f, BOT_W - 16, 1, CLR_DIVIDER);

    // --- Pixel Size section ---
    C2D_TextParse(&t, staticBuf, "Pixel Size");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)STYLE_PX_LABEL_Y, 0.5f, sc, sc, CLR_DIM);

    char buf[8];
    snprintf(buf, sizeof(buf), "%dpx", p->pixel_size);
    C2D_TextParse(&t, staticBuf, buf);
    C2D_DrawText(&t, C2D_WithColor, 280.0f, (float)STYLE_PX_LABEL_Y, 0.5f, sc, sc, CLR_DIM);

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

    C2D_DrawRectSolid(8, STYLE_PX_Y + 24, 0.5f, BOT_W - 16, 1, CLR_DIVIDER);
    (void)ranges;
}

// ---------------------------------------------------------------------------
// FX tab
// ---------------------------------------------------------------------------

void draw_fx_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                 const FilterParams *p, bool settings_flash) {
    float sc = 0.44f;
    C2D_Text t;
    char buf_str[8];

    C2D_TextParse(&t, staticBuf, "Effects");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)FXTAB_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    // Row 1: None, Scan-H, Scan-V, LCD
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

    // Row 2: Vignette, Chroma, Grain
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

    (void)settings_flash;
}

// ---------------------------------------------------------------------------
// MORE tab (settings overlay)
// ---------------------------------------------------------------------------

void draw_more_tab(C2D_TextBuf staticBuf,
                   const FilterParams *p, int save_scale,
                   bool settings_flash) {
    float sc = 0.46f;
    C2D_Text t;

    C2D_TextParse(&t, staticBuf, "More Options");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    C2D_DrawRectSolid(0, 26, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Save Scale row ---
    C2D_TextParse(&t, staticBuf, "Save Scale");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_SCALE_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_pill((float)MORE_STOG_X0, MORE_SCALE_Y - MORE_STOG_H / 2,
              MORE_STOG_W, MORE_STOG_H,
              save_scale == 1 ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "1x");
    C2D_DrawText(&t, C2D_WithColor, MORE_STOG_X0 + 20.0f, MORE_SCALE_Y - 8.0f,
                 0.5f, sc, sc, save_scale == 1 ? CLR_WHITE : CLR_TEXT);
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
