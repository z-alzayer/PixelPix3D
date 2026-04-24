#ifndef UI_H
#define UI_H

#include <stdio.h>
#include <stdbool.h>
#include <citro2d.h>
#include <citro3d.h>
#include "filter.h"
#include "app_state.h"

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

// ---------------------------------------------------------------------------
// Shoot mode indices
// ---------------------------------------------------------------------------

#define SHOOT_MODE_GBCAM      0
#define SHOOT_MODE_WIGGLE     1
#define SHOOT_MODE_LOMO       2
#define SHOOT_MODE_BEND       3
#define SHOOT_MODE_FX         4
#define SHOOT_MODE_COUNT      5

// Shoot quick-access row: capture selectors + effect stage entry points.
#define SHOOT_STAGE_BTN_COUNT  7
#define SHOOT_STAGE_GRID_COLS  4
#define SHOOT_STAGE_GRID_ROWS  2
#define SHOOT_MODE_ROW1_Y     44
#define SHOOT_MODE_ROW_H      28
#define SHOOT_MODE_BTN_GAP     4
#define SHOOT_MODE_BTN_W      ((BOT_W - (SHOOT_STAGE_GRID_COLS + 1) * SHOOT_MODE_BTN_GAP) / SHOOT_STAGE_GRID_COLS)
#define SHOOT_MODE_ROW2_Y     (SHOOT_MODE_ROW1_Y + SHOOT_MODE_ROW_H + SHOOT_MODE_BTN_GAP)

// Full contextual panel (replaces grid when a mode is "open")
// Occupies y=43..157: below top strip, above save button
// Back button at top of panel: y=43..63
#define SHOOT_PANEL_Y       43
#define SHOOT_PANEL_H      115   // 43..158
#define SHOOT_BACK_Y        43
#define SHOOT_BACK_H        20
#define SHOOT_BACK_W        64
#define SHOOT_GB_TOGGLE_X   (SHOOT_BACK_W + 8)
#define SHOOT_GB_TOGGLE_Y   (SHOOT_BACK_Y + 2)
#define SHOOT_GB_TOGGLE_W   56
#define SHOOT_GB_TOGGLE_H   (SHOOT_BACK_H - 4)
#define SHOOT_CONTENT_Y    (SHOOT_BACK_Y + SHOOT_BACK_H + 4)  // ~67
#define SHOOT_CONTENT_H    (SHOOT_SAVE_Y - SHOOT_CONTENT_Y)   // ~91

// Horizontal sliders inside GB Cam panel (4 sliders stacked)
// Each row: label + slider + value. Rows at relative offsets from SHOOT_CONTENT_Y
#define SHOOT_HSLIDER_ROW_H   20   // spacing between slider rows
#define SHOOT_HSLIDER_LBL_W   28   // left label column width
#define SHOOT_HSLIDER_VAL_W   28   // right value column width
#define SHOOT_HSLIDER_X      (4 + SHOOT_HSLIDER_LBL_W + 4)
#define SHOOT_HSLIDER_W      (BOT_W - SHOOT_HSLIDER_X - SHOOT_HSLIDER_VAL_W - 8)

// Timer/Photobooth panel: 3 timer buttons
#define SHOOT_TIMER_BTN_Y   (SHOOT_CONTENT_Y + 8)
#define SHOOT_TIMER_BTN_H    32
#define SHOOT_TIMER_BTN_W    88   // 3 buttons with gaps: (320-4*4)/3 ~= 101, keep 88 for breathing room
#define SHOOT_TIMER_BTN_GAP   4

// Wiggle panel sliders (2 horizontal sliders: Frames / Delay)
#define SHOOT_WIGGLE_ROW1_Y  (SHOOT_CONTENT_Y + 4)
#define SHOOT_WIGGLE_ROW2_Y  (SHOOT_CONTENT_Y + 28)

// Lomo preset grid (3 cols × 2 rows inside the Lomo contextual panel)
#define LOMO_GRID_COLS  3
#define LOMO_GRID_ROWS  2
#define LOMO_GRID_GAP   4
#define LOMO_GRID_BTN_W ((BOT_W - (LOMO_GRID_COLS + 1) * LOMO_GRID_GAP) / LOMO_GRID_COLS)
#define LOMO_GRID_BTN_H 30

// Bend preset grid (same 3×2 layout as Lomo)
#define BEND_GRID_COLS  LOMO_GRID_COLS
#define BEND_GRID_ROWS  LOMO_GRID_ROWS
#define BEND_GRID_GAP   LOMO_GRID_GAP
#define BEND_GRID_BTN_W LOMO_GRID_BTN_W
#define BEND_GRID_BTN_H LOMO_GRID_BTN_H

// Timer button values shared between draw and input
#define SHOOT_TIMER_VAL_COUNT       4
#define SHOOT_TIMER_VALS_INIT  { 0, 3, 5, 10 }
#define SHOOT_TIMER_LBLS_INIT  { "Off", "3s", "5s", "10s" }

