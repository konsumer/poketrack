// Delay effect unit
// P0 TIME:     00=4ms  FF=1s
// P1 FEEDBACK: 00=0%   FF=95%
// P2 MIX:      00=dry  FF=wet  7F=50/50
// P3 SPREAD:   00=mono  FF=ping-pong (L/R offset)
// P4-P7: unused
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define DELAY_MAX 44100  // 1 second at 44100 Hz

struct UnitState {
  float buf_l[DELAY_MAX];
  float buf_r[DELAY_MAX];
  int write;
  float sample_rate;
};

static UnitState* delay_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void delay_destroy(UnitState* s) { free(s); }
static void delay_kill(UnitState* s) {
  memset(s->buf_l, 0, sizeof(s->buf_l));
  memset(s->buf_r, 0, sizeof(s->buf_r));
}

static void delay_render(UnitState* s, const uint8_t* p,
                         const float* in_l, const float* in_r,
                         float* out_l, float* out_r, uint32_t frames) {
  float delay_secs = p2f(p[0], 0.004f, 1.0f);
  float feedback = p2f(p[1], 0.0f, 0.95f);
  float mix = p2f(p[2], 0.0f, 1.0f);
  float spread = p2f(p[3], 0.0f, 1.0f);
  int delay_samp = (int)(delay_secs * s->sample_rate);
  if (delay_samp < 1)
    delay_samp = 1;
  if (delay_samp >= DELAY_MAX)
    delay_samp = DELAY_MAX - 1;
  // Spread: ping-pong by offsetting R delay by a small amount
  int spread_off = (int)(spread * delay_samp * 0.5f);

  for (uint32_t f = 0; f < frames; f++) {
    int read_l = (s->write - delay_samp + DELAY_MAX) % DELAY_MAX;
    int read_r = (s->write - delay_samp - spread_off + DELAY_MAX * 2) % DELAY_MAX;

    float dl = s->buf_l[read_l];
    float dr = s->buf_r[read_r];

    s->buf_l[s->write] = in_l[f] + dl * feedback;
    s->buf_r[s->write] = in_r[f] + dr * feedback;
    s->write = (s->write + 1) % DELAY_MAX;

    out_l[f] = in_l[f] * (1.0f - mix) + dl * mix;
    out_r[f] = in_r[f] * (1.0f - mix) + dr * mix;
  }
}

const UnitDef unit_delay = {
    .id = "delay",
    .name = "DELAY",
    .is_source = false,
    .num_params = 4,
    .param_names = {"TIME", "FDBK", "MIX", "SPRD"},
    .param_defaults = {64, 100, 80, 0},
    .create = delay_create,
    .destroy = delay_destroy,
    .kill = delay_kill,
    .render = delay_render,
};
