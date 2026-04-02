#include "ui_draw.h"
#include "filter.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Palette editor tab (accessed from MORE)
// ---------------------------------------------------------------------------

void draw_palette_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
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

void draw_calibrate_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                        const FilterRanges *ranges, bool settings_flash) {
    float sc = 0.44f;
    C2D_Text t;
    char buf_str[20];

    // Back hint
    C2D_TextParse(&t, staticBuf, "< More");
    C2D_DrawText(&t, C2D_WithColor, 4.0f, 4.0f, 0.5f, 0.38f, 0.38f, CLR_DIM);

    C2D_TextBufClear(dynBuf);

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
             const char gallery_paths[][64], int gallery_sel, int gallery_scroll,
             int shoot_mode, bool shoot_mode_open,
             int shoot_timer_secs,
             int wiggle_frames, int wiggle_delay_ms,
             bool wiggle_preview) {
    (void)settings_row;

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
        draw_shoot_tab(staticBuf, selfie, save_flash, user_palettes,
                       p.palette, gallery_mode, &p, ranges,
                       shoot_mode, shoot_mode_open,
                       shoot_timer_secs,
                       wiggle_frames, wiggle_delay_ms,
                       wiggle_preview);
        if (gallery_mode)
            draw_gallery_tab(staticBuf, dynBuf, gallery_count, gallery_paths,
                             gallery_sel, gallery_scroll);
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
