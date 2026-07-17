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

static void ducker_render(UnitState *s, const uint8_t *p,
                          const float *in_l, const float *in_r,
                          float *out_l, float *out_r, uint32_t frames) {
  // Pass audio through unchanged — ducking happens by writing to a target param.
  if (in_l && out_l != in_l) memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r && out_r != in_r) memcpy(out_r, in_r, frames * sizeof(float));

  // Continuous level, not a hard gate — ramps from THRESH up to "fully
  // ducked" over a small window, so a tuned threshold catches only the
  // loud attack (e.g. a kick) and ignores its own quieter decay tail.
  float thresh = p2f(p[1], 0.0f, 0.5f);
  float target = (g_sidechain_rms[p[0]] - thresh) / 0.05f;
  target = target < 0.0f ? 0.0f : target > 1.0f ? 1.0f : target;

  if (target >= s->env) {
    s->env = target;  // instant attack
  } else {
    // Exponential release toward target, one step covering the whole block
    float rel_ms = p2f(p[2], 10.0f, 500.0f);
    float decay = expf(-(float)frames / (s->sample_rate * rel_ms * 0.001f));
    s->env = target + (s->env - target) * decay;
  }

  if (!p[8] || !p[3] || !g_lfo_song)  // ON; AMNT=0 also means "don't touch the target"
    return;

  float env_eff = p[5] ? (1.0f - s->env) : s->env;  // INV
  float amount = p[3] / 255.0f;
  float raw = (float)p[4] * (1.0f - amount * env_eff);  // CNTR pulled down
  uint8_t val = raw < 0.0f ? 0 : raw > 255.0f ? 255 : (uint8_t)raw;
  tracker_set_global_param(g_lfo_song, p[6], p[7], val);  // INST, PARAM
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
