// The Syntakt's digital voice loop, run on the ColdFire interpreter.
// Contract decoded by Modded-Cycles (notes/16, tools/emu/stengine.py).

#ifndef ENGINE_H
#define ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "cf.h"

#define ST_VOICES 8
#define ST_FRAMES 32  // per block, at 48 kHz
#define ST_RATE 48000
#define ST_IMAGE_SIZE 383760  // section 7 of Syntakt OS 1.41/1.42

// machine types (index into the Syntakt's machine names)
enum {
  ST_BD_MODERN = 0,
  ST_SD_BASIC = 1,
  ST_CY_ALLOY = 2,
  ST_PC_CARBON = 3,
  ST_SY_TONE = 4,
  ST_SY_CHORD = 5,
  ST_SD_VINTAGE = 6,
  ST_CP_VINTAGE = 7,
  ST_SY_TOY = 36,
  ST_SY_BITS = 37,
  ST_SY_SWARM = 38
};

// parameter slots: 8.8 words at params + 0x1c + 2*slot
enum { ST_TUNE = 18,
       ST_P1 = 19,
       ST_P2 = 20,
       ST_P3 = 21,
       ST_P4 = 22,
       ST_PUNCH = 23,
       ST_GATE = 24,
       ST_DECAY = 25,
       ST_OVER = 26 };

typedef struct st_engine {
  cf_cpu* cpu;
  uint8_t params[ST_VOICES][142];
  int32_t pmod[ST_VOICES];
} st_engine;

// image = section 7; returns NULL on error
st_engine* st_new(const uint8_t* image, size_t len);
void st_free(st_engine* e);

void st_machine(st_engine* e, int track, int type);
void st_param(st_engine* e, int track, int slot, uint16_t value_8_8);
void st_note(st_engine* e, int track, int32_t semis_16_16);

// stop computing a voice until its next trig (its current engine is set out of
// range, which the voice loop skips; a trig brings back the track's machine)
void st_park(st_engine* e, int track);

// one block for all 8 voices; returns cf error (0 = ok)
int st_block(st_engine* e, uint32_t trig_mask, uint32_t release_mask, int32_t out[ST_VOICES][ST_FRAMES]);

#endif
