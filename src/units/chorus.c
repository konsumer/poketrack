// Chorus effect
// P0 RATE:  00=0.1Hz  FF=5Hz  (LFO speed)
// P1 DEPTH: 00=0ms    FF=5ms  (modulation depth)
// P2 DELAY: 00=5ms    FF=40ms (base delay)
// P3 MIX:   00=dry    FF=wet
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define BUF_SIZE 8192  // ~186ms at 44100 Hz (power of 2)

struct UnitState {
  float buf_l[BUF_SIZE];
  float buf_r[BUF_SIZE];
  int write;
  float lfo_l, lfo_r;  // LFO phase 0-1 (R is 90° behind)
  float sample_rate;
};

static UnitState* chorus_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->lfo_r = 0.25f;  // 90° offset
  return s;
}
static void chorus_destroy(UnitState* s) { free(s); }
static void chorus_kill(UnitState* s) {
  memset(s->buf_l, 0, sizeof(s->buf_l));
  memset(s->buf_r, 0, sizeof(s->buf_r));
}

static float read_interp(float* buf, float pos) {
  int i0 = (int)pos & (BUF_SIZE - 1);
  int i1 = (i0 + 1) & (BUF_SIZE - 1);
  float fr = pos - (int)pos;
  return buf[i0] * (1.0f - fr) + buf[i1] * fr;
}

static void chorus_render(UnitState* s, const uint8_t* p,
                          const float* in_l, const float* in_r,
                          float* out_l, float* out_r, uint32_t frames) {
  if (s->sample_rate <= 0)
    return;
  float rate = p2f(p[0], 0.1f, 5.0f);
  float depth = p2f(p[1], 0.0f, 0.005f) * s->sample_rate;  // in samples
  float delay = p2f(p[2], 0.005f, 0.04f) * s->sample_rate;
  float mix = p2f(p[3], 0.0f, 1.0f);
  float lfo_inc = rate / s->sample_rate;

  for (uint32_t f = 0; f < frames; f++) {
    s->buf_l[s->write] = in_l[f];
    s->buf_r[s->write] = in_r[f];

    float mod_l = unit_sin(s->lfo_l) * depth;
    float mod_r = unit_sin(s->lfo_r) * depth;
    float pos_l = (s->write - delay + mod_l + BUF_SIZE * 2) - 1;
    float pos_r = (s->write - delay + mod_r + BUF_SIZE * 2) - 1;

    float wet_l = read_interp(s->buf_l, pos_l);
    float wet_r = read_interp(s->buf_r, pos_r);

    out_l[f] = in_l[f] * (1.0f - mix) + wet_l * mix;
    out_r[f] = in_r[f] * (1.0f - mix) + wet_r * mix;

    s->write = (s->write + 1) & (BUF_SIZE - 1);
    s->lfo_l += lfo_inc;
    if (s->lfo_l >= 1.0f)
      s->lfo_l -= 1.0f;
    s->lfo_r += lfo_inc;
    if (s->lfo_r >= 1.0f)
      s->lfo_r -= 1.0f;
  }
}

const UnitDef unit_chorus = {
    .id = "chorus",
    .name = "CHORUS",
    .is_source = false,
    .num_params = 4,
    .param_names = {"RATE", "DPTH", "DLY", "MIX"},
    .param_defaults = {30, 80, 60, 128},
    .create = chorus_create,
    .destroy = chorus_destroy,
    .kill = chorus_kill,
    .render = chorus_render,
};
