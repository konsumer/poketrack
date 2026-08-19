// Send bus — splits this chain's audio between playing here and feeding
// another instrument's chain, so several instruments can share one reverb
// (or any other effect chain) instead of each carrying its own copy.
// P0 MIX:  00=all dry (no send)  80=half here / half sent  FF=all sent
// P1 INST: destination instrument 00-FF
#include <stdlib.h>

#include "unit.h"

struct UnitState {
  uint8_t unused;  // stateless effect; instances only exist to satisfy the engine
};

static UnitState* route_create(float sr) {
  (void)sr;
  return calloc(1, sizeof(UnitState));
}
static void route_destroy(UnitState* s) { free(s); }

static void route_render(UnitState* s, const uint8_t* p,
                         const float* in_l, const float* in_r,
                         float* out_l, float* out_r, uint32_t frames) {
  (void)s;
  // p2f_center (not p2f) so 0x80 is an exact 50/50 split
  float send = p2f_center(p[0], 0.0f, 1.0f);
  if (send <= 0.0f) {
    if (out_l != in_l)
      memcpy(out_l, in_l, frames * sizeof(float));
    if (out_r != in_r)
      memcpy(out_r, in_r, frames * sizeof(float));
    return;
  }

  // Send first: out_* usually aliases in_*, so scaling the dry signal below
  // would otherwise send the already-attenuated version.
  audio_send_bus(p[1], in_l, in_r, frames, send);

  float dry = 1.0f - send;
  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] = in_l[f] * dry;
    out_r[f] = in_r[f] * dry;
  }
}

const UnitDef unit_route = {
    .id = "route",
    .name = "ROUTE",
    .is_source = false,
    .num_params = 2,
    .param_names = {"MIX", "INST"},
    // MIX defaults to 0 (pure pass-through) so dropping a ROUTE in never
    // changes the sound until a destination has actually been picked —
    // same reasoning as LFO/DUCKER defaulting to OFF.
    .param_defaults = {0x00, 0x00},
    .create = route_create,
    .destroy = route_destroy,
    .render = route_render,
};
