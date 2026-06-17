#include "unit_registry.h"

#include <string.h>

static const UnitDef *REGISTRY[] = {
    &unit_osc,
    &unit_drum,
    &unit_sf2,
#ifndef __EMSCRIPTEN__
    &unit_sfz,
#endif
#ifndef __EMSCRIPTEN__
    &unit_clap,
#endif
    &unit_delay,
    &unit_dist,
    &unit_reverb,
    &unit_chorus,
    &unit_flanger,
    &unit_phaser,
    &unit_gran,
    &unit_filter,
    &unit_sampler,
    &unit_bitcrush,
    &unit_tremolo,
    &unit_compressor,
    &unit_ducker,
    &unit_midi,
    &unit_lfo,
    NULL,
};

const UnitDef *unit_find(const char *id) {
  if (!id || !id[0])
    return NULL;
  for (int i = 0; REGISTRY[i]; i++)
    if (strcmp(REGISTRY[i]->id, id) == 0)
      return REGISTRY[i];
  return NULL;
}

void unit_list(const UnitDef **out, int *count) {
  *count = 0;
  for (int i = 0; REGISTRY[i]; i++) out[(*count)++] = REGISTRY[i];
}
