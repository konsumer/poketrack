// Turntable source unit — a sample player built to be scratched.
//
// A record player is one continuous motion (the platter) plus one gate (the
// fader). This unit models both, per sample rather than per block:
//
//   * the platter: a gesture generator swings the playback velocity forward
//     and back inside the loop region. DPTH sets how far it swings (0 = the
//     record just plays), RATE how fast, SHPE the gesture's shape. Velocity
//     goes negative on the back half, which is what a scratch actually is.
//   * the fader: CUT attenuates the forward half of the gesture, which turns
//     a baby scratch into a chirp. With DPTH at 0 and CUT up, the record runs
//     steady and the fader chops it — a transform.
//
//      baby scratch    CUT=00, DPTH high        forward/back, open
//      chirp           CUT high, DPTH high      push cut, pull heard
//      tear            SHPE=SAW, DPTH high      hard reverse each cycle
//      transform       DPTH=00, CUT high        steady record, fader chops
//
// Audio-rate matters: an LFO modulating a sampler's speed only moves it once
// per render block (~10ms), so cuts and reverses are stepped. Here the
// platter and fader are computed every sample.
//
// P0 LSTR: 00=0%  FF=100% of sample — scratch region start
// P1 LEND: 00=0%  FF=100% of sample — scratch region end
// P2 TUNE: 00=-12st 80=center FF=+12st
// P3 DPTH: 00=no scratch  FF=3x swing (velocity reaches -2x..+4x of rate)
// P4 RATE: gesture speed, 0.25Hz-16Hz
// P5 SHPE: 0=Sine 1=Tri 2=Saw
// P6 CUT:  00=fader open on both halves  FF=forward half silent
// P7 VOL:  00=0  FF=1
//
// Note held = platter turning and fader open; release stops it.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sample_cache.h"
#include "unit.h"

struct UnitState {
  SampleCacheEntry* smp;  // shared; decoded once per file, not per instance

  float phase;  // read position in the sample
  float rate;   // base playback rate (samples per engine sample)
  float g;      // gesture phase [0,1)
  bool playing;

  float engine_sr;
};

static UnitState* tt_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->engine_sr = sr;
  return s;
}

static void tt_destroy(UnitState* s) {
  sample_cache_release(s->smp);
  free(s);
}

static void tt_set_data(UnitState* s, const char* data, const char* base_dir) {
  char path[1024];
  path[0] = '\0';
  if (data && data[0])
    unit_resolve_path(base_dir, data, path, sizeof(path));

  if (s->smp && strcmp(s->smp->path, path) == 0)
    return;  // already pointing at this file

  sample_cache_release(s->smp);
  s->smp = path[0] ? sample_cache_acquire(path) : NULL;
}

// Warm the shared cache on the main thread, so the first note doesn't decode
// the file on the audio thread.
static void tt_preload(const char* data, const char* base_dir) {
  if (!data || !data[0])
    return;
  char path[1024];
  unit_resolve_path(base_dir, data, path, sizeof(path));
  sample_cache_preload(path);
}

// Region in samples; falls back to the whole sample if the slice is empty.
static void tt_region(const UnitState* s, const uint8_t* p,
                      uint32_t* lo, uint32_t* hi) {
  uint32_t n = s->smp->num_samples;
  float ls = p[0] / 255.0f, le = p[1] / 255.0f;
  if (le < ls)
    le = ls;
  uint32_t a = (uint32_t)(ls * (float)(n - 1));
  uint32_t b = (uint32_t)(le * (float)(n - 1));
  if (b >= n)
    b = n - 1;
  if (b <= a) {  // a scratch needs a span to travel across
    a = 0;
    b = n - 1;
  }
  *lo = a;
  *hi = b;
}

static void tt_note_on(UnitState* s, float pitch, uint8_t vel, const uint8_t* p) {
  (void)vel;
  if (!s->smp || s->smp->num_samples == 0)
    return;

  float tune_semi = p2f_center(p[2], -12.0f, 12.0f);
  float pitch_ratio = powf(2.0f, (pitch + tune_semi - 60.0f) / 12.0f);
  s->rate = pitch_ratio * ((float)s->smp->wav_sr / s->engine_sr);

  uint32_t lo, hi;
  tt_region(s, p, &lo, &hi);
  s->phase = (float)lo;  // needle down at the start of the scratch region
  s->g = 0.0f;
  s->playing = true;
}

