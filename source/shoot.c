#include <string.h>
#include <stdlib.h>
#include <3ds.h>

#include "shoot.h"
#include "camera.h"
#include "ui.h"
#include "image_load.h"
#include "wigglegram.h"
#include "settings.h"
#include "sound.h"

// ---------------------------------------------------------------------------
// Global save thread state
// ---------------------------------------------------------------------------

SaveThreadState s_save;

// ---------------------------------------------------------------------------
// Background save thread worker (runs on core 1)
// ---------------------------------------------------------------------------

static void save_thread_func(void *arg) {
    SaveThreadState *st = (SaveThreadState *)arg;
    uint8_t *rgb_priv     = malloc(CAMERA_WIDTH * CAMERA_HEIGHT * 3);
    uint8_t *upscale_priv = malloc(SAVE_SCALE * CAMERA_WIDTH * SAVE_SCALE * CAMERA_HEIGHT * 3);
    if (!rgb_priv || !upscale_priv) {
        free(rgb_priv);
        free(upscale_priv);
        threadExit(1);
    }
    while (true) {
        LightEvent_Wait(&st->request_event);
        if (st->quit) break;

        char path[64];
        memcpy(path, st->save_path, sizeof(path));

        if (st->wiggle_mode) {
            save_wiggle_apng(path,
                             st->snapshot_buf,  CAMERA_WIDTH, CAMERA_HEIGHT,
                             st->snapshot_buf2,
                             st->wiggle_n_frames,
                             st->wiggle_delay_ms,
                             NULL,
                             st->wiggle_offset_dx,
                             st->wiggle_offset_dy);
        } else {
            int scale = st->save_scale;
            rgb565_to_rgb888(rgb_priv, (const uint16_t *)st->snapshot_buf,
                             CAMERA_WIDTH * CAMERA_HEIGHT);
            nn_upscale(upscale_priv, rgb_priv, CAMERA_WIDTH, CAMERA_HEIGHT, scale);
            save_jpeg(path, upscale_priv, CAMERA_WIDTH * scale, CAMERA_HEIGHT * scale);
        }

        st->busy = false;
        LightEvent_Signal(&st->done_event);
    }
    free(rgb_priv);
    free(upscale_priv);
    threadExit(0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Thread save_thread_start(uint8_t *snapshot_buf, uint8_t *snapshot_buf2) {
    s_save.snapshot_buf  = snapshot_buf;
    s_save.snapshot_buf2 = snapshot_buf2;
    LightEvent_Init(&s_save.request_event, RESET_ONESHOT);
    LightEvent_Init(&s_save.done_event,    RESET_ONESHOT);
    s_save.busy = false;
    s_save.quit = false;
    APT_SetAppCpuTimeLimit(30);
    return threadCreate(save_thread_func, &s_save,
                        SAVE_THREAD_STACK_SIZE, 0x3F, 1, false);
}

void save_thread_stop(Thread thread) {
    s_save.quit = true;
    LightEvent_Signal(&s_save.request_event);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
}

// ---------------------------------------------------------------------------
// Helper: start a wiggle capture into preview mode
// ---------------------------------------------------------------------------

static void begin_wiggle_capture(WiggleState *wig,
                                 u8 *buf, u8 *wiggle_left, u8 *wiggle_right,
                                 uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT]) {
    memcpy(wiggle_left,  buf,                      CAMERA_SCREEN_SIZE);
    memcpy(wiggle_right, buf + CAMERA_SCREEN_SIZE, CAMERA_SCREEN_SIZE);
    wig->has_align = false;
    wig->offset_dx = 0;
    wig->offset_dy = 0;
    wig->n_frames = build_wiggle_preview_frames(wiggle_preview_frames,
                                wiggle_left, wiggle_right,
                                CAMERA_WIDTH, CAMERA_HEIGHT,
                                wig->n_frames, NULL,
                                wig->offset_dx, wig->offset_dy,
                                &wig->crop_w, &wig->crop_h);
    wig->preview           = true;
    wig->preview_frame     = 0;
    wig->preview_last_tick = svcGetSystemTick();
    play_shutter_click();
}

// ---------------------------------------------------------------------------
// Helper: trigger a normal JPEG save on the background thread
// ---------------------------------------------------------------------------

static void begin_jpeg_save(AppState *app, u8 *filtered_buf) {
    char save_path[64];
    if (next_save_path(SAVE_DIR, save_path, sizeof(save_path))) {
        settings_save_file_counter(file_counter_next());
        memcpy(s_save.snapshot_buf, filtered_buf, CAMERA_SCREEN_SIZE);
        memcpy(s_save.save_path, save_path, sizeof(save_path));
        s_save.wiggle_mode = false;
        s_save.save_scale  = app->save_scale;
        s_save.busy = true;
        app->save_flash = 20;
        play_shutter_click();
        LightEvent_Signal(&s_save.request_event);
    }
}

// ---------------------------------------------------------------------------
// timer_update — called each frame when shoot->timer_active is true
// ---------------------------------------------------------------------------

void timer_update(ShootState *shoot, WiggleState *wig, AppState *app,
                  u32 kDown,
                  u8 *buf, u8 *filtered_buf,
                  u8 *wiggle_left, u8 *wiggle_right,
                  uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT]) {
    // B cancels countdown
    if (kDown & KEY_B) {
        shoot->timer_active = false;
        return;
    }

    // Advance countdown using wall-clock ticks
    u64 now = svcGetSystemTick();
    int elapsed_ms = (int)((now - shoot->timer_prev_tick) * 1000 / SYSCLOCK_ARM11);
    shoot->timer_prev_tick = now;
    shoot->timer_remaining_ms -= elapsed_ms;

    if (shoot->timer_remaining_ms > 0)
        return;

    shoot->timer_active = false;

    // Fire save using the mode that was active before switching to Timer
    if (shoot->shoot_mode == SHOOT_MODE_WIGGLE) {
        begin_wiggle_capture(wig, buf, wiggle_left, wiggle_right, wiggle_preview_frames);
    } else if (!s_save.busy) {
        begin_jpeg_save(app, filtered_buf);
    }
}

// ---------------------------------------------------------------------------
// shoot_trigger — called when A / do_save fires outside wiggle-preview/timer
// ---------------------------------------------------------------------------

void shoot_trigger(ShootState *shoot, WiggleState *wig, AppState *app,
                   u8 *buf, u8 *filtered_buf,
                   u8 *wiggle_left, u8 *wiggle_right,
                   uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT]) {
    if (shoot->shoot_timer_secs > 0 && !shoot->timer_active) {
        // Start countdown
        shoot->timer_remaining_ms = shoot->shoot_timer_secs * 1000;
        shoot->timer_prev_tick    = svcGetSystemTick();
        shoot->timer_active       = true;
    } else if (shoot->shoot_mode == SHOOT_MODE_WIGGLE) {
        begin_wiggle_capture(wig, buf, wiggle_left, wiggle_right, wiggle_preview_frames);
    } else {
        begin_jpeg_save(app, filtered_buf);
    }
}
