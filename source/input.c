#include "input.h"

bool hit(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

bool handle_touch(touchPosition touch, u32 kDown, u32 kHeld,
                  FilterParams *p,
                  bool *do_cam_toggle, bool *do_save) {
    *do_cam_toggle = false;
    *do_save       = false;

    bool touched = (kHeld & KEY_TOUCH) != 0;
    bool tapped  = (kDown & KEY_TOUCH) != 0;
    if (!touched) return false;

    int tx = touch.px, ty = touch.py;

    // CAM button (tap only)
    if (tapped && hit(tx, ty, BTN_CAM_X, BTN_CAM_Y, BTN_CAM_W, BTN_CAM_H)) {
        *do_cam_toggle = true;
        return true;
    }

    // SAVE button (tap only)
    if (tapped && hit(tx, ty, BTN_SAVE_X, BTN_SAVE_Y, BTN_SAVE_W, BTN_SAVE_H)) {
        *do_save = true;
        return true;
    }

    // Palette buttons (tap only)
    if (tapped && ty >= PAL_BTN_Y && ty < PAL_BTN_Y + PAL_BTN_H) {
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

    // Continuous sliders (drag)
    if (tx >= TRACK_X - 8 && tx <= TRACK_X + TRACK_W + 8) {
        if (ty >= ROW_BRIGHT - 13 && ty < ROW_BRIGHT + 13) {
            p->brightness = touch_x_to_val(tx, 0.0f, 2.0f);
            return true;
        }
        if (ty >= ROW_CONTRAST - 13 && ty < ROW_CONTRAST + 13) {
            p->contrast = touch_x_to_val(tx, 0.5f, 2.0f);
            return true;
        }
        if (ty >= ROW_SAT - 13 && ty < ROW_SAT + 13) {
            p->saturation = touch_x_to_val(tx, 0.0f, 2.0f);
            return true;
        }
        if (ty >= ROW_GAMMA - 13 && ty < ROW_GAMMA + 13) {
            p->gamma = touch_x_to_val(tx, 0.5f, 2.0f);
            return true;
        }
    }

    return false;
}
