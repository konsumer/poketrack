#include "unit_registry.h"

#include <math.h>
#include <string.h>

float g_unit_sin_lut[UNIT_SIN_N + 1];

void unit_dsp_init(void) {
  for (int i = 0; i <= UNIT_SIN_N; i++)
    g_unit_sin_lut[i] = sinf(2.0f * (float)M_PI * i / UNIT_SIN_N);
}

static const UnitDef* REGISTRY[] = {
    &unit_osc,
    &unit_fm,
    &unit_drum,
    &unit_sf2,
#ifndef __EMSCRIPTEN__
    &unit_sfz,
#endif
    &unit_clap,
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
    &unit_chopper,
    &unit_pangain,
    &unit_compressor,
    &unit_ducker,
    &unit_midi,
    &unit_lfo,
    NULL,
};

const UnitDef* unit_find(const char* id) {
  if (!id || !id[0])
    return NULL;
  for (int i = 0; REGISTRY[i]; i++)
    if (strcmp(REGISTRY[i]->id, id) == 0)
      return REGISTRY[i];
  return NULL;
}

void unit_list(const UnitDef** out, int* count) {
  *count = 0;
  for (int i = 0; REGISTRY[i]; i++) out[(*count)++] = REGISTRY[i];
}
