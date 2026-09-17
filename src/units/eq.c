// 16-band graphic EQ (stereo).
//
// P0..P15: band gains, 00=-15dB 80=0dB(exact, bypassed) FF=+15dB
//
// One band per 2/3 octave, ISO centre frequencies from 20Hz to 20kHz — 16
// bands is both the audible span and UNIT_MAX_PARAMS, which is why each band
// gets exactly one param (its gain) and the centres are fixed. That's what
// makes this a graphic EQ rather than a parametric one: reach for FILTER when
// you want to sweep a cutoff, and this when you want to shape a spectrum.
//
// Each band is an RBJ peaking biquad and the bands are cascaded, like a
// hardware graphic EQ; Q comes from the 2/3-octave spacing, so neighbours meet
// at about -3dB. Bands are independent — boosting all 16 adds up (that's the
// design, not a bug: there's no automatic makeup gain).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define EQ_BANDS 16  // == UNIT_MAX_PARAMS: one gain per band

// ISO preferred 2/3-octave centres. Spacing is 10^(1/15) (~1.585, i.e. 2^2/3)
// between the exact series values; these are the familiar rounded ones.
static const float eq_centres[EQ_BANDS] = {20.0f, 31.5f, 50.0f, 80.0f, 125.0f, 200.0f, 315.0f, 500.0f,
                                           800.0f, 1250.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 12500.0f, 20000.0f};

// RBJ bandwidth form of Q for a 2/3-octave band: Q = 1 / (2*sinh(ln2/2 * BW)).
#define EQ_Q 2.1453f

#define EQ_MAX_DB 15.0f

// A band whose gain is within this of 0dB is skipped outright — the exact-0dB
// case (param 0x80) plus the one step either side, which is inaudible and
// cheaper to skip than to run.
#define EQ_FLAT_DB 0.05f

typedef struct {
  float b0, b1, b2, a1, a2;
  float s1l, s2l;  // transposed direct form II
  float s1r, s2r;
} EqBand;

struct UnitState {
  float sample_rate;
  EqBand bands[EQ_BANDS];
  // Coefficients are only rebuilt when a param actually changes: render() is
  // called per tick-aligned sub-block (see audio.c — that can be ONE sample),
  // so recomputing 16 biquads per call would cost more than the filtering.
  uint8_t last_params[EQ_BANDS];
  bool params_valid;
  // Indices of the non-flat bands, so a flat EQ costs nothing at all.
  int active_count;
  uint8_t active[EQ_BANDS];
};

static UnitState* eq_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}

static void eq_destroy(UnitState* s) { free(s); }

static void eq_kill(UnitState* s) {
  memset(s->bands, 0, sizeof(s->bands));
  s->active_count = 0;
  s->params_valid = false;
}

static void eq_update(UnitState* s, const uint8_t* p) {
  int active = 0;

  for (int b = 0; b < EQ_BANDS; b++) {
    float db = p2f_center(p[b], -EQ_MAX_DB, EQ_MAX_DB);
    if (fabsf(db) < EQ_FLAT_DB) {
      // Flat: clear the band's state so it comes back with a clean slate
      // rather than a stale tail from before it was flattened.
      memset(&s->bands[b], 0, sizeof(s->bands[b]));
      continue;
    }

    float fc = eq_centres[b];
    float nyquist_guard = s->sample_rate * 0.45f;
    if (fc > nyquist_guard)
      fc = nyquist_guard;

    double A = pow(10.0, db / 40.0);  // sqrt of the linear gain
    double w0 = 2.0 * M_PI * fc / s->sample_rate;
    double cw = cos(w0);
    double sw = sin(w0);
    double alpha = sw / (2.0 * EQ_Q);

    // RBJ peaking EQ, normalised by a0
    double a0 = 1.0 + alpha / A;
    s->bands[b].b0 = (float)((1.0 + alpha * A) / a0);
    s->bands[b].b1 = (float)((-2.0 * cw) / a0);
    s->bands[b].b2 = (float)((1.0 - alpha * A) / a0);
    s->bands[b].a1 = (float)((-2.0 * cw) / a0);
    s->bands[b].a2 = (float)((1.0 - alpha / A) / a0);

    s->active[active++] = (uint8_t)b;
  }

  s->active_count = active;
}

static void eq_render(UnitState* s, const uint8_t* p,
                      const float* in_l, const float* in_r,
                      float* out_l, float* out_r, uint32_t frames) {
  if (!s->params_valid || memcmp(p, s->last_params, EQ_BANDS) != 0) {
    eq_update(s, p);
    memcpy(s->last_params, p, EQ_BANDS);
    s->params_valid = true;
  }

  // Every band flat: pass through. (The chain renders effects in place, so
  // this is usually nothing at all.)
  if (s->active_count == 0) {
    if (out_l != in_l)
      memcpy(out_l, in_l, frames * sizeof(float));
    if (out_r != in_r)
      memcpy(out_r, in_r, frames * sizeof(float));
    return;
  }

  int nactive = s->active_count;

  for (uint32_t i = 0; i < frames; i++) {
    float l = in_l[i];
    float r = in_r[i];

    for (int k = 0; k < nactive; k++) {
      EqBand* bd = &s->bands[s->active[k]];
      float y = bd->b0 * l + bd->s1l;
      bd->s1l = bd->b1 * l - bd->a1 * y + bd->s2l;
      bd->s2l = bd->b2 * l - bd->a2 * y;
      l = y;

      y = bd->b0 * r + bd->s1r;
      bd->s1r = bd->b1 * r - bd->a1 * y + bd->s2r;
      bd->s2r = bd->b2 * r - bd->a2 * y;
      r = y;
    }

    out_l[i] = l;
    out_r[i] = r;
  }
}

static char eq_fmt_buf[16];
static const char* eq_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx < 0 || idx >= EQ_BANDS)
    return NULL;
  float db = p2f_center(val, -EQ_MAX_DB, EQ_MAX_DB);
  // Signed only when it isn't flat: "0.0 dB", not "+0.0 dB".
  if (db > EQ_FLAT_DB)
    snprintf(eq_fmt_buf, sizeof(eq_fmt_buf), "%+.1f dB", (double)db);
  else if (db < -EQ_FLAT_DB)
    snprintf(eq_fmt_buf, sizeof(eq_fmt_buf), "%.1f dB", (double)db);
  else
    snprintf(eq_fmt_buf, sizeof(eq_fmt_buf), "0.0 dB");
  return eq_fmt_buf;
}

const UnitDef unit_eq = {
    .id = "eq",
    .name = "EQ",
    .is_source = false,
    .num_params = EQ_BANDS,
    // Band centres in Hz — the param's own name, so the ADD row reads as a
    // frequency and its value as dB.
    .param_names = {"20", "31.5", "50", "80", "125", "200", "315", "500",
                    "800", "1250", "2000", "3150", "5000", "8000", "12500", "20000"},
    .param_defaults = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                       0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
    .format_param_val = eq_format_param,
    .create = eq_create,
    .destroy = eq_destroy,
    .kill = eq_kill,
    .render = eq_render,
};
