// Oscillator source unit
// P0 WAVE: 00-50=sine 51-101=square 102-152=saw 153-203=tri 204-255=noise
// P1 ATK:  00=1ms  FF=2s
// P2 DCY:  00=1ms  FF=2s
// P3 SUS:  00=0    FF=1.0
// P4 REL:  00=1ms  FF=4s
// P5 DET:  00=-12st 7F=0 FF=+12st
// P6 PW:   00=0.05 FF=0.95 (square pulse width)
// P7 VOL:  00=0    FF=1.0
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define OSC_POLY 4
#define TWO_PI 6.28318530718f

typedef struct {
  bool active;
  uint8_t note;
  float freq;
  float phase;
  float env_level, env_time;
  int env_stage;  // 0=atk 1=dcy 2=sus 3=rel 4=off
  float vel;
  uint32_t noise_seed;
} OscVoice;

struct UnitState {
  OscVoice voices[OSC_POLY];
  float sample_rate;
};

static uint32_t lcg(uint32_t *s) {
  *s = *s * 1664525u + 1013904223u;
  return *s;
}

static float osc_sample(OscVoice *v, int wave, float pw) {
  float p = v->phase;
  switch (wave) {
    case 0:
      return sinf(p * TWO_PI);
    case 1:
      return (p < pw) ? 1.0f : -1.0f;
    case 2:
      return 2.0f * p - 1.0f;
    case 3:
      return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    default:
      return (float)(int32_t)lcg(&v->noise_seed) / 2147483648.0f;
  }
}

static float env_tick(OscVoice *v, float dt, float atk, float dcy, float sus, float rel) {
  switch (v->env_stage) {
    case 0:
      v->env_time += dt;
      v->env_level = (atk > 0) ? v->env_time / atk : 1.0f;
      if (v->env_level >= 1.0f) {
        v->env_level = 1.0f;
        v->env_stage = 1;
        v->env_time = 0;
      }
      break;
    case 1:
      v->env_time += dt;
      v->env_level = 1.0f - (1.0f - sus) * (v->env_time / dcy);
      if (v->env_time >= dcy) {
        v->env_level = sus;
        v->env_stage = 2;
      }
      break;
    case 2:
      v->env_level = sus;
      break;
    case 3:
      v->env_time += dt;
      v->env_level -= (sus > 0 ? sus : 0.5f) / rel * dt;
      if (v->env_level <= 0) {
        v->env_level = 0;
        v->env_stage = 4;
        v->active = false;
      }
      break;
    default:
      return 0;
  }
  return v->env_level;
}

static UnitState *osc_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void osc_destroy(UnitState *s) { free(s); }

static void osc_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  OscVoice *v = NULL;
  for (int i = 0; i < OSC_POLY; i++)
    if (!s->voices[i].active) {
      v = &s->voices[i];
      break;
    }
  if (!v)
    v = &s->voices[0];  // steal
  float det = p2f(p[5], -12.0f, 12.0f);
  *v = (OscVoice){
      .active = true,
      .note = note,
      .vel = vel / 127.0f,
      .freq = 440.0f * powf(2.0f, (note - 69 + det) / 12.0f),
      .noise_seed = 12345u + note,
  };
}

static void osc_note_off(UnitState *s, uint8_t note) {
  for (int i = 0; i < OSC_POLY; i++) {
    OscVoice *v = &s->voices[i];
    if (v->active && v->note == note && v->env_stage < 3) {
      v->env_stage = 3;
      v->env_time = 0;
    }
  }
}

static void osc_kill(UnitState *s) { memset(s->voices, 0, sizeof(s->voices)); }

static void osc_render(UnitState *s, const uint8_t *p,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (s->sample_rate <= 0)
    return;
  int wave = p[0] < 5 ? p[0] : 4;
  float atk = p2f(p[1], 0.001f, 2.0f);
  float dcy = p2f(p[2], 0.001f, 2.0f);
  float sus = p2f(p[3], 0.0f, 1.0f);
  float rel = p2f(p[4], 0.001f, 4.0f);
  float pw = p2f(p[6], 0.05f, 0.95f);
  float vol = p2f(p[7], 0.0f, 1.0f);
  float dt = 1.0f / s->sample_rate;

  for (uint32_t f = 0; f < frames; f++) {
    float mix = 0;
    for (int i = 0; i < OSC_POLY; i++) {
      OscVoice *v = &s->voices[i];
      if (!v->active)
        continue;
      float env = env_tick(v, dt, atk, dcy, sus, rel);
      mix += osc_sample(v, wave, pw) * env * v->vel;
      v->phase += v->freq / s->sample_rate;
      if (v->phase >= 1.0f)
        v->phase -= 1.0f;
    }
    mix = tanhf(mix * 0.7f) * vol;
    out_l[f] += mix;
    out_r[f] += mix;
  }
}

static const char *const osc_wave_names[] = {"SINE", "SQR", "SAW", "TRI", "NOISE"};

const UnitDef unit_osc = {
    .id = "osc",
    .name = "OSC",
    .is_source = true,
    .num_params = 8,
    .param_names = {"WAVE", "ATK", "DCY", "SUS", "REL", "DET", "PW", "VOL"},
    .param_defaults = {2, 2, 25, 178, 12, 128, 128, 200},
    .param_enums = {osc_wave_names},
    .param_enum_count = {5},
    .create = osc_create,
    .destroy = osc_destroy,
    .note_on = osc_note_on,
    .note_off = osc_note_off,
    .kill = osc_kill,
    .render = osc_render,
};
