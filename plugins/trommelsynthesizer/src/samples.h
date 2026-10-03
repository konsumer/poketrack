// Embedded ROM-style sample bank for the sample-based machines (TR-707/505).
#pragma once
#include <stdint.h>

typedef struct {
  const uint8_t* data;  // 8-bit mu-law (G.711), mono
  uint32_t frames;      // = bytes in data
  uint32_t rate;
} Sample;

// NULL when the machine has no sample for that voice.
const Sample* sample_get(int machine, int voice);
