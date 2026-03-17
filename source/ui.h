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

#define CLR_BG          C2D_Color32( 18,  18,  24, 255)
#define CLR_TRACK       C2D_Color32( 55,  55,  60, 255)
#define CLR_FILL        C2D_Color32( 68, 148,  68, 255)
#define CLR_HANDLE      C2D_Color32(168, 224,  88, 255)
#define CLR_BTN         C2D_Color32( 38,  42,  50, 255)
#define CLR_BTN_SEL     C2D_Color32( 68, 148,  68, 255)
#define CLR_TEXT        C2D_Color32(210, 228, 190, 255)
#define CLR_DIM         C2D_Color32(120, 130, 110, 255)
#define CLR_DIVIDER     C2D_Color32( 50,  55,  60, 255)
#define CLR_TITLE       C2D_Color32(168, 224,  88, 255)
#define CLR_ROW_CURSOR  C2D_Color32( 68, 148,  68,  80)
#define CLR_SWATCH_SEL  C2D_Color32(255, 220,  50, 255)

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

// ---------------------------------------------------------------------------
// Tab bar geometry — 4 equal segments across the full 320px width
// [Camera 0..79] [Settings 80..159] [CAM 160..239] [Save 240..319]
// ---------------------------------------------------------------------------

#define TAB_BAR_Y       0
#define TAB_BAR_H      30
#define TAB_SEG_W      80    // each of the 4 segments is 80px wide

#define TAB_0_X         0
#define TAB_0_W        TAB_SEG_W
#define TAB_1_X        80
#define TAB_1_W        TAB_SEG_W

// CAM and SAVE live in the tab bar as action segments
#define BTN_CAM_X     160
#define BTN_CAM_Y       0
#define BTN_CAM_W      80
#define BTN_CAM_H      30

#define BTN_SAVE_X    240
#define BTN_SAVE_Y      0
#define BTN_SAVE_W     80
#define BTN_SAVE_H     30

// ---------------------------------------------------------------------------
// Settings tab geometry
// ---------------------------------------------------------------------------

// Toggle button pairs
#define STOG_W         60
#define STOG_H         22
#define STOG_X0       195    // left option button X
#define STOG_X1       260    // right option button X

// Settings row Y centres
#define SROW_SAVE_SCALE   55
#define SROW_DITHER       90
#define SROW_INVERT      125

// Wide action buttons (centred: x = (320-200)/2 = 60)
#define SWBTN_W       200
#define SWBTN_H        24
#define SWBTN_X        60
#define SROW_SAVE_DEF 185

// ---------------------------------------------------------------------------
// Palette tab geometry
// ---------------------------------------------------------------------------

// Palette selector strip (y=31..64)
#define PALTAB_PALSEL_Y       31
#define PALTAB_PALSEL_H       34
#define PALTAB_PALSEL_BTN_W   (BOT_W / PALETTE_COUNT)  // 53px each

// Colour swatch strip (y=66..120): swatch i at x = 4 + i*40
#define PALTAB_SWATCH_Y        66
#define PALTAB_SWATCH_H        55
#define PALTAB_SWATCH_W        36

// RGB slider rows (reuse TRACK_X / TRACK_W)
#define PALTAB_ROW_R          142
#define PALTAB_ROW_G          163
#define PALTAB_ROW_B          184

// Reset button (centred)
#define PALTAB_RESET_Y        210
#define PALTAB_RESET_W         80
#define PALTAB_RESET_H         22
#define PALTAB_RESET_X        ((BOT_W - PALTAB_RESET_W) / 2)

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
             bool save_flash, bool warn3d,
             int active_tab, int save_scale, bool settings_flash,
             int settings_row,
             const PaletteDef *user_palettes,
             int palette_sel_pal, int palette_sel_color);

#endif
