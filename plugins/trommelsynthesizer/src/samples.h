// Embedded ROM-style sample bank for the sample-based machines (TR-707/505).
#pragma once
#include <stdint.h>

typedef struct {
  const uint8_t* data;  // little-endian int16, mono
  uint32_t frames;
  uint32_t rate;
} Sample;

// NULL when the machine has no sample for that voice.
const Sample* sample_get(int machine, int voice);
