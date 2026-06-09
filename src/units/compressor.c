// RMS compressor effect unit
// P0 THRS: 00=-60dB  FF=0dB
// P1 RATO: 00=1:1    FF=20:1
// P2 ATK:  00=0.1ms  FF=100ms
// P3 REL:  00=10ms   FF=2000ms
// P4 GAIN: 00=0dB    FF=+24dB makeup
#include <math.h>
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float rms_l, rms_r;  // RMS estimator state
  float gain_db;       // current gain in dB (smoothed)
  float sample_rate;
};

static UnitState *comp_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->gain_db = 0.0f;
  return s;
}
static void comp_destroy(UnitState *s) { free(s); }
static void comp_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s;
  (void)n;
  (void)v;
  (void)p;
}
static void comp_note_off(UnitState *s, uint8_t n) {
  (void)s;
  (void)n;
}
static void comp_kill(UnitState *s) {
  s->rms_l = s->rms_r = 0.0f;
  s->gain_db = 0.0f;
}

static void comp_render(UnitState *s, const uint8_t *p,
                        const float *in_l, const float *in_r,
                        float *out_l, float *out_r, uint32_t frames) {
  float threshold_db = p2f(p[0], -60.0f, 0.0f);
  float ratio = p2f(p[1], 1.0f, 20.0f);
  float atk_ms = p2f(p[2], 0.1f, 100.0f);
  float rel_ms = p2f(p[3], 10.0f, 2000.0f);
  float makeup_db = p2f(p[4], 0.0f, 24.0f);

  float sr = s->sample_rate;
  // One-pole coefficient for attack and release
  float atk_coef = expf(-1.0f / (sr * atk_ms * 0.001f));
  float rel_coef = expf(-1.0f / (sr * rel_ms * 0.001f));

  // RMS estimator coefficient (approx 10ms window)
  float rms_coef = expf(-1.0f / (sr * 0.01f));

  float rms_l = s->rms_l;
  float rms_r = s->rms_r;
  float gain_db = s->gain_db;

  for (uint32_t f = 0; f < frames; f++) {
    float il = in_l[f];
    float ir = in_r[f];

    // RMS estimate
    rms_l = rms_coef * rms_l + (1.0f - rms_coef) * il * il;
    rms_r = rms_coef * rms_r + (1.0f - rms_coef) * ir * ir;
    float rms = sqrtf(0.5f * (rms_l + rms_r));

    // Level in dB
    float level_db;
    if (rms < 1e-9f)
      level_db = -180.0f;
    else
      level_db = 20.0f * log10f(rms);

    // Compute target gain reduction in dB
    float target_db = 0.0f;
    if (level_db > threshold_db) {
      target_db = threshold_db + (level_db - threshold_db) / ratio - level_db;
    }

    // Smooth with attack/release
    float coef = (target_db < gain_db) ? atk_coef : rel_coef;
    gain_db = coef * gain_db + (1.0f - coef) * target_db;

    float total_db = gain_db + makeup_db;
    float gain_lin = powf(10.0f, total_db / 20.0f);

    out_l[f] = il * gain_lin;
    out_r[f] = ir * gain_lin;
  }

  s->rms_l = rms_l;
  s->rms_r = rms_r;
  s->gain_db = gain_db;
}

const UnitDef unit_compressor = {
    .id = "comp",
    .name = "COMP",
    .is_source = false,
    .num_params = 5,
    .param_names = {"THRS", "RATO", "ATK", "REL", "GAIN"},
    .param_defaults = {0xC0, 0x40, 0x20, 0x60, 0x80},
    .create = comp_create,
    .destroy = comp_destroy,
    .note_on = comp_note_on,
    .note_off = comp_note_off,
    .kill = comp_kill,
    .render = comp_render,
};
