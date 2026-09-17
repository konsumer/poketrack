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
    &unit_eq,
    &unit_sampler,
    &unit_turntable,
    &unit_bitcrush,
    &unit_tremolo,
    &unit_chopper,
    &unit_pangain,
    &unit_compressor,
    &unit_ducker,
    &unit_route,
    &unit_midi,
    &unit_lfo,
    &unit_arp,
    &unit_microtonal,
    &unit_chord,
    &unit_pitchbend,
    NULL,
};

// unit_list() copies the whole registry into a caller-supplied array and has
// no way to bounds-check it: the instrument screen's unit picker and the test
// suite both hand it 64 entries, so growing the registry past that would be a
// silent stack write in the UI. Catch it here instead, at compile time.
#define UNIT_REGISTRY_MAX 64
_Static_assert(sizeof(REGISTRY) / sizeof(REGISTRY[0]) - 1 < UNIT_REGISTRY_MAX,
               "unit registry outgrew the fixed-size buffers unit_list() callers pass — raise them and UNIT_REGISTRY_MAX");

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
