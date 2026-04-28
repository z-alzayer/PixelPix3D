#include "ui_draw.h"
#include "filter.h"
#include "lomo.h"
#include "bend.h"
#include "sticker.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Frame definitions
// ---------------------------------------------------------------------------

// FRAME_COUNT is defined in ui.h
static const char *frame_names[FRAME_COUNT] = {
    "Polaroid", "Film", "GB Border", "Stripes", "Vignette", "Film Color", "Halftone"
};
// (paths used only in main.c compositing; names shown here in picker)

static const int s_fx_modes_compact[6] = {
    FX_SCAN_H, FX_SCAN_V, FX_LCD, FX_VIGNETTE, FX_CHROMA, FX_GRAIN
};

static const char *s_fx_labels_compact[6] = {
    "Scan-H", "Scan-V", "LCD", "Vignette", "Chroma", "Grain"
};

static bool preset_is_empty(const PipelinePreset *preset) {
    return !preset->gb_enabled &&
           !preset->base_enabled &&
           !preset->bend_enabled &&
           preset->fx_mode == FX_NONE;
}

static void draw_fx_intensity_row(C2D_TextBuf staticBuf, C2D_Text *t,
                                  const FilterParams *p, float divider_y,
                                  float label_y, float slider_y, float value_y,
                                  bool compact_handle) {
    C2D_DrawRectSolid(0, divider_y, 0.5f, BOT_W, 1, CLR_DIVIDER);
    C2D_TextParse(t, staticBuf, "Intensity");
    C2D_DrawText(t, C2D_WithColor, 4.0f, label_y, 0.5f, 0.38f, 0.38f,
                 (p->fx_mode == FX_NONE) ? CLR_TRACK : CLR_TEXT);

    if (p->fx_mode != FX_NONE) {
        if (compact_handle) {
            const float handle_w = 10.0f;
            const float handle_h = 10.0f;
            C2D_DrawRectSolid(TRACK_X, slider_y - TRACK_H / 2.0f,
                              0.5f, TRACK_W, TRACK_H, CLR_TRACK);
            float hx = slider_val_to_x((float)p->fx_intensity, 0.0f, 10.0f);
            float fill_w = hx - TRACK_X;
            if (fill_w > 0.0f) {
                C2D_DrawRectSolid(TRACK_X, slider_y - TRACK_H / 2.0f,
                                  0.5f, fill_w, TRACK_H, CLR_FILL);
            }
            draw_rounded_rect(hx - handle_w / 2.0f, slider_y - handle_h / 2.0f,
                              handle_w, handle_h, 3.0f, CLR_HANDLE);
        } else {
            draw_slider(0, slider_y, 0.0f, 10.0f, (float)p->fx_intensity);
        }
    } else {
        C2D_DrawRectSolid(TRACK_X, slider_y - TRACK_H / 2.0f,
                          0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", p->fx_intensity);
    C2D_TextParse(t, staticBuf, buf);
    C2D_DrawText(t, C2D_WithColor, 284.0f, value_y, 0.5f, 0.38f, 0.38f, CLR_DIM);
}

static void draw_fx_panel_compact(C2D_TextBuf staticBuf, C2D_Text *t,
                                  const FilterParams *p, float cy) {
    float sc = 0.40f;
    const float btn_w = 100.0f;
    const float btn_h = 22.0f;
    const float gap_x = 6.0f;
    const float gap_y = 6.0f;
    const float grid_x = 4.0f;
    const float row2_y = cy + btn_h + gap_y;

    for (int i = 0; i < 6; i++) {
        int row = i / 3;
        int col = i % 3;
        float bx = grid_x + col * (btn_w + gap_x);
        float by = (row == 0) ? cy : row2_y;
        bool sel = (p->fx_mode == s_fx_modes_compact[i]);
        draw_pill(bx, by, btn_w, btn_h, sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(t, staticBuf, s_fx_labels_compact[i]);
        float tw = 0, th = 0;
        C2D_TextGetDimensions(t, sc, sc, &tw, &th);
        C2D_DrawText(t, C2D_WithColor,
                     bx + (btn_w - tw) * 0.5f,
                     by + (btn_h - th) * 0.5f - 1.0f,
                     0.5f, sc, sc, sel ? CLR_WHITE : CLR_TEXT);
    }

    draw_fx_intensity_row(staticBuf, t, p, cy + 62.0f, cy + 66.0f, cy + 84.0f,
                          cy + 66.0f, true);
}

// ---------------------------------------------------------------------------
// Bottom nav bar (always visible at y=200)
// ---------------------------------------------------------------------------

void draw_bottom_nav(C2D_TextBuf buf, int active_tab) {
    C2D_Text t;
    const char *labels[4] = { "Shoot", "Style", "Presets", "More" };

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
// track_x_fixed: if > 0, use as fixed track start (aligns multiple sliders); else derive from label width.
// track_w_override: if > 0, use as track width; else fill to right edge (BOT_W - 8).

void draw_shoot_tab(C2D_TextBuf staticBuf,
                    bool selfie, int save_flash,
                    const PaletteDef *user_palettes,
                    int active_palette,
                    bool gallery_mode,
                    const FilterParams *p, const FilterRanges *ranges,
                    int shoot_mode, int capture_mode, bool shoot_mode_open,
                    int stereo_output,
                    bool gb_enabled,
                    int shoot_timer_secs, bool timer_open,
                    int wiggle_frames, int wiggle_delay_ms,
                    bool wiggle_preview,
                    int wiggle_offset_dx, int wiggle_offset_dy,
                    bool lomo_enabled, int lomo_preset,
                    bool bend_enabled, int bend_preset) {
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
        C2D_TextParse(&t, staticBuf, "START Clear");
        float ctw = 0, cth = 0;
        C2D_TextGetDimensions(&t, 0.28f, 0.28f, &ctw, &cth);
        C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)SHOOT_SAVE_Y + 5.0f, 0.5f,
                     0.28f, 0.28f, CLR_DIM);
        if (!shoot_mode_open && !timer_open) {
            // ---- Quick-access row: capture selectors + effect stages ----
            static const char *mode_labels[SHOOT_STAGE_BTN_COUNT] = {
                "Still", "Stereo", "GB", "Tone",
                "Lomo", "Bend", "FX", "Timer"
            };

            for (int i = 0; i < SHOOT_STAGE_BTN_COUNT; i++) {
                int row = i / SHOOT_STAGE_GRID_COLS;
                int col = i % SHOOT_STAGE_GRID_COLS;
                float bx = SHOOT_MODE_BTN_GAP + col * (SHOOT_MODE_BTN_W + SHOOT_MODE_BTN_GAP);
                float by = (float)SHOOT_MODE_ROW1_Y +
                           row * (SHOOT_MODE_ROW_H + SHOOT_MODE_BTN_GAP);
                bool sel = false;
                if (i == 0) sel = (capture_mode == CAPTURE_MODE_STILL);
                else if (i == 1) sel = (capture_mode == CAPTURE_MODE_STEREO);
                else if (i == 2) sel = gb_enabled;
                else if (i == 3) sel = (!gb_enabled &&
                                        (fabsf(p->brightness - ranges->bright_def) > 0.001f ||
                                         fabsf(p->contrast - ranges->contrast_def) > 0.001f ||
                                         fabsf(p->saturation - ranges->sat_def) > 0.001f ||
                                         fabsf(p->gamma - ranges->gamma_def) > 0.001f));
                else if (i == 4) sel = lomo_enabled;
                else if (i == 5) sel = bend_enabled;
                else if (i == 6) sel = (p->fx_mode != FX_NONE);
                else if (i == 7) sel = (shoot_timer_secs > 0);

                draw_pill(bx, by, SHOOT_MODE_BTN_W, SHOOT_MODE_ROW_H,
                          (i == 7 && sel) ? CLR_CONFIRM : (sel ? CLR_ACCENT : CLR_BTN));

                C2D_TextParse(&t, staticBuf, mode_labels[i]);
                float tw2 = 0, th2 = 0;
                C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw2, &th2);
                C2D_DrawText(&t, C2D_WithColor,
                             bx + (SHOOT_MODE_BTN_W - tw2) / 2.0f,
                             by + (SHOOT_MODE_ROW_H - th2) / 2.0f - 1.0f,
                             0.5f, 0.42f, 0.42f,
                             sel ? CLR_WHITE : CLR_TEXT);
            }
        } else if (timer_open) {
            // ---- Timer settings panel ----
            draw_pill(4.0f, (float)SHOOT_BACK_Y + 2, (float)SHOOT_BACK_W, (float)SHOOT_BACK_H - 4, CLR_BTN);
            C2D_TextParse(&t, staticBuf, "< Back");
            C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         4.0f + ((float)SHOOT_BACK_W - tw) * 0.5f,
                         (float)SHOOT_BACK_Y + ((float)SHOOT_BACK_H - th) * 0.5f - 1.0f,
                         0.5f, 0.40f, 0.40f, CLR_TEXT);
            C2D_TextParse(&t, staticBuf, "Timer");
            C2D_TextGetDimensions(&t, 0.46f, 0.46f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         (float)SHOOT_BACK_W + 8.0f + ((BOT_W - SHOOT_BACK_W - 12.0f) - tw) * 0.5f,
                         (float)SHOOT_BACK_Y + ((float)SHOOT_BACK_H - th) * 0.5f - 1.0f,
                         0.5f, 0.46f, 0.46f, CLR_ACCENT);
            C2D_DrawRectSolid(0, (float)(SHOOT_BACK_Y + SHOOT_BACK_H + 2), 0.5f, BOT_W, 1, CLR_DIVIDER);

            float cy = (float)SHOOT_CONTENT_Y;
            C2D_TextParse(&t, staticBuf, "Countdown delay before capture:");
            C2D_DrawText(&t, C2D_WithColor, 8.0f, cy, 0.5f, 0.38f, 0.38f, CLR_DIM);

            static const int  timer_vals[SHOOT_TIMER_VAL_COUNT] = SHOOT_TIMER_VALS_INIT;
            static const char *timer_lbls[SHOOT_TIMER_VAL_COUNT] = SHOOT_TIMER_LBLS_INIT;
            float total_btn_w = SHOOT_TIMER_VAL_COUNT * SHOOT_TIMER_BTN_W + (SHOOT_TIMER_VAL_COUNT - 1) * SHOOT_TIMER_BTN_GAP;
            float btn_start_x = (BOT_W - total_btn_w) * 0.5f;
            for (int i = 0; i < SHOOT_TIMER_VAL_COUNT; i++) {
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

            // "No capture" notice — top screen is disabled in this panel
            C2D_TextParse(&t, staticBuf, "Top screen disabled — settings only");
            C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor, (BOT_W - tw) * 0.5f, cy + 62.0f,
                         0.5f, 0.36f, 0.36f, CLR_DIM);

        } else {
            // ---- Capture mode contextual panel ----

            // Back button (top-left)
            draw_pill(4.0f, (float)SHOOT_BACK_Y + 2, (float)SHOOT_BACK_W, (float)SHOOT_BACK_H - 4, CLR_BTN);
            C2D_TextParse(&t, staticBuf, "< Back");
            C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         4.0f + ((float)SHOOT_BACK_W - tw) * 0.5f,
                         (float)SHOOT_BACK_Y + ((float)SHOOT_BACK_H - th) * 0.5f - 1.0f,
                         0.5f, 0.40f, 0.40f, CLR_TEXT);

            if (shoot_mode == SHOOT_MODE_GBCAM) {
                draw_pill((float)SHOOT_GB_TOGGLE_X, (float)SHOOT_GB_TOGGLE_Y,
                          (float)SHOOT_GB_TOGGLE_W, (float)SHOOT_GB_TOGGLE_H,
                          gb_enabled ? CLR_ACCENT : CLR_BTN);
                C2D_TextParse(&t, staticBuf, gb_enabled ? "GB On" : "GB Off");
                float gtw = 0, gth = 0;
                C2D_TextGetDimensions(&t, 0.32f, 0.32f, &gtw, &gth);
                C2D_DrawText(&t, C2D_WithColor,
                             SHOOT_GB_TOGGLE_X + (SHOOT_GB_TOGGLE_W - gtw) * 0.5f,
                             SHOOT_GB_TOGGLE_Y + (SHOOT_GB_TOGGLE_H - gth) * 0.5f - 1.0f,
                             0.5f, 0.32f, 0.32f,
                             gb_enabled ? CLR_WHITE : CLR_TEXT);
            }

            // Mode title (right of back button)
            static const char *mode_titles[SHOOT_MODE_COUNT] = {
                "GB Filter", "Stereo", "Tone", "Base Look", "Bend", "Post FX"
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

            if (shoot_mode == SHOOT_MODE_GBCAM || shoot_mode == SHOOT_MODE_TONE) {
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
                float vtrack_top = cy + 14.0f;
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

            } else if (shoot_mode == SHOOT_MODE_WIGGLE) {
                // Wiggle panel layout:
                //   Left zone  (x=0..159):  X horizontal slider (top) + Y vertical slider (bottom-left)
                //   Mid zone   (x=160..249): (Y vertical slider centred here)
                //   Right zone (x=160..319): Delay vertical slider
                // All vertical sliders end at SHOOT_SAVE_Y-20 to avoid overlap with save button.
                // Left zone (x=0..158): two stepper rows for X and Y offsets
                // Only shown after capture (wiggle_preview == true)
                // Layout per row: label | [-] [value] [+] [R]
                #define WIG_BTN_W   28
                #define WIG_BTN_H   22
                #define WIG_VAL_W   36
                #define WIG_RST_W   22
                // x positions: label at 2, then [-] at 18, value, [+], [R]
                #define WIG_MINUS_X  18.0f
                #define WIG_VAL_X    (WIG_MINUS_X + WIG_BTN_W + 2)
                #define WIG_PLUS_X   (WIG_VAL_X + WIG_VAL_W + 2)
                #define WIG_RST_X    (WIG_PLUS_X + WIG_BTN_W + 2)

                if (!wiggle_preview) {
                    // Pre-capture: just show a hint
                    C2D_Text th2; float thw=0,thh=0;
                    C2D_TextParse(&th2, staticBuf, "A = Capture");
                    C2D_TextGetDimensions(&th2, 0.38f, 0.38f, &thw, &thh);
                    C2D_DrawText(&th2, C2D_WithColor,
                                 (158.0f - thw) * 0.5f, cy + (((float)SHOOT_SAVE_Y - cy) - thh) * 0.5f,
                                 0.5f, 0.38f, 0.38f, CLR_DIM);
                } else {

                // -- Row 1: X offset --
                {
                    float ry = cy + 4.0f;
                    // label
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"X");
                      C2D_TextGetDimensions(&t,0.36f,0.36f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,4.0f+(16.0f-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.36f,0.36f,CLR_DIM); }
                    draw_pill(WIG_MINUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"-");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_MINUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                    draw_rounded_rect(WIG_VAL_X, ry, WIG_VAL_W, WIG_BTN_H, 3.0f, CLR_TRACK);
                    { char buf[8]; snprintf(buf,sizeof(buf),"%d",wiggle_offset_dx);
                      C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,buf);
                      C2D_TextGetDimensions(&t,0.33f,0.33f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_VAL_X+(WIG_VAL_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.33f,0.33f,CLR_TEXT); }
                    draw_pill(WIG_PLUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"+");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_PLUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                    draw_pill(WIG_RST_X, ry, WIG_RST_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"R");
                      C2D_TextGetDimensions(&t,0.30f,0.30f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_RST_X+(WIG_RST_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.30f,0.30f,CLR_TEXT); }
                }

                // -- Row 2: Y offset --
                {
                    float ry = cy + 32.0f;
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"Y");
                      C2D_TextGetDimensions(&t,0.36f,0.36f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,4.0f+(16.0f-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.36f,0.36f,CLR_DIM); }
                    draw_pill(WIG_MINUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"-");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_MINUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                    draw_rounded_rect(WIG_VAL_X, ry, WIG_VAL_W, WIG_BTN_H, 3.0f, CLR_TRACK);
                    { char buf[8]; snprintf(buf,sizeof(buf),"%d",wiggle_offset_dy);
                      C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,buf);
                      C2D_TextGetDimensions(&t,0.33f,0.33f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_VAL_X+(WIG_VAL_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.33f,0.33f,CLR_TEXT); }
                    draw_pill(WIG_PLUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"+");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_PLUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                    draw_pill(WIG_RST_X, ry, WIG_RST_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"R");
                      C2D_TextGetDimensions(&t,0.30f,0.30f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_RST_X+(WIG_RST_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.30f,0.30f,CLR_TEXT); }
                }

                // -- Row 3: total animation frames --
                if (stereo_output == STEREO_OUTPUT_WIGGLE) {
                    float ry = cy + 60.0f;
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"Fr");
                      C2D_TextGetDimensions(&t,0.32f,0.32f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,4.0f+(16.0f-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.32f,0.32f,CLR_DIM); }
                    draw_pill(WIG_MINUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"-");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_MINUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                    draw_rounded_rect(WIG_VAL_X, ry, WIG_VAL_W, WIG_BTN_H, 3.0f, CLR_TRACK);
                    { char buf[8]; snprintf(buf,sizeof(buf),"%d",wiggle_frames);
                      C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,buf);
                      C2D_TextGetDimensions(&t,0.33f,0.33f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_VAL_X+(WIG_VAL_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.33f,0.33f,CLR_TEXT); }
                    draw_pill(WIG_PLUS_X, ry, WIG_BTN_W, WIG_BTN_H, CLR_BTN);
                    { C2D_Text t; float tw=0,th=0; C2D_TextParse(&t,staticBuf,"+");
                      C2D_TextGetDimensions(&t,0.44f,0.44f,&tw,&th);
                      C2D_DrawText(&t,C2D_WithColor,WIG_PLUS_X+(WIG_BTN_W-tw)*0.5f,ry+(WIG_BTN_H-th)*0.5f,0.5f,0.44f,0.44f,CLR_TEXT); }
                }
                } // end wiggle_preview else

                #undef WIG_BTN_W
                #undef WIG_BTN_H
                #undef WIG_VAL_W
                #undef WIG_RST_W
                #undef WIG_MINUS_X
                #undef WIG_VAL_X
                #undef WIG_PLUS_X
                #undef WIG_RST_X

                // Divider
                C2D_DrawRectSolid(158.0f, cy, 0.5f, 1.0f, (float)SHOOT_SAVE_Y - cy, CLR_DIVIDER);

                // -- Delay: presets + stepper in right zone (x=160..319) --
                {
                    #define DZONE_X    160
                    #define DZONE_W    160
                    #define DZONE_CX   (DZONE_X + DZONE_W / 2)
                    // Output selector
                    {
                        C2D_Text td; float lw = 0, lh = 0;
                        C2D_TextParse(&td, staticBuf, "Output");
                        C2D_TextGetDimensions(&td, 0.36f, 0.36f, &lw, &lh);
                        C2D_DrawText(&td, C2D_WithColor,
                                     DZONE_CX - lw * 0.5f, cy + 4.0f,
                                     0.5f, 0.36f, 0.36f, CLR_DIM);
                    }
                    {
                        static const char *out_labels[2] = {"Wiggle", "Ana"};
                        const int out_vals[2] = {STEREO_OUTPUT_WIGGLE, STEREO_OUTPUT_ANAGLYPH};
                        #define OPILL_W   54
                        #define OPILL_H   17
                        #define OPILL_GAP  5
                        float total_w = 2 * OPILL_W + OPILL_GAP;
                        float px0 = DZONE_X + (DZONE_W - total_w) * 0.5f;
                        float py0 = cy + 20.0f;
                        for (int i = 0; i < 2; i++) {
                            float bx = px0 + i * (OPILL_W + OPILL_GAP);
                            bool sel = (stereo_output == out_vals[i]);
                            draw_pill(bx, py0, OPILL_W, OPILL_H,
                                      sel ? CLR_ACCENT : CLR_BTN);
                            C2D_Text tp; float tw = 0, th = 0;
                            C2D_TextParse(&tp, staticBuf, out_labels[i]);
                            C2D_TextGetDimensions(&tp, 0.32f, 0.32f, &tw, &th);
                            C2D_DrawText(&tp, C2D_WithColor,
                                         bx + (OPILL_W - tw) * 0.5f,
                                         py0 + (OPILL_H - th) * 0.5f,
                                         0.5f, 0.32f, 0.32f,
                                         sel ? CLR_WHITE : CLR_TEXT);
                        }
                        #undef OPILL_W
                        #undef OPILL_H
                        #undef OPILL_GAP
                    }

                    // Preset pills: 50 / 100 / 200 / 500
                    if (stereo_output == STEREO_OUTPUT_WIGGLE) {
                        static const int presets[4] = {50, 100, 200, 500};
                        static const char *preset_labels[4] = {"50", "100", "200", "500"};
                        #define DPILL_W   32
                        #define DPILL_H   16
                        #define DPILL_GAP  3
                        float total_w = 4 * DPILL_W + 3 * DPILL_GAP;
                        float px0 = DZONE_X + (DZONE_W - total_w) * 0.5f;
                        float py0 = cy + 42.0f;
                        for (int i = 0; i < 4; i++) {
                            float bx = px0 + i * (DPILL_W + DPILL_GAP);
                            bool sel = (wiggle_delay_ms == presets[i]);
                            draw_pill(bx, py0, DPILL_W, DPILL_H,
                                      sel ? CLR_ACCENT : CLR_BTN);
                            C2D_Text tp; float tw = 0, th = 0;
                            C2D_TextParse(&tp, staticBuf, preset_labels[i]);
                            C2D_TextGetDimensions(&tp, 0.33f, 0.33f, &tw, &th);
                            C2D_DrawText(&tp, C2D_WithColor,
                                         bx + (DPILL_W - tw) * 0.5f,
                                         py0 + (DPILL_H - th) * 0.5f,
                                         0.5f, 0.33f, 0.33f,
                                         sel ? CLR_WHITE : CLR_TEXT);
                        }
                        #undef DPILL_W
                        #undef DPILL_H
                        #undef DPILL_GAP
                    }
                    // Stepper row: [ - ]  [ NNNms ]  [ + ]
                    if (stereo_output == STEREO_OUTPUT_WIGGLE) {
                        #define DSTEP_BTN_W  22
                        #define DSTEP_BTN_H  18
                        #define DSTEP_VAL_W  54
                        float sy = cy + 64.0f;
                        float total_w = 2 * DSTEP_BTN_W + DSTEP_VAL_W + 4;
                        float sx0 = DZONE_X + (DZONE_W - total_w) * 0.5f;
                        // "-" button
                        draw_pill(sx0, sy, DSTEP_BTN_W, DSTEP_BTN_H, CLR_BTN);
                        {
                            C2D_Text tm; float tw = 0, th = 0;
                            C2D_TextParse(&tm, staticBuf, "-");
                            C2D_TextGetDimensions(&tm, 0.44f, 0.44f, &tw, &th);
                            C2D_DrawText(&tm, C2D_WithColor,
                                         sx0 + (DSTEP_BTN_W - tw) * 0.5f,
                                         sy + (DSTEP_BTN_H - th) * 0.5f,
                                         0.5f, 0.44f, 0.44f, CLR_TEXT);
                        }
                        // Value label
                        float vx = sx0 + DSTEP_BTN_W + 2;
                        draw_rounded_rect(vx, sy, DSTEP_VAL_W, DSTEP_BTN_H, 3.0f, CLR_TRACK);
                        {
                            char vbuf[10]; snprintf(vbuf, sizeof(vbuf), "%dms", wiggle_delay_ms);
                            C2D_Text tv; float tw = 0, th = 0;
                            C2D_TextParse(&tv, staticBuf, vbuf);
                            C2D_TextGetDimensions(&tv, 0.33f, 0.33f, &tw, &th);
                            C2D_DrawText(&tv, C2D_WithColor,
                                         vx + (DSTEP_VAL_W - tw) * 0.5f,
                                         sy + (DSTEP_BTN_H - th) * 0.5f,
                                         0.5f, 0.33f, 0.33f, CLR_TEXT);
                        }
                        // "+" button
                        float px_btn = vx + DSTEP_VAL_W + 2;
                        draw_pill(px_btn, sy, DSTEP_BTN_W, DSTEP_BTN_H, CLR_BTN);
                        {
                            C2D_Text tp; float tw = 0, th = 0;
                            C2D_TextParse(&tp, staticBuf, "+");
                            C2D_TextGetDimensions(&tp, 0.44f, 0.44f, &tw, &th);
                            C2D_DrawText(&tp, C2D_WithColor,
                                         px_btn + (DSTEP_BTN_W - tw) * 0.5f,
                                         sy + (DSTEP_BTN_H - th) * 0.5f,
                                         0.5f, 0.44f, 0.44f, CLR_TEXT);
                        }
                        #undef DSTEP_BTN_W
                        #undef DSTEP_BTN_H
                        #undef DSTEP_VAL_W
                    }
                    if (stereo_output == STEREO_OUTPUT_ANAGLYPH) {
                        C2D_Text ta; float aw = 0, ah = 0;
                        C2D_TextParse(&ta, staticBuf, "red/cyan JPG");
                        C2D_TextGetDimensions(&ta, 0.36f, 0.36f, &aw, &ah);
                        C2D_DrawText(&ta, C2D_WithColor,
                                     DZONE_CX - aw * 0.5f, cy + 50.0f,
                                     0.5f, 0.36f, 0.36f, CLR_DIM);
                    }
                    #undef DZONE_X
                    #undef DZONE_W
                    #undef DZONE_CX
                }


                #undef WIG_VTRACK_W
                #undef WIG_VHANDLE_W
                #undef WIG_VHANDLE_H
                #undef WIG_VTOP
                #undef WIG_VBOT
                #undef WIG_VH

            } else if (shoot_mode == SHOOT_MODE_LOMO) {
                // 3×2 grid of preset buttons
                for (int row = 0; row < LOMO_GRID_ROWS; row++) {
                    for (int col = 0; col < LOMO_GRID_COLS; col++) {
                        int idx = row * LOMO_GRID_COLS + col;
                        if (idx >= LOMO_PRESET_COUNT) break;
                        float bx = LOMO_GRID_GAP + col * (LOMO_GRID_BTN_W + LOMO_GRID_GAP);
                        float by = cy + row * (LOMO_GRID_BTN_H + LOMO_GRID_GAP);
                        bool sel = lomo_enabled && (lomo_preset == idx);
                        draw_pill(bx, by, LOMO_GRID_BTN_W, LOMO_GRID_BTN_H,
                                  sel ? CLR_ACCENT : CLR_BTN);
                        C2D_TextParse(&t, staticBuf, lomo_presets[idx].name);
                        float tw2 = 0, th2 = 0;
                        C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw2, &th2);
                        C2D_DrawText(&t, C2D_WithColor,
                                     bx + (LOMO_GRID_BTN_W - tw2) / 2.0f,
                                     by + (LOMO_GRID_BTN_H - th2) / 2.0f - 1.0f,
                                     0.5f, 0.42f, 0.42f,
                                     sel ? CLR_WHITE : CLR_TEXT);
                    }
                }
            } else if (shoot_mode == SHOOT_MODE_BEND) {
                // 3×2 grid of circuit-bend preset buttons
                for (int row = 0; row < BEND_GRID_ROWS; row++) {
                    for (int col = 0; col < BEND_GRID_COLS; col++) {
                        int idx = row * BEND_GRID_COLS + col;
                        if (idx >= BEND_PRESET_COUNT) break;
                        float bx = BEND_GRID_GAP + col * (BEND_GRID_BTN_W + BEND_GRID_GAP);
                        float by = cy + row * (BEND_GRID_BTN_H + BEND_GRID_GAP);
                        bool sel = bend_enabled && (bend_preset == idx);
                        draw_pill(bx, by, BEND_GRID_BTN_W, BEND_GRID_BTN_H,
                                  sel ? CLR_ACCENT : CLR_BTN);
                        C2D_TextParse(&t, staticBuf, bend_presets[idx].name);
                        float tw2 = 0, th2 = 0;
                        C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw2, &th2);
                        C2D_DrawText(&t, C2D_WithColor,
                                     bx + (BEND_GRID_BTN_W - tw2) / 2.0f,
                                     by + (BEND_GRID_BTN_H - th2) / 2.0f - 1.0f,
                                     0.5f, 0.42f, 0.42f,
                                     sel ? CLR_WHITE : CLR_TEXT);
                    }
                }
            } else if (shoot_mode == SHOOT_MODE_FX) {
                draw_fx_panel_compact(staticBuf, &t, p, cy);
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
        } else if (wiggle_preview) {
            save_bg  = CLR_CONFIRM;
            save_txt = CLR_WHITE;
            save_label = "Confirm Save";
        } else {
            save_bg  = CLR_ACCENT;
            save_txt = CLR_WHITE;
            save_label = (capture_mode == CAPTURE_MODE_STEREO) ? "Capture" : "Save";
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
    float tw, th;
    (void)dynBuf;

    // Full-screen gallery context — owns y=0..NAV_Y (200px)
    // Header bar: y=0..30  — title + Close/Edit buttons
    // Thumbnail grid: y=32..NAV_Y-4
    // Scroll arrows at right edge

    // --- Header bar ---
    C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, 30.0f, CLR_PANEL);
    C2D_DrawRectSolid(0, 30.0f, 0.5f, BOT_W, 1.0f, CLR_DIVIDER);

    C2D_TextParse(&t, staticBuf, "Gallery");
    C2D_TextGetDimensions(&t, 0.48f, 0.48f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 (BOT_W - tw) * 0.5f, (30.0f - th) * 0.5f,
                 0.5f, 0.48f, 0.48f, CLR_ACCENT);

    // Close button (left)
    draw_pill(4.0f, 3.0f, 50.0f, 24.0f, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Close");
    C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 4.0f + (50.0f - tw) * 0.5f, 3.0f + (24.0f - th) * 0.5f,
                 0.5f, 0.38f, 0.38f, CLR_TEXT);

    // Edit button (right, only when something selected)
    if (gallery_count > 0) {
        draw_pill(BOT_W - 54.0f, 3.0f, 50.0f, 24.0f, CLR_ACCENT);
        C2D_TextParse(&t, staticBuf, "Edit");
        C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     BOT_W - 54.0f + (50.0f - tw) * 0.5f, 3.0f + (24.0f - th) * 0.5f,
                     0.5f, 0.38f, 0.38f, CLR_WHITE);
    }

    if (gallery_count == 0) {
        C2D_TextParse(&t, staticBuf, "No photos yet");
        C2D_TextGetDimensions(&t, 0.55f, 0.55f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     (BOT_W - tw) * 0.5f, 80.0f + (CONTENT_H - 80.0f - th) * 0.5f,
                     0.5f, 0.55f, 0.55f, CLR_DIM);
        return;
    }

    // Thumbnail grid — 4 cols × 4 rows starting at y=33
    // Available height: 200-30-nav = but we draw inside content area y=31..199
    // Row height: (199-31-4) / 4 = 41px; cell width: (302-5*4)/4 = 71px
    #define GAL_GRID_Y0     32
    #define GAL_GRID_COLS    4
    #define GAL_GRID_ROWS    4
    #define GAL_CELL_W      71
    #define GAL_CELL_H      40
    #define GAL_CELL_GAP     3
    #define GAL_ROW_H       (GAL_CELL_H + GAL_CELL_GAP)
    #define GAL_SCROLL_X    302  // right edge scroll arrows

    for (int row = 0; row < GAL_GRID_ROWS; row++) {
        for (int col = 0; col < GAL_GRID_COLS; col++) {
            int idx = gallery_scroll * GAL_GRID_COLS + row * GAL_GRID_COLS + col;
            if (idx >= gallery_count) goto done_gallery_grid;

            float cx = GAL_CELL_GAP + col * (GAL_CELL_W + GAL_CELL_GAP);
            float cy = GAL_GRID_Y0  + row * GAL_ROW_H;
            bool sel = (idx == gallery_sel);

            draw_pill(cx, cy, GAL_CELL_W, GAL_CELL_H,
                      sel ? CLR_ACCENT : CLR_BTN);

            const char *path = gallery_paths[idx];
            const char *slash = path;
            for (const char *p = path; *p; p++) if (*p == '/') slash = p + 1;
            char label[10] = {0};
            int n = 0;
            bool is_wiggle = false;
            if (sscanf(slash, "GB_%d.JPG", &n) == 1)
                snprintf(label, sizeof(label), "%04d", n);
            else if (sscanf(slash, "GW_%d.png", &n) == 1) {
                snprintf(label, sizeof(label), "%04d", n);
                is_wiggle = true;
            } else {
                snprintf(label, sizeof(label), "?");
            }
            if (is_wiggle)
                C2D_DrawRectSolid(cx + 3.0f, cy + 3.0f, 0.4f, 6.0f, 6.0f,
                                  sel ? CLR_WHITE : CLR_ACCENT);
            C2D_TextParse(&t, staticBuf, label);
            C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         cx + (GAL_CELL_W - tw) * 0.5f,
                         cy + (GAL_CELL_H - th) * 0.5f,
                         0.5f, 0.40f, 0.40f,
                         sel ? CLR_WHITE : CLR_TEXT);
        }
    }
    done_gallery_grid:;

    // Scroll arrows (right column)
    {
        int total_rows = (gallery_count + GAL_GRID_COLS - 1) / GAL_GRID_COLS;
        int max_scroll = total_rows - GAL_GRID_ROWS;
        if (max_scroll < 0) max_scroll = 0;
        u32 up_clr = gallery_scroll > 0          ? CLR_BTN : CLR_TRACK;
        u32 dn_clr = gallery_scroll < max_scroll  ? CLR_BTN : CLR_TRACK;
        draw_pill((float)GAL_SCROLL_X, (float)GAL_GRID_Y0,       14.0f, 78.0f, up_clr);
        draw_pill((float)GAL_SCROLL_X, (float)GAL_GRID_Y0 + 82.0f, 14.0f, 78.0f, dn_clr);
        C2D_TextParse(&t, staticBuf, "^");
        C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     GAL_SCROLL_X + (14.0f - tw) * 0.5f,
                     GAL_GRID_Y0 + (78.0f - th) * 0.5f,
                     0.5f, 0.40f, 0.40f, CLR_TEXT);
        C2D_TextParse(&t, staticBuf, "v");
        C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     GAL_SCROLL_X + (14.0f - tw) * 0.5f,
                     GAL_GRID_Y0 + 82.0f + (78.0f - th) * 0.5f,
                     0.5f, 0.40f, 0.40f, CLR_TEXT);
    }

    #undef GAL_GRID_Y0
    #undef GAL_GRID_COLS
    #undef GAL_GRID_ROWS
    #undef GAL_CELL_W
    #undef GAL_CELL_H
    #undef GAL_CELL_GAP
    #undef GAL_ROW_H
    #undef GAL_SCROLL_X
}

