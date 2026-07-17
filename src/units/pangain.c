// Pan / gain utility effect
// P0 PAN:  00=L    80=center     FF=R
// P1 GAIN: 00=mute 80=no change  FF=+6dB
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float sample_rate;
};

static UnitState *pangain_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void pangain_destroy(UnitState *s) { free(s); }
static void pangain_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s;
  (void)n;
  (void)v;
  (void)p;
}
static void pangain_note_off(UnitState *s, uint8_t n) {
  (void)s;
  (void)n;
}
static void pangain_kill(UnitState *s) { (void)s; }

static void pangain_render(UnitState *s, const uint8_t *p,
                           const float *in_l, const float *in_r,
                           float *out_l, float *out_r, uint32_t frames) {
  (void)s;
  float pan = p2f(p[0], -1.0f, 1.0f);
  float gain = p2f(p[1], 0.0f, 2.0f);

  float gl = (pan <= 0.0f ? 1.0f : 1.0f - pan) * gain;
  float gr = (pan >= 0.0f ? 1.0f : 1.0f + pan) * gain;

  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] = in_l[f] * gl;
    out_r[f] = in_r[f] * gr;
  }
}

const UnitDef unit_pangain = {
    .id = "pangain",
    .name = "PAN/GAIN",
    .is_source = false,
    .num_params = 2,
    .param_names = {"PAN", "GAIN"},
    .param_defaults = {0x80, 0x80},
    .create = pangain_create,
    .destroy = pangain_destroy,
    .note_on = pangain_note_on,
    .note_off = pangain_note_off,
    .kill = pangain_kill,
    .render = pangain_render,
};
