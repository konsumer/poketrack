// The 11 digital Syntakt machines the voice loop computes, and their knobs.
// Names, ranges and defaults are the ones the Syntakt shows (its UI's
// parameter descriptors, OS 1.41/1.42).

#ifndef MACHINES_H
#define MACHINES_H

#include <stdint.h>

#include "engine.h"

#define MACHINE_COUNT 11
#define KNOB_COUNT 8

typedef struct {
  uint8_t slot;  // ST_TUNE..ST_OVER
  uint8_t min, max, def;
  const char* code;  // short name, as on the Syntakt screen
  const char* name;
} knob_t;

typedef struct {
  uint8_t type;  // ST_* machine type
  const char* name;
  knob_t knobs[KNOB_COUNT];
} machine_t;

extern const machine_t machines[MACHINE_COUNT];

#endif
