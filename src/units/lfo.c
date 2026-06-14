// LFO param modulator — modulates a param on any instrument each render block
// P0 RATE:  00=0.1Hz  FF=20Hz
// P1 DPTH:  00=0      FF=127 (added to/subtracted from center)
// P2 SHPE:  0=Sine 1=Square 2=Saw 3=Tri
// P3 INST:  target instrument 0-FF
// P4 PRMP:  target param (global index across chain slots, 0-FF)
// P5 CNTR:  center value for modulation (default 0x80)
// P6 ON:    0=off 1=on
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../tracker.h"
#include "unit.h"

#define TWO_PI 6.28318530718f

extern TrackerSong *g_lfo_song;

struct UnitState {
  float phase;
  float sample_rate;
};

static UnitState *lfo_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void lfo_destroy(UnitState *s) { free(s); }
static void lfo_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s; (void)n; (void)v; (void)p;
}
static void lfo_note_off(UnitState *s, uint8_t n) { (void)s; (void)n; }
static void lfo_kill(UnitState *s) { (void)s; }

// Resolve global param index to (slot, local_param) and write val
static void write_param(TrackerSong *song, int inst_idx, int global_param, uint8_t val) {
  TrackerInstrument *inst = &song->instruments[inst_idx];
  int remaining = global_param;
  for (int sl = 0; sl < CHAIN_MAX; sl++) {
    if (!inst->chain[sl].unit_id[0])
      continue;
    extern const UnitDef *unit_find(const char *id);
    const UnitDef *def = unit_find(inst->chain[sl].unit_id);
    if (!def)
      continue;
    int cnt = def->num_params;
    if (remaining < cnt) {
      inst->chain[sl].params[remaining] = val;
      return;
    }
    remaining -= cnt;
  }
}

static void lfo_render(UnitState *s, const uint8_t *p,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, uint32_t frames) {
  // Pass audio through unchanged
  if (in_l) memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r) memcpy(out_r, in_r, frames * sizeof(float));

  if (!g_lfo_song || !p[6])
    return;

  float rate  = p2f(p[0], 0.1f, 20.0f);
  float depth = p[1] / 2.0f;   // 0-127 swing each side
  int   shape = p[2] > 3 ? 3 : p[2];
  int   inst  = p[3];
  int   prm   = p[4];
  float cntr  = (float)p[5];

  float phase_inc = rate / s->sample_rate;

  // Compute LFO at midpoint of block for param update (one write per block is fine)
  float phase = s->phase + phase_inc * (frames / 2.0f);
  if (phase >= 1.0f) phase -= (float)(int)phase;

  float lfo;
  switch (shape) {
    case 0: lfo = sinf(TWO_PI * phase); break;
    case 1: lfo = (phase < 0.5f) ? 1.0f : -1.0f; break;
    case 2: lfo = 2.0f * phase - 1.0f; break;
    case 3: lfo = 1.0f - 4.0f * fabsf(phase - 0.5f); break;
    default: lfo = 0.0f;
  }

  float raw = cntr + lfo * depth;
  uint8_t val = raw < 0.0f ? 0 : raw > 255.0f ? 255 : (uint8_t)raw;
  write_param(g_lfo_song, inst, prm, val);

  // Advance phase over full block
  s->phase += phase_inc * frames;
  while (s->phase >= 1.0f) s->phase -= 1.0f;
}

static void lfo_init_params(uint8_t *params, int inst_idx) {
  params[3] = (uint8_t)(inst_idx & 0xFF);
}

static const char *const lfo_shape_names[] = {"SINE", "SQR", "SAW", "TRI"};
static const char *const lfo_on_names[] = {"OFF", "ON"};

const UnitDef unit_lfo = {
    .id = "lfo",
    .name = "LFO",
    .is_source = false,
    .num_params = 7,
    .param_names = {"RATE", "DPTH", "SHPE", "INST", "PRMP", "CNTR", "ON"},
    .param_defaults = {0x20, 0x40, 0, 0, 0, 0x80, 0},
    .param_enums = {NULL, NULL, lfo_shape_names, NULL, NULL, NULL, lfo_on_names},
    .param_enum_count = {0, 0, 4, 0, 0, 0, 2},
    .init_params = lfo_init_params,
    .create = lfo_create,
    .destroy = lfo_destroy,
    .note_on = lfo_note_on,
    .note_off = lfo_note_off,
    .kill = lfo_kill,
    .render = lfo_render,
};
