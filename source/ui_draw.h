// ui_draw.h — Internal drawing primitives shared across ui_*.c files.
// NOT part of the public UI API — do not include from ui.h.

#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "ui.h"

// ---------------------------------------------------------------------------
// Rounded-rect primitives (defined in ui_widgets.c)
// ---------------------------------------------------------------------------

void draw_rounded_rect(float x, float y, float w, float h, float r, u32 col);
void draw_pill(float x, float y, float w, float h, u32 col);
void draw_rounded_rect_on_panel(float x, float y, float w, float h, float r, u32 col);

// ---------------------------------------------------------------------------
// Primary tab panels (defined in ui_tabs.c)
// ---------------------------------------------------------------------------

void draw_bottom_nav(C2D_TextBuf buf, int active_tab);
void draw_shoot_tab(C2D_TextBuf staticBuf,
                    bool selfie, int save_flash,
                    const PaletteDef *user_palettes,
                    int active_palette,
                    bool gallery_mode,
                    const FilterParams *p, const FilterRanges *ranges,
                    int shoot_mode, bool shoot_mode_open,
                    int shoot_timer_secs, bool timer_open,
                    int wiggle_frames, int wiggle_delay_ms,
                    bool wiggle_preview,
                    int wiggle_offset_dx, int wiggle_offset_dy,
                    int lomo_preset,
                    int bend_preset);
void draw_gallery_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                      int gallery_count, const char gallery_paths[][64],
                      int gallery_sel, int gallery_scroll);
void draw_gallery_edit_tab(C2D_TextBuf staticBuf,
                           int edit_tab, int sticker_cat, int sticker_sel, int sticker_scroll,
                           int gallery_frame,
                           float sticker_cursor_x, float sticker_cursor_y,
                           float sticker_pending_scale, float sticker_pending_angle,
                           bool sticker_placing);
void draw_style_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                    const FilterParams *p, const FilterRanges *ranges);
void draw_fx_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                 const FilterParams *p, bool settings_flash);
void draw_more_tab(C2D_TextBuf staticBuf,
                   const FilterParams *p, int save_scale,
                   int shutter_button, bool settings_flash);

// ---------------------------------------------------------------------------
// Secondary overlay tabs (defined in ui_overlay.c)
// ---------------------------------------------------------------------------

void draw_palette_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                      const PaletteDef *user_palettes,
                      int palette_sel_pal, int palette_sel_color,
                      bool settings_flash);
void draw_calibrate_tab(C2D_TextBuf staticBuf, C2D_TextBuf dynBuf,
                        const FilterRanges *ranges, bool settings_flash);

#endif
