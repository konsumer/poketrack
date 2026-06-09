// Bit crusher + rate crusher effect unit
// P0 BITS: FF=16bit clean  00=1bit harsh  (maps to 1-16 bits)
// P1 RATE: FF=no downsampling  00=32x step  (maps to 1-32 step)
#include <math.h>
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  float held_l, held_r;
  int step_counter;
};

static UnitState *bitcrush_create(float sr) {
  (void)sr;
  UnitState *s = calloc(1, sizeof(*s));
  s->step_counter = 0;
  return s;
}
static void bitcrush_destroy(UnitState *s) { free(s); }
static void bitcrush_note_on(UnitState *s, uint8_t n, uint8_t v, const uint8_t *p) {
  (void)s;
  (void)n;
  (void)v;
  (void)p;
}
static void bitcrush_note_off(UnitState *s, uint8_t n) {
  (void)s;
  (void)n;
}
static void bitcrush_kill(UnitState *s) {
  s->held_l = s->held_r = 0.0f;
  s->step_counter = 0;
}

static void bitcrush_render(UnitState *s, const uint8_t *p,
                            const float *in_l, const float *in_r,
                            float *out_l, float *out_r, uint32_t frames) {
  // bits: 0xFF=16, 0x00=1 — map linearly
  int bits = 1 + (int)(p[0] / 255.0f * 15.0f + 0.5f);
  if (bits < 1)
    bits = 1;
  if (bits > 16)
    bits = 16;

  // step: 0xFF=1 (no downsample), 0x00=32
  int step = 32 - (int)(p[1] / 255.0f * 31.0f + 0.5f);
  if (step < 1)
    step = 1;
  if (step > 32)
    step = 32;

  float levels = (float)(1 << (bits - 1));  // 2^(bits-1)

  for (uint32_t f = 0; f < frames; f++) {
    if (s->step_counter <= 0) {
      // Sample new value and quantize
      float ql = floorf(in_l[f] * levels + 0.5f) / levels;
      float qr = floorf(in_r[f] * levels + 0.5f) / levels;
      // clamp
      if (ql > 1.0f)
        ql = 1.0f;
      if (ql < -1.0f)
        ql = -1.0f;
      if (qr > 1.0f)
        qr = 1.0f;
      if (qr < -1.0f)
        qr = -1.0f;
      s->held_l = ql;
      s->held_r = qr;
      s->step_counter = step;
    }
    out_l[f] = s->held_l;
    out_r[f] = s->held_r;
    s->step_counter--;
  }
}

const UnitDef unit_bitcrush = {
    .id = "bcrush",
    .name = "BCRUSH",
    .is_source = false,
    .num_params = 2,
    .param_names = {"BITS", "RATE"},
    .param_defaults = {0xFF, 0xFF},
    .create = bitcrush_create,
    .destroy = bitcrush_destroy,
    .note_on = bitcrush_note_on,
    .note_off = bitcrush_note_off,
    .kill = bitcrush_kill,
    .render = bitcrush_render,
};
