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
#include "pipeline.h"
#include "anaglyph.h"
#include "nintendo_photo.h"

// ---------------------------------------------------------------------------
// Global save thread state
// ---------------------------------------------------------------------------

SaveThreadState s_save;

static int corrected_portrait_rotation(int raw_quadrants) {
    return raw_quadrants ? ((raw_quadrants + 2) & 3) : 0;
}

static int detect_portrait_quadrants_from_accel(const accelVector *accel) {
    int ax = accel->x < 0 ? -accel->x : accel->x;
    int ay = accel->y < 0 ? -accel->y : accel->y;
    int az = accel->z < 0 ? -accel->z : accel->z;
    int lateral = (ax > ay) ? ax : ay;
    int lateral_delta = (ax > ay) ? (ax - ay) : (ay - ax);

    if (lateral <= az + 120 || lateral <= 160 || lateral_delta <= 100)
        return 0;

    if (ax >= ay)
        return (accel->x >= 0) ? 1 : 3;
    return (accel->y >= 0) ? 1 : 3;
}

static int capture_portrait_rotation(const AppState *app) {
    accelVector accel = {0};
    hidAccelRead(&accel);
    int fresh_quadrants = detect_portrait_quadrants_from_accel(&accel);

    if (fresh_quadrants != 0 &&
        fresh_quadrants == app->portrait_rotate_quadrants)
        return corrected_portrait_rotation(fresh_quadrants);
    return 0;
}

