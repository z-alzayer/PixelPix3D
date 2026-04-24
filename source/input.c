#include "input.h"
#include "lomo.h"
#include "bend.h"
#include "settings.h"

bool hit(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static bool handle_fx_tab_touch(int tx, int ty, bool tapped, bool touched,
                                FilterParams *p) {
    if (tapped && ty >= FXTAB_BTN_Y1 && ty < FXTAB_BTN_Y1 + FXTAB_BTN_H) {
        for (int i = 0; i < 4; i++) {
            int bx = FXTAB_R1_X0 + i * (FXTAB_R1_W + FXTAB_R1_GAP);
            if (tx >= bx && tx < bx + FXTAB_R1_W) {
                p->fx_mode = i;
                return true;
            }
        }
    }
    if (tapped && ty >= FXTAB_BTN_Y2 && ty < FXTAB_BTN_Y2 + FXTAB_BTN_H) {
        for (int i = 0; i < 3; i++) {
            int bx = FXTAB_R2_X0 + i * (FXTAB_R2_W + FXTAB_R2_GAP);
            if (tx >= bx && tx < bx + FXTAB_R2_W) {
                p->fx_mode = 4 + i;
                return true;
            }
        }
    }
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
    return false;
}

static bool handle_fx_compact_touch(int tx, int ty, bool tapped, bool touched,
                                    FilterParams *p, float cy) {
    const int fx_btn_w = 100;
    const int fx_btn_h = 22;
    const int fx_btn_gap_x = 6;
    const int fx_btn_gap_y = 6;
    const int fx_grid_x = 4;
    const int fx_row2_y = (int)cy + fx_btn_h + fx_btn_gap_y;
    static const int fx_modes[6] = {
        FX_SCAN_H, FX_SCAN_V, FX_LCD,
        FX_VIGNETTE, FX_CHROMA, FX_GRAIN
    };

    if (tapped) {
        for (int i = 0; i < 6; i++) {
            int row = i / 3;
            int col = i % 3;
            int bx = fx_grid_x + col * (fx_btn_w + fx_btn_gap_x);
            int by = (row == 0) ? (int)cy : fx_row2_y;
            if (hit(tx, ty, bx, by, fx_btn_w, fx_btn_h)) {
                if (p->fx_mode == fx_modes[i]) p->fx_mode = FX_NONE;
                else p->fx_mode = fx_modes[i];
                return true;
            }
        }
    }
    if (touched && p->fx_mode != FX_NONE &&
        ty >= (int)(cy + 84.0f - 14.0f) && ty < (int)(cy + 84.0f + 14.0f) &&
        tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
        float t_val = (float)(tx - TRACK_X) / TRACK_W;
        if (t_val < 0.0f) t_val = 0.0f;
        if (t_val > 1.0f) t_val = 1.0f;
        p->fx_intensity = (int)(t_val * 10.0f + 0.5f);
        if (p->fx_intensity < 0)  p->fx_intensity = 0;
        if (p->fx_intensity > 10) p->fx_intensity = 10;
        return true;
    }
    return false;
}

bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  AppState *app, ShootState *shoot, WiggleState *wig,
                  GalleryState *gal, EditState *edit,
                  bool *do_cam_toggle, bool *do_save, bool *do_defaults_save,
                  bool *do_defaults_reset,
                  bool *do_gallery_toggle,
                  bool *do_edit_cancel, bool *do_edit_savenew,
                  bool *do_edit_overwrite, bool *do_edit_enter) {
    *do_cam_toggle    = false;
    *do_save          = false;
    *do_defaults_save = false;
    *do_defaults_reset = false;
    *do_gallery_toggle = false;
    *do_edit_cancel   = false;
    *do_edit_savenew  = false;
    *do_edit_overwrite = false;
    *do_edit_enter    = false;

    bool touched = (kHeld & KEY_TOUCH) != 0;
    bool tapped  = (kDown & KEY_TOUCH) != 0;
    if (!touched) return false;

    int tx = touch.px, ty = touch.py;

    FilterParams *p = &app->params;

    // -----------------------------------------------------------------------
    // Gallery edit mode — intercepts ALL touch when active (must be first)
    // -----------------------------------------------------------------------
    if (edit->active) {
        if (!tapped) return false;

        // Action bar (bottom strip)
        if (ty >= GEDIT_ACT_Y) {
            if (hit(tx, ty, GEDIT_BTN_CANCEL_X,  GEDIT_ACT_Y, GEDIT_BTN_CANCEL_W,  GEDIT_ACT_H)) {
                *do_edit_cancel = true; return true;
            }
            if (hit(tx, ty, GEDIT_BTN_SAVENEW_X, GEDIT_ACT_Y, GEDIT_BTN_SAVENEW_W, GEDIT_ACT_H)) {
                *do_edit_savenew = true; return true;
            }
            if (hit(tx, ty, GEDIT_BTN_OVERW_X,   GEDIT_ACT_Y, GEDIT_BTN_OVERW_W,   GEDIT_ACT_H)) {
                *do_edit_overwrite = true; return true;
            }
            return false;
        }

        // Tab bar
        if (ty < GEDIT_TAB_H) {
            edit->tab = (tx < GEDIT_TAB_W) ? 0 : 1;
            return true;
        }

        // Category strip (sticker tab only): y = GEDIT_TAB_H+2 .. GEDIT_TAB_H+20, x=0..159
        #define CAT_STRIP_Y0  (GEDIT_TAB_H + 2)
        #define CAT_STRIP_H   18
        #define CAT_BTN_W_I   ((160 - 4) / STICKER_CAT_COUNT)
        if (edit->tab == 0 && tx < 160 &&
            ty >= CAT_STRIP_Y0 && ty < CAT_STRIP_Y0 + CAT_STRIP_H) {
            int ci = (tx - 2) / CAT_BTN_W_I;
            if (ci < 0) ci = 0;
            if (ci >= STICKER_CAT_COUNT) ci = STICKER_CAT_COUNT - 1;
            if (ci != edit->sticker_cat) {
                edit->sticker_cat   = ci;
                edit->sticker_sel   = 0;
                edit->sticker_scroll = 0;
                sticker_cat_load(ci);
            }
            return true;
        }
        #undef CAT_STRIP_Y0
        #undef CAT_STRIP_H
        #undef CAT_BTN_W_I

        // Info area tap (y = GEDIT_PICKER_BOT..GEDIT_ACT_Y) → "Place" selected sticker
        if (edit->tab == 0 && ty >= GEDIT_PICKER_BOT && ty < GEDIT_ACT_Y) {
            *do_edit_enter = true;
            return true;
        }

        // Right panel scroll buttons (x>=160, sticker tab only)
        #define RSCROLL_UP_Y0   116
        #define RSCROLL_DN_Y0   141
        #define RSCROLL_BTN_H    22
        if (edit->tab == 0 && tx >= 160) {
            sticker_cat_load(edit->sticker_cat);
            int _rc = sticker_cats[edit->sticker_cat].count;
            int _tr = (_rc + GEDIT_STICKER_COLS - 1) / GEDIT_STICKER_COLS;
            int _ms = _tr - GEDIT_STICKER_ROWS;
            if (_ms < 0) _ms = 0;
            if (ty >= RSCROLL_UP_Y0 && ty < RSCROLL_UP_Y0 + RSCROLL_BTN_H) {
                if (edit->sticker_scroll > 0) edit->sticker_scroll--;
                return true;
            }
            if (ty >= RSCROLL_DN_Y0 && ty < RSCROLL_DN_Y0 + RSCROLL_BTN_H) {
                if (edit->sticker_scroll < _ms) edit->sticker_scroll++;
                return true;
            }
        }
        #undef RSCROLL_UP_Y0
        #undef RSCROLL_DN_Y0
        #undef RSCROLL_BTN_H

        // Sticker grid base y (after category strip)
        #define SGRID_BASE_Y  (GEDIT_TAB_H + 2 + 18 + 2)   // 50

        // Picker area — left panel (x=0..159)
        if (ty >= SGRID_BASE_Y && ty < GEDIT_PICKER_BOT && tx < 160) {
            if (edit->tab == 0) {
                // Sticker grid
                #define SGRID_X0    2
                #define SGRID_ROW_H GEDIT_STICKER_ROW_H
                sticker_cat_load(edit->sticker_cat);
                int cat_count  = sticker_cats[edit->sticker_cat].count;

                int col = (tx - SGRID_X0) / (GEDIT_STICKER_CELL + GEDIT_STICKER_GAP);
                int row = (ty - SGRID_BASE_Y) / SGRID_ROW_H;
                if (col >= 0 && col < GEDIT_STICKER_COLS && row >= 0 && row < GEDIT_STICKER_ROWS) {
                    int idx = (edit->sticker_scroll * GEDIT_STICKER_COLS) + row * GEDIT_STICKER_COLS + col;
                    if (idx >= 0 && idx < cat_count) {
                        edit->sticker_sel = idx;
                        return true;
                    }
                }
                #undef SGRID_X0
                #undef SGRID_ROW_H
                #undef SGRID_BASE_Y
            } else {
                // Frame picker
                for (int i = 0; i < FRAME_COUNT; i++) {
                    int fy = GEDIT_PICKER_Y + i * FRAME_ROW_H;
                    if (ty >= fy && ty < fy + FRAME_PILL_H && tx < 160) {
                        edit->gallery_frame = i;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Bottom nav bar (y >= NAV_Y, always active)
    // -----------------------------------------------------------------------
    if (tapped && ty >= NAV_Y) {
        if (edit->active)  { *do_edit_cancel = true; return true; }
        if (gal->mode)     { *do_gallery_toggle = true; return true; }

        int seg = tx / NAV_SEG_W;
        if (seg < 0) seg = 0;
        if (seg > 3) seg = 3;

        if (seg == TAB_SHOOT) {
            app->active_tab = TAB_SHOOT;
        } else if (seg == TAB_STYLE) {
            app->active_tab = TAB_STYLE;
        } else if (seg == TAB_FX) {
            app->active_tab = TAB_FX;
        } else if (seg == TAB_MORE) {
            app->active_tab = (app->active_tab == TAB_MORE) ? TAB_SHOOT : TAB_MORE;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Gallery mode — full-screen context (intercepts all touch when active)
    // -----------------------------------------------------------------------
    if (gal->mode && !edit->active && tapped && ty < NAV_Y) {
        #define GAL_GRID_Y0   32
        #define GAL_GRID_COLS  4
        #define GAL_GRID_ROWS  4
        #define GAL_CELL_W    71
        #define GAL_CELL_H    40
        #define GAL_CELL_GAP   3
        #define GAL_ROW_H    (GAL_CELL_H + GAL_CELL_GAP)
        #define GAL_SCROLL_X 302

        // Close button (header, left)
        if (hit(tx, ty, 4, 3, 50, 24)) {
            *do_gallery_toggle = true;
            return true;
        }
        // Edit button (header, right)
        if (gal->count > 0 && hit(tx, ty, BOT_W - 54, 3, 50, 24)) {
            *do_edit_enter = true;
            return true;
        }
        // Scroll arrows
        {
            int total_rows = (gal->count + GAL_GRID_COLS - 1) / GAL_GRID_COLS;
            int max_scroll = total_rows - GAL_GRID_ROWS;
            if (max_scroll < 0) max_scroll = 0;
            if (hit(tx, ty, GAL_SCROLL_X, GAL_GRID_Y0, 14, 78)) {
                if (gal->scroll > 0) gal->scroll--;
                return true;
            }
            if (hit(tx, ty, GAL_SCROLL_X, GAL_GRID_Y0 + 82, 14, 78)) {
                if (gal->scroll < max_scroll) gal->scroll++;
                return true;
            }
        }
        // Thumbnail grid taps
        if (ty >= GAL_GRID_Y0 && tx < GAL_SCROLL_X) {
            int col = (tx - GAL_CELL_GAP) / (GAL_CELL_W + GAL_CELL_GAP);
            int row = (ty - GAL_GRID_Y0)  / GAL_ROW_H;
            if (col >= 0 && col < GAL_GRID_COLS && row >= 0 && row < GAL_GRID_ROWS) {
                int idx = gal->scroll * GAL_GRID_COLS + row * GAL_GRID_COLS + col;
                if (idx >= 0 && idx < gal->count) {
                    gal->sel = idx;
                    return true;
                }
            }
        }
        #undef GAL_GRID_Y0
        #undef GAL_GRID_COLS
        #undef GAL_GRID_ROWS
        #undef GAL_CELL_W
        #undef GAL_CELL_H
        #undef GAL_CELL_GAP
        #undef GAL_ROW_H
        #undef GAL_SCROLL_X
        return false;
    }

    // -----------------------------------------------------------------------
    // SHOOT tab inputs (content area, y < NAV_Y) — only when NOT in gallery
    // -----------------------------------------------------------------------
    if (app->active_tab == TAB_SHOOT && !gal->mode && ty < NAV_Y) {
        if (wig->preview) {
            // Wiggle preview uses its own touch handler so it still responds
            // while camera capture is interrupted.
            return false;
        }

        // Shoot strip (y < SHOOT_STRIP_H)
        if (ty < SHOOT_STRIP_H) {
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
                        int new_pal = (i < 6) ? i : PALETTE_NONE;
                        if (shoot->capture_mode == CAPTURE_MODE_WIGGLE) {
                            if (wig->filter_active && p->palette == new_pal)
                                wig->filter_active = false;
                            else {
                                p->palette = new_pal;
                                wig->filter_active = true;
                            }
                            wig->rebuild = true;
                        } else {
                            shoot->gb_enabled = true;
                            p->palette = new_pal;
                        }
                        return true;
                    }
                }
            }

            // Gallery button opens gallery context
            if (tapped && tx >= SHOOT_GAL_X) {
                *do_gallery_toggle = true;
                return true;
            }
        }

        {
            if (!shoot->shoot_mode_open && !shoot->timer_open) {
                // ---- Quick-access row taps ----
                if (tapped && ty >= SHOOT_MODE_ROW1_Y && ty < SHOOT_MODE_ROW1_Y + SHOOT_MODE_ROW_H) {
                    for (int col = 0; col < SHOOT_STAGE_BTN_COUNT; col++) {
                        int bx = SHOOT_MODE_BTN_GAP + col * (SHOOT_MODE_BTN_W + SHOOT_MODE_BTN_GAP);
                        if (tx >= bx && tx < bx + SHOOT_MODE_BTN_W) {
                            if (col == 0) {
                                shoot->capture_mode = CAPTURE_MODE_STILL;
                                shoot->shoot_mode = SHOOT_MODE_GBCAM;
                            } else if (col == 1) {
                                shoot->capture_mode = CAPTURE_MODE_WIGGLE;
                                shoot->shoot_mode = SHOOT_MODE_WIGGLE;
                            } else if (col == 2) {
                                shoot->shoot_mode = SHOOT_MODE_GBCAM;
                            } else if (col == 3) {
                                shoot->shoot_mode = SHOOT_MODE_LOMO;
                            } else if (col == 4) {
                                shoot->shoot_mode = SHOOT_MODE_BEND;
                            } else if (col == 5) {
                                shoot->shoot_mode = SHOOT_MODE_FX;
                            }
                            shoot->shoot_mode_open = true;
                            return true;
                        }
                    }
                }
                if (tapped && hit(tx, ty,
                                  (BOT_W - SHOOT_TIMER_PILL_W) / 2,
                                  SHOOT_TIMER_ROW_Y,
                                  SHOOT_TIMER_PILL_W,
                                  SHOOT_TIMER_PILL_H)) {
                    shoot->timer_open = true;
                    return true;
                }
            } else if (shoot->timer_open) {
                // ---- Timer settings panel ----
                if (tapped && hit(tx, ty, 4, SHOOT_BACK_Y + 2, SHOOT_BACK_W, SHOOT_BACK_H - 4)) {
                    shoot->timer_open = false;
                    return true;
                }
                static const int timer_vals[SHOOT_TIMER_VAL_COUNT] = SHOOT_TIMER_VALS_INIT;
                float total_btn_w = SHOOT_TIMER_VAL_COUNT * SHOOT_TIMER_BTN_W + (SHOOT_TIMER_VAL_COUNT - 1) * SHOOT_TIMER_BTN_GAP;
                float btn_start_x = (BOT_W - total_btn_w) * 0.5f;
                float btn_y = (float)SHOOT_CONTENT_Y + 20.0f;
                if (tapped && ty >= (int)btn_y && ty < (int)(btn_y + SHOOT_TIMER_BTN_H)) {
                    for (int i = 0; i < SHOOT_TIMER_VAL_COUNT; i++) {
                        float bx = btn_start_x + i * (SHOOT_TIMER_BTN_W + SHOOT_TIMER_BTN_GAP);
                        if (tx >= (int)bx && tx < (int)(bx + SHOOT_TIMER_BTN_W)) {
                            shoot->shoot_timer_secs = timer_vals[i];
                            return true;
                        }
                    }
                }
            } else {
                // ---- Capture mode panel ----

                // Back button
                if (tapped && hit(tx, ty, 4, SHOOT_BACK_Y + 2, SHOOT_BACK_W, SHOOT_BACK_H - 4)) {
                    shoot->shoot_mode_open = false;
                    wig->preview  = false;
                    return true;
                }

                if (shoot->shoot_mode == SHOOT_MODE_GBCAM) {
                    if (tapped && hit(tx, ty,
                                      SHOOT_GB_TOGGLE_X, SHOOT_GB_TOGGLE_Y,
                                      SHOOT_GB_TOGGLE_W, SHOOT_GB_TOGGLE_H)) {
                        if (shoot->capture_mode == CAPTURE_MODE_WIGGLE) {
                            wig->filter_active = !wig->filter_active;
                            wig->rebuild = true;
                        } else {
                            shoot->gb_enabled = !shoot->gb_enabled;
                        }
                        return true;
                    }

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
                            if      (col == 0) { mn = app->ranges.bright_min;   mx = app->ranges.bright_max;   field = &p->brightness;  }
                            else if (col == 1) { mn = app->ranges.contrast_min; mx = app->ranges.contrast_max; field = &p->contrast;    }
                            else if (col == 2) { mn = app->ranges.sat_min;      mx = app->ranges.sat_max;      field = &p->saturation;  }
                            else               { mn = app->ranges.gamma_min;    mx = app->ranges.gamma_max;    field = &p->gamma;       }
                            if (shoot->capture_mode == CAPTURE_MODE_WIGGLE) {
                                wig->filter_active = true;
                                wig->rebuild = true;
                            } else {
                                shoot->gb_enabled = true;
                            }
                            *field = mn + t_val * (mx - mn);
                            return true;
                        }
                    }
                    #undef VCOL_W
                    #undef VHANDLE_W
                    #undef VHANDLE_H
                } else if (shoot->shoot_mode == SHOOT_MODE_LOMO) {
                    // 3×2 preset grid
                    float cy = (float)SHOOT_CONTENT_Y;
                    if (tapped) {
                        for (int row = 0; row < LOMO_GRID_ROWS; row++) {
                            for (int col = 0; col < LOMO_GRID_COLS; col++) {
                                int idx = row * LOMO_GRID_COLS + col;
                                if (idx >= LOMO_PRESET_COUNT) break;
                                int bx = LOMO_GRID_GAP + col * (LOMO_GRID_BTN_W + LOMO_GRID_GAP);
                                int by = (int)(cy + row * (LOMO_GRID_BTN_H + LOMO_GRID_GAP));
                                if (hit(tx, ty, bx, by, LOMO_GRID_BTN_W, LOMO_GRID_BTN_H)) {
                                    if (shoot->lomo_enabled && shoot->lomo_preset == idx)
                                        shoot->lomo_enabled = false;
                                    else {
                                        shoot->lomo_preset = idx;
                                        shoot->lomo_enabled = true;
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                } else if (shoot->shoot_mode == SHOOT_MODE_BEND) {
                    // 3×2 circuit-bend preset grid
                    float cy = (float)SHOOT_CONTENT_Y;
                    if (tapped) {
                        for (int row = 0; row < BEND_GRID_ROWS; row++) {
                            for (int col = 0; col < BEND_GRID_COLS; col++) {
                                int idx = row * BEND_GRID_COLS + col;
                                if (idx >= BEND_PRESET_COUNT) break;
                                int bx = BEND_GRID_GAP + col * (BEND_GRID_BTN_W + BEND_GRID_GAP);
                                int by = (int)(cy + row * (BEND_GRID_BTN_H + BEND_GRID_GAP));
                                if (hit(tx, ty, bx, by, BEND_GRID_BTN_W, BEND_GRID_BTN_H)) {
                                    if (shoot->bend_enabled && shoot->bend_preset == idx)
                                        shoot->bend_enabled = false;
                                    else {
                                        shoot->bend_preset = idx;
                                        shoot->bend_enabled = true;
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                } else if (shoot->shoot_mode == SHOOT_MODE_FX) {
                    float cy = (float)SHOOT_CONTENT_Y;
                    if (handle_fx_compact_touch(tx, ty, tapped, touched, p, cy))
                        return true;
                } else if (shoot->shoot_mode == SHOOT_MODE_WIGGLE) {
                    #define WIG_BTN_W  28
                    #define WIG_BTN_H  22
                    #define WIG_VAL_W  36
                    #define WIG_RST_W  22
                    #define WIG_MINUS_X  18
                    #define WIG_VAL_X    (WIG_MINUS_X + WIG_BTN_W + 2)
                    #define WIG_PLUS_X   (WIG_VAL_X + WIG_VAL_W + 2)
                    #define WIG_RST_X    (WIG_PLUS_X + WIG_BTN_W + 2)

                    // X/Y offset buttons handled in wigglegram.c (outside captureInterrupted)

                    // Delay: preset pills + stepper (right zone x=160..319)
                    if (tapped && tx >= 160) {
                        // Preset pills row
                        float py0 = (float)SHOOT_CONTENT_Y + 20.0f;
                        #define DPILL_W   32
                        #define DPILL_H   16
                        #define DPILL_GAP  3
                        static const int presets[4] = {50, 100, 200, 500};
                        float total_pw = 4 * DPILL_W + 3 * DPILL_GAP;
                        float px0 = 160.0f + (160.0f - total_pw) * 0.5f;
                        if (ty >= (int)py0 && ty < (int)(py0 + DPILL_H)) {
                            for (int i = 0; i < 4; i++) {
                                float bx = px0 + i * (DPILL_W + DPILL_GAP);
                                if (tx >= (int)bx && tx < (int)(bx + DPILL_W)) {
                                    wig->delay_ms = presets[i];
                                    return true;
                                }
                            }
                        }
                        #undef DPILL_W
                        #undef DPILL_H
                        #undef DPILL_GAP

                        // Stepper row
                        float sy = (float)SHOOT_CONTENT_Y + 44.0f;
                        #define DSTEP_BTN_W  22
                        #define DSTEP_BTN_H  18
                        #define DSTEP_VAL_W  54
                        float total_sw = 2 * DSTEP_BTN_W + DSTEP_VAL_W + 4;
                        float sx0 = 160.0f + (160.0f - total_sw) * 0.5f;
                        if (ty >= (int)sy && ty < (int)(sy + DSTEP_BTN_H)) {
                            if (tx >= (int)sx0 && tx < (int)(sx0 + DSTEP_BTN_W)) {
                                wig->delay_ms -= 10;
                                if (wig->delay_ms < 10) wig->delay_ms = 10;
                                return true;
                            }
                            float px_btn = sx0 + DSTEP_BTN_W + 2 + DSTEP_VAL_W + 2;
                            if (tx >= (int)px_btn && tx < (int)(px_btn + DSTEP_BTN_W)) {
                                wig->delay_ms += 10;
                                if (wig->delay_ms > 1000) wig->delay_ms = 1000;
                                return true;
                            }
                        }
                        #undef DSTEP_BTN_W
                        #undef DSTEP_BTN_H
                        #undef DSTEP_VAL_W
                    }

                    #undef WIG_BTN_W
                    #undef WIG_BTN_H
                    #undef WIG_VAL_W
                    #undef WIG_RST_W
                    #undef WIG_MINUS_X
                    #undef WIG_VAL_X
                    #undef WIG_PLUS_X
                    #undef WIG_RST_X
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
    if (app->active_tab == TAB_STYLE && ty < NAV_Y) {

        // Palette buttons row 1
        if (tapped && ty >= STYLE_PAL_Y0 && ty < STYLE_PAL_Y0 + STYLE_PAL_H) {
            int count = 4;
            float total_w = count * STYLE_PAL_W + (count - 1) * STYLE_PAL_GAP;
            float start_x = (BOT_W - total_w) / 2.0f;
            for (int col = 0; col < count; col++) {
                float bx = start_x + col * (STYLE_PAL_W + STYLE_PAL_GAP);
                if (tx >= (int)bx && tx < (int)(bx + STYLE_PAL_W)) {
                    p->palette = col;
                    return true;
                }
            }
        }
        // Palette buttons row 2
        if (tapped && ty >= STYLE_PAL_Y1 && ty < STYLE_PAL_Y1 + STYLE_PAL_H) {
            int count = 3;
            float total_w = count * STYLE_PAL_W + (count - 1) * STYLE_PAL_GAP;
            float start_x = (BOT_W - total_w) / 2.0f;
            for (int col = 0; col < count; col++) {
                float bx = start_x + col * (STYLE_PAL_W + STYLE_PAL_GAP);
                if (tx >= (int)bx && tx < (int)(bx + STYLE_PAL_W)) {
                    p->palette = (col < 2) ? (4 + col) : PALETTE_NONE;
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
    if (app->active_tab == TAB_FX && ty < NAV_Y) {
        if (handle_fx_tab_touch(tx, ty, tapped, touched, p))
            return true;
    }

    // -----------------------------------------------------------------------
    // MORE tab inputs
    // -----------------------------------------------------------------------
    if (app->active_tab == TAB_MORE && tapped && ty < NAV_Y) {
        // Save Scale: 1x / 2x / 3x / 4x
        for (int sc = 0; sc < 4; sc++) {
            int bx = MORE_STOG_X0 + sc * (MORE_SCALE_BTN_W + MORE_SCALE_BTN_GAP);
            if (hit(tx, ty, bx, MORE_SCALE_Y - MORE_STOG_H / 2, MORE_SCALE_BTN_W, MORE_STOG_H)) {
                app->save_scale = sc + 1;
                return true;
            }
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
        // Shutter button: A
        if (hit(tx, ty, MORE_SHUT_STOG_X0, MORE_SHUT_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            app->shutter_button = 0;
            return true;
        }
        // Shutter button: L/R
        if (hit(tx, ty, MORE_SHUT_STOG_X1, MORE_SHUT_Y - MORE_STOG_H / 2, MORE_STOG_W, MORE_STOG_H)) {
            app->shutter_button = 1;
            return true;
        }
        // Palette Editor button
        if (hit(tx, ty, MORE_PALED_X, MORE_POWED_Y, MORE_POWED_W, MORE_POWED_H)) {
            app->active_tab = TAB_PALETTE_ED;
            return true;
        }
        // Calibrate button
        if (hit(tx, ty, MORE_CALIB_X, MORE_POWED_Y, MORE_POWED_W, MORE_POWED_H)) {
            app->active_tab = TAB_CALIBRATE;
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
    if (app->active_tab == TAB_PALETTE_ED) {
        if (tapped && ty < NAV_Y) {
            // Back
            if (hit(tx, ty, 0, 0, 60, 20)) {
                app->active_tab = TAB_MORE;
                return true;
            }
            // Palette selector strip
            if (ty >= PALTAB_PALSEL_Y && ty < PALTAB_PALSEL_Y + PALTAB_PALSEL_H) {
                int i = tx / PALTAB_PALSEL_BTN_W;
                if (i >= 0 && i < PALETTE_COUNT) {
                    app->palette_sel_pal   = i;
                    app->palette_sel_color = 0;
                    return true;
                }
            }
            // Colour swatch strip
            if (ty >= PALTAB_SWATCH_Y && ty < PALTAB_SWATCH_Y + PALTAB_SWATCH_H) {
                int size = app->user_palettes[app->palette_sel_pal].size;
                for (int i = 0; i < size; i++) {
                    int sx = 4 + i * (PALTAB_SWATCH_W + 4);
                    if (tx >= sx && tx < sx + PALTAB_SWATCH_W) {
                        app->palette_sel_color = i;
                        return true;
                    }
                }
            }
            // Reset button
            if (hit(tx, ty, PALTAB_RESET_X, PALTAB_BTN_Y, PALTAB_RESET_W, PALTAB_BTN_H)) {
                app->user_palettes[app->palette_sel_pal] = palettes[app->palette_sel_pal];
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
            PaletteDef *pal = &app->user_palettes[app->palette_sel_pal];
            int ci = app->palette_sel_color;
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
    if (app->active_tab == TAB_CALIBRATE && ty < NAV_Y) {
        // Back
        if (tapped && hit(tx, ty, 0, 0, 60, 20)) {
            app->active_tab = TAB_MORE;
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

            CAL_ROW(ROW_BRIGHT,   CAL_BRIGHT_ABS_MIN,   CAL_BRIGHT_ABS_MAX,   app->ranges.bright_min,   app->ranges.bright_max,   app->ranges.bright_def)
            CAL_ROW(ROW_CONTRAST, CAL_CONTRAST_ABS_MIN, CAL_CONTRAST_ABS_MAX, app->ranges.contrast_min, app->ranges.contrast_max, app->ranges.contrast_def)
            CAL_ROW(ROW_SAT,      CAL_SAT_ABS_MIN,      CAL_SAT_ABS_MAX,      app->ranges.sat_min,      app->ranges.sat_max,      app->ranges.sat_def)
            CAL_ROW(ROW_GAMMA,    CAL_GAMMA_ABS_MIN,    CAL_GAMMA_ABS_MAX,    app->ranges.gamma_min,    app->ranges.gamma_max,    app->ranges.gamma_def)
            #undef CAL_ROW
        }
        // Save as Default
        if (tapped && hit(tx, ty, CAL_SAVEDEF_X, CAL_SAVEDEF_Y, CAL_SAVEDEF_W, CAL_SAVEDEF_H)) {
            *do_defaults_save = true;
            return true;
        }
        // Reset to Default
        if (tapped && hit(tx, ty, CAL_RESET_X, CAL_RESET_Y, CAL_RESET_W, CAL_RESET_H)) {
            *do_defaults_reset = true;
            return true;
        }
    }

    return false;
}
