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
                  bool gallery_mode, int gallery_count, int *gallery_sel, int *gallery_scroll) {
    *do_cam_toggle    = false;
    *do_save          = false;
    *do_defaults_save = false;
    *do_gallery_toggle = false;

    bool touched = (kHeld & KEY_TOUCH) != 0;
    bool tapped  = (kDown & KEY_TOUCH) != 0;
    if (!touched) return false;

    int tx = touch.px, ty = touch.py;

    // --- Tab bar (always active, checked first) ---
    if (tapped && ty < TAB_BAR_H) {
        if (hit(tx, ty, TAB_0_X, TAB_BAR_Y, TAB_0_W, TAB_BAR_H)) {
            if (*active_tab == 0) {
                // Camera button toggles gallery when already on camera tab
                *do_gallery_toggle = true;
            } else {
                *active_tab = 0;
            }
            return true;
        }
        if (hit(tx, ty, TAB_1_X, TAB_BAR_Y, TAB_1_W, TAB_BAR_H)) {
            *active_tab = 1;
            return true;
        }
        // Context-sensitive slots 3 & 4
        if (*active_tab == 0) {
            // Camera context: CAM toggle + Save
            if (hit(tx, ty, BTN_CAM_X, BTN_CAM_Y, BTN_CAM_W, BTN_CAM_H)) {
                *do_cam_toggle = true;
                return true;
            }
            if (hit(tx, ty, BTN_SAVE_X, BTN_SAVE_Y, BTN_SAVE_W, BTN_SAVE_H)) {
                *do_save = true;
                return true;
            }
        } else {
            // Settings context: Calibrate tab (slot 3) + Palette tab (slot 4)
            if (hit(tx, ty, BTN_CAM_X, BTN_CAM_Y, BTN_CAM_W, BTN_CAM_H)) {
                *active_tab = (*active_tab == 3) ? 1 : 3;
                return true;
            }
            if (hit(tx, ty, BTN_SAVE_X, BTN_SAVE_Y, BTN_SAVE_W, BTN_SAVE_H)) {
                *active_tab = (*active_tab == 2) ? 1 : 2;
                return true;
            }
        }
    }

    // --- Gallery grid inputs (when gallery is open) ---
    if (gallery_mode && *active_tab == 0 && tapped) {
        int total_rows = (gallery_count + GALLERY_COLS - 1) / GALLERY_COLS;
        int max_scroll = total_rows - GALLERY_ROWS;
        if (max_scroll < 0) max_scroll = 0;

        // Scroll buttons
        if (hit(tx, ty, BTN_GSCROLL_X, BTN_GSCROLL_UP_Y, BTN_GSCROLL_W, BTN_GSCROLL_H)) {
            if (*gallery_scroll > 0) (*gallery_scroll)--;
            return true;
        }
        if (hit(tx, ty, BTN_GSCROLL_X, BTN_GSCROLL_DN_Y, BTN_GSCROLL_W, BTN_GSCROLL_H)) {
            if (*gallery_scroll < max_scroll) (*gallery_scroll)++;
            return true;
        }

        // Grid cell taps
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

    // --- Camera tab inputs ---
    if (*active_tab == 0) {

        // Palette buttons (tap only, only when not in gallery mode)
        if (!gallery_mode && tapped && ty >= PAL_BTN_Y && ty < PAL_BTN_Y + PAL_BTN_H) {
            for (int i = 0; i < 7; i++) {
                int bx = PAL_BTN_X0 + i * (PAL_BTN_W + 2);
                if (tx >= bx && tx < bx + PAL_BTN_W) {
                    p->palette = (i < 6) ? i : PALETTE_NONE;
                    return true;
                }
            }
        }

        // Pixel-size slider (drag, snaps to integer 1-8)
        if (ty >= ROW_PXSIZE - 14 && ty < ROW_PXSIZE + 14 &&
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

        // Continuous sliders (drag) — bounds from calibrated ranges
        if (tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
            if (ty >= ROW_BRIGHT - 13 && ty < ROW_BRIGHT + 13) {
                p->brightness = touch_x_to_val(tx, ranges->bright_min, ranges->bright_max);
                return true;
            }
            if (ty >= ROW_CONTRAST - 13 && ty < ROW_CONTRAST + 13) {
                p->contrast = touch_x_to_val(tx, ranges->contrast_min, ranges->contrast_max);
                return true;
            }
            if (ty >= ROW_SAT - 13 && ty < ROW_SAT + 13) {
                p->saturation = touch_x_to_val(tx, ranges->sat_min, ranges->sat_max);
                return true;
            }
            if (ty >= ROW_GAMMA - 13 && ty < ROW_GAMMA + 13) {
                p->gamma = touch_x_to_val(tx, ranges->gamma_min, ranges->gamma_max);
                return true;
            }
        }
    }

    // --- Settings tab inputs (tap only) ---
    if (*active_tab == 1 && tapped) {
        // Save Scale toggles
        if (hit(tx, ty, STOG_X0, SROW_SAVE_SCALE - STOG_H/2, STOG_W, STOG_H)) {
            *save_scale = 1;
            return true;
        }
        if (hit(tx, ty, STOG_X1, SROW_SAVE_SCALE - STOG_H/2, STOG_W, STOG_H)) {
            *save_scale = 2;
            return true;
        }

        // Dither mode buttons (4 narrow buttons)
        for (int dm = 0; dm < 4; dm++) {
            if (hit(tx, ty, SDITH_X0 + dm * (SDITH_W + SDITH_GAP), SROW_DITHER - STOG_H/2, SDITH_W, STOG_H)) {
                p->dither_mode = dm;
                return true;
            }
        }

        // Invert toggles
        if (hit(tx, ty, STOG_X0, SROW_INVERT - STOG_H/2, STOG_W, STOG_H)) {
            p->invert = false;
            return true;
        }
        if (hit(tx, ty, STOG_X1, SROW_INVERT - STOG_H/2, STOG_W, STOG_H)) {
            p->invert = true;
            return true;
        }

        // Save as Default — set flag so caller drives the flash and actual save
        if (hit(tx, ty, SWBTN_X, SROW_SAVE_DEF - SWBTN_H/2, SWBTN_W, SWBTN_H)) {
            *do_defaults_save = true;
            return true;
        }
    }

    // --- Calibrate tab inputs ---
    if (*active_tab == 3 && touched && tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
        // Helper: pick closest of three handles (min, def, max) and update it
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

        CAL_ROW(ROW_BRIGHT,   0.0f, 4.0f, ranges->bright_min,   ranges->bright_max,   ranges->bright_def)
        CAL_ROW(ROW_CONTRAST, 0.1f, 4.0f, ranges->contrast_min, ranges->contrast_max, ranges->contrast_def)
        CAL_ROW(ROW_SAT,      0.0f, 4.0f, ranges->sat_min,      ranges->sat_max,      ranges->sat_def)
        CAL_ROW(ROW_GAMMA,    0.1f, 4.0f, ranges->gamma_min,    ranges->gamma_max,    ranges->gamma_def)
        #undef CAL_ROW
    }
    // Save as Default button in Calibrate tab
    if (*active_tab == 3 && tapped) {
        if (hit(tx, ty, SWBTN_X, SROW_SAVE_DEF - SWBTN_H/2, SWBTN_W, SWBTN_H)) {
            *do_defaults_save = true;
            return true;
        }
    }

    // --- Palette tab inputs ---
    if (*active_tab == 2) {
        // Tap-only: palette selector, swatch selection, reset button
        if (tapped) {
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
            if (hit(tx, ty, PALTAB_RESET_X, PALTAB_BTN_Y,
                    PALTAB_RESET_W, PALTAB_BTN_H)) {
                user_palettes[*palette_sel_pal] = palettes[*palette_sel_pal];
                return true;
            }
            // Save as Default button
            if (hit(tx, ty, PALTAB_SAVE_DEF_X, PALTAB_BTN_Y,
                    PALTAB_SAVE_DEF_W, PALTAB_BTN_H)) {
                *do_defaults_save = true;
                return true;
            }
        }
        // Drag: HS rectangle and Value strip
        if (touched) {
            PaletteDef *pal = &user_palettes[*palette_sel_pal];
            int ci = *palette_sel_color;
            float cur_h, cur_s, cur_v;
            // HS rectangle
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
            // Value strip
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

    return false;
}
