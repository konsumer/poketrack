// Playable instrument around the Syntakt voice loop: params, note-to-voice
// allocation, parking silent voices, and resampling from the engine's 48 kHz.

#ifndef SYNTH_H
#define SYNTH_H

#include <stdbool.h>
#include <stdint.h>

#include "machines.h"

// param ids: a few globals, then 8 knobs per machine
enum { PID_MACHINE = 0,
       PID_LEVEL,
       PID_GATE,
       PID_VOICES,
       PID_VELOCITY,
       PID_RELEASE,
       PID_GLOBAL_COUNT };
#define PID_KNOB0 16
#define PID_KNOB(machine, knob) (PID_KNOB0 + (machine) * KNOB_COUNT + (knob))
#define PARAM_COUNT (PID_GLOBAL_COUNT + MACHINE_COUNT * KNOB_COUNT)
#define RELEASE_MAX 2.0  // seconds; Release at 1.0 means no fade (note-off is ignored)

typedef struct {
  uint32_t id;
  const char* name;
  const char* module;
  double min, max, def;
  bool stepped, is_enum;
} param_info_t;

typedef struct synth synth_t;

// image: the engine (section 7), or NULL for a plugin that only outputs silence
synth_t* synth_new(const uint8_t* image, double rate);
void synth_free(synth_t* s);
bool synth_has_engine(const synth_t* s);
void synth_set_rate(synth_t* s, double rate);
void synth_reset(synth_t* s);

void synth_note_on(synth_t* s, int key, double velocity);
void synth_note_off(synth_t* s, int key);

void synth_set_param(synth_t* s, uint32_t id, double value);
double synth_get_param(const synth_t* s, uint32_t id);

void synth_render(synth_t* s, float* left, float* right, uint32_t frames);

// param list: index -> info (false past the end), and lookup by id
bool param_info(uint32_t index, param_info_t* out);
bool param_info_by_id(uint32_t id, param_info_t* out);
void param_text(uint32_t id, double value, char* out, uint32_t size);

#endif
