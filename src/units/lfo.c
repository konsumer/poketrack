// LFO param modulator — modulates a param on any instrument each render block
// P0 RATE:  00=0.1Hz  FF=20Hz
// P1 SHPE:  0=Sine 1=Square 2=Saw 3=Tri
// P2 INST:  target instrument 0-FF
// P3 PARAM: target param (global index across chain slots, 0-FF)
// P4 CNTR:  center value for modulation (default 0x80)
// P5 DPTH:  00=0      FF=127 (added to/subtracted from center)
// P6 ON:    0=off 1=on
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

struct UnitState {
  float phase;
  float sample_rate;
};

static UnitState* lfo_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void lfo_destroy(UnitState* s) { free(s); }

static void lfo_render(UnitState* s, const uint8_t* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r, uint32_t frames) {
  // Pass audio through unchanged (effects usually render in-place: skip the copy)
  if (in_l && out_l != in_l)
    memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r && out_r != in_r)
    memcpy(out_r, in_r, frames * sizeof(float));

  if (!p[6])
    return;

  float rate = p2f(p[0], 0.1f, 20.0f);
  int shape = p[1] > 3 ? 3 : p[1];
  int inst = p[2];
  int prm = p[3];
  float cntr = (float)p[4];
  float depth = p[5] / 2.0f;  // 0-127 swing each side

  float phase_inc = rate / s->sample_rate;

  // Compute LFO at midpoint of block for param update (one write per block is fine)
  float phase = s->phase + phase_inc * (frames / 2.0f);
  if (phase >= 1.0f)
    phase -= (float)(int)phase;

  float raw = cntr + unit_lfo_wave(shape, phase) * depth;
  uint8_t val = raw < 0.0f ? 0 : raw > 255.0f ? 255
                                              : (uint8_t)raw;
  audio_mod_set_param(inst, prm, val);

  // Advance phase over full block
  s->phase += phase_inc * frames;
  while (s->phase >= 1.0f) s->phase -= 1.0f;
}

static void lfo_init_params(uint8_t* params, int inst_idx) {
  params[2] = (uint8_t)(inst_idx & 0xFF);
}

static const char* const lfo_shape_names[] = {"SINE", "SQR", "SAW", "TRI"};
static const char* const lfo_on_names[] = {"OFF", "ON"};

const UnitDef unit_lfo = {
    .id = "lfo",
    .name = "LFO",
    .is_source = false,
    .num_params = 7,
    .param_names = {"RATE", "SHPE", "INST", "PARAM", "CNTR", "DPTH", "ON"},
    .param_defaults = {0x20, 0, 0, 0, 0x80, 0x40, 0},
    .param_enums = {NULL, lfo_shape_names, NULL, NULL, NULL, NULL, lfo_on_names},
    .param_enum_count = {0, 4, 0, 0, 0, 0, 2},
    .init_params = lfo_init_params,
    .create = lfo_create,
    .destroy = lfo_destroy,
    .render = lfo_render,
};
