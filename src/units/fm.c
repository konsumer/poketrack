// 2-operator FM synthesis unit (carrier modulated by one operator)
// P0 RATIO: modulator freq ratio (enum: 0.25 0.5 1 1.5 2 3 4 5 6 7 8 10 12 16)
// P1 DEPTH: modulation index  00=0.0  FF=12.0
// P2 ATK:   00=1ms  FF=2s
// P3 DCY:   00=1ms  FF=2s
// P4 SUS:   00=0    FF=1.0
// P5 REL:   00=1ms  FF=4s
// P6 FDBK:  00=0    FF=4.0  (modulator self-feedback)
// P7 VOL:   00=0    FF=1.0
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define FM_POLY 4
#define TWO_PI 6.28318530718f

static const float fm_ratios[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f,
                                  4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f,
                                  12.0f, 16.0f};
#define FM_RATIO_COUNT 14

typedef struct {
  bool active;
  uint8_t note;
  float car_freq, mod_freq;
  float car_phase, mod_phase;
  float mod_prev;
  float env_level, env_time;
  int env_stage;
  float vel;
} FMVoice;

struct UnitState {
  FMVoice voices[FM_POLY];
  float sample_rate;
};

static float env_tick(FMVoice* v, float dt, float atk, float dcy, float sus, float rel) {
  return unit_env_tick(&v->active, &v->env_level, &v->env_time, &v->env_stage,
                        dt, atk, dcy, sus, rel);
}

static UnitState* fm_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void fm_destroy(UnitState* s) { free(s); }

static void fm_note_on(UnitState* s, uint8_t note, uint8_t vel, const uint8_t* p) {
  FMVoice* v = NULL;
  for (int i = 0; i < FM_POLY; i++)
    if (!s->voices[i].active) {
      v = &s->voices[i];
      break;
    }
  if (!v)
    v = &s->voices[0];

  int ri = (int)(p[0] / 255.0f * (FM_RATIO_COUNT - 1) + 0.5f);
  if (ri >= FM_RATIO_COUNT)
    ri = FM_RATIO_COUNT - 1;
  float base = 440.0f * powf(2.0f, (note - 69) / 12.0f);

  *v = (FMVoice){
      .active = true,
      .note = note,
      .vel = vel / 127.0f,
      .car_freq = base,
      .mod_freq = base * fm_ratios[ri],
  };
}

static void fm_note_off(UnitState* s, uint8_t note) {
  for (int i = 0; i < FM_POLY; i++) {
    FMVoice* v = &s->voices[i];
    if (v->active && v->note == note && v->env_stage < 3) {
      v->env_stage = 3;
      v->env_time = 0;
    }
  }
}

static void fm_kill(UnitState* s) { memset(s->voices, 0, sizeof(s->voices)); }

static void fm_render(UnitState* s, const uint8_t* p,
                      const float* in_l, const float* in_r,
                      float* out_l, float* out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (s->sample_rate <= 0)
    return;

  // unit_sin takes cycles, not radians — fold the 1/2pi into the depths
  float depth = p2f(p[1], 0.0f, 12.0f) / TWO_PI;
  float atk = p2f(p[2], 0.001f, 2.0f);
  float dcy = p2f(p[3], 0.001f, 2.0f);
  float sus = p2f(p[4], 0.0f, 1.0f);
  float rel = p2f(p[5], 0.001f, 4.0f);
  float fdbk = p2f(p[6], 0.0f, 4.0f) / TWO_PI;
  float vol = p2f(p[7], 0.0f, 1.0f);
  float dt = 1.0f / s->sample_rate;

  for (uint32_t f = 0; f < frames; f++) {
    float mix = 0;
    for (int i = 0; i < FM_POLY; i++) {
      FMVoice* v = &s->voices[i];
      if (!v->active)
        continue;
      float env = env_tick(v, dt, atk, dcy, sus, rel);

      // modulator with self-feedback
      float mod_out = unit_sin(v->mod_phase + fdbk * v->mod_prev);
      v->mod_prev = mod_out;

      // carrier modulated by operator
      float car = unit_sin(v->car_phase + depth * mod_out);
      mix += car * env * v->vel;

      v->mod_phase = unit_phase_advance(v->mod_phase, v->mod_freq * dt);
      v->car_phase = unit_phase_advance(v->car_phase, v->car_freq * dt);
    }
    mix = unit_softclip(mix * 0.5f) * vol;
    out_l[f] += mix;
    out_r[f] += mix;
  }
}

static const char* const fm_ratio_names[] = {
    "0.25", "0.5", "1", "1.5", "2", "3",
    "4", "5", "6", "7", "8", "10", "12", "16"};

static const char* fm_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx != 0)
    return NULL;
  int ri = (int)(val / 255.0f * (FM_RATIO_COUNT - 1) + 0.5f);
  if (ri >= FM_RATIO_COUNT)
    ri = FM_RATIO_COUNT - 1;
  return fm_ratio_names[ri];
}

const UnitDef unit_fm = {
    .id = "fm",
    .name = "FM",
    .is_source = true,
    .num_params = 8,
    .param_names = {"RATIO", "DEPTH", "ATK", "DCY", "SUS", "REL", "FDBK", "VOL"},
    .param_defaults = {36, 60, 0x00, 0x20, 0x00, 0x00, 0, 0xC8},
    .create = fm_create,
    .destroy = fm_destroy,
    .note_on = fm_note_on,
    .note_off = fm_note_off,
    .kill = fm_kill,
    .render = fm_render,
    .format_param_val = fm_format_param,
};
