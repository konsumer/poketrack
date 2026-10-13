// Where the engine image goes. The template plugin ships with this slot
// empty; the generator (web/patch.js) finds SLOT_MAGIC in the .wasm and
// writes the user's own section 7 into it. Every byte of the empty slot is
// non-zero (no NUL after the magic, 0xffffffff sentinels, 0xa5 fill): the
// linker leaves zero runs out of data segments, which would split the slot.

#ifndef SLOT_H
#define SLOT_H

#include <stdint.h>

#include "engine.h"

#define SLOT_MAGIC "SYNTAKT-ENGINE-SLOT-v1----------"  // 32 chars, stored without its NUL
#define SLOT_EMPTY 0xffffffffu

typedef struct {
  char magic[32] __attribute__((nonstring));
  uint32_t len;  // little-endian: SLOT_EMPTY, or ST_IMAGE_SIZE once patched
  uint32_t fnv;  // FNV-1a of data, written by the generator
  uint8_t data[ST_IMAGE_SIZE];
} engine_slot_t;

extern engine_slot_t syntakt_engine_slot;

// the image if the slot holds a good one, else NULL
const uint8_t* slot_image(void);

#endif
