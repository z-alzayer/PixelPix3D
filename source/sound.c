#include "sound.h"
#include <3ds.h>

// Uses the camera service's built-in shutter sound — no DSP firmware or
// csnd conflict; this is the same sound the official 3DS camera app plays.

int  sound_init(void) { return 0; }  // nothing to initialise
void sound_exit(void) {}             // nothing to release

void play_shutter_click(void) {
    CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
}
