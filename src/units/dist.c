// Distortion effect unit
// P0 DRIVE: 00=clean  FF=hard clip
// P1 TONE:  00=dark   FF=bright (post-dist low-pass cutoff)
// P2 PRE:   input gain into the waveshaper (00=silent  40=1x  FF=4x)
// P3 POST:  output/makeup gain (00=silent  80=1x  FF=2x)
// P4-P7: unused
#include <math.h>
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float lp_l, lp_r;  // low-pass filter state
  float sample_rate;
};

static UnitState *dist_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void dist_destroy(UnitState *s) { free(s); }
static void dist_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s;
  (void)n;
  (void)v;
  (void)p;
}
static void dist_note_off(UnitState *s, uint8_t n) {
  (void)s;
  (void)n;
}
static void dist_kill(UnitState *s) { s->lp_l = s->lp_r = 0; }

static float waveshape(float x, float drive) {
  // Soft clip at low drive, hard clip at high drive
  if (drive < 0.5f) {
    float k = drive * 2.0f;
    return tanhf(x * (1.0f + k * 4.0f)) / tanhf(1.0f + k * 4.0f);
  } else {
    float k = (drive - 0.5f) * 2.0f;
    float gain = 1.0f + k * 20.0f;
    float clipped = x * gain;
    if (clipped > 1.0f)
      clipped = 1.0f;
    if (clipped < -1.0f)
      clipped = -1.0f;
    return clipped;
  }
}

static void dist_render(UnitState *s, const uint8_t *p,
                        const float *in_l, const float *in_r,
                        float *out_l, float *out_r, uint32_t frames) {
  float drive = p2f(p[0], 0.0f, 1.0f);
  float tone = p2f(p[1], 0.02f, 1.0f);  // LP cutoff normalized
  float pre = p2f(p[2], 0.0f, 4.0f);    // input gain into shaper
  float post = p2f(p[3], 0.0f, 2.0f);   // output/makeup gain

  // Low-pass coefficient
  float lp_c = 1.0f - expf(-6.28318f * tone * 8000.0f / s->sample_rate);

  for (uint32_t f = 0; f < frames; f++) {
    float dl = waveshape(in_l[f] * pre, drive);
    float dr = waveshape(in_r[f] * pre, drive);
    s->lp_l += lp_c * (dl - s->lp_l);
    s->lp_r += lp_c * (dr - s->lp_r);
    out_l[f] = s->lp_l * post;
    out_r[f] = s->lp_r * post;
  }
}

const UnitDef unit_dist = {
    .id = "dist",
    .name = "DIST",
    .is_source = false,
    .num_params = 4,
    .param_names = {"DRIV", "TONE", "PRE", "POST"},
    .param_defaults = {80, 180, 64, 128},
    .create = dist_create,
    .destroy = dist_destroy,
    .note_on = dist_note_on,
    .note_off = dist_note_off,
    .kill = dist_kill,
    .render = dist_render,
};
