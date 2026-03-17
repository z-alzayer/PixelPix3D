#ifndef UI_H
#define UI_H

#include <stdio.h>
#include <stdbool.h>
#include <citro2d.h>
#include <citro3d.h>
#include "filter.h"

// ---------------------------------------------------------------------------
// Bottom screen dimensions
// ---------------------------------------------------------------------------

#define BOT_W  320
#define BOT_H  240

// ---------------------------------------------------------------------------
// UI colours (RGBA)
// ---------------------------------------------------------------------------

#define CLR_BG       C2D_Color32( 18,  18,  24, 255)
#define CLR_TRACK    C2D_Color32( 55,  55,  60, 255)
#define CLR_FILL     C2D_Color32( 68, 148,  68, 255)
#define CLR_HANDLE   C2D_Color32(168, 224,  88, 255)
#define CLR_BTN      C2D_Color32( 38,  42,  50, 255)
#define CLR_BTN_SEL  C2D_Color32( 68, 148,  68, 255)
#define CLR_TEXT     C2D_Color32(210, 228, 190, 255)
#define CLR_DIM      C2D_Color32(120, 130, 110, 255)
#define CLR_DIVIDER  C2D_Color32( 50,  55,  60, 255)
#define CLR_TITLE    C2D_Color32(168, 224,  88, 255)

// ---------------------------------------------------------------------------
// Slider + button geometry
// ---------------------------------------------------------------------------

#define TRACK_X   102
#define TRACK_W   176
#define TRACK_H     6
#define HANDLE_W   12
#define HANDLE_H   20

// Continuous slider row centres (y of the track mid-line)
#define ROW_BRIGHT    44
#define ROW_CONTRAST  72
#define ROW_SAT      100
#define ROW_GAMMA    128

// Pixel-size snap slider
#define ROW_PXSIZE   158
#define PX_STOPS     8

// Palette buttons (y=206..238)
#define PAL_BTN_Y     206
#define PAL_BTN_H      30
#define PAL_BTN_W      42
#define PAL_BTN_X0      4

// Action buttons top-right
#define BTN_CAM_X     213
#define BTN_CAM_Y       4
#define BTN_CAM_W      46
#define BTN_CAM_H      22

#define BTN_SAVE_X    264
#define BTN_SAVE_Y      4
#define BTN_SAVE_W     50
#define BTN_SAVE_H     22

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int   px_stop_x(int val);
float slider_val_to_x(float val, float mn, float mx);
float touch_x_to_val(int px, float mn, float mx);

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void draw_slider(float cx, float cy, float mn, float mx, float val);
void draw_snap_slider(int px_val);
void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             bool save_flash, bool warn3d);

#endif
