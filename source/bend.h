#ifndef BEND_H
#define BEND_H

// ---------------------------------------------------------------------------
// Circuit Bend presets — psychedelic glitch effects that simulate broken
// hardware, JPEG corruption, and colour overflow.
// ---------------------------------------------------------------------------

#include <stdint.h>

#define BEND_PRESET_COUNT 6

#define BEND_CORRUPT   0
#define BEND_MELT      1
#define BEND_SWAP      2
#define BEND_SOLARIZE  3
#define BEND_FEEDBACK  4
#define BEND_POSTERIZE 5

typedef struct {
    const char *name;
    int         id;
} BendPreset;

static const BendPreset bend_presets[BEND_PRESET_COUNT] = {
    { "Corrupt",   BEND_CORRUPT   },
    { "Overflow",  BEND_MELT      },
    { "Byteshift", BEND_SWAP      },
    { "Solarize",  BEND_SOLARIZE  },
    { "Scramble",  BEND_FEEDBACK  },
    { "Acid",      BEND_POSTERIZE },
};

// Apply the selected circuit-bend preset to an RGB888 buffer in-place.
void apply_bend(uint8_t *rgb, int w, int h, int preset_id, int frame_count);

#endif