// ---------------------------------------------------------------------------
// Gallery edit tab (replaces gallery when gallery_edit_mode is true)
// ---------------------------------------------------------------------------

void draw_gallery_edit_tab(C2D_TextBuf staticBuf,
                           int edit_tab, int sticker_cat, int sticker_sel, int sticker_scroll,
                           int gallery_frame,
                           float sticker_cursor_x, float sticker_cursor_y,
                           float sticker_pending_scale, float sticker_pending_angle,
                           bool sticker_placing) {
    C2D_Text t;
    float tw, th;

    // Layout split: left panel (picker) x=0..159, right panel (preview) x=160..319
    // divided at x=160 with a 1px divider.
    #define GEDIT_SPLIT_X  160

    // --- Tab bar across full width: [Stickers] [Frames] ---
    for (int i = 0; i < 2; i++) {
        bool sel = (edit_tab == i);
        draw_pill((float)(i * GEDIT_TAB_W), (float)GEDIT_TAB_Y,
                  (float)GEDIT_TAB_W, (float)GEDIT_TAB_H,
                  sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(&t, staticBuf, i == 0 ? "Stickers" : "Frames");
        C2D_TextGetDimensions(&t, 0.44f, 0.44f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     i * GEDIT_TAB_W + (GEDIT_TAB_W - tw) * 0.5f,
                     GEDIT_TAB_Y + (GEDIT_TAB_H - th) * 0.5f,
                     0.5f, 0.44f, 0.44f,
                     sel ? CLR_WHITE : CLR_TEXT);
    }

    // Divider below tab bar
    C2D_DrawRectSolid(0, (float)GEDIT_TAB_H, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // Vertical divider between left picker and right preview
    C2D_DrawRectSolid((float)GEDIT_SPLIT_X, (float)GEDIT_TAB_H, 0.5f,
                      1.0f, (float)(GEDIT_ACT_Y - GEDIT_TAB_H), CLR_DIVIDER);

    // ===== LEFT PANEL: picker (x=0..159) =====
    if (edit_tab == 0) {
        // Category strip — small pill per category, across left panel
        #define CAT_STRIP_H  18
        #define CAT_STRIP_Y  (GEDIT_TAB_H + 2)
        #define CAT_BTN_W    ((GEDIT_SPLIT_X - 4) / STICKER_CAT_COUNT)
        for (int ci = 0; ci < STICKER_CAT_COUNT; ci++) {
            float bx = 2.0f + ci * CAT_BTN_W;
            bool csel = (ci == sticker_cat);
            draw_pill(bx, (float)CAT_STRIP_Y, (float)(CAT_BTN_W - 2), (float)(CAT_STRIP_H - 2),
                      csel ? CLR_ACCENT : CLR_TRACK);
            C2D_TextParse(&t, staticBuf, sticker_cats[ci].label);
            C2D_TextGetDimensions(&t, 0.30f, 0.30f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         bx + ((CAT_BTN_W - 2) - tw) * 0.5f,
                         (float)CAT_STRIP_Y + ((CAT_STRIP_H - 2) - th) * 0.5f,
                         0.5f, 0.30f, 0.30f,
                         csel ? CLR_WHITE : CLR_TEXT);
        }
        C2D_DrawRectSolid(0, (float)(CAT_STRIP_Y + CAT_STRIP_H), 0.5f, (float)GEDIT_SPLIT_X, 1, CLR_DIVIDER);

        // Sticker grid — uses GEDIT_STICKER_* constants from ui.h
        #define SGRID_X0   2
        #define SGRID_Y0   (CAT_STRIP_Y + CAT_STRIP_H + 2)

        sticker_cat_load(sticker_cat);
        int cat_count  = sticker_cats[sticker_cat].count;
        int total_rows = (cat_count + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
        int max_scroll = total_rows - GEDIT_STICKER_ROWS;
        if (max_scroll < 0) max_scroll = 0;

        int visible_start = sticker_scroll * GEDIT_STICKER_COLS;
        for (int row = 0; row < GEDIT_STICKER_ROWS; row++) {
            for (int col = 0; col < GEDIT_STICKER_COLS; col++) {
                int idx = visible_start + row * GEDIT_STICKER_COLS + col;
                if (idx >= cat_count) goto done_sticker_grid;

                float cx = SGRID_X0 + col * (GEDIT_STICKER_CELL + GEDIT_STICKER_GAP);
                float cy = SGRID_Y0 + row * GEDIT_STICKER_ROW_H;
                bool sel = (idx == sticker_sel);

                if (sel)
                    C2D_DrawRectSolid(cx - 2, cy - 2, 0.45f,
                                      GEDIT_STICKER_CELL + 4, GEDIT_STICKER_CELL + 4, CLR_ACCENT);
                C2D_DrawRectSolid(cx, cy, 0.46f, GEDIT_STICKER_CELL, GEDIT_STICKER_CELL, CLR_WHITE);
                draw_sticker_c2d(sticker_cat, idx, cx, cy, (float)GEDIT_STICKER_CELL, (float)GEDIT_STICKER_CELL);
            }
        }
        done_sticker_grid:;

        #undef SGRID_X0
        #undef SGRID_Y0
        #undef CAT_BTN_W
        #undef CAT_STRIP_H
        #undef CAT_STRIP_Y

    } else {
        // Frame picker — all items auto-sized to fit picker area
        for (int i = 0; i < FRAME_COUNT; i++) {
            float fy = GEDIT_PICKER_Y + i * FRAME_ROW_H;
            bool sel = (gallery_frame == i);
            draw_pill(2.0f, fy, (float)(GEDIT_SPLIT_X - 4), (float)FRAME_PILL_H,
                      sel ? CLR_ACCENT : CLR_BTN);
            C2D_TextParse(&t, staticBuf, frame_names[i]);
            C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         2.0f + ((GEDIT_SPLIT_X - 4) - tw) * 0.5f,
                         fy + (FRAME_PILL_H - th) * 0.5f,
                         0.5f, 0.38f, 0.38f,
                         sel ? CLR_WHITE : CLR_TEXT);
        }
    }

    // ===== RIGHT PANEL: sticker preview + scroll controls (x=161..319) =====
    if (edit_tab == 0) {
        sticker_cat_load(sticker_cat);
        int _cat_count = sticker_cats[sticker_cat].count;
        int _total_rows = (_cat_count + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
        int _max_scroll = _total_rows - GEDIT_STICKER_ROWS;
        if (_max_scroll < 0) _max_scroll = 0;

        // Right panel horizontal centre
        #define RPANEL_X    (GEDIT_SPLIT_X + 1)
        #define RPANEL_W    (BOT_W - RPANEL_X)
        #define RPANEL_CX   (RPANEL_X + RPANEL_W / 2)

        // Preview image — 64×64, centred in right panel, starting just below tab bar
        #define PREV_W  64
        #define PREV_H  64
        #define PREV_X  (RPANEL_CX - PREV_W / 2)
        #define PREV_Y  (GEDIT_TAB_H + 4)

        C2D_DrawRectSolid((float)PREV_X - 1, (float)PREV_Y - 1, 0.46f,
                          PREV_W + 2, PREV_H + 2, CLR_DIVIDER);
        C2D_DrawRectSolid((float)PREV_X, (float)PREV_Y, 0.47f,
                          PREV_W, PREV_H, CLR_WHITE);
        if (sticker_sel >= 0 && sticker_sel < _cat_count)
            draw_sticker_c2d(sticker_cat, sticker_sel, (float)PREV_X, (float)PREV_Y, PREV_W, PREV_H);

        // Sticker name below preview
        float name_y = PREV_Y + PREV_H + 4.0f;
        if (!sticker_placing && sticker_sel >= 0 && sticker_sel < _cat_count) {
            const char *full_path = sticker_cats[sticker_cat].icons[sticker_sel].path;
            const char *base = full_path;
            for (const char *p = full_path; *p; p++) if (*p == '/') base = p + 1;
            static char icon_name[32];
            int ni = 0;
            for (; base[ni] && base[ni] != '.' && ni < 31; ni++)
                icon_name[ni] = (base[ni] == '_') ? ' ' : base[ni];
            icon_name[ni] = '\0';
            C2D_TextParse(&t, staticBuf, icon_name);
            C2D_TextGetDimensions(&t, 0.32f, 0.32f, &tw, &th);
            C2D_DrawText(&t, C2D_WithColor,
                         RPANEL_X + (RPANEL_W - tw) * 0.5f,
                         name_y, 0.5f, 0.32f, 0.32f, CLR_TEXT);
        }

        // Scroll buttons + page indicator — stacked in right panel below name
        #define SARROW_W   (RPANEL_W - 6)
        #define SARROW_H   22
        #define SARROW_X   (RPANEL_X + 3)
        float btn_y = name_y + 16.0f;

        u32 up_col = sticker_scroll > 0          ? CLR_BTN : CLR_TRACK;
        u32 dn_col = sticker_scroll < _max_scroll ? CLR_BTN : CLR_TRACK;

        draw_pill((float)SARROW_X, btn_y,           (float)SARROW_W, (float)SARROW_H, up_col);
        draw_pill((float)SARROW_X, btn_y + SARROW_H + 3, (float)SARROW_W, (float)SARROW_H, dn_col);

        C2D_TextParse(&t, staticBuf, "^ Prev");
        C2D_TextGetDimensions(&t, 0.34f, 0.34f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     SARROW_X + (SARROW_W - tw) * 0.5f,
                     btn_y + (SARROW_H - th) * 0.5f,
                     0.5f, 0.34f, 0.34f, CLR_TEXT);

        C2D_TextParse(&t, staticBuf, "Next v");
        C2D_TextGetDimensions(&t, 0.34f, 0.34f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     SARROW_X + (SARROW_W - tw) * 0.5f,
                     btn_y + SARROW_H + 3 + (SARROW_H - th) * 0.5f,
                     0.5f, 0.34f, 0.34f, CLR_TEXT);

        // Page indicator below buttons
        char pg[24];
        snprintf(pg, sizeof(pg), "p.%d/%d",
                 sticker_scroll + 1, _total_rows > 0 ? _total_rows : 1);
        C2D_TextParse(&t, staticBuf, pg);
        C2D_TextGetDimensions(&t, 0.30f, 0.30f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     RPANEL_X + (RPANEL_W - tw) * 0.5f,
                     btn_y + 2 * SARROW_H + 8.0f,
                     0.5f, 0.30f, 0.30f, CLR_DIM);

        #undef SARROW_W
        #undef SARROW_H
        #undef SARROW_X
        #undef PREV_W
        #undef PREV_H
        #undef PREV_X
        #undef PREV_Y
        #undef RPANEL_X
        #undef RPANEL_W
        #undef RPANEL_CX
    } else {
        // Right panel for frame tab: hint text
        C2D_TextParse(&t, staticBuf, "Tap to select");
        C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     GEDIT_SPLIT_X + 2.0f + ((BOT_W - GEDIT_SPLIT_X - 2) - tw) * 0.5f,
                     GEDIT_PICKER_Y + 20.0f,
                     0.5f, 0.36f, 0.36f, CLR_DIM);
        C2D_TextParse(&t, staticBuf, "a frame");
        C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor,
                     GEDIT_SPLIT_X + 2.0f + ((BOT_W - GEDIT_SPLIT_X - 2) - tw) * 0.5f,
                     GEDIT_PICKER_Y + 38.0f,
                     0.5f, 0.36f, 0.36f, CLR_DIM);
    }

    // Divider above action bar
    C2D_DrawRectSolid(0, (float)GEDIT_PICKER_BOT, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Action bar ---
    C2D_DrawRectSolid(0, (float)GEDIT_ACT_Y - 1, 0.5f, BOT_W, 1, CLR_DIVIDER);

    draw_pill(GEDIT_BTN_CANCEL_X,  GEDIT_ACT_Y, GEDIT_BTN_CANCEL_W,  GEDIT_ACT_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Cancel");
    C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 GEDIT_BTN_CANCEL_X + (GEDIT_BTN_CANCEL_W - tw) * 0.5f,
                 GEDIT_ACT_Y + (GEDIT_ACT_H - th) * 0.5f,
                 0.5f, 0.38f, 0.38f, CLR_TEXT);

    draw_pill(GEDIT_BTN_SAVENEW_X, GEDIT_ACT_Y, GEDIT_BTN_SAVENEW_W, GEDIT_ACT_H, CLR_ACCENT);
    C2D_TextParse(&t, staticBuf, "Save New");
    C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 GEDIT_BTN_SAVENEW_X + (GEDIT_BTN_SAVENEW_W - tw) * 0.5f,
                 GEDIT_ACT_Y + (GEDIT_ACT_H - th) * 0.5f,
                 0.5f, 0.38f, 0.38f, CLR_WHITE);

    draw_pill(GEDIT_BTN_OVERW_X,   GEDIT_ACT_Y, GEDIT_BTN_OVERW_W,   GEDIT_ACT_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Overwrite");
    C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 GEDIT_BTN_OVERW_X + (GEDIT_BTN_OVERW_W - tw) * 0.5f,
                 GEDIT_ACT_Y + (GEDIT_ACT_H - th) * 0.5f,
                 0.5f, 0.38f, 0.38f, CLR_TEXT);

    #undef GEDIT_SPLIT_X
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
                 const PipelinePreset *presets, int preset_selected,
                 bool settings_flash) {
    C2D_Text t;

    C2D_TextParse(&t, staticBuf, "Presets");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)FXTAB_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);
    C2D_TextParse(&t, staticBuf, "Tap a slot to load it.");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, 28.0f, 0.5f, 0.36f, 0.36f, CLR_DIM);
    C2D_TextParse(&t, staticBuf, "Store Current writes over the selected slot.");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, 44.0f, 0.5f, 0.32f, 0.32f, CLR_DIM);

    for (int i = 0; i < PIPELINE_PRESET_COUNT; i++) {
        float by = 60.0f + i * 26.0f;
        bool sel = (preset_selected == i);
        draw_pill(8.0f, by, 304.0f, 24.0f, sel ? CLR_ACCENT : CLR_BTN);
        C2D_TextParse(&t, staticBuf, presets[i].name);
        float tw = 0, th = 0;
        C2D_TextGetDimensions(&t, 0.36f, 0.36f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor, 16.0f, by + (24.0f - th) * 0.5f - 1.0f,
                     0.5f, 0.36f, 0.36f, sel ? CLR_WHITE : CLR_TEXT);

        const char *status = preset_is_empty(&presets[i]) ? "Empty" : "Stored";
        C2D_TextParse(&t, staticBuf, status);
        C2D_TextGetDimensions(&t, 0.32f, 0.32f, &tw, &th);
        C2D_DrawText(&t, C2D_WithColor, 304.0f - tw, by + (24.0f - th) * 0.5f - 1.0f,
                     0.5f, 0.32f, 0.32f, sel ? CLR_WHITE : CLR_DIM);
    }

    C2D_DrawRectSolid(0, 164.0f, 0.5f, BOT_W, 1, CLR_DIVIDER);
    draw_pill(24.0f, 170.0f, 132.0f, 24.0f, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Reset Custom");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor, 24.0f + (132.0f - tw) * 0.5f,
                 170.0f + (24.0f - th) * 0.5f - 1.0f,
                 0.5f, 0.38f, 0.38f, CLR_TEXT);

    draw_pill(164.0f, 170.0f, 132.0f, 24.0f, CLR_CONFIRM);
    C2D_TextParse(&t, staticBuf, "Store Current");
    C2D_TextGetDimensions(&t, 0.40f, 0.40f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor, 164.0f + (132.0f - tw) * 0.5f,
                 170.0f + (24.0f - th) * 0.5f - 1.0f,
                 0.5f, 0.40f, 0.40f, CLR_WHITE);

    (void)settings_flash;
    (void)dynBuf;
}

