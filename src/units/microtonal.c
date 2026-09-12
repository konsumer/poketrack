// Microtonal scaler — note modifier. Place it at the top of an instrument
// chain, ahead of the source.
//
// A tracker note means a *step of a scale*, not a semitone. With STEPS = 24
// each note is a quarter tone (50c) and the note field's ten-odd octaves cover
// only five — the trade you make to address pitches *between* the twelve
// western notes. This is the tracker-native stand-in for the usual MIDI trick
// of playing a note plus a pitch bend: instead of bending, you write the fine
// step directly.
//
//   pitch = ROOT + (note - ROOT) * 12 / STEPS
//
// ROOT is the one note that keeps its written pitch (the anchor); everything
// is measured from it. STEPS = 12 is a passthrough, so the modifier is inert
// until you pick a finer division.
//
// Worked example, ROOT = C4 (60), STEPS = 24:
//   note 60 -> C4 (261.6 Hz)
//   note 61 -> C4 + 50c          (between C and C#, impossible in 12-EDO)
//   note 62 -> C4 + 100c (C#4)
//   note 72 -> C4 + 600c (F#4)   (a western note 72 would be C5)
//
// P0 ON:     OFF passes notes through untouched
// P1 STEPS:  notes per octave — 12 is bypass, higher is finer
// P2 ROOT:   the note that plays its written pitch (anchor), 0-127
#include <stdlib.h>

#include "unit.h"

// Stateless — the mapping is pure — but every instance still needs its own
// handle, so the state is just an allocation.
struct UnitState {
  int unused;
};

// Notes per octave. 12 is the western grid; the rest are equal divisions that
// put playable pitches between the semitones. 19 and 31 are the usual
// meantone/neutral scales; 24/36/48/72/96 are quarter-tone and finer.
static const char* const mt_steps_names[] = {"12", "17", "19", "22", "24",
                                             "31", "36", "48", "53", "72", "96"};
static const int mt_steps_vals[] = {12, 17, 19, 22, 24, 31, 36, 48, 53, 72, 96};
#define MT_NSTEPS ((int)(sizeof(mt_steps_names) / sizeof(mt_steps_names[0])))

static UnitState* mt_create(float sr) {
  (void)sr;
  return calloc(1, sizeof(UnitState));
}
static void mt_destroy(UnitState* s) { free(s); }
static void mt_kill(UnitState* s) { (void)s; }

static bool mt_note_event(UnitState* s, const uint8_t* p, float pitch, uint8_t vel, bool on) {
  (void)s;
  if (!p[0])
    return false;  // OFF — hand notes through untouched

  int steps = mt_steps_vals[p[1] < MT_NSTEPS ? p[1] : 4];
  float root = (float)(p[2] > 127 ? 127 : p[2]);
  float out = root + (pitch - root) * 12.0f / (float)steps;
  unit_note_emit(out, vel, on);
  return true;
}

static void mt_render(UnitState* s, const uint8_t* p,
                      const float* in_l, const float* in_r,
                      float* out_l, float* out_r, uint32_t frames) {
  (void)s;
  (void)p;
  // Pure pass-through: the unit only rewrites notes.
  if (in_l && out_l != in_l)
    memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r && out_r != in_r)
    memcpy(out_r, in_r, frames * sizeof(float));
}

static const char* const mt_on_names[] = {"OFF", "ON"};

static char mt_fmt_buf[8];
static const char* mt_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx == 2) {  // ROOT — show the note number, not a hex byte
    snprintf(mt_fmt_buf, sizeof(mt_fmt_buf), "%d", val > 127 ? 127 : val);
    return mt_fmt_buf;
  }
  return NULL;
}

const UnitDef unit_microtonal = {
    .id = "micro",
    .name = "MICRO",
    .is_source = false,
    .role_label = "NOTE",
    .num_params = 3,
    .param_names = {"ON", "STEPS", "ROOT"},
    .param_defaults = {1, 4, 60},  // ON, 24 steps/octave, anchored at C4
    .param_enums = {mt_on_names, mt_steps_names, NULL},
    .param_enum_count = {2, MT_NSTEPS, 0},
    .format_param_val = mt_format_param,
    .create = mt_create,
    .destroy = mt_destroy,
    .kill = mt_kill,
    .note_event = mt_note_event,
    .render = mt_render,
};
