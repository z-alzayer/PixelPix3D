#include "ui_draw.h"
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
// Citro2D has no native rounded rect, so we rasterise each corner as N
// horizontal scanlines approximating a quarter-circle using sqrtf.
// N = (int)r gives 1px-tall slices — enough for smooth curves at 3DS res.
// ---------------------------------------------------------------------------

void draw_rounded_rect(float x, float y, float w, float h, float r, u32 col) {
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    if (r <= 0.5f) {
        C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
        return;
    }

    int N = (int)(r + 0.5f);

    // Centre band: full width, inner height (skips corner rows)
    C2D_DrawRectSolid(x, y + r, 0.5f, w, h - 2.0f * r, col);

    for (int i = 0; i < N; i++) {
        float t  = (N - i - 0.5f) / (float)N;
        float dx = r * (1.0f - sqrtf(1.0f - t * t));
        float span_x = x + dx;
        float span_w = w - 2.0f * dx;

        C2D_DrawRectSolid(span_x, y + i,             0.5f, span_w, 1.0f, col);
        C2D_DrawRectSolid(span_x, y + h - 1.0f - i, 0.5f, span_w, 1.0f, col);
    }
}

// Rounded button — ~28% of height gives soft but not fully pill-shaped corners
void draw_pill(float x, float y, float w, float h, u32 col) {
    draw_rounded_rect(x, y, w, h, h * 0.28f, col);
}

void draw_rounded_rect_on_panel(float x, float y, float w, float h,
                                float r, u32 col) {
    draw_rounded_rect(x, y, w, h, r, col);
}

// ---------------------------------------------------------------------------
// Slider drawing helpers
// ---------------------------------------------------------------------------

void draw_slider(float cx, float cy, float mn, float mx, float val) {
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = slider_val_to_x(val, mn, mx);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    draw_rounded_rect(hx - HANDLE_W / 2.0f, cy - HANDLE_H / 2.0f,
                      HANDLE_W, HANDLE_H, 3.0f, CLR_HANDLE);
    (void)cx;
}

void draw_snap_slider(float cy, int px_val) {
    C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, TRACK_W, TRACK_H, CLR_TRACK);
    float hx = (float)px_stop_x(px_val);
    float fill_w = hx - TRACK_X;
    if (fill_w > 0)
        C2D_DrawRectSolid(TRACK_X, cy - TRACK_H / 2.0f, 0.5f, fill_w, TRACK_H, CLR_FILL);
    draw_rounded_rect(hx - HANDLE_W / 2.0f, cy - HANDLE_H / 2.0f,
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
