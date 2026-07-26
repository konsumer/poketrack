// Phaser effect (allpass cascade with LFO)
// P0 RATE:   00=0.1Hz   FF=6Hz
// P1 DEPTH:  00=narrow  FF=wide (sweep range)
// P2 STAGES: 00=2       FF=8 (allpass stages, mapped to 2/4/6/8)
// P3 FDBK:   00=none    FF=strong resonance
// P4 MIX:    00=dry     FF=wet
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define MAX_STAGES 8

typedef struct {
  float x1, y1;
} AP1;  // 1st-order all-pass state

struct UnitState {
  AP1 apl[MAX_STAGES], apr[MAX_STAGES];
  float lfo;
  float fdbk_l, fdbk_r;
  float sample_rate;
};

static UnitState* phaser_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void phaser_destroy(UnitState* s) { free(s); }
static void phaser_kill(UnitState* s) {
  memset(s->apl, 0, sizeof(s->apl));
  memset(s->apr, 0, sizeof(s->apr));
  s->lfo = s->fdbk_l = s->fdbk_r = 0;
  // sample_rate intentionally preserved
}

// 1st-order all-pass: a = (tan(pi*fc/sr)-1)/(tan(pi*fc/sr)+1)
static inline float ap_tick1(AP1* ap, float x, float a) {
  float y = a * x + ap->x1 - a * ap->y1;
  ap->x1 = x;
  ap->y1 = y;
  return y;
}

static void phaser_render(UnitState* s, const uint8_t* p,
                          const float* in_l, const float* in_r,
                          float* out_l, float* out_r, uint32_t frames) {
  if (s->sample_rate <= 0)
    return;
  float rate = p2f(p[0], 0.1f, 6.0f);
  float depth = p2f(p[1], 0.0f, 1.0f);
  int stages = 2 + (p[2] / 64) * 2;  // 2,4,6,8
  if (stages > MAX_STAGES)
    stages = MAX_STAGES;
  float fdbk = p2f(p[3], 0.0f, 0.9f);
  float mix = p2f(p[4], 0.0f, 1.0f);
  float lfo_inc = rate / s->sample_rate;
  // Sweep: 100Hz to 4000Hz
  float f_lo = 100.0f / s->sample_rate;
  float f_hi = 4000.0f / s->sample_rate;

  float a = 0.0f;
  for (uint32_t f = 0; f < frames; f++) {
    // Coefficient at control rate — the sweep is <=6Hz, tanf per sample is waste
    if ((f & 15) == 0) {
      float lfo_val = (unit_sin(s->lfo) * 0.5f + 0.5f) * depth;
      float fc = f_lo + lfo_val * (f_hi - f_lo);
      float t = tanf((float)M_PI * fc);
      a = (t - 1.0f) / (t + 1.0f);
    }

    float xl = in_l[f] + s->fdbk_l * fdbk;
    float xr = in_r[f] + s->fdbk_r * fdbk;
    for (int i = 0; i < stages; i++) {
      xl = ap_tick1(&s->apl[i], xl, a);
      xr = ap_tick1(&s->apr[i], xr, a);
    }
    s->fdbk_l = xl;
    s->fdbk_r = xr;

    out_l[f] = in_l[f] * (1.0f - mix) + xl * mix;
    out_r[f] = in_r[f] * (1.0f - mix) + xr * mix;

    s->lfo = unit_phase_advance(s->lfo, lfo_inc);
  }
}

const UnitDef unit_phaser = {
    .id = "phaser",
    .name = "PHASR",
    .is_source = false,
    .num_params = 5,
    .param_names = {"RATE", "DPTH", "STGS", "FDBK", "MIX"},
    .param_defaults = {25, 200, 64, 60, 150},
    .create = phaser_create,
    .destroy = phaser_destroy,
    .kill = phaser_kill,
    .render = phaser_render,
};
