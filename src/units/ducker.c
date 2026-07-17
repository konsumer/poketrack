// Sidechain ducker — modulates a param on any instrument each render block,
// pulling it down from CNTR while the SRC instrument is sounding.
// P0 SRC:    sidechain source instrument 00-FF
// P1 THRESH: sidechain level below which SRC is ignored (00=any sound  FF=0.5 RMS)
// P2 REL:    00=10ms  FF=500ms
// P3 AMNT:   00=no duck  FF=full duck (down to 0 at CNTR)
// P4 CNTR:   center/unduck value for the target param (default 0x80)
// P5 INV:    0=duck when src plays  1=duck when src silent
// P6 INST:   target instrument 0-FF (defaults to self)
// P7 PARAM:  target param (global index across chain slots, 0-FF)
// P8 ON:     0=off 1=on
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../tracker.h"
#include "unit.h"

extern float g_sidechain_rms[256];
extern TrackerSong *g_lfo_song;

struct UnitState {
  float env;  // current envelope (0=silent, 1=loud)
  float sample_rate;
};

static UnitState *ducker_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->env = 0.0f;
  return s;
}
static void ducker_destroy(UnitState *s) { free(s); }
static void ducker_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s;
  (void)n;
  (void)v;
  (void)p;
}
static void ducker_note_off(UnitState *s, uint8_t n) {
  (void)s;
  (void)n;
}
static void ducker_kill(UnitState *s) { s->env = 0.0f; }

// Resolve global param index to (slot, local_param) and write val
static void ducker_write_param(TrackerSong *song, int inst_idx, int global_param, uint8_t val) {
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

static void ducker_render(UnitState *s, const uint8_t *p,
                          const float *in_l, const float *in_r,
                          float *out_l, float *out_r, uint32_t frames) {
  // Pass audio through unchanged — ducking happens by writing to a target param.
  if (in_l) memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r) memcpy(out_r, in_r, frames * sizeof(float));

  int src_inst = p[0];
  float rel_ms = p2f(p[2], 10.0f, 500.0f);
  float amount = p[3] / 255.0f;
  float cntr = (float)p[4];
  int invert = p[5] ? 1 : 0;
  int inst = p[6];
  int prm = p[7];
  int on = p[8];

  float sr = s->sample_rate;
  float rel_coef = expf(-1.0f / (sr * rel_ms * 0.001f));

  // Continuous level, not a hard gate — ramps from THRESH up to "fully
  // ducked" over a small window, so a tuned threshold catches only the
  // loud attack (e.g. a kick) and ignores its own quieter decay tail.
  float thresh = p2f(p[1], 0.0f, 0.5f);
  float sc_rms = g_sidechain_rms[src_inst];
  float target = (sc_rms - thresh) / 0.05f;
  target = target < 0.0f ? 0.0f : target > 1.0f ? 1.0f : target;

  float env = s->env;
  for (uint32_t f = 0; f < frames; f++) {
    if (target > env)
      env = target;  // instant attack
    else
      env = rel_coef * env + (1.0f - rel_coef) * target;
  }
  s->env = env;

  if (!on || amount <= 0.0f || !g_lfo_song)
    return;

  float env_eff = invert ? (1.0f - env) : env;
  float raw = cntr * (1.0f - amount * env_eff);
  uint8_t val = raw < 0.0f ? 0 : raw > 255.0f ? 255 : (uint8_t)raw;
  ducker_write_param(g_lfo_song, inst, prm, val);
}

static void ducker_init_params(uint8_t *params, int inst_idx) {
  params[6] = (uint8_t)(inst_idx & 0xFF);
}

static const char *const ducker_inv_names[] = {"NORM", "INV"};
static const char *const ducker_on_names[] = {"OFF", "ON"};

const UnitDef unit_ducker = {
    .id = "ducker",
    .name = "DUCKER",
    .is_source = false,
    .num_params = 9,
    .param_names = {"SRC", "THRESH", "REL", "AMNT", "CNTR", "INV", "INST", "PARAM", "ON"},
    .param_defaults = {0, 0, 0x40, 0xC0, 0x80, 0, 0, 0, 0},
    .param_enums = {NULL, NULL, NULL, NULL, NULL, ducker_inv_names, NULL, NULL, ducker_on_names},
    .param_enum_count = {0, 0, 0, 0, 0, 2, 0, 0, 2},
    .init_params = ducker_init_params,
    .create = ducker_create,
    .destroy = ducker_destroy,
    .note_on = ducker_note_on,
    .note_off = ducker_note_off,
    .kill = ducker_kill,
    .render = ducker_render,
};