static void tt_note_off(UnitState* s, float pitch) {
  (void)pitch;
  s->playing = false;
}

static void tt_kill(UnitState* s) { s->playing = false; }

// Gesture waveform, -1..1. Sine and tri are smooth; saw snaps back, which is
// the hard "tear" reverse.
static float tt_shape(int shape, float g) {
  switch (shape) {
    case 1:
      return 1.0f - 4.0f * fabsf(g - 0.5f);
    case 2:
      return 2.0f * g - 1.0f;
    default:
      return unit_sin(g);
  }
}

static void tt_render(UnitState* s, const uint8_t* p,
                      const float* in_l, const float* in_r,
                      float* out_l, float* out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->playing || !s->smp || s->smp->num_samples == 0)
    return;

  uint32_t lo, hi;
  tt_region(s, p, &lo, &hi);

  float depth = p2f(p[3], 0.0f, 3.0f);                   // velocity swing
  float g_inc = p2f(p[4], 0.25f, 16.0f) / s->engine_sr;  // gesture rate → per sample
  int shape = p[5];
  float cut = p[6] / 255.0f;
  float vol = p2f(p[7], 0.0f, 1.0f);

  const float* samples = s->smp->samples;
  float phase = s->phase;
  float g = s->g;
  float span = (float)(hi - lo);
  uint32_t n = s->smp->num_samples;

  for (uint32_t f = 0; f < frames; f++) {
    float mod = tt_shape(shape, g);
    float vel = s->rate * (1.0f + depth * mod);

    // Fader: cut the forward half of the gesture (the "push"), so what is
    // left is the pull-back — a chirp. With depth 0 this alone chops a
    // steady record, which is a transform.
    float gate = (cut > 0.0f && mod > 0.0f) ? 1.0f - cut : 1.0f;

    // A platter that isn't moving makes no sound (and would be pure DC).
    if (fabsf(vel) > 1e-4f && gate > 0.0f) {
      uint32_t i0 = (uint32_t)phase;
      uint32_t i1 = i0 + 1;
      float frac = phase - (float)i0;
      if (i0 >= n)
        i0 = n - 1;
      if (i1 >= n)
        i1 = n - 1;
      float smp = (samples[i0] * (1.0f - frac) + samples[i1] * frac) * gate * vol;
      out_l[f] += smp;
      out_r[f] += smp;
    }

    phase += vel;
    if (span > 0.0f) {  // wrap the scratch region, either direction
      if (phase >= (float)hi)
        phase -= span;
      else if (phase < (float)lo)
        phase += span;
    }

    g += g_inc;
    if (g >= 1.0f)
      g -= 1.0f;
  }

  s->phase = phase;
  s->g = g;
}

static const char* const tt_shape_names[] = {"SINE", "TRI", "SAW"};

static char tt_fmt_buf[16];
static const char* tt_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx == 4) {  // RATE — show the gesture frequency, not a hex byte
    snprintf(tt_fmt_buf, sizeof(tt_fmt_buf), "%.2f Hz", (double)p2f(val, 0.25f, 16.0f));
    return tt_fmt_buf;
  }
  return NULL;
}

const UnitDef unit_turntable = {
    .id = "turntab",
    .name = "TURNTABLE",
    .data_hint = "record.wav",
    .file_filter = "*.wav *.mp3 *.ogg *.flac",
    .is_source = true,
    .num_params = 8,
    .param_names = {"LSTR", "LEND", "TUNE", "DPTH", "RATE", "SHPE", "CUT", "VOL"},
    .param_defaults = {0x00, 0xFF, 0x80, 0x00, 0x80, 0, 0x00, 0xC8},
    .param_enums = {NULL, NULL, NULL, NULL, NULL, tt_shape_names, NULL, NULL},
    .param_enum_count = {0, 0, 0, 0, 0, 3, 0, 0},
    .format_param_val = tt_format_param,
    .create = tt_create,
    .destroy = tt_destroy,
    .set_data = tt_set_data,
    .preload_data = tt_preload,
    .note_on = tt_note_on,
    .note_off = tt_note_off,
    .kill = tt_kill,
    .render = tt_render,
};
