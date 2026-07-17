// Freeverb reverb effect
// P0 ROOM:  00=tight   FF=huge
// P1 DAMP:  00=bright  FF=dark
// P2 WIDTH: 00=mono    FF=stereo
// P3 MIX:   00=dry     FF=wet
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define COMB_N 8
#define AP_N 4

// Comb filter buffer sizes (L, R — R offset by +23 for stereo)
static const int CBUF_L[COMB_N] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int CBUF_R[COMB_N] = {1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640};
static const int ABUF_L[AP_N] = {556, 441, 341, 225};
static const int ABUF_R[AP_N] = {579, 464, 364, 248};

typedef struct {
  float* buf;
  int n, pos;
  float fstore, fb, d1, d2;
} CombF;
typedef struct {
  float* buf;
  int n, pos;
} ApF;

struct UnitState {
  CombF cl[COMB_N], cr[COMB_N];
  ApF al[AP_N], ar[AP_N];
  float sample_rate;
};

static UnitState* reverb_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  for (int i = 0; i < COMB_N; i++) {
    s->cl[i].buf = calloc(CBUF_L[i], sizeof(float));
    s->cl[i].n = CBUF_L[i];
    s->cr[i].buf = calloc(CBUF_R[i], sizeof(float));
    s->cr[i].n = CBUF_R[i];
  }
  for (int i = 0; i < AP_N; i++) {
    s->al[i].buf = calloc(ABUF_L[i], sizeof(float));
    s->al[i].n = ABUF_L[i];
    s->ar[i].buf = calloc(ABUF_R[i], sizeof(float));
    s->ar[i].n = ABUF_R[i];
  }
  return s;
}

static void reverb_destroy(UnitState* s) {
  for (int i = 0; i < COMB_N; i++) {
    free(s->cl[i].buf);
    free(s->cr[i].buf);
  }
  for (int i = 0; i < AP_N; i++) {
    free(s->al[i].buf);
    free(s->ar[i].buf);
  }
  free(s);
}

static void reverb_kill(UnitState* s) {
  for (int i = 0; i < COMB_N; i++) {
    memset(s->cl[i].buf, 0, s->cl[i].n * sizeof(float));
    memset(s->cr[i].buf, 0, s->cr[i].n * sizeof(float));
    s->cl[i].fstore = s->cr[i].fstore = 0;
  }
  for (int i = 0; i < AP_N; i++) {
    memset(s->al[i].buf, 0, s->al[i].n * sizeof(float));
    memset(s->ar[i].buf, 0, s->ar[i].n * sizeof(float));
  }
}

static inline float comb_tick(CombF* c, float x) {
  float out = c->buf[c->pos];
  c->fstore = out * c->d2 + c->fstore * c->d1;
  c->buf[c->pos] = x + c->fstore * c->fb;
  if (++c->pos >= c->n)
    c->pos = 0;
  return out;
}

static inline float ap_tick(ApF* a, float x) {
  float b = a->buf[a->pos];
  a->buf[a->pos] = x + b * 0.5f;
  if (++a->pos >= a->n)
    a->pos = 0;
  return b - x;
}

static void reverb_render(UnitState* s, const uint8_t* p,
                          const float* in_l, const float* in_r,
                          float* out_l, float* out_r, uint32_t frames) {
  float room = p2f(p[0], 0.1f, 0.98f);
  float damp = p2f(p[1], 0.0f, 1.0f);
  float width = p2f(p[2], 0.0f, 1.0f);
  float mix = p2f(p[3], 0.0f, 1.0f);
  float d1 = damp, d2 = 1.0f - damp;

  for (int i = 0; i < COMB_N; i++) {
    s->cl[i].fb = s->cr[i].fb = room;
    s->cl[i].d1 = s->cr[i].d1 = d1;
    s->cl[i].d2 = s->cr[i].d2 = d2;
  }

  float w1 = mix * (0.5f + width * 0.5f);
  float w2 = mix * (0.5f - width * 0.5f);
  float dry = 1.0f - mix;

  for (uint32_t f = 0; f < frames; f++) {
    float mono = (in_l[f] + in_r[f]) * 0.015f;
    float wl = 0, wr = 0;
    for (int i = 0; i < COMB_N; i++) {
      wl += comb_tick(&s->cl[i], mono);
      wr += comb_tick(&s->cr[i], mono);
    }
    for (int i = 0; i < AP_N; i++) {
      wl = ap_tick(&s->al[i], wl);
      wr = ap_tick(&s->ar[i], wr);
    }
    out_l[f] = in_l[f] * dry + wl * w1 + wr * w2;
    out_r[f] = in_r[f] * dry + wr * w1 + wl * w2;
  }
}

const UnitDef unit_reverb = {
    .id = "reverb",
    .name = "REVERB",
    .is_source = false,
    .num_params = 4,
    .param_names = {"ROOM", "DAMP", "WDTH", "MIX"},
    .param_defaults = {140, 80, 200, 100},
    .create = reverb_create,
    .destroy = reverb_destroy,
    .kill = reverb_kill,
    .render = reverb_render,
};
