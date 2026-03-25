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

// Dither-mode 4-button strip
#define SDITH_W        30    // each button width
#define SDITH_GAP       2    // gap between buttons
#define SDITH_X0      188    // x of first button

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

// 2D hue-saturation picker rectangle (x:4..315, y:124..193)
#define PALTAB_HS_X             4
#define PALTAB_HS_Y           124
#define PALTAB_HS_W           312
#define PALTAB_HS_H            70

// Value (brightness) strip (x:4..315, y:196..207)
#define PALTAB_VAL_X            4
#define PALTAB_VAL_Y          196
#define PALTAB_VAL_W          312
#define PALTAB_VAL_H           12

// Reset + Save-as-Default buttons side-by-side (y=218..237)
#define PALTAB_BTN_Y          218
#define PALTAB_BTN_H           20
#define PALTAB_RESET_W         80
#define PALTAB_RESET_H         PALTAB_BTN_H
#define PALTAB_RESET_Y        (PALTAB_BTN_Y + PALTAB_BTN_H / 2)  // kept for legacy hit-test compat
#define PALTAB_RESET_X        ((BOT_W / 2) - PALTAB_RESET_W - 2)
#define PALTAB_SAVE_DEF_W     120
#define PALTAB_SAVE_DEF_H      PALTAB_BTN_H
#define PALTAB_SAVE_DEF_X     ((BOT_W / 2) + 2)

// ---------------------------------------------------------------------------
// Gallery tab geometry
// ---------------------------------------------------------------------------

#define GALLERY_COLS      4
#define GALLERY_CELL_W   74
#define GALLERY_CELL_H   44
#define GALLERY_GAP       4
#define GALLERY_ROW_H    (GALLERY_CELL_H + GALLERY_GAP)
#define GALLERY_ROWS      4
#define GALLERY_START_Y  (TAB_BAR_H + GALLERY_GAP)

// Gallery scroll buttons (right edge of gallery grid)
#define BTN_GSCROLL_X    302
#define BTN_GSCROLL_W     18
#define BTN_GSCROLL_H     26
#define BTN_GSCROLL_UP_Y  (GALLERY_START_Y)
#define BTN_GSCROLL_DN_Y  (BOT_H - BTN_GSCROLL_H)

// ---------------------------------------------------------------------------
// FX tab geometry
// ---------------------------------------------------------------------------

// Mode button row 1: None, Scan-H, Scan-V, LCD (4 buttons x 75px, 4px gap, x start=4)
#define FXTAB_BTN_Y1    53
#define FXTAB_BTN_Y2    80
#define FXTAB_BTN_H     24
#define FXTAB_R1_W      75
#define FXTAB_R1_GAP     4
#define FXTAB_R1_X0      4
// Mode button row 2: Vignette, Chroma, Grain (3 buttons x 101px, 4px gap, x start=4)
#define FXTAB_R2_W     101
#define FXTAB_R2_GAP     4
#define FXTAB_R2_X0      4
// Intensity slider y-centre (reuses TRACK_X, TRACK_W)
#define FXTAB_SLIDER_Y 130
// Description text y
#define FXTAB_DESC_Y   170
// Save-as-Default button y-centre (reuses SWBTN_X, SWBTN_W, SWBTN_H)
#define FXTAB_SAVE_Y   222

// ---------------------------------------------------------------------------
// Calibrate tab geometry
// ---------------------------------------------------------------------------

// Range slider handles (left=min, right=max) + default-value dot
#define RHANDLE_W     10
#define RHANDLE_H     18
#define DOT_SZ         8    // side of the default-value square dot

// Calibrate rows reuse ROW_BRIGHT / ROW_CONTRAST / ROW_SAT / ROW_GAMMA

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
void draw_range_slider(float cy, float abs_min, float abs_max,
                       float val_min, float val_max, float val_def);
void draw_ui(C3D_RenderTarget *bot,
             C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
             FilterParams p, bool selfie,
             bool save_flash, bool warn3d,
             int active_tab, int save_scale, bool settings_flash,
             int settings_row,
             const PaletteDef *user_palettes,
             int palette_sel_pal, int palette_sel_color,
             const FilterRanges *ranges,
             bool comparing,
             bool gallery_mode, int gallery_count,
             const char gallery_paths[][64], int gallery_sel, int gallery_scroll);

#endif
