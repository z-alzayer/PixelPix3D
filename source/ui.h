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
// UI colours — Nintendo 3DS system UI inspired
// ---------------------------------------------------------------------------

#define CLR_BG          C2D_Color32(240, 244, 248, 255)   // near-white cool bg
#define CLR_PANEL       C2D_Color32(220, 228, 238, 255)   // light blue-grey panel
#define CLR_WHITE       C2D_Color32(255, 255, 255, 255)
#define CLR_DIVIDER     C2D_Color32(180, 190, 205, 255)
#define CLR_TEXT        C2D_Color32( 30,  30,  40, 255)   // near-black
#define CLR_DIM         C2D_Color32(100, 110, 130, 255)   // secondary grey
#define CLR_ACCENT      C2D_Color32(  0, 100, 200, 255)   // Nintendo blue
#define CLR_ACCENT_DARK C2D_Color32(  0,  70, 160, 255)   // pressed blue
#define CLR_SEL         C2D_Color32(  0, 180, 255, 255)   // cyan selection ring
#define CLR_CONFIRM     C2D_Color32( 50, 180,  80, 255)   // green save confirm
#define CLR_BTN         C2D_Color32(200, 210, 225, 255)   // unselected button
#define CLR_BTN_SEL     CLR_ACCENT
#define CLR_BTN_TEXT    CLR_TEXT
#define CLR_BTN_SEL_TXT CLR_WHITE

// Legacy aliases used by palette/calibrate tabs (kept for minimal churn)
#define CLR_TRACK       C2D_Color32(190, 200, 215, 255)
#define CLR_FILL        CLR_ACCENT
#define CLR_HANDLE      CLR_ACCENT_DARK
#define CLR_TITLE       CLR_ACCENT
#define CLR_ROW_CURSOR  C2D_Color32(  0, 100, 200,  60)
#define CLR_SWATCH_SEL  CLR_SEL

// ---------------------------------------------------------------------------
// Bottom nav bar (always visible, y=200..240, h=40)
// ---------------------------------------------------------------------------

#define NAV_Y        200
#define NAV_H         40
#define NAV_SEG_W     80   // 4 segments x 80px = 320px
#define NAV_INDICATOR_H  3  // coloured underline on active tab

// Tab indices
#define TAB_SHOOT  0
#define TAB_STYLE  1
#define TAB_FX     2
#define TAB_MORE   3
#define TAB_PALETTE_ED  4   // entered from MORE
#define TAB_CALIBRATE   5   // entered from MORE

// Content area: y=0..200
#define CONTENT_H  200

// ---------------------------------------------------------------------------
// Shoot tab geometry (y=0..200)
// ---------------------------------------------------------------------------

// Top strip: y=0..42 (h=42)
//   Left zone  [0..79]:   camera flip button
//   Centre     [80..239]: 7 palette swatches
//   Right zone [240..319]: pixel-size buttons
#define SHOOT_STRIP_Y   0
#define SHOOT_STRIP_H  42

// Camera flip button
#define SHOOT_CAM_X     0
#define SHOOT_CAM_Y     SHOOT_STRIP_Y
#define SHOOT_CAM_W    80
#define SHOOT_CAM_H    SHOOT_STRIP_H

// Palette swatches (7 swatches centred in 80..239 = 160px wide)
// Each swatch: 18px wide, 4px gap → 7*18 + 6*4 = 126+24 = 150px, centred in 160 = offset 5
#define SHOOT_PAL_ZONE_X  80
#define SHOOT_PAL_ZONE_W 160
#define SHOOT_SWATCH_W   18
#define SHOOT_SWATCH_H   26
#define SHOOT_SWATCH_GAP  4
#define SHOOT_SWATCH_Y   (SHOOT_STRIP_Y + (SHOOT_STRIP_H - SHOOT_SWATCH_H) / 2)
// x of swatch i = SHOOT_PAL_ZONE_X + swatch_offset + i*(SHOOT_SWATCH_W+SHOOT_SWATCH_GAP)
// total width = 7*18 + 6*4 = 150; offset = (160-150)/2 = 5
#define SHOOT_SWATCH_X0  (SHOOT_PAL_ZONE_X + 5)