// ---------------------------------------------------------------------------
// MORE tab (settings overlay)
// ---------------------------------------------------------------------------

void draw_more_tab(C2D_TextBuf staticBuf,
                   const FilterParams *p, int save_scale,
                   int shutter_button, bool settings_flash) {
    float sc = 0.46f;
    C2D_Text t;

    C2D_TextParse(&t, staticBuf, "More Options");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_LABEL_Y, 0.5f, 0.50f, 0.50f, CLR_ACCENT);

    C2D_DrawRectSolid(0, 26, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Save Scale row: 1x / 2x / 3x / 4x ---
    {
        static const char *scale_labels[4] = { "1x", "2x", "3x", "4x" };
        C2D_TextParse(&t, staticBuf, "Save Scale");
        C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_SCALE_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
        for (int si = 0; si < 4; si++) {
            float bx = (float)(MORE_STOG_X0 + si * (MORE_SCALE_BTN_W + MORE_SCALE_BTN_GAP));
            bool sel = (save_scale == si + 1);
            draw_pill(bx, MORE_SCALE_Y - MORE_STOG_H / 2,
                      MORE_SCALE_BTN_W, MORE_STOG_H,
                      sel ? CLR_ACCENT : CLR_BTN);
            C2D_TextParse(&t, staticBuf, scale_labels[si]);
            float tw2 = 0, th2 = 0;
            C2D_TextGetDimensions(&t, 0.38f, 0.38f, &tw2, &th2);
            C2D_DrawText(&t, C2D_WithColor,
                         bx + ((float)MORE_SCALE_BTN_W - tw2) / 2.0f,
                         (float)MORE_SCALE_Y - 8.0f,
                         0.5f, 0.38f, 0.38f,
                         sel ? CLR_WHITE : CLR_TEXT);
        }
    }

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

    // --- Shutter button row ---
    C2D_TextParse(&t, staticBuf, "Shutter");
    C2D_DrawText(&t, C2D_WithColor, 8.0f, (float)MORE_SHUT_Y - 8.0f, 0.5f, sc, sc, CLR_TEXT);
    draw_pill((float)MORE_SHUT_STOG_X0, MORE_SHUT_Y - MORE_STOG_H / 2,
              MORE_STOG_W, MORE_STOG_H,
              !shutter_button ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "A");
    C2D_DrawText(&t, C2D_WithColor, MORE_SHUT_STOG_X0 + 22.0f, MORE_SHUT_Y - 8.0f,
                 0.5f, sc, sc, !shutter_button ? CLR_WHITE : CLR_TEXT);
    draw_pill((float)MORE_SHUT_STOG_X1, MORE_SHUT_Y - MORE_STOG_H / 2,
              MORE_STOG_W, MORE_STOG_H,
              shutter_button ? CLR_ACCENT : CLR_BTN);
    C2D_TextParse(&t, staticBuf, "L/R");
    C2D_DrawText(&t, C2D_WithColor, MORE_SHUT_STOG_X1 + 14.0f, MORE_SHUT_Y - 8.0f,
                 0.5f, sc, sc, shutter_button ? CLR_WHITE : CLR_TEXT);

    C2D_DrawRectSolid(0, MORE_DIV_Y, 0.5f, BOT_W, 1, CLR_DIVIDER);

    // --- Power-user row: Palette Editor | Calibrate ---
    draw_pill((float)MORE_PALED_X, MORE_POWED_Y,
              MORE_POWED_W, MORE_POWED_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Palette Editor");
    float tw = 0, th = 0;
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_PALED_X + (MORE_POWED_W - tw) / 2.0f, MORE_POWED_Y + 2.0f,
                 0.5f, 0.42f, 0.42f, CLR_TEXT);

    draw_pill((float)MORE_CALIB_X, MORE_POWED_Y,
              MORE_POWED_W, MORE_POWED_H, CLR_BTN);
    C2D_TextParse(&t, staticBuf, "Calibrate");
    C2D_TextGetDimensions(&t, 0.42f, 0.42f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_CALIB_X + (MORE_POWED_W - tw) / 2.0f, MORE_POWED_Y + 2.0f,
                 0.5f, 0.42f, 0.42f, CLR_TEXT);

    // --- Save as Default ---
    u32 def_col = settings_flash ? CLR_CONFIRM : CLR_BTN;
    u32 def_txt = settings_flash ? CLR_WHITE   : CLR_TEXT;
    draw_pill((float)MORE_SAVEDEF_X, MORE_SAVEDEF_Y,
              MORE_SAVEDEF_W, MORE_SAVEDEF_H, def_col);
    C2D_TextParse(&t, staticBuf, "Save as Default");
    C2D_TextGetDimensions(&t, 0.44f, 0.44f, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor,
                 MORE_SAVEDEF_X + (MORE_SAVEDEF_W - tw) / 2.0f, MORE_SAVEDEF_Y + 1.0f,
                 0.5f, 0.44f, 0.44f, def_txt);
}
