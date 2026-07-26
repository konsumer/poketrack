// Flanger effect (very short modulated delay with feedback)
// P0 RATE:  00=0.05Hz  FF=4Hz
// P1 DEPTH: 00=0%      FF=100% (of delay range)
// P2 FDBK:  00=none    FF=strong
// P3 MIX:   00=dry     FF=wet
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define FLNG_BUF 1024  // ~23ms at 44100 (power of 2)

struct UnitState {
  float buf_l[FLNG_BUF], buf_r[FLNG_BUF];
  int write;
  float lfo_l, lfo_r;
  float sample_rate;
};

static UnitState* flanger_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->lfo_r = 0.25f;
  return s;
}
static void flanger_destroy(UnitState* s) { free(s); }
static void flanger_kill(UnitState* s) {
  memset(s->buf_l, 0, sizeof(s->buf_l));
  memset(s->buf_r, 0, sizeof(s->buf_r));
}

static void flanger_render(UnitState* s, const uint8_t* p,
                           const float* in_l, const float* in_r,
                           float* out_l, float* out_r, uint32_t frames) {
  if (s->sample_rate <= 0)
    return;
  float rate = p2f(p[0], 0.05f, 4.0f);
  float depth = p2f(p[1], 0.0f, 1.0f);
  float fdbk = p2f(p[2], 0.0f, 0.95f);
  float mix = p2f(p[3], 0.0f, 1.0f);
  // Delay range: 0.5ms-5ms
  float dmin = 0.0005f * s->sample_rate;
  float dmax = 0.005f * s->sample_rate;
  float d_range = (dmax - dmin) * depth;
  float lfo_inc = rate / s->sample_rate;

  for (uint32_t f = 0; f < frames; f++) {
    float del_l = dmin + (unit_sin(s->lfo_l) * 0.5f + 0.5f) * d_range;
    float del_r = dmin + (unit_sin(s->lfo_r) * 0.5f + 0.5f) * d_range;

    float wet_l = unit_delay_read(s->buf_l, FLNG_BUF - 1, (s->write - del_l + FLNG_BUF * 2));
    float wet_r = unit_delay_read(s->buf_r, FLNG_BUF - 1, (s->write - del_r + FLNG_BUF * 2));

    s->buf_l[s->write] = in_l[f] + wet_l * fdbk;
    s->buf_r[s->write] = in_r[f] + wet_r * fdbk;

    out_l[f] = in_l[f] * (1.0f - mix) + wet_l * mix;
    out_r[f] = in_r[f] * (1.0f - mix) + wet_r * mix;

    s->write = (s->write + 1) & (FLNG_BUF - 1);
    s->lfo_l = unit_phase_advance(s->lfo_l, lfo_inc);
    s->lfo_r = unit_phase_advance(s->lfo_r, lfo_inc);
  }
}

const UnitDef unit_flanger = {
    .id = "flanger",
    .name = "FLNGR",
    .is_source = false,
    .num_params = 4,
    .param_names = {"RATE", "DPTH", "FDBK", "MIX"},
    .param_defaults = {25, 180, 100, 150},
    .create = flanger_create,
    .destroy = flanger_destroy,
    .kill = flanger_kill,
    .render = flanger_render,
};