// Gallery button (right zone [240..319])
#define SHOOT_GAL_X     240
#define SHOOT_GAL_Y     SHOOT_STRIP_Y
#define SHOOT_GAL_W      80
#define SHOOT_GAL_H     SHOOT_STRIP_H

// Divider below strip
#define SHOOT_DIVIDER_Y  SHOOT_STRIP_H

// Future / blank area: y=42..158 (reserved for photobooth, frames, etc.)
#define SHOOT_FUTURE_Y   43
#define SHOOT_FUTURE_H  115

// Save button: y=158..200, full width
#define SHOOT_SAVE_Y    158
#define SHOOT_SAVE_H     42

// ---------------------------------------------------------------------------
// Style tab geometry (y=0..200)
// ---------------------------------------------------------------------------

// Section label
#define STYLE_LABEL_Y     8

// Palette buttons: 2 rows of 4 then row of 3 (or 4+3), large tap targets
// Row 1: 4 buttons, Row 2: 3 buttons (+ "None")
#define STYLE_PAL_Y0     28   // top of first palette row
#define STYLE_PAL_H      36   // height of each palette button
#define STYLE_PAL_GAP     4
#define STYLE_PAL_Y1    (STYLE_PAL_Y0 + STYLE_PAL_H + STYLE_PAL_GAP)
// 4 buttons per row: w = (320 - 5*4) / 4 = 300/4 = 75
#define STYLE_PAL_W      75
// Row 1: 4 palettes; Row 2: 3 palettes + None
// x of button i in row: 4 + i*(75+4)

// Pixel size section
#define STYLE_PX_LABEL_Y  114
#define STYLE_PX_Y       130   // snap slider centre y
#define PX_STOPS          8

// Vertical image-adjustment sliders on SHOOT tab (Bright / Contrast / Sat / Gamma)
// 4 columns of 80px each, middle area y=54..150
#define SHOOT_VSLIDER_Y       54  // top of track
#define SHOOT_VSLIDER_H       80  // track height
#define SHOOT_VSLIDER_BOTTOM (SHOOT_VSLIDER_Y + SHOOT_VSLIDER_H)
#define SHOOT_VSLIDER_COL_W   80  // width per column
#define SHOOT_VSLIDER_TRACK_W  6  // track rect width
#define SHOOT_VSLIDER_HANDLE_W 18 // handle width
#define SHOOT_VSLIDER_HANDLE_H 10 // handle height
// centre x of slider i = i * SHOOT_VSLIDER_COL_W + SHOOT_VSLIDER_COL_W/2

// ---------------------------------------------------------------------------
// FX tab geometry (y=0..200) — largely reusing existing layout, restyled
// ---------------------------------------------------------------------------

#define FXTAB_LABEL_Y      8
#define FXTAB_BTN_Y1      28
#define FXTAB_BTN_Y2      60
#define FXTAB_BTN_H       28
#define FXTAB_R1_W        75
#define FXTAB_R1_GAP       4
#define FXTAB_R1_X0        4
#define FXTAB_R2_W       100
#define FXTAB_R2_GAP       4
#define FXTAB_R2_X0        4
#define FXTAB_SLIDER_Y   120
#define FXTAB_DESC_Y     158
// No Save as Default here — moved to MORE

// ---------------------------------------------------------------------------
// MORE tab geometry (settings overlay, y=0..200)
// ---------------------------------------------------------------------------

#define MORE_LABEL_Y       8

// Save Scale row
#define MORE_SCALE_Y      38
#define MORE_STOG_H       24
#define MORE_STOG_W       56
#define MORE_STOG_X0     188
#define MORE_STOG_X1     248

// Dither row
#define MORE_DITH_Y       72
#define MORE_SDITH_W      28
#define MORE_SDITH_GAP     3
#define MORE_SDITH_X0    188

// Invert row
#define MORE_INV_Y       106
#define MORE_INV_STOG_X0 188
#define MORE_INV_STOG_X1 248

// Divider
#define MORE_DIV_Y       130

// Power-user buttons row: Palette Editor | Calibrate
#define MORE_POWED_Y     140
#define MORE_POWED_H      26
#define MORE_POWED_W     150
#define MORE_PALED_X       4
#define MORE_CALIB_X     162

