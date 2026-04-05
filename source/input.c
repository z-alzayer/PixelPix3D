#include "input.h"
#include "settings.h"

bool hit(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  FilterParams *p,
                  bool *do_cam_toggle, bool *do_save, bool *do_defaults_save,
                  int *active_tab, int *save_scale,
                  FilterParams *default_params,
                  FilterRanges *ranges,
                  PaletteDef *user_palettes,
                  int *palette_sel_pal, int *palette_sel_color,
                  bool *do_gallery_toggle,
                  bool gallery_mode, int gallery_count, int *gallery_sel, int *gallery_scroll,
                  int *shoot_mode, bool *shoot_mode_open,
                  int *shoot_timer_secs,
                  int *wiggle_frames, int *wiggle_delay_ms) {
    *do_cam_toggle    = false;
    *do_save          = false;
    *do_defaults_save = false;
    *do_gallery_toggle = false;

    bool touched = (kHeld & KEY_TOUCH) != 0;
    bool tapped  = (kDown & KEY_TOUCH) != 0;
    if (!touched) return false;

    int tx = touch.px, ty = touch.py;

    // -----------------------------------------------------------------------
    // Bottom nav bar (y >= NAV_Y, always active)
    // -----------------------------------------------------------------------
    if (tapped && ty >= NAV_Y) {
        int seg = tx / NAV_SEG_W;
        if (seg < 0) seg = 0;
        if (seg > 3) seg = 3;

        if (seg == TAB_SHOOT) {
            if (gallery_mode) *do_gallery_toggle = true;  // close gallery
            *active_tab = TAB_SHOOT;
        } else if (seg == TAB_STYLE) {
            *active_tab = TAB_STYLE;
        } else if (seg == TAB_FX) {
            *active_tab = TAB_FX;
        } else if (seg == TAB_MORE) {
            *active_tab = (*active_tab == TAB_MORE) ? TAB_SHOOT : TAB_MORE;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // SHOOT tab inputs (content area, y < NAV_Y)
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_SHOOT && ty < NAV_Y) {

        // Gallery grid inputs
        if (gallery_mode && tapped) {
            int total_rows = (gallery_count + GALLERY_COLS - 1) / GALLERY_COLS;
            int max_scroll = total_rows - GALLERY_ROWS;
            if (max_scroll < 0) max_scroll = 0;

            if (hit(tx, ty, BTN_GSCROLL_X, BTN_GSCROLL_UP_Y, BTN_GSCROLL_W, BTN_GSCROLL_H)) {
                if (*gallery_scroll > 0) (*gallery_scroll)--;
                return true;
            }
            if (hit(tx, ty, BTN_GSCROLL_X, BTN_GSCROLL_DN_Y, BTN_GSCROLL_W, BTN_GSCROLL_H)) {
                if (*gallery_scroll < max_scroll) (*gallery_scroll)++;
                return true;
            }
            if (ty >= GALLERY_START_Y) {
                int col = (tx - GALLERY_GAP) / (GALLERY_CELL_W + GALLERY_GAP);
                int row = (ty - GALLERY_START_Y) / GALLERY_ROW_H;
                if (col >= 0 && col < GALLERY_COLS && row >= 0 && row < GALLERY_ROWS) {
                    int idx = (*gallery_scroll * GALLERY_COLS) + row * GALLERY_COLS + col;
                    if (idx >= 0 && idx < gallery_count) {
                        *gallery_sel = idx;
                        return true;
                    }
                }
            }
        }

        // Shoot strip (y < SHOOT_STRIP_H)
        if (!gallery_mode && ty < SHOOT_STRIP_H) {
            // Camera flip button (left zone)
            if (tapped && tx < SHOOT_CAM_W) {
                *do_cam_toggle = true;
                return true;
            }

            // Palette swatches (centre zone)
            if (tapped && tx >= SHOOT_PAL_ZONE_X && tx < SHOOT_PAL_ZONE_X + SHOOT_PAL_ZONE_W) {
                for (int i = 0; i < 7; i++) {
                    int sx = SHOOT_SWATCH_X0 + i * (SHOOT_SWATCH_W + SHOOT_SWATCH_GAP);
                    if (tx >= sx && tx < sx + SHOOT_SWATCH_W) {
                        p->palette = (i < 6) ? i : PALETTE_NONE;
                        return true;
                    }
                }
            }

            // Gallery button (right zone)
            if (tapped && tx >= SHOOT_GAL_X) {
                *do_gallery_toggle = true;
                return true;
            }
        }

        if (!gallery_mode) {
            if (!*shoot_mode_open) {
                // ---- Mode grid taps: open that mode's panel ----
                if (tapped && ty >= SHOOT_MODE_ROW1_Y && ty < SHOOT_MODE_ROW1_Y + SHOOT_MODE_ROW_H) {
                    for (int col = 0; col < SHOOT_MODE_COUNT; col++) {
                        int bx = SHOOT_MODE_BTN_GAP + col * (SHOOT_MODE_BTN_W + SHOOT_MODE_BTN_GAP);
                        if (tx >= bx && tx < bx + SHOOT_MODE_BTN_W) {
                            *shoot_mode = col;
                            *shoot_mode_open = true;
                            return true;
                        }
                    }
                }
            } else {
                // ---- Inside a mode panel ----

                // Back button
                if (tapped && hit(tx, ty, 4, SHOOT_BACK_Y + 2, SHOOT_BACK_W, SHOOT_BACK_H - 4)) {
                    *shoot_mode_open = false;
                    return true;
                }

                // Per-mode panel controls
                if (*shoot_mode == SHOOT_MODE_GBCAM) {
                    // 4 vertical sliders, each in an 80px wide column
                    // vtrack_top = SHOOT_CONTENT_Y + 14, vtrack_bot = SHOOT_SAVE_Y - 6
                    #define VCOL_W   80
                    #define VHANDLE_W 14
                    #define VHANDLE_H  8
                    float vtrack_top = (float)SHOOT_CONTENT_Y + 14.0f;
                    float vtrack_bot = (float)SHOOT_SAVE_Y - 6.0f;
                    float vtrack_h   = vtrack_bot - vtrack_top;
                    if (touched && ty >= (int)(vtrack_top - VHANDLE_H) && ty <= (int)(vtrack_bot + VHANDLE_H)) {
                        int col = tx / VCOL_W;
                        if (col >= 0 && col < 4) {
                            float t_val = 1.0f - (float)(ty - vtrack_top) / vtrack_h;
                            if (t_val < 0.0f) t_val = 0.0f;
                            if (t_val > 1.0f) t_val = 1.0f;
                            float mn, mx;
                            float *field = NULL;
                            if      (col == 0) { mn = ranges->bright_min;   mx = ranges->bright_max;   field = &p->brightness;  }
                            else if (col == 1) { mn = ranges->contrast_min; mx = ranges->contrast_max; field = &p->contrast;    }
                            else if (col == 2) { mn = ranges->sat_min;      mx = ranges->sat_max;      field = &p->saturation;  }
                            else               { mn = ranges->gamma_min;    mx = ranges->gamma_max;    field = &p->gamma;       }
                            *field = mn + t_val * (mx - mn);
                            return true;
                        }
                    }
                    #undef VCOL_W
                    #undef VHANDLE_W
                    #undef VHANDLE_H
                } else if (*shoot_mode == SHOOT_MODE_TIMER) {
                    // 4 timer buttons: 3s / 5s / 10s / 15s
                    static const int timer_vals[4] = { 3, 5, 10, 15 };
                    float total_btn_w = 4 * SHOOT_TIMER_BTN_W + 3 * SHOOT_TIMER_BTN_GAP;
                    float btn_start_x = (BOT_W - total_btn_w) * 0.5f;
                    float btn_y = (float)SHOOT_CONTENT_Y + 20.0f;
                    if (tapped && ty >= (int)btn_y && ty < (int)(btn_y + SHOOT_TIMER_BTN_H)) {
                        for (int i = 0; i < 4; i++) {
                            float bx = btn_start_x + i * (SHOOT_TIMER_BTN_W + SHOOT_TIMER_BTN_GAP);
                            if (tx >= (int)bx && tx < (int)(bx + SHOOT_TIMER_BTN_W)) {
                                *shoot_timer_secs = timer_vals[i];
                                return true;
                            }
                        }
                    }
                } else if (*shoot_mode == SHOOT_MODE_WIGGLE) {
                    // Frames slider (row 0) and Delay slider (row 1)
                    float row_ys[2] = {
                        (float)SHOOT_CONTENT_Y + 6.0f,
                        (float)SHOOT_CONTENT_Y + 6.0f + RHANDLE_H + 10.0f
                    };
                    for (int i = 0; i < 2; i++) {
                        float track_cy = row_ys[i] + TRACK_H * 0.5f + 1.0f;
                        if (touched && ty >= (int)(track_cy - RHANDLE_H) && ty <= (int)(track_cy + RHANDLE_H) &&
                            tx >= 64 && tx <= 72 + 210 + 8) {
                            float t_val = (float)(tx - 72) / 210.0f;
                            if (t_val < 0.0f) t_val = 0.0f;
                            if (t_val > 1.0f) t_val = 1.0f;
                            if (i == 0) {
                                *wiggle_frames = 2 + (int)(t_val * 6.0f + 0.5f);  // 2..8
                                if (*wiggle_frames < 2) *wiggle_frames = 2;
                                if (*wiggle_frames > 8) *wiggle_frames = 8;
                            } else {
                                *wiggle_delay_ms = 10 + (int)(t_val * 990.0f + 0.5f);  // 10..1000
                                if (*wiggle_delay_ms < 10)   *wiggle_delay_ms = 10;
                                if (*wiggle_delay_ms > 1000) *wiggle_delay_ms = 1000;
                            }
                            return true;
                        }
                    }
                }
            }

            // Save button (always at bottom when not in gallery)
            if (tapped && ty >= SHOOT_SAVE_Y && ty < SHOOT_SAVE_Y + SHOOT_SAVE_H) {
                *do_save = true;
                return true;
            }
        }
    }

    // -----------------------------------------------------------------------
    // STYLE tab inputs
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_STYLE && ty < NAV_Y) {

        // Palette buttons row 1 (y = STYLE_PAL_Y0 .. STYLE_PAL_Y0 + STYLE_PAL_H)
        if (tapped && ty >= STYLE_PAL_Y0 && ty < STYLE_PAL_Y0 + STYLE_PAL_H) {
            // Row 1: 4 buttons (indices 0..3) centred in 320px
            int count = 4;
            float total_w = count * STYLE_PAL_W + (count - 1) * STYLE_PAL_GAP;
            float start_x = (BOT_W - total_w) / 2.0f;
            for (int col = 0; col < count; col++) {
                float bx = start_x + col * (STYLE_PAL_W + STYLE_PAL_GAP);
                if (tx >= (int)bx && tx < (int)(bx + STYLE_PAL_W)) {
                    p->palette = col;  // indices 0..3
                    return true;
                }
            }
        }
        // Palette buttons row 2 (y = STYLE_PAL_Y1 .. STYLE_PAL_Y1 + STYLE_PAL_H)
        if (tapped && ty >= STYLE_PAL_Y1 && ty < STYLE_PAL_Y1 + STYLE_PAL_H) {
            // Row 2: 3 buttons (indices 4..6: GBA, DB, None) centred
            int count = 3;
            float total_w = count * STYLE_PAL_W + (count - 1) * STYLE_PAL_GAP;
            float start_x = (BOT_W - total_w) / 2.0f;
            for (int col = 0; col < count; col++) {
                float bx = start_x + col * (STYLE_PAL_W + STYLE_PAL_GAP);
                if (tx >= (int)bx && tx < (int)(bx + STYLE_PAL_W)) {
                    p->palette = (col < 2) ? (4 + col) : PALETTE_NONE;  // 4=GBA, 5=DB, NONE
                    return true;
                }
            }
        }

        // Pixel-size snap slider
        if (ty >= STYLE_PX_Y - 14 && ty < STYLE_PX_Y + 14 &&
            tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
            float t_val = (float)(tx - TRACK_X) / TRACK_W;
            if (t_val < 0.0f) t_val = 0.0f;
            if (t_val > 1.0f) t_val = 1.0f;
            int val = (int)(t_val * (PX_STOPS - 1) + 0.5f) + 1;
            if (val < 1) val = 1;
            if (val > PX_STOPS) val = PX_STOPS;
            p->pixel_size = val;
            return true;
        }

    }

    // -----------------------------------------------------------------------
    // FX tab inputs
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_FX && ty < NAV_Y) {
        // Mode buttons row 1
        if (tapped && ty >= FXTAB_BTN_Y1 && ty < FXTAB_BTN_Y1 + FXTAB_BTN_H) {
            for (int i = 0; i < 4; i++) {
                int bx = FXTAB_R1_X0 + i * (FXTAB_R1_W + FXTAB_R1_GAP);
                if (tx >= bx && tx < bx + FXTAB_R1_W) {
                    p->fx_mode = i;
                    return true;
                }
            }
        }
        // Mode buttons row 2
        if (tapped && ty >= FXTAB_BTN_Y2 && ty < FXTAB_BTN_Y2 + FXTAB_BTN_H) {
            for (int i = 0; i < 3; i++) {
                int bx = FXTAB_R2_X0 + i * (FXTAB_R2_W + FXTAB_R2_GAP);
                if (tx >= bx && tx < bx + FXTAB_R2_W) {
                    p->fx_mode = 4 + i;
                    return true;
                }
            }
        }
        // Intensity slider
        if (touched && p->fx_mode != FX_NONE &&
            ty >= FXTAB_SLIDER_Y - 14 && ty < FXTAB_SLIDER_Y + 14 &&
            tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
            float t_val = (float)(tx - TRACK_X) / TRACK_W;
            if (t_val < 0.0f) t_val = 0.0f;
            if (t_val > 1.0f) t_val = 1.0f;
            p->fx_intensity = (int)(t_val * 10.0f + 0.5f);
            if (p->fx_intensity < 0)  p->fx_intensity = 0;
            if (p->fx_intensity > 10) p->fx_intensity = 10;
            return true;
        }
    }

    // -----------------------------------------------------------------------
    // MORE tab inputs
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_MORE && tapped && ty < NAV_Y) {
        // Save Scale: 1x
        if (hit(tx, ty, MORE_STOG_X0, MORE_SCALE_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            *save_scale = 1;
            return true;
        }
        // Save Scale: 2x
        if (hit(tx, ty, MORE_STOG_X1, MORE_SCALE_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            *save_scale = 2;
            return true;
        }
        // Dither buttons
        for (int dm = 0; dm < 4; dm++) {
            int bx = MORE_SDITH_X0 + dm * (MORE_SDITH_W + MORE_SDITH_GAP);
            if (hit(tx, ty, bx, MORE_DITH_Y - MORE_STOG_H / 2, MORE_SDITH_W, MORE_STOG_H)) {
                p->dither_mode = dm;
                return true;
            }
        }
        // Invert Off
        if (hit(tx, ty, MORE_INV_STOG_X0, MORE_INV_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            p->invert = false;
            return true;
        }
        // Invert On
        if (hit(tx, ty, MORE_INV_STOG_X1, MORE_INV_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            p->invert = true;
            return true;
        }
        // Palette Editor button
        if (hit(tx, ty, MORE_PALED_X, MORE_POWED_Y, MORE_POWED_W, MORE_POWED_H)) {
            *active_tab = TAB_PALETTE_ED;
            return true;
        }
        // Calibrate button
        if (hit(tx, ty, MORE_CALIB_X, MORE_POWED_Y, MORE_POWED_W, MORE_POWED_H)) {
            *active_tab = TAB_CALIBRATE;
            return true;
        }
        // Save as Default
        if (hit(tx, ty, MORE_SAVEDEF_X, MORE_SAVEDEF_Y, MORE_SAVEDEF_W, MORE_SAVEDEF_H)) {
            *do_defaults_save = true;
            return true;
        }
    }

    // -----------------------------------------------------------------------
    // Palette editor tab inputs
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_PALETTE_ED) {
        if (tapped && ty < NAV_Y) {
            // Back ("< More" tap area: top-left ~60x20)
            if (hit(tx, ty, 0, 0, 60, 20)) {
                *active_tab = TAB_MORE;
                return true;
            }
            // Palette selector strip
            if (ty >= PALTAB_PALSEL_Y && ty < PALTAB_PALSEL_Y + PALTAB_PALSEL_H) {
                int i = tx / PALTAB_PALSEL_BTN_W;
                if (i >= 0 && i < PALETTE_COUNT) {
                    *palette_sel_pal   = i;
                    *palette_sel_color = 0;
                    return true;
                }
            }
            // Colour swatch strip
            if (ty >= PALTAB_SWATCH_Y && ty < PALTAB_SWATCH_Y + PALTAB_SWATCH_H) {
                int size = user_palettes[*palette_sel_pal].size;
                for (int i = 0; i < size; i++) {
                    int sx = 4 + i * (PALTAB_SWATCH_W + 4);
                    if (tx >= sx && tx < sx + PALTAB_SWATCH_W) {
                        *palette_sel_color = i;
                        return true;
                    }
                }
            }
            // Reset button
            if (hit(tx, ty, PALTAB_RESET_X, PALTAB_BTN_Y, PALTAB_RESET_W, PALTAB_BTN_H)) {
                user_palettes[*palette_sel_pal] = palettes[*palette_sel_pal];
                return true;
            }
            // Save as Default button
            if (hit(tx, ty, PALTAB_SAVE_DEF_X, PALTAB_BTN_Y, PALTAB_SAVE_DEF_W, PALTAB_BTN_H)) {
                *do_defaults_save = true;
                return true;
            }
        }
        // Drag: HS rectangle and Value strip
        if (touched && ty < NAV_Y) {
            PaletteDef *pal = &user_palettes[*palette_sel_pal];
            int ci = *palette_sel_color;
            float cur_h, cur_s, cur_v;
            if (ty >= PALTAB_HS_Y && ty < PALTAB_HS_Y + PALTAB_HS_H) {
                int cx = tx < PALTAB_HS_X ? PALTAB_HS_X :
                         (tx > PALTAB_HS_X + PALTAB_HS_W ? PALTAB_HS_X + PALTAB_HS_W : tx);
                int cy = ty < PALTAB_HS_Y ? PALTAB_HS_Y :
                         (ty > PALTAB_HS_Y + PALTAB_HS_H ? PALTAB_HS_Y + PALTAB_HS_H : ty);
                float h = (float)(cx - PALTAB_HS_X) / PALTAB_HS_W * 360.0f;
                float s = 1.0f - (float)(cy - PALTAB_HS_Y) / PALTAB_HS_H;
                if (s < 0.0f) s = 0.0f;
                if (s > 1.0f) s = 1.0f;
                rgb_to_hsv(pal->colors[ci][0], pal->colors[ci][1], pal->colors[ci][2],
                           &cur_h, &cur_s, &cur_v);
                hsv_to_rgb(h, s, cur_v,
                           &pal->colors[ci][0], &pal->colors[ci][1], &pal->colors[ci][2]);
                return true;
            }
            if (ty >= PALTAB_VAL_Y && ty < PALTAB_VAL_Y + PALTAB_VAL_H) {
                float v = (float)(tx - PALTAB_VAL_X) / PALTAB_VAL_W;
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                rgb_to_hsv(pal->colors[ci][0], pal->colors[ci][1], pal->colors[ci][2],
                           &cur_h, &cur_s, &cur_v);
                hsv_to_rgb(cur_h, cur_s, v,
                           &pal->colors[ci][0], &pal->colors[ci][1], &pal->colors[ci][2]);
                return true;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Calibrate tab inputs
    // -----------------------------------------------------------------------
    if (*active_tab == TAB_CALIBRATE && ty < NAV_Y) {
        // Back
        if (tapped && hit(tx, ty, 0, 0, 60, 20)) {
            *active_tab = TAB_MORE;
            return true;
        }
        // Range sliders (drag)
        if (touched && tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
            #define CAL_ROW(row_y, abs_mn, abs_mx, fmin, fmax, fdef) \
                if (ty >= (row_y) - 14 && ty < (row_y) + 14) { \
                    float raw = touch_x_to_val(tx, (abs_mn), (abs_mx)); \
                    float lx = slider_val_to_x((fmin), (abs_mn), (abs_mx)); \
                    float rx = slider_val_to_x((fmax), (abs_mn), (abs_mx)); \
                    float dx = slider_val_to_x((fdef), (abs_mn), (abs_mx)); \
                    float ftx = (float)tx; \
                    float dl = ftx - lx; if (dl < 0) dl = -dl; \
                    float dr = ftx - rx; if (dr < 0) dr = -dr; \
                    float dd = ftx - dx; if (dd < 0) dd = -dd; \
                    if (dl <= dr && dl <= dd) { \
                        (fmin) = raw; if ((fmin) > (fmax)) (fmin) = (fmax); \
                        if ((fdef) < (fmin)) (fdef) = (fmin); \
                    } else if (dr <= dl && dr <= dd) { \
                        (fmax) = raw; if ((fmax) < (fmin)) (fmax) = (fmin); \
                        if ((fdef) > (fmax)) (fdef) = (fmax); \
                    } else { \
                        (fdef) = raw; \
                        if ((fdef) < (fmin)) (fdef) = (fmin); \
                        if ((fdef) > (fmax)) (fdef) = (fmax); \
                    } \
                    return true; \
                }

            CAL_ROW(ROW_BRIGHT,   CAL_BRIGHT_ABS_MIN,   CAL_BRIGHT_ABS_MAX,   ranges->bright_min,   ranges->bright_max,   ranges->bright_def)
            CAL_ROW(ROW_CONTRAST, CAL_CONTRAST_ABS_MIN, CAL_CONTRAST_ABS_MAX, ranges->contrast_min, ranges->contrast_max, ranges->contrast_def)
            CAL_ROW(ROW_SAT,      CAL_SAT_ABS_MIN,      CAL_SAT_ABS_MAX,      ranges->sat_min,      ranges->sat_max,      ranges->sat_def)
            CAL_ROW(ROW_GAMMA,    CAL_GAMMA_ABS_MIN,    CAL_GAMMA_ABS_MAX,    ranges->gamma_min,    ranges->gamma_max,    ranges->gamma_def)
            #undef CAL_ROW
        }
        // Save as Default
        if (tapped && hit(tx, ty, CAL_SAVEDEF_X, CAL_SAVEDEF_Y, CAL_SAVEDEF_W, CAL_SAVEDEF_H)) {
            *do_defaults_save = true;
            return true;
        }
    }

    (void)default_params;
    return false;
}