static void rotate_rgb888_quadrants(uint8_t *dst, const uint8_t *src,
                                    int src_w, int src_h, int quadrants) {
    int q = quadrants & 3;
    if (q == 0) {
        memcpy(dst, src, src_w * src_h * 3);
        return;
    }

    if (q == 1) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_h - 1 - y;
                int dst_y = x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else if (q == 3) {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = y;
                int dst_y = src_w - 1 - x;
                memcpy(dst + (dst_y * src_h + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    } else {
        for (int y = 0; y < src_h; y++) {
            for (int x = 0; x < src_w; x++) {
                int dst_x = src_w - 1 - x;
                int dst_y = src_h - 1 - y;
                memcpy(dst + (dst_y * src_w + dst_x) * 3,
                       src + (y * src_w + x) * 3, 3);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Background save thread worker (runs on core 1)
// ---------------------------------------------------------------------------

static void save_thread_func(void *arg) {
    SaveThreadState *st = (SaveThreadState *)arg;
    while (true) {
        LightEvent_Wait(&st->request_event);
        if (st->quit) break;

        char path[64];
        memcpy(path, st->save_path, sizeof(path));

        if (st->wiggle_mode) {
            save_wiggle_gif(path,
                             st->snapshot_buf,
                             st->wiggle_cap_w, st->wiggle_cap_h,
                             st->snapshot_buf2,
                             st->wiggle_n_frames,
                             st->wiggle_delay_ms,
                             st->wiggle_offset_dx,
                             st->wiggle_offset_dy,
                             st->rotate_quadrants,
                             &st->wiggle_recipe);
        } else if (st->anaglyph_mode) {
            save_anaglyph_png(path,
                              st->snapshot_buf,
                              st->wiggle_cap_w, st->wiggle_cap_h,
                              st->snapshot_buf2,
                              st->wiggle_offset_dx,
                              st->wiggle_offset_dy,
                              st->rotate_quadrants,
                              &st->anaglyph_recipe,
                              st->anaglyph_colors);
        } else {
            // Stills are always written in Nintendo 3DS Camera format:
            // 640x480 HNI_XXXX.JPG plus, for stereo shots, the .MPO pair.
            int cap_w = st->still_cap_w > 0 ? st->still_cap_w : CAMERA_WIDTH;
            int cap_h = st->still_cap_h > 0 ? st->still_cap_h : CAMERA_HEIGHT;
            int eyes = st->still_stereo ? 2 : 1;

            uint8_t *work = malloc(cap_w * cap_h * 3);
            uint8_t *rot = (st->rotate_quadrants != 0)
                         ? malloc(cap_w * cap_h * 3)
                         : NULL;
            uint8_t *canvas[2] = {
                malloc(HNI_WIDTH * HNI_HEIGHT * 3),
                (eyes == 2) ? malloc(HNI_WIDTH * HNI_HEIGHT * 3) : NULL,
            };

            if (work && canvas[0] && (eyes == 1 || canvas[1]) &&
                (st->rotate_quadrants == 0 || rot)) {
                for (int eye = 0; eye < eyes; eye++) {
                    const uint16_t *src = (const uint16_t *)
                        (eye ? st->snapshot_buf2 : st->snapshot_buf);
                    rgb565_to_rgb888(work, src, cap_w * cap_h);
                    pipeline_apply(work, cap_w, cap_h, &st->still_recipe, 0);

                    const uint8_t *img = work;
                    int img_w = cap_w;
                    int img_h = cap_h;
                    if (st->rotate_quadrants != 0) {
                        rotate_rgb888_quadrants(rot, work, cap_w, cap_h,
                                                st->rotate_quadrants);
                        img = rot;
                        img_w = cap_h;
                        img_h = cap_w;
                    }
                    hni_fit_canvas(canvas[eye], img, img_w, img_h);
                }
                write_hni_photo(path,
                                (eyes == 2) ? st->save_path2 : NULL,
                                canvas[0], canvas[1]);
            }
            free(work);
            free(rot);
            free(canvas[0]);
            free(canvas[1]);
        }

        st->busy = false;
        LightEvent_Signal(&st->done_event);
    }
    threadExit(0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Thread save_thread_start(uint8_t *snapshot_buf, uint8_t *snapshot_buf2) {
    s_save.snapshot_buf  = snapshot_buf;
    s_save.snapshot_buf2 = snapshot_buf2;
    s_save.rotate_quadrants = 0;
    s_save.anaglyph_mode = false;
    s_save.still_cap_w = CAMERA_WIDTH;
    s_save.still_cap_h = CAMERA_HEIGHT;
    s_save.still_recipe = (EffectRecipe){0};
    s_save.wiggle_recipe = (EffectRecipe){0};
    s_save.anaglyph_recipe = (EffectRecipe){0};
    anaglyph_default_colors(s_save.anaglyph_colors);
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
                                 uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                                 int cam_w, int cam_h,
                                 int rotate_quadrants) {
    int screen_size = cam_w * cam_h * 2;
    memcpy(wiggle_left,  buf,               screen_size);
    memcpy(wiggle_right, buf + screen_size, screen_size);
    wig->offset_dx  = wig->last_wiggle_offset_dx;
    wig->offset_dy  = wig->last_wiggle_offset_dy;
    wig->capture_w  = cam_w;
    wig->capture_h  = cam_h;
    wig->capture_rotate_quadrants = rotate_quadrants;
    wig->manual_align = false;
    wig->align_dragging = false;
    wig->align_changed = false;
    wig->preview_frame_count = build_wiggle_preview_frames(wiggle_preview_frames,
                                wiggle_left, wiggle_right,
                                cam_w, cam_h,
                                wig->n_frames,
                                wig->offset_dx, wig->offset_dy,
                                &wig->crop_w, &wig->crop_h);
    wig->preview           = true;
    wig->rebuild           = true;  // trigger filter application on first tick
    wig->preview_frame     = 0;
    wig->preview_last_tick = svcGetSystemTick();
    play_shutter_click();
}

static void begin_anaglyph_capture(WiggleState *wig,
                                   u8 *buf, u8 *wiggle_left, u8 *wiggle_right,
                                   int cam_w, int cam_h,
                                   int rotate_quadrants) {
    int screen_size = cam_w * cam_h * 2;
    memcpy(wiggle_left,  buf,               screen_size);
    memcpy(wiggle_right, buf + screen_size, screen_size);
    wig->offset_dx  = wig->last_anaglyph_offset_dx;
    wig->offset_dy  = wig->last_anaglyph_offset_dy;
    wig->capture_w  = cam_w;
    wig->capture_h  = cam_h;
    wig->capture_rotate_quadrants = rotate_quadrants;
    wig->manual_align = false;
    wig->align_dragging = false;
    wig->align_changed = false;
    wig->n_frames = 1;
    wig->preview_frame_count = 1;
    wig->crop_w = CAMERA_WIDTH;
    wig->crop_h = CAMERA_HEIGHT;
    wig->preview = true;
    wig->rebuild = true;
    wig->preview_frame = 0;
    wig->preview_last_tick = svcGetSystemTick();
    play_shutter_click();
}

// ---------------------------------------------------------------------------
// Helper: trigger a normal JPEG save on the background thread
// ---------------------------------------------------------------------------

static void begin_jpeg_save(AppState *app, u8 *buf, const EffectRecipe *recipe) {
    char jpg_path[64];
    char mpo_path[64];
    if (next_still_paths(jpg_path, mpo_path, sizeof(jpg_path))) {
        int cap_size = app->cam_w * app->cam_h * 2;
        int rotate = capture_portrait_rotation(app);
        // 3D pair needs the two synchronized outer cameras in landscape;
        // selfie (inner cam) and portrait shots save the JPG only.
        bool stereo = !app->selfie && rotate == 0;
        memcpy(s_save.snapshot_buf, buf, cap_size);
        if (stereo)
            memcpy(s_save.snapshot_buf2, buf + cap_size, cap_size);
        memcpy(s_save.save_path, jpg_path, sizeof(jpg_path));
        memcpy(s_save.save_path2, mpo_path, sizeof(mpo_path));
        s_save.still_stereo = stereo;
        s_save.wiggle_mode = false;
        s_save.anaglyph_mode = false;
        s_save.still_cap_w = app->cam_w;
        s_save.still_cap_h = app->cam_h;
        s_save.still_recipe = recipe ? *recipe : (EffectRecipe){0};
        s_save.rotate_quadrants = rotate;
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
                  uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                  const EffectRecipe *recipe) {
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
    if (shoot->capture_mode == CAPTURE_MODE_STEREO &&
        shoot->stereo_output == STEREO_OUTPUT_WIGGLE) {
        begin_wiggle_capture(wig, buf, wiggle_left, wiggle_right,
                             wiggle_preview_frames, app->cam_w, app->cam_h,
                             capture_portrait_rotation(app));
    } else if (shoot->capture_mode == CAPTURE_MODE_STEREO &&
               shoot->stereo_output == STEREO_OUTPUT_ANAGLYPH) {
        (void)recipe;
        begin_anaglyph_capture(wig, buf, wiggle_left, wiggle_right,
                               app->cam_w, app->cam_h,
                               capture_portrait_rotation(app));
    } else if (!s_save.busy) {
        (void)filtered_buf;
        begin_jpeg_save(app, buf, recipe);
    }
}

// ---------------------------------------------------------------------------
// shoot_trigger — called when A / do_save fires outside wiggle-preview/timer
// ---------------------------------------------------------------------------

void shoot_trigger(ShootState *shoot, WiggleState *wig, AppState *app,
                   u8 *buf, u8 *filtered_buf,
                   u8 *wiggle_left, u8 *wiggle_right,
                   uint16_t wiggle_preview_frames[][CAMERA_WIDTH * CAMERA_HEIGHT],
                   const EffectRecipe *recipe) {
    if (shoot->shoot_timer_secs > 0 && !shoot->timer_active) {
        // Start countdown
        shoot->timer_remaining_ms = shoot->shoot_timer_secs * 1000;
        shoot->timer_prev_tick    = svcGetSystemTick();
        shoot->timer_active       = true;
    } else if (shoot->capture_mode == CAPTURE_MODE_STEREO &&
               shoot->stereo_output == STEREO_OUTPUT_WIGGLE) {
        begin_wiggle_capture(wig, buf, wiggle_left, wiggle_right,
                             wiggle_preview_frames, app->cam_w, app->cam_h,
                             capture_portrait_rotation(app));
    } else if (shoot->capture_mode == CAPTURE_MODE_STEREO &&
               shoot->stereo_output == STEREO_OUTPUT_ANAGLYPH) {
        (void)recipe;
        begin_anaglyph_capture(wig, buf, wiggle_left, wiggle_right,
                               app->cam_w, app->cam_h,
                               capture_portrait_rotation(app));
    } else {
        (void)filtered_buf;
        begin_jpeg_save(app, buf, recipe);
    }
}
