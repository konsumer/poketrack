#include "engine.h"

#include <stdlib.h>
#include <string.h>

#define BASE 0x40000400u
#define VOICE_LOOP 0x40004324u
#define VOICE_RESET 0x40003ee0u
#define VOICE_PREP 0x40002544u
#define VOICE_BUF 0x80008a60u
#define VOICE_BUF_OFF 0x56c
#define VOICE0 0x80000000u
#define VSTRIDE 1800
#define PMOD 0x80008150u
#define TSTRIDE 142
#define MTYPE_OFF 0x3e
#define UPD_OFF 0x1c
#define CODE_END 0x400148f0u  // code ends where the engine tables start

// our own scratch window, outside the program's memory: params, output, return address
#define SCRATCH 0x50000000u
#define PARAMS SCRATCH
#define OUT (SCRATCH + 0x1000)
#define STOP (SCRATCH + 0x2000)
#define STACK 0x90010000u

#define CALL_BUDGET 5000000
#define BLOCK_BUDGET 20000000

static const struct {
  uint32_t lo, hi, dst;
} data_copies[] = {{0x4004f6e0u, 0x40057670u, 0x80000000u}, {0x40057670u, 0x4005df10u, 0x80008000u}};

static int call(st_engine* e, uint32_t fn, int argc, const uint32_t* argv, uint32_t sp) {
  cf_cpu* cpu = e->cpu;
  cf_wr32(cpu, sp, STOP);
  for (int i = 0; i < argc; i++) cf_wr32(cpu, sp + 4 + 4 * i, argv[i]);
  cpu->r[15] = sp;
  cpu->pc = fn;
  return cf_run(cpu, STOP, argc == 4 ? BLOCK_BUDGET : CALL_BUDGET);
}

st_engine* st_new(const uint8_t* image, size_t len) {
  if (len != ST_IMAGE_SIZE)
    return NULL;
  st_engine* e = calloc(1, sizeof(st_engine));
  if (!e)
    return NULL;
  cf_cpu* cpu = e->cpu = cf_new();
  if (!cpu) {
    free(e);
    return NULL;
  }
  cf_map(cpu, 0x40000000u, 0x04100000u);  // SDRAM: image + BSS (to 0x4404f980)
  cf_map(cpu, 0x80000000u, 0x00020000u);  // internal SRAM (voices, tables)
  cf_map(cpu, SCRATCH, 0x00010000u);
  cf_map(cpu, 0x90000000u, 0x00020000u);  // stack
  cf_write(cpu, BASE, image, len);
  if (cf_set_code(cpu, BASE, CODE_END))
    goto fail;
  static const uint8_t nops[] = {0x4e, 0x71, 0x4e, 0x71};
  cf_write(cpu, STOP, nops, sizeof(nops));
  for (size_t i = 0; i < sizeof(data_copies) / sizeof(data_copies[0]); i++) {
    cf_write(cpu, data_copies[i].dst, image + (data_copies[i].lo - BASE), data_copies[i].hi - data_copies[i].lo);
  }
  // voice init, as the program's own 0x40000fd6..0x40001014
  for (uint32_t t = 0; t < ST_VOICES; t++) {
    uint32_t v = VOICE0 + VSTRIDE * t;
    if (call(e, VOICE_PREP, 0, NULL, STACK - 0x200))
      goto fail;
    cf_wr32(cpu, v + VOICE_BUF_OFF, VOICE_BUF + 0x30 * t);
    if (call(e, VOICE_RESET, 1, &v, STACK - 0x200))
      goto fail;
  }
  return e;
fail:
  st_free(e);
  return NULL;
}

void st_free(st_engine* e) {
  if (!e)
    return;
  cf_free(e->cpu);
  free(e);
}

void st_machine(st_engine* e, int track, int type) {
  e->params[track][MTYPE_OFF] = (uint8_t)type;
}

void st_param(st_engine* e, int track, int slot, uint16_t v) {
  e->params[track][UPD_OFF + 2 * slot] = (uint8_t)(v >> 8);
  e->params[track][UPD_OFF + 2 * slot + 1] = (uint8_t)v;
}

void st_note(st_engine* e, int track, int32_t semis_16_16) {
  e->pmod[track] = semis_16_16;
}

void st_park(st_engine* e, int track) {
  uint32_t v = VOICE0 + VSTRIDE * (uint32_t)track;
  cf_wr32(e->cpu, v, 12);
  cf_wr32(e->cpu, v + 4, 12);
}

int st_block(st_engine* e, uint32_t trig_mask, uint32_t release_mask, int32_t out[ST_VOICES][ST_FRAMES]) {
  cf_cpu* cpu = e->cpu;
  for (int t = 0; t < ST_VOICES; t++) {
    cf_write(cpu, PARAMS + TSTRIDE * t, e->params[t], TSTRIDE);
    cf_wr32(cpu, PMOD + 4 * t, (uint32_t)e->pmod[t]);
  }
  uint32_t argv[4] = {OUT, PARAMS, trig_mask, release_mask};
  int err = call(e, VOICE_LOOP, 4, argv, STACK - 0x100);
  for (int t = 0; t < ST_VOICES; t++) {
    for (int i = 0; i < ST_FRAMES; i++) out[t][i] = (int32_t)cf_rd32(cpu, OUT + 0x80 * t + 4 * i);
  }
  return err;
}
