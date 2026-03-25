#ifndef SOUND_H
#define SOUND_H

// Initialize csnd service. Call once at startup before play_shutter_click.
// Returns 0 on success, non-zero on failure (gracefully degraded — no crash).
int  sound_init(void);

// Play a short camera shutter click on the 3DS speaker.
// Non-blocking; safe to call every frame (ignores if still playing).
void play_shutter_click(void);

// Release csnd service. Call at shutdown.
void sound_exit(void);

#endif