// Save as Default button
#define MORE_SAVEDEF_Y   174
#define MORE_SAVEDEF_H    26
#define MORE_SAVEDEF_X    60
#define MORE_SAVEDEF_W   200

// ---------------------------------------------------------------------------
// Slider + handle geometry (shared, used by Style, Palette, Calibrate)
// ---------------------------------------------------------------------------

#define TRACK_X   102
#define TRACK_W   176
#define TRACK_H     5
#define HANDLE_W   14
#define HANDLE_H   22

// Range slider (calibrate) handles
#define RHANDLE_W  10
#define RHANDLE_H  18
#define DOT_SZ      8

// Camera tab rows (kept for calibrate reuse)
#define ROW_BRIGHT    44
#define ROW_CONTRAST  72
#define ROW_SAT      100
#define ROW_GAMMA    128

// ---------------------------------------------------------------------------
// Palette editor tab geometry (navigated from MORE)
// ---------------------------------------------------------------------------

#define PALTAB_PALSEL_Y       4
#define PALTAB_PALSEL_H      30
#define PALTAB_PALSEL_BTN_W  (BOT_W / PALETTE_COUNT)

#define PALTAB_SWATCH_Y       38
#define PALTAB_SWATCH_H       50
#define PALTAB_SWATCH_W       36

#define PALTAB_HS_X            4
#define PALTAB_HS_Y           92
#define PALTAB_HS_W          312
#define PALTAB_HS_H           68

#define PALTAB_VAL_X           4
#define PALTAB_VAL_Y         164
#define PALTAB_VAL_W         312
#define PALTAB_VAL_H          12

#define PALTAB_BTN_Y         180
#define PALTAB_BTN_H          18
#define PALTAB_RESET_W        80
#define PALTAB_RESET_H        PALTAB_BTN_H
#define PALTAB_RESET_Y       (PALTAB_BTN_Y + PALTAB_BTN_H / 2)
#define PALTAB_RESET_X       ((BOT_W / 2) - PALTAB_RESET_W - 2)
#define PALTAB_SAVE_DEF_W    120
#define PALTAB_SAVE_DEF_H     PALTAB_BTN_H
#define PALTAB_SAVE_DEF_X    ((BOT_W / 2) + 2)

// ---------------------------------------------------------------------------
// Gallery tab geometry (toggled from SHOOT tab)
// ---------------------------------------------------------------------------

#define GALLERY_COLS      4
#define GALLERY_CELL_W   74
#define GALLERY_CELL_H   44
#define GALLERY_GAP       4
#define GALLERY_ROW_H    (GALLERY_CELL_H + GALLERY_GAP)
#define GALLERY_ROWS      3   // 3 rows fit below the shoot strip (158px avail)
#define GALLERY_START_Y  (SHOOT_STRIP_H + GALLERY_GAP)

#define BTN_GSCROLL_X    302
#define BTN_GSCROLL_W     18
#define BTN_GSCROLL_H     26
#define BTN_GSCROLL_UP_Y  (GALLERY_START_Y)
#define BTN_GSCROLL_DN_Y  (CONTENT_H - BTN_GSCROLL_H)

// ---------------------------------------------------------------------------
// Calibrate tab geometry (navigated from MORE)
// ---------------------------------------------------------------------------

#define CAL_BRIGHT_ABS_MIN   0.0f
#define CAL_BRIGHT_ABS_MAX   4.0f
#define CAL_CONTRAST_ABS_MIN 0.1f
#define CAL_CONTRAST_ABS_MAX 4.0f
#define CAL_SAT_ABS_MIN      0.0f
#define CAL_SAT_ABS_MAX      4.0f
#define CAL_GAMMA_ABS_MIN    0.1f
#define CAL_GAMMA_ABS_MAX    4.0f

#define CAL_SAVEDEF_X   60
#define CAL_SAVEDEF_W  200
#define CAL_SAVEDEF_H   24
#define CAL_SAVEDEF_Y  185

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
void draw_snap_slider(float cy, int px_val);
void draw_range_slider(float cy, float abs_min, float abs_max,
                       float val_min, float val_max, float val_def);
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
             const char gallery_paths[][64], int gallery_sel, int gallery_scroll);

#endif
