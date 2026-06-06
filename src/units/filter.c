// Stereo SVF (State Variable Filter) effect unit
// P0 MODE:  0=LP 1=HP 2=BP 3=Notch
// P1 CUTF:  00=20Hz  FF=20kHz (log scale)
// P2 RESO:  00=Q0.5  FF=Q8.0
#include <math.h>
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float low_l, band_l;
  float low_r, band_r;
  float sample_rate;
};

static UnitState *filter_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void filter_destroy(UnitState *s) { free(s); }
static void filter_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s; (void)n; (void)v; (void)p;
}
static void filter_note_off(UnitState *s, uint8_t n) { (void)s; (void)n; }
static void filter_kill(UnitState *s) {
  s->low_l = s->band_l = s->low_r = s->band_r = 0.0f;
}

static void filter_render(UnitState *s, const uint8_t *p,
                          const float *in_l, const float *in_r,
                          float *out_l, float *out_r, uint32_t frames) {
  int mode = p[0];
  if (mode > 3) mode = 3;

  // Log-scale cutoff: 20Hz to 20kHz
  float t = p[1] / 255.0f;
  float fc = 20.0f * powf(1000.0f, t);  // 20 * 1000^t gives 20..20000

  float sr = s->sample_rate;
  float max_fc = sr * 0.49f;
  if (fc > max_fc) fc = max_fc;

  float q = p2f(p[2], 0.5f, 8.0f);
  float damp = 1.0f / q;  // Chamberlin SVF uses 1/Q as damping coefficient

  float f = 2.0f * sinf((float)M_PI * fc / sr);
  // Exact Schur-Cohn bound for Chamberlin SVF: f^2 + 2*f*damp < 4
  // Solving: max_f = sqrt(damp^2 + 4) - damp
  float max_f = (sqrtf(damp * damp + 4.0f) - damp) * 0.99f;
  if (f > max_f) f = max_f;

  float low_l = s->low_l;
  float band_l = s->band_l;
  float low_r = s->low_r;
  float band_r = s->band_r;

  // Recover from corrupted state (NaN/Inf from prior instability)
  if (!isfinite(low_l) || !isfinite(band_l)) { low_l = band_l = 0.0f; }
  if (!isfinite(low_r) || !isfinite(band_r)) { low_r = band_r = 0.0f; }

  for (uint32_t i = 0; i < frames; i++) {
    float inl = in_l[i];
    float inr = in_r[i];

    // Chamberlin SVF
    float high_l = inl - damp * band_l - low_l;
    band_l = f * high_l + band_l;
    low_l  = f * band_l + low_l;
    float notch_l = high_l + low_l;

    float high_r = inr - damp * band_r - low_r;
    band_r = f * high_r + band_r;
    low_r  = f * band_r + low_r;
    float notch_r = high_r + low_r;

    switch (mode) {
      case 0: out_l[i] = low_l;    out_r[i] = low_r;    break;
      case 1: out_l[i] = high_l;   out_r[i] = high_r;   break;
      case 2: out_l[i] = band_l;   out_r[i] = band_r;   break;
      case 3: out_l[i] = notch_l;  out_r[i] = notch_r;  break;
    }
  }

  s->low_l  = low_l;
  s->band_l = band_l;
  s->low_r  = low_r;
  s->band_r = band_r;
}

static const char * const filter_mode_names[] = {"LP", "HP", "BP", "NOTCH"};

const UnitDef unit_filter = {
    .id = "filter",
    .name = "FILTER",
    .is_source = false,
    .num_params = 3,
    .param_names = {"MODE", "CUTF", "RESO"},
    .param_defaults = {0, 0x60, 0x20},
    .param_enums = {filter_mode_names},
    .param_enum_count = {4},
    .create = filter_create,
    .destroy = filter_destroy,
    .note_on = filter_note_on,
    .note_off = filter_note_off,
    .kill = filter_kill,
    .render = filter_render,
};