#define SHOOT_PRESET_ROW_H      22
#define SHOOT_PRESET_ROW_GAP     6
#define SHOOT_PRESET_ROW_X       8
#define SHOOT_PRESET_ROW_W      304
#define SHOOT_PRESET_SAVE_W     132
#define SHOOT_PRESET_SAVE_H      24

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

// Save Scale row — 4 buttons: 1x / 2x / 3x / 4x
#define MORE_SCALE_Y          38
#define MORE_STOG_H           24
#define MORE_STOG_W           56
#define MORE_STOG_X0         188
#define MORE_STOG_X1         248
#define MORE_SCALE_BTN_W      28
#define MORE_SCALE_BTN_GAP     4

// Dither row
#define MORE_DITH_Y       72
#define MORE_SDITH_W      28
#define MORE_SDITH_GAP     3
#define MORE_SDITH_X0    188

// Invert row
#define MORE_INV_Y       106
#define MORE_INV_STOG_X0 188
#define MORE_INV_STOG_X1 248

// Shutter button row
#define MORE_SHUT_Y      140
#define MORE_SHUT_STOG_X0 188
#define MORE_SHUT_STOG_X1 248

// Divider
#define MORE_DIV_Y       160

// Power-user buttons row: Palette Editor | Calibrate
#define MORE_POWED_Y     164
#define MORE_POWED_H      16
#define MORE_POWED_W     150
#define MORE_PALED_X       4
#define MORE_CALIB_X     162

// Save as Default button
#define MORE_SAVEDEF_Y   183
#define MORE_SAVEDEF_H    16
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
// Gallery edit mode geometry (bottom screen editor — full 240px, no nav bar)
// ---------------------------------------------------------------------------

// Edit tab bar (Stickers / Frames)
#define GEDIT_TAB_Y       0
#define GEDIT_TAB_H      28
#define GEDIT_TAB_W     160   // half of BOT_W

// Sticker/frame picker area  y=29..194 (h=166)
// cat strip 20px at y=29 + 3 rows*41px + 2px gap + 20px arrows = 165px fits
#define GEDIT_PICKER_Y   29
#define GEDIT_PICKER_H  166
#define GEDIT_PICKER_BOT 195

// Sticker grid: 3 columns, 36px cells, 5px gaps
#define GEDIT_STICKER_COLS    3
#define GEDIT_STICKER_CELL    36
#define GEDIT_STICKER_GAP      5
#define GEDIT_STICKER_ROW_H  (GEDIT_STICKER_CELL + GEDIT_STICKER_GAP)
#define GEDIT_STICKER_ROWS    3

// Action bar  y=196..239 (h=43) — tall enough for readable button text
#define GEDIT_ACT_Y     196
#define GEDIT_ACT_H      40
// Three equal-width buttons across 320px with 2px gaps
#define GEDIT_BTN_CANCEL_X    2
#define GEDIT_BTN_CANCEL_W  103
#define GEDIT_BTN_SAVENEW_X 107
#define GEDIT_BTN_SAVENEW_W 103
#define GEDIT_BTN_OVERW_X   212
#define GEDIT_BTN_OVERW_W   106

// Edit button shown in gallery strip when a photo is selected
#define GEDIT_EDIT_BTN_X     4
#define GEDIT_EDIT_BTN_Y     4
#define GEDIT_EDIT_BTN_W    56
#define GEDIT_EDIT_BTN_H    34

// Frame picker row sizing — auto-fits FRAME_COUNT items into picker area
#define FRAME_ROW_H       (GEDIT_PICKER_H / FRAME_COUNT)
#define FRAME_PILL_H      (FRAME_ROW_H - 2)

// Sticker credit line
#define STICKER_CREDIT  "Stickers: alexkovacsart.itch.io"

// Frame count and romfs paths (must match frame_names[] in ui_tabs.c)
#define FRAME_COUNT  7
#define FRAME_PATHS_INIT { \
    "romfs:/frames/polaroid.png",  \
    "romfs:/frames/film.png",      \
    "romfs:/frames/gb_border.png", \
    "romfs:/frames/stripes.png",   \
    "romfs:/frames/vignette.png",  \
    "romfs:/frames/film_color.png",\
    "romfs:/frames/halftone.png"   \
}

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

#define CAL_SAVEDEF_X   20
#define CAL_SAVEDEF_W  130
#define CAL_SAVEDEF_H   20
#define CAL_SAVEDEF_Y  176

#define CAL_RESET_X    (CAL_SAVEDEF_X + CAL_SAVEDEF_W + 8)
#define CAL_RESET_W    130
#define CAL_RESET_H    CAL_SAVEDEF_H
#define CAL_RESET_Y    CAL_SAVEDEF_Y

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
             const AppState *app, const ShootState *shoot,
             const WiggleState *wig, const GalleryState *gal,
             const EditState *edit,
             bool warn3d, bool comparing, int timer_countdown);

#endif
