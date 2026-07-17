// Pan / gain utility effect
// P0 PAN:  00=L    80=center (exact)  FF=R
// P1 GAIN: 00=mute 80=unity (exact)   FF=+6dB
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  uint8_t unused;  // stateless effect; instances only exist to satisfy the engine
};

static UnitState* pangain_create(float sr) {
  (void)sr;
  return calloc(1, sizeof(UnitState));
}
static void pangain_destroy(UnitState* s) { free(s); }

static void pangain_render(UnitState* s, const uint8_t* p,
                           const float* in_l, const float* in_r,
                           float* out_l, float* out_r, uint32_t frames) {
  (void)s;
  if (p[0] == 0x80 && p[1] == 0x80 && out_l == in_l && out_r == in_r)
    return;  // neutral and in-place: nothing to do

  // Mapped around 0x80 (not p2f) so center pan / unity gain are exact
  float pan = (p[0] - 128) / 127.0f;
  if (pan < -1.0f)
    pan = -1.0f;
  float gain = p[1] / 128.0f;

  float gl = (pan <= 0.0f ? 1.0f : 1.0f - pan) * gain;
  float gr = (pan >= 0.0f ? 1.0f : 1.0f + pan) * gain;

  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] = in_l[f] * gl;
    out_r[f] = in_r[f] * gr;
  }
}

const UnitDef unit_pangain = {
    .id = "pangain",
    .name = "PAN/GAIN",
    .is_source = false,
    .num_params = 2,
    .param_names = {"PAN", "GAIN"},
    .param_defaults = {0x80, 0x80},
    .create = pangain_create,
    .destroy = pangain_destroy,
    .render = pangain_render,
};
