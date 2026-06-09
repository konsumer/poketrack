// Synthesized drum source unit
// P0 TYPE:  00-63=kick  64-127=snare  128-191=hihat-c  192-255=hihat-o
// P1 DECAY: 00=short  FF=long
// P2 TONE:  00=dark(sine)  FF=bright(noise)
// P3 PUNCH: 00=soft  FF=punchy
// P4 VOL:   00=silent  FF=full
// pitch derived from note (C4=normal)
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

struct UnitState {
  bool active;
  int type;  // 0=kick 1=snare 2=hihat-c 3=hihat-o
  float phase;
  float pitch_start, pitch_end, pitch_tau, pitch_t;
  float amp, amp_tau;
  float tone, vol;
  uint32_t noise_seed;
  float sample_rate;
};

static uint32_t lcg(uint32_t *s) {
  *s = *s * 1664525u + 1013904223u;
  return *s;
}

static UnitState *drum_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void drum_destroy(UnitState *s) { free(s); }

static void drum_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  s->type = p[0] < 4 ? p[0] : 3;
  float pitch_m = powf(2.0f, (note - 60) / 12.0f);
  float decay_t = p2f(p[1], 0.02f, 0.9f);
  s->tone = p2f(p[2], 0.0f, 1.0f);
  float punch = p2f(p[3], 0.8f, 3.0f);
  s->vol = p2f(p[4], 0.0f, 1.0f) * (vel / 127.0f);
  s->phase = 0;
  s->amp = punch;
  s->noise_seed = 99991u;
  s->pitch_t = 0;
  s->active = true;

  switch (s->type) {
    case 0:  // kick
      s->pitch_start = 150.0f * pitch_m;
      s->pitch_end = 42.0f * pitch_m;
      s->pitch_tau = 0.04f;
      s->amp_tau = decay_t;
      break;
    case 1:  // snare
      s->pitch_start = s->pitch_end = 180.0f * pitch_m;
      s->pitch_tau = 999.0f;
      s->amp_tau = decay_t * 0.5f;
      break;
    case 2:  // hihat closed
      s->pitch_start = s->pitch_end = 9000.0f;
      s->pitch_tau = 999.0f;
      s->amp_tau = decay_t * 0.15f;
      break;
    case 3:  // hihat open
      s->pitch_start = s->pitch_end = 7500.0f;
      s->pitch_tau = 999.0f;
      s->amp_tau = decay_t * 0.7f;
      break;
  }
}
static void drum_note_off(UnitState *s, uint8_t note) {
  (void)s;
  (void)note;
}
static void drum_kill(UnitState *s) { s->active = false; }

static void drum_render(UnitState *s, const uint8_t *p,
                        const float *in_l, const float *in_r,
                        float *out_l, float *out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  (void)p;
  if (!s->active)
    return;
  float dt = 1.0f / s->sample_rate;

  for (uint32_t f = 0; f < frames; f++) {
    if (!s->active)
      break;
    s->pitch_t += dt;
    float pitch = s->pitch_end + (s->pitch_start - s->pitch_end) * expf(-s->pitch_t / s->pitch_tau);
    s->amp -= s->amp * (dt / s->amp_tau);
    if (s->amp < 0.0001f) {
      s->amp = 0;
      s->active = false;
      break;
    }

    float samp = 0;
    if (s->type == 0) {
      samp = sinf(s->phase * 6.28318f);
    } else if (s->type == 1) {
      float tone_s = sinf(s->phase * 6.28318f);
      float noise_s = (float)(int32_t)lcg(&s->noise_seed) / 2147483648.0f;
      samp = tone_s * (1.0f - s->tone) + noise_s * s->tone;
    } else {
      samp = (float)(int32_t)lcg(&s->noise_seed) / 2147483648.0f;
    }
    s->phase += pitch / s->sample_rate;
    if (s->phase >= 1.0f)
      s->phase -= 1.0f;

    float out = tanhf(samp * s->amp) * s->vol;
    out_l[f] += out;
    out_r[f] += out;
  }
}

static const char *const drum_type_names[] = {"KICK", "SNARE", "HIHAT", "HIHAT-O"};

const UnitDef unit_drum = {
    .id = "drum",
    .name = "DRUM",
    .is_source = true,
    .num_params = 5,
    .param_names = {"TYPE", "DECAY", "TONE", "PUNCH", "VOL"},
    .param_defaults = {0, 110, 80, 160, 200},
    .param_enums = {drum_type_names},
    .param_enum_count = {4},
    .create = drum_create,
    .destroy = drum_destroy,
    .note_on = drum_note_on,
    .note_off = drum_note_off,
    .kill = drum_kill,
    .render = drum_render,
};
