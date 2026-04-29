#ifndef SHOOT_H
#define SHOOT_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds.h>
#include "app_state.h"
#include "camera.h"
#include "wigglegram.h"
#include "pipeline.h"

// ---------------------------------------------------------------------------
// Background save thread state
// ---------------------------------------------------------------------------

#define SAVE_THREAD_STACK_SIZE (64 * 1024)

typedef struct SaveThreadState {
    uint8_t      *snapshot_buf;    // malloc'd once, CAMERA_SCREEN_SIZE bytes (RGB565)
    uint8_t      *snapshot_buf2;   // malloc'd once, CAMERA_SCREEN_SIZE bytes (RGB565) — right cam
    char          save_path[64];
    int           save_scale;
    int           rotate_quadrants; // 0 = landscape, 1 = CW 90, 3 = CCW 90
    bool          wiggle_mode;     // true = save GIF from both cam buffers
    bool          anaglyph_mode;   // true = save red/cyan PNG from both cam buffers
    int           wiggle_n_frames;
    int           wiggle_delay_ms;
    WiggleAlign   wiggle_align_result;
    bool          wiggle_has_align;
    int           wiggle_offset_dx;
    int           wiggle_offset_dy;
    int           wiggle_cap_w;    // capture resolution (400 or 640)
    int           wiggle_cap_h;    // (240 or 480)
    EffectRecipe  wiggle_recipe;   // snapshot of the active processing recipe
    EffectRecipe  anaglyph_recipe; // snapshot of the active processing recipe
    volatile bool busy;            // main sets true on trigger; worker clears on finish
    volatile bool quit;            // main sets true at shutdown
    LightEvent    request_event;   // RESET_ONESHOT: main signals worker to start
    LightEvent    done_event;      // RESET_ONESHOT: worker signals when finished
} SaveThreadState;

// Global save thread state (owned by shoot.c)
extern SaveThreadState s_save;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Launch the background save thread on core 1.  Returns the Thread handle,
// or NULL on failure.  Caller must pass pre-allocated snapshot buffers.
Thread save_thread_start(uint8_t *snapshot_buf, uint8_t *snapshot_buf2);

// Signal the save thread to quit and wait for it to finish.
void save_thread_stop(Thread thread);

// ---------------------------------------------------------------------------
// Per-frame logic extracted from the main loop
// ---------------------------------------------------------------------------

// Handle the shoot-timer countdown.  Called when shoot->timer_active is true.
// May fire a save (JPEG or wiggle capture) when the countdown expires.
// wiggle_preview_frames: static buffer for building wiggle preview.
void timer_update(ShootState *shoot, WiggleState *wig, AppState *app,
                  u32 kDown,
                  u8 *buf, u8 *filtered_buf,
                  u8 *wiggle_left, u8 *wiggle_right,
                  uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                  const EffectRecipe *recipe);

// Handle the A-button / do_save trigger when not in wiggle-preview or timer
// mode.  Starts a timer countdown, begins wiggle capture, or saves a JPEG.
void shoot_trigger(ShootState *shoot, WiggleState *wig, AppState *app,
                   u8 *buf, u8 *filtered_buf,
                   u8 *wiggle_left, u8 *wiggle_right,
                   uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                   const EffectRecipe *recipe);

#endif
