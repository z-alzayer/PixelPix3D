#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <3ds.h>
#include "filter.h"
#include "sticker.h"
#include "wigglegram.h"
#include "camera.h"
#include "pipeline.h"

// ---------------------------------------------------------------------------
// Shoot mode state (GB Cam / Wiggle / Lomo + timer)
// ---------------------------------------------------------------------------

typedef struct {
    int  shoot_mode;       // SHOOT_MODE_GBCAM / WIGGLE / LOMO
    bool shoot_mode_open;  // dropdown visible
    int  capture_mode;     // still or stereo capture selection
    int  stereo_output;    // STEREO_OUTPUT_WIGGLE / ANAGLYPH
    bool timer_open;       // timer picker visible
    bool presets_open;     // preset panel visible
    int  preset_selected;  // active preset slot
    int  shoot_timer_secs; // 0 = disabled
    bool gb_enabled;
    int  lomo_preset;
    bool lomo_enabled;
    int  bend_preset;
    bool bend_enabled;
    EffectPipeline pipeline;
    PipelinePreset presets[PIPELINE_PRESET_COUNT];

    // timer countdown
    bool timer_active;
    int  timer_remaining_ms;
    u64  timer_prev_tick;
} ShootState;

// ---------------------------------------------------------------------------
// Wiggle capture / preview
// ---------------------------------------------------------------------------

typedef struct WiggleState {
    bool          preview;          // showing captured pair
    bool          filter_active;    // palette/fx applied to wiggle output
    int           n_frames;         // requested frame count
    int           delay_ms;
    int           preview_frame;    // current cycling index
    u64           preview_last_tick;
    WiggleAlign   align_res;
    bool          has_align;
    int           offset_dx;
    int           offset_dy;
    bool          rebuild;          // offsets changed, need re-blend
    int           crop_w;
    int           crop_h;
    int           dpad_repeat;
    int           last_wiggle_offset_dx;
    int           last_wiggle_offset_dy;
    int           last_anaglyph_offset_dx;
    int           last_anaglyph_offset_dy;
    int           capture_w;      // resolution of captured pair (400 or 640)
    int           capture_h;      // (240 or 480)
    int           capture_rotate_quadrants; // locked orientation from shutter press
} WiggleState;

// ---------------------------------------------------------------------------
// Gallery browser
// ---------------------------------------------------------------------------

#define GALLERY_MAX               256
#define GALLERY_WIGGLE_MAX_FRAMES   8

typedef struct {
    bool  mode;              // gallery visible
    int   sel;
    int   scroll;
    char  paths[GALLERY_MAX][64];
    int   count;
    int   loaded;            // index of photo currently in thumb buffers (-1 = none)
    int   n_frames;          // 1 = still, >1 = wiggle animation
    int   delay_ms;
    u64   anim_tick;
    int   anim_frame;
} GalleryState;

// ---------------------------------------------------------------------------
// Gallery edit / sticker placement
// ---------------------------------------------------------------------------

typedef struct {
    bool  active;            // edit mode on
    int   tab;               // 0 = stickers, 1 = frames
    int   sticker_cat;
    int   sticker_sel;
    int   sticker_scroll;
    int   gallery_frame;     // active frame overlay index (-1 = none)
    PlacedSticker placed[STICKER_MAX];
    int   save_flash;

    // placement cursor
    float cursor_x;
    float cursor_y;
    float pending_scale;
    float pending_angle;
    bool  placing;           // sticker "picked up"
} EditState;

// ---------------------------------------------------------------------------
// Top-level app / UI state
// ---------------------------------------------------------------------------

typedef struct {
    int   active_tab;
    bool  selfie;
    bool  cam_active;
    int   save_flash;
    int   settings_flash;
    int   frame_count;
    int   save_scale;
    int   settings_row;

    FilterParams  params;
    FilterParams  default_params;
    PaletteDef    user_palettes[PALETTE_COUNT];
    FilterRanges  ranges;
    int           palette_sel_pal;
    int           palette_sel_color;
    int           cam_w;          // current camera capture width  (400 or 640)
    int           cam_h;          // current camera capture height (240 or 480)
    int           shutter_button; // 0 = A (default), 1 = L/R
    int           portrait_rotate_quadrants; // smoothed raw portrait orientation, 0/1/3
} AppState;

#endif
