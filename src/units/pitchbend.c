// Bend — note modifier. Place it at the top of an instrument chain, ahead of
// the source; it shifts every incoming note by a fraction of a semitone, the
// way a MIDI pitch wheel does.
//
// One value, centred on no bend:
//   00  ->  a semitone down (landing on the note below)
//   80  ->  no bend, the written note
//   FF  ->  a semitone up (landing on the note above)
//
// Because it acts on notes rather than on a param, the bend is sampled when a
// note starts and remembered until it ends — so automating BEND per step (from
// the pattern's FX column) bends each note independently without stranding the
// ones already sounding.
//
// P0 BEND: 00=-1 semitone  80=no bend  FF=+1 semitone
#include <stdlib.h>

#include "unit.h"

#define PB_MAX_IN 16

typedef struct {
  bool used;
  float in;   // the written pitch (for note-off matching)
  float out;  // what we actually sent
} PbEntry;

struct UnitState {
  PbEntry notes[PB_MAX_IN];
};

static UnitState* pb_create(float sr) {
  (void)sr;
  return calloc(1, sizeof(UnitState));
}
static void pb_destroy(UnitState* s) { free(s); }
static void pb_kill(UnitState* s) { (void)s; }

static float pb_offset(const uint8_t* p) { return p2f_center(p[0], -1.0f, 1.0f); }

static bool pb_note_event(UnitState* s, const uint8_t* p, float pitch, uint8_t vel, bool on) {
  if (!on) {
    // Release with the bend the note actually started with, even if BEND has
    // been moved since — otherwise the source never sees its own pitch again.
    for (int i = 0; i < PB_MAX_IN; i++)
      if (s->notes[i].used && fabsf(s->notes[i].in - pitch) < 0.001f) {
        unit_note_emit(s->notes[i].out, 0, false);
        s->notes[i].used = false;
        return true;
      }
    unit_note_emit(pitch + pb_offset(p), 0, false);  // untracked: best effort
    return true;
  }

  float out = pitch + pb_offset(p);
  PbEntry* e = NULL;
  for (int i = 0; i < PB_MAX_IN; i++)
    if (s->notes[i].used && fabsf(s->notes[i].in - pitch) < 0.001f) {
      e = &s->notes[i];
      break;
    }
  if (!e)
    for (int i = 0; i < PB_MAX_IN; i++)
      if (!s->notes[i].used) {
        e = &s->notes[i];
        break;
      }
  if (e) {
    e->used = true;
    e->in = pitch;
    e->out = out;
  }
  unit_note_emit(out, vel, true);
  return true;
}

static void pb_render(UnitState* s, const uint8_t* p,
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

static char pb_fmt_buf[16];
static const char* pb_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx != 0)
    return NULL;
  snprintf(pb_fmt_buf, sizeof(pb_fmt_buf), "%+.2f st", (double)p2f_center(val, -1.0f, 1.0f));
  return pb_fmt_buf;
}

const UnitDef unit_pitchbend = {
    .id = "bend",
    .name = "BEND",
    .is_source = false,
    .role_label = "NOTE",
    .num_params = 1,
    .param_names = {"BEND"},
    .param_defaults = {0x80},
    .format_param_val = pb_format_param,
    .create = pb_create,
    .destroy = pb_destroy,
    .kill = pb_kill,
    .note_event = pb_note_event,
    .render = pb_render,
};
