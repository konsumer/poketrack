// Chord — note modifier. Place it at the top of an instrument chain, ahead of
// the source, and it turns every incoming note into a chord.
//
// It exists to feed ARPEGGIATOR: a tracker track is monophonic (each note
// releases the last), so an arpeggiator behind it would only ever see one note
// at a time. CHORD -> ARP gives it a real chord to run through.
//
// The intervals are in semitones above the played note. INV moves that many of
// the lowest chord tones up an octave, which is how you get inversions and
// smoother arpeggio lines out of one chord shape.
//
// P0 ON:   OFF passes notes through untouched
// P1 TYPE: chord shape
// P2 INV:  0-3, number of chord tones to lift an octave
#include <stdlib.h>

#include "unit.h"

#define CH_MAX_IN 16
#define CH_MAX_OUT 8

typedef struct {
  bool used;
  float in;  // the incoming pitch (for note-off matching)
  int count;
  float out[CH_MAX_OUT];
} ChEntry;

struct UnitState {
  ChEntry notes[CH_MAX_IN];
};

// Semitone offsets above the root; -1 terminates.
static const int ch_iv[][4] = {
    {0, 4, 7, -1},    // MAJ
    {0, 3, 7, -1},    // MIN
    {0, 4, 7, 10},    // DOM7
    {0, 3, 7, 10},    // MIN7
    {0, 4, 7, 11},    // MAJ7
    {0, 5, 7, -1},    // SUS4
    {0, 3, 6, -1},    // DIM
    {0, 4, 8, -1},    // AUG
    {0, 7, -1, -1},   // P5
    {0, 12, -1, -1},  // OCT
};
#define CH_NTYPES ((int)(sizeof(ch_iv) / sizeof(ch_iv[0])))

static UnitState* ch_create(float sr) {
  (void)sr;
  return calloc(1, sizeof(UnitState));
}
static void ch_destroy(UnitState* s) { free(s); }
static void ch_kill(UnitState* s) { (void)s; }

static void ch_release(UnitState* s, ChEntry* e) {
  for (int i = 0; i < e->count; i++)
    unit_note_emit(e->out[i], 0, false);
  e->used = false;
}

static bool ch_note_event(UnitState* s, const uint8_t* p, float pitch, uint8_t vel, bool on) {
  if (!p[0]) {  // OFF — hand notes through untouched
    for (int i = 0; i < CH_MAX_IN; i++)
      if (s->notes[i].used)
        ch_release(s, &s->notes[i]);
    return false;
  }

  if (!on) {
    for (int i = 0; i < CH_MAX_IN; i++)
      if (s->notes[i].used && fabsf(s->notes[i].in - pitch) < 0.001f)
        ch_release(s, &s->notes[i]);
    return true;
  }

  ChEntry* e = NULL;
  for (int i = 0; i < CH_MAX_IN; i++)
    if (s->notes[i].used && fabsf(s->notes[i].in - pitch) < 0.001f) {
      e = &s->notes[i];
      break;
    }
  if (!e)
    for (int i = 0; i < CH_MAX_IN; i++)
      if (!s->notes[i].used) {
        e = &s->notes[i];
        break;
      }
  if (!e)
    return true;  // no room to track it; swallow rather than leak a raw note

  const int* iv = ch_iv[p[1] < CH_NTYPES ? p[1] : 0];
  int inv = p[2] * 4 / 256;
  e->used = true;
  e->in = pitch;
  e->count = 0;
  for (int i = 0; i < CH_MAX_OUT && iv[i] >= 0; i++) {
    float out = pitch + (float)iv[i] + (i < inv ? 12.0f : 0.0f);
    e->out[e->count++] = out;
    unit_note_emit(out, vel, true);
  }
  return true;
}

static void ch_render(UnitState* s, const uint8_t* p,
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

static const char* const ch_on_names[] = {"OFF", "ON"};
static const char* const ch_type_names[] = {"MAJ", "MIN", "DOM7", "MIN7", "MAJ7",
                                            "SUS4", "DIM", "AUG", "P5", "OCT"};

static char ch_fmt_buf[8];
static const char* ch_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx == 2) {  // INV — show the rotation, not a hex byte
    snprintf(ch_fmt_buf, sizeof(ch_fmt_buf), "%d", val * 4 / 256);
    return ch_fmt_buf;
  }
  return NULL;
}

const UnitDef unit_chord = {
    .id = "chord",
    .name = "CHORD",
    .is_source = false,
    .role_label = "NOTE",
    .num_params = 3,
    .param_names = {"ON", "TYPE", "INV"},
    .param_defaults = {1, 0, 0},
    .param_enums = {ch_on_names, ch_type_names, NULL},
    .param_enum_count = {2, CH_NTYPES, 0},
    .format_param_val = ch_format_param,
    .create = ch_create,
    .destroy = ch_destroy,
    .kill = ch_kill,
    .note_event = ch_note_event,
    .render = ch_render,
};
