#include "slot.h"

#include <string.h>

// FNV-1a of section 7 of Syntakt OS 1.41/1.42, so a wrong or damaged image is refused
#define IMAGE_FNV 0x91ff4351u

// non-static and non-const so the compiler can't fold the empty slot away
__attribute__((used)) engine_slot_t syntakt_engine_slot = {SLOT_MAGIC, SLOT_EMPTY, SLOT_EMPTY, {[0 ... ST_IMAGE_SIZE - 1] = 0xa5}};

static uint32_t fnv1a(const uint8_t* p, uint32_t n) {
  uint32_t h = 0x811c9dc5u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
  return h;
}

const uint8_t* slot_image(void) {
  engine_slot_t* s = &syntakt_engine_slot;
  if (s->len != ST_IMAGE_SIZE || s->fnv != IMAGE_FNV)
    return NULL;
  if (fnv1a(s->data, s->len) != IMAGE_FNV)
    return NULL;
  return s->data;
}
