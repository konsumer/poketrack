// Sidechain volume ducker effect unit
// P0 AMNT: 00=no duck  FF=full duck
// P1 SRC:  instrument index 00-FF
// P2 REL:  00=10ms  FF=500ms
// P3 INV:  0=duck when src plays  1=duck when src silent
#include <math.h>
#include <stdlib.h>

#include "unit.h"

extern float g_sidechain_rms[256];

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
  float amount = p[0] / 255.0f;
  int src_inst = p[1];  // instrument index 0-255
  float rel_ms = p2f(p[2], 10.0f, 500.0f);
  int invert = p[3] ? 1 : 0;

  float sr = s->sample_rate;
  float rel_coef = expf(-1.0f / (sr * rel_ms * 0.001f));

  // Get sidechain level
  float sc_rms = g_sidechain_rms[src_inst];

  // Target env: 1 when sidechain is active, 0 when silent
  float target = (sc_rms > 0.001f) ? 1.0f : 0.0f;

  float env = s->env;

  for (uint32_t f = 0; f < frames; f++) {
    // Smooth envelope with release (fast attack implicit via no attack coeff)
    if (target > env)
      env = target;  // instant attack
    else
      env = rel_coef * env + (1.0f - rel_coef) * target;

    // duck_amount: when env=1 and invert=0, gain = 1-amount; when invert=1, reversed
    float duck;
    if (!invert)
      duck = 1.0f - amount * env;  // duck when src plays
    else
      duck = 1.0f - amount * (1.0f - env);  // duck when src is silent

    out_l[f] = in_l[f] * duck;
    out_r[f] = in_r[f] * duck;
  }

  s->env = env;
}

static const char *const ducker_inv_names[] = {"NORM", "INV"};

const UnitDef unit_ducker = {
    .id = "ducker",
    .name = "DUCKER",
    .is_source = false,
    .num_params = 4,
    .param_names = {"AMNT", "SRC", "REL", "INV"},
    .param_defaults = {0xC0, 0, 0x40, 0},
    .param_enums = {NULL, NULL, NULL, ducker_inv_names},
    .param_enum_count = {0, 0, 0, 2},
    .create = ducker_create,
    .destroy = ducker_destroy,
    .note_on = ducker_note_on,
    .note_off = ducker_note_off,
    .kill = ducker_kill,
    .render = ducker_render,
};
