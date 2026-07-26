// Tremolo / autopan LFO effect unit
// P0 RATE:  00=0.1Hz  FF=20Hz
// P1 DEPTH: 00=0      FF=1.0
// P2 SHAPE: 0=Sine 1=Square 2=Saw 3=Tri
// P3 MODE:  0=Trem 1=Pan 2=Both
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float phase;  // 0..1
  float sample_rate;
};

static UnitState* tremolo_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void tremolo_destroy(UnitState* s) { free(s); }

static void tremolo_render(UnitState* s, const uint8_t* p,
                           const float* in_l, const float* in_r,
                           float* out_l, float* out_r, uint32_t frames) {
  float rate = p2f(p[0], 0.1f, 20.0f);
  float depth = p[1] / 255.0f;
  int shape = p[2];
  if (shape > 3)
    shape = 3;
  int mode = p[3];
  if (mode > 2)
    mode = 2;

  float phase_inc = rate / s->sample_rate;
  float phase = s->phase;

  for (uint32_t f = 0; f < frames; f++) {
    float lfo = unit_lfo_wave(shape, phase);

    float gl = 1.0f, gr = 1.0f;
    switch (mode) {
      case 0:  // Trem: both channels
        gl = gr = 1.0f - depth * 0.5f * (1.0f + lfo);
        break;
      case 1:  // Pan
        gl = 1.0f - depth * lfo * 0.5f;
        gr = 1.0f + depth * lfo * 0.5f;
        break;
      case 2:  // Both
      {
        float trem = 1.0f - depth * 0.5f * (1.0f + lfo);
        gl = trem * (1.0f - depth * lfo * 0.5f);
        gr = trem * (1.0f + depth * lfo * 0.5f);
      } break;
    }

    out_l[f] = in_l[f] * gl;
    out_r[f] = in_r[f] * gr;

    phase = unit_phase_advance(phase, phase_inc);
  }

  s->phase = phase;
}

static const char* const tremolo_shape_names[] = {"SINE", "SQR", "SAW", "TRI"};
static const char* const tremolo_mode_names[] = {"TREM", "PAN", "BOTH"};

const UnitDef unit_tremolo = {
    .id = "tremolo",
    .name = "TREMOLO",
    .is_source = false,
    .num_params = 4,
    .param_names = {"RATE", "DPTH", "SHPE", "MODE"},
    .param_defaults = {0x20, 0x60, 0, 0},
    .param_enums = {NULL, NULL, tremolo_shape_names, tremolo_mode_names},
    .param_enum_count = {0, 0, 4, 3},
    .create = tremolo_create,
    .destroy = tremolo_destroy,
    .render = tremolo_render,
};
