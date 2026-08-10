// LFO param modulator — modulates a param on any instrument each render block
// P0 RATE:  00=0.1Hz  FF=20Hz (used when SYNC=FREE)
// P1 SHPE:  0=Sine 1=Square 2=Saw 3=Tri
// P2 INST:  target instrument 0-FF
// P3 PARAM: target param (global index across chain slots, 0-FF)
// P4 CNTR:  center value for modulation (default 0x80)
// P5 DPTH:  00=0      FF=127 (added to/subtracted from center)
// P6 ON:    0=off 1=on
// P7 SYNC:  FREE=use RATE, else lock the cycle to a note division of the
//           song tempo (4/1 = one cycle per 4 bars, 1/8 = eight cycles/bar),
//           via g_unit_samples_per_line (see unit.h).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

struct UnitState {
  float phase;
  float sample_rate;
  uint32_t last_epoch;  // last-seen g_unit_play_epoch, to catch play-start
};

static UnitState* lfo_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void lfo_destroy(UnitState* s) { free(s); }

// Index 0 is FREE (RATE param governs instead); the rest are multiples of a
// whole note (bar), matching chopper.c's SIZE table.
static const char* const lfo_sync_names[] = {"FREE", "4/1", "2/1", "1/1", "1/2",
                                             "1/4", "1/8", "1/16", "1/32"};
static const float lfo_sync_mult[] = {4, 2, 1, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
#define LFO_NSYNC ((int)(sizeof(lfo_sync_names) / sizeof(lfo_sync_names[0])))

static void lfo_render(UnitState* s, const uint8_t* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r, uint32_t frames) {
  // Pass audio through unchanged (effects usually render in-place: skip the copy)
  if (in_l && out_l != in_l)
    memcpy(out_l, in_l, frames * sizeof(float));
  if (in_r && out_r != in_r)
    memcpy(out_r, in_r, frames * sizeof(float));

  if (!p[6])
    return;

  int shape = p[1] > 3 ? 3 : p[1];
  int inst = p[2];
  int prm = p[3];
  float cntr = (float)p[4];
  float depth = p[5] / 2.0f;  // 0-127 swing each side

  int sync = p[7] > LFO_NSYNC - 1 ? LFO_NSYNC - 1 : p[7];

  // Snap back onto the bar grid at play-start, so a synced LFO always starts
  // its cycle from the same point instead of free-running from wherever it
  // happened to be left. Tracked regardless of sync mode so toggling SYNC
  // mid-playback doesn't itself trigger a reset.
  bool play_started = s->last_epoch != g_unit_play_epoch;
  s->last_epoch = g_unit_play_epoch;
  if (sync != 0 && play_started)
    s->phase = 0;

  float phase_inc;
  uint32_t spl = g_unit_samples_per_line;
  if (sync == 0 || !spl) {
    // FREE (or tempo not known yet): plain Hz rate.
    float rate = p2f(p[0], 0.1f, 20.0f);
    phase_inc = rate / s->sample_rate;
  } else {
    // One cycle every (whole-note-in-samples * multiplier).
    float period_samples = (float)spl * 16.0f * lfo_sync_mult[sync - 1];
    phase_inc = 1.0f / period_samples;
  }

  // Compute LFO at midpoint of block for param update (one write per block is fine)
  float phase = s->phase + phase_inc * (frames / 2.0f);
  if (phase >= 1.0f)
    phase -= (float)(int)phase;

  float raw = cntr + unit_lfo_wave(shape, phase) * depth;
  uint8_t val = raw < 0.0f ? 0 : raw > 255.0f ? 255
                                              : (uint8_t)raw;
  audio_mod_set_param(inst, prm, val);

  // Advance phase over full block
  s->phase += phase_inc * frames;
  while (s->phase >= 1.0f) s->phase -= 1.0f;
}

static void lfo_init_params(uint8_t* params, int inst_idx) {
  params[2] = (uint8_t)(inst_idx & 0xFF);
}

static const char* const lfo_shape_names[] = {"SINE", "SQR", "SAW", "TRI"};
static const char* const lfo_on_names[] = {"OFF", "ON"};

const UnitDef unit_lfo = {
    .id = "lfo",
    .name = "LFO",
    .is_source = false,
    .num_params = 8,
    .param_names = {"RATE", "SHPE", "INST", "PARAM", "CNTR", "DPTH", "ON", "SYNC"},
    .param_defaults = {0x20, 0, 0, 0, 0x80, 0x40, 0, 0},
    .param_enums = {NULL, lfo_shape_names, NULL, NULL, NULL, NULL, lfo_on_names, lfo_sync_names},
    .param_enum_count = {0, 4, 0, 0, 0, 0, 2, LFO_NSYNC},
    .init_params = lfo_init_params,
    .create = lfo_create,
    .destroy = lfo_destroy,
    .render = lfo_render,
};
