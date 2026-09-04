// Delay effect unit
// P0 TIME:     00=4ms  FF=1s (used when SYNC=FREE)
// P1 FEEDBACK: 00=0%   FF=95%
// P2 MIX:      00=dry  FF=wet  7F=50/50
// P3 SPREAD:   00=mono  FF=ping-pong (L/R offset)
// P4 SYNC:     FREE=use TIME, else lock the delay length to a note division
//              of the song tempo, via g_unit_samples_per_line (see unit.h
//              and lfo.c, which uses the same table). Long divisions get
//              clamped to the 1s buffer, so e.g. 4/1, 2/1 and 1/1 all fold
//              to the same 1s delay at normal tempos.
// P5-P7: unused
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define DELAY_MAX 44100  // 1 second at 44100 Hz

struct UnitState {
  float buf_l[DELAY_MAX];
  float buf_r[DELAY_MAX];
  int write;
  float sample_rate;
};

// Index 0 is FREE (TIME param governs instead); the rest are multiples of a
// whole note (bar), matching lfo.c's SYNC table.
static const char* const delay_sync_names[] = {"FREE", "4/1", "2/1", "1/1", "1/2",
                                               "1/4", "1/8", "1/16", "1/32"};
static const float delay_sync_mult[] = {4, 2, 1, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
#define DELAY_NSYNC ((int)(sizeof(delay_sync_names) / sizeof(delay_sync_names[0])))

static UnitState* delay_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void delay_destroy(UnitState* s) { free(s); }
static void delay_kill(UnitState* s) {
  memset(s->buf_l, 0, sizeof(s->buf_l));
  memset(s->buf_r, 0, sizeof(s->buf_r));
}

static void delay_render(UnitState* s, const uint8_t* p,
                         const float* in_l, const float* in_r,
                         float* out_l, float* out_r, uint32_t frames) {
  float feedback = p2f(p[1], 0.0f, 0.95f);
  float mix = p2f(p[2], 0.0f, 1.0f);
  float spread = p2f(p[3], 0.0f, 1.0f);

  int sync = p[4] > DELAY_NSYNC - 1 ? DELAY_NSYNC - 1 : p[4];
  int delay_samp;
  uint32_t spl = g_unit_samples_per_line;
  if (sync == 0 || !spl) {
    // FREE (or tempo not known yet): plain seconds range.
    float delay_secs = p2f(p[0], 0.004f, 1.0f);
    delay_samp = (int)(delay_secs * s->sample_rate);
  } else {
    // One repeat every (whole-note-in-samples * multiplier).
    delay_samp = (int)((float)spl * 16.0f * delay_sync_mult[sync - 1] + 0.5f);
  }
  if (delay_samp < 1)
    delay_samp = 1;
  if (delay_samp >= DELAY_MAX)
    delay_samp = DELAY_MAX - 1;
  // Spread: ping-pong by offsetting R delay by a small amount
  int spread_off = (int)(spread * delay_samp * 0.5f);

  for (uint32_t f = 0; f < frames; f++) {
    // Wrap by comparison, not %: DELAY_MAX isn't a power of two, so the
    // modulo can't become a mask, and three of them per sample measured 3.2x
    // slower than this. read_r is derived from read_l (spread_off < DELAY_MAX,
    // so one conditional add is always enough).
    int read_l = s->write - delay_samp;
    if (read_l < 0)
      read_l += DELAY_MAX;
    int read_r = read_l - spread_off;
    if (read_r < 0)
      read_r += DELAY_MAX;

    float dl = s->buf_l[read_l];
    float dr = s->buf_r[read_r];

    s->buf_l[s->write] = in_l[f] + dl * feedback;
    s->buf_r[s->write] = in_r[f] + dr * feedback;
    if (++s->write >= DELAY_MAX)
      s->write = 0;

    out_l[f] = in_l[f] * (1.0f - mix) + dl * mix;
    out_r[f] = in_r[f] * (1.0f - mix) + dr * mix;
  }
}

const UnitDef unit_delay = {
    .id = "delay",
    .name = "DELAY",
    .is_source = false,
    .num_params = 5,
    .param_names = {"TIME", "FDBK", "MIX", "SPRD", "SYNC"},
    .param_defaults = {64, 100, 80, 0, 0},
    .param_enums = {NULL, NULL, NULL, NULL, delay_sync_names},
    .param_enum_count = {0, 0, 0, 0, DELAY_NSYNC},
    .create = delay_create,
    .destroy = delay_destroy,
    .kill = delay_kill,
    .render = delay_render,
};
