// bench image.bin scenario.txt out.raw [ref.raw]
// Renders a scenario (see mix1.txt) with the interpreter, writes the raw
// blocks (per block 8 voices x 32 int32 LE) and, given a reference render
// from ref_bench.py, compares them.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"

#define MAX_EVENTS 4096

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct {
  int block, track;
  char cmd[16], key[16];
  double value;
} event_t;

static const char* slot_names[] = {"tune", "p1", "p2", "p3", "p4", "punch", "gate", "decay", "over"};

static uint8_t* slurp(const char* path, size_t* len) {
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* b = malloc(*len);
  if (b && fread(b, 1, *len, f) != *len) {
    free(b);
    b = NULL;
  }
  fclose(f);
  return b;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: bench image.bin scenario.txt out.raw [ref.raw]\n");
    return 1;
  }
  size_t len;
  uint8_t* img = slurp(argv[1], &len);
  if (!img) {
    fprintf(stderr, "can't read %s\n", argv[1]);
    return 1;
  }

  static event_t ev[MAX_EVENTS];
  int nev = 0, end = 0;
  FILE* sf = fopen(argv[2], "r");
  if (!sf) {
    fprintf(stderr, "can't read %s\n", argv[2]);
    return 1;
  }
  char line[256];
  while (fgets(line, sizeof(line), sf)) {
    char* hash = strchr(line, '#');
    if (hash)
      *hash = 0;
    event_t e = {0};
    char a[16] = "", b[16] = "", c[16] = "", d[16] = "";
    int n = sscanf(line, "%15s %15s %15s %15s %15s", a, e.cmd, b, c, d);
    if (n < 1)
      continue;
    if (!strcmp(a, "end")) {
      end = atoi(e.cmd);
      continue;
    }
    e.block = atoi(a);
    e.track = atoi(b);
    if (!strcmp(e.cmd, "set")) {
      snprintf(e.key, sizeof(e.key), "%s", c);
      e.value = atof(d);
    } else {
      e.value = atof(c);
    }
    if (nev < MAX_EVENTS)
      ev[nev++] = e;
  }
  fclose(sf);

  st_engine* eng = st_new(img, len);
  if (!eng) {
    fprintf(stderr, "engine init failed\n");
    return 1;
  }

  size_t rlen = 0;
  int32_t* ref = NULL;
  if (argc > 4) {
    ref = (int32_t*)slurp(argv[4], &rlen);
    if (!ref)
      fprintf(stderr, "can't read %s, not comparing\n", argv[4]);
  }

  FILE* of = fopen(argv[3], "wb");
  int32_t out[ST_VOICES][ST_FRAMES];
  long long maxdiff = 0, ndiff = 0;
  int first_bad = -1;
  double t0 = now();
  uint64_t i0 = eng->cpu->icount;
  for (int blk = 0; blk < end; blk++) {
    uint32_t trig = 0, rel = 0;
    for (int i = 0; i < nev; i++) {
      event_t* e = &ev[i];
      if (e->block != blk)
        continue;
      if (!strcmp(e->cmd, "machine"))
        st_machine(eng, e->track, (int)e->value);
      else if (!strcmp(e->cmd, "note"))
        st_note(eng, e->track, (int32_t)lround(e->value * 65536));
      else if (!strcmp(e->cmd, "trig"))
        trig |= 1u << e->track;
      else if (!strcmp(e->cmd, "release"))
        rel |= 1u << e->track;
      else if (!strcmp(e->cmd, "park"))
        st_park(eng, e->track);
      else if (!strcmp(e->cmd, "set")) {
        for (int s = 0; s < 9; s++) {
          if (!strcmp(e->key, slot_names[s]))
            st_param(eng, e->track, ST_TUNE + s, (uint16_t)lround(e->value * 256));
        }
      }
    }
    int err = st_block(eng, trig, rel, out);
    if (err) {
      cf_cpu* c = eng->cpu;
      fprintf(stderr, "block %d: error %d at pc 0x%08x op 0x%04x addr 0x%08x\n", blk, err, c->err_pc, c->err_op, c->err_addr);
      return 1;
    }
    fwrite(out, sizeof(out), 1, of);
    if (ref && (size_t)(blk + 1) * sizeof(out) <= rlen) {
      const int32_t* r = ref + (size_t)blk * ST_VOICES * ST_FRAMES;
      for (int k = 0; k < ST_VOICES * ST_FRAMES; k++) {
        long long dd = llabs((long long)((int32_t*)out)[k] - r[k]);
        if (dd) {
          ndiff++;
          if (first_bad < 0)
            first_bad = blk;
        }
        if (dd > maxdiff)
          maxdiff = dd;
      }
    }
  }
  fclose(of);
  double secs = now() - t0;
  double audio = end * (double)ST_FRAMES / ST_RATE;
  printf("%d blocks (%.3fs audio) in %.3fs: %.1fx realtime, %.1fM instructions/s\n", end, audio, secs, audio / secs, (eng->cpu->icount - i0) / secs / 1e6);
  if (ref) {
    printf("vs reference: %lld differing samples, max diff %lld", ndiff, maxdiff);
    if (first_bad >= 0)
      printf(", first in block %d", first_bad);
    printf("\n");
  }
#ifdef CF_PROFILE
  extern uint64_t cf_prof[0x20000];
  FILE* pf = fopen("build/profile.txt", "w");
  for (int i = 0; i < 0x20000; i++) {
    if (cf_prof[i])
      fprintf(pf, "%x %llu\n", 0x40000000u + 2 * i, (unsigned long long)cf_prof[i]);
  }
  fclose(pf);
#endif
  st_free(eng);
  return ref && ndiff ? 2 : 0;
}
