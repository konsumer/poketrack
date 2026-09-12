// Sample player source unit — WAV/MP3/OGG/FLAC via raylib
// P0 LOOP:   0=Off 1=Fwd 2=PingPong 3=Rev
// P1 LSTART: 00=0%  FF=100% of sample
// P2 LEND:   00=0%  FF=100% of sample
// P3 TUNE:   00=-12st  80=center  FF=+12st
// P4 STRT:   00=0%     FF=100% of sample (play start offset)
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sample_cache.h"
#include "unit.h"

#define LOOP_OFF 0
#define LOOP_FWD 1
#define LOOP_PINGPONG 2
#define LOOP_REV 3

struct UnitState {
  SampleCacheEntry* smp;  // shared; decoded once per file, not per instance

  float phase;    // current read position (float for interpolation)
  float rate;     // playback rate (samples per engine sample)
  int direction;  // +1 or -1 (for ping-pong)
  bool playing;

  float engine_sr;
};

static UnitState* sampler_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->engine_sr = sr;
  s->direction = 1;
  return s;
}

static void sampler_destroy(UnitState* s) {
  sample_cache_release(s->smp);
  free(s);
}

static void sampler_set_data(UnitState* s, const char* data, const char* base_dir) {
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
static void sampler_preload(const char* data, const char* base_dir) {
  if (!data || !data[0])
    return;
  char path[1024];
  unit_resolve_path(base_dir, data, path, sizeof(path));
  sample_cache_preload(path);
}

static void sampler_note_on(UnitState* s, float pitch, uint8_t vel, const uint8_t* p) {
  (void)vel;
  if (!s->smp || s->smp->num_samples == 0)
    return;

  float tune_semi = p2f_center(p[3], -12.0f, 12.0f);
  float pitch_ratio = powf(2.0f, (pitch + tune_semi - 60.0f) / 12.0f);
  s->rate = pitch_ratio * ((float)s->smp->wav_sr / s->engine_sr);
  s->phase = (p[4] / 255.0f) * (float)(s->smp->num_samples - 1);
  s->direction = 1;
  s->playing = true;
}

static void sampler_note_off(UnitState* s, float pitch) {
  (void)pitch;
  // Only stop if not looping
  // We don't have params here; kill for non-looping handled in render
  // by checking loop mode. For simplicity, note_off just sets a flag
  // but render will check loop mode and stop only if LOOP_OFF.
  // We signal note_off via playing=false only if no looping — but we
  // don't have params here. Use a flag.
  s->playing = false;
}

static void sampler_kill(UnitState* s) {
  s->playing = false;
}

static void sampler_render(UnitState* s, const uint8_t* p,
                           const float* in_l, const float* in_r,
                           float* out_l, float* out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->playing || !s->smp || s->smp->num_samples == 0)
    return;

  int loop_mode = p[0];
  if (loop_mode > 3)
    loop_mode = 3;

  float ls_frac = p[1] / 255.0f;
  float le_frac = p[2] / 255.0f;
  if (le_frac < ls_frac)
    le_frac = ls_frac;

  uint32_t n = s->smp->num_samples;
  uint32_t loop_start = (uint32_t)(ls_frac * (n - 1));
  uint32_t loop_end = (uint32_t)(le_frac * (n - 1));
  if (loop_end >= n)
    loop_end = n - 1;
  if (loop_start > loop_end)
    loop_start = loop_end;

  const float* samples = s->smp->samples;
  float phase = s->phase;
  int dir = s->direction;
  float rate = s->rate;

  for (uint32_t f = 0; f < frames; f++) {
    if (!s->playing)
      break;

    // Linear interpolate
    uint32_t i0 = (uint32_t)phase;
    uint32_t i1 = i0 + 1;
    float frac = phase - (float)i0;
    if (i0 >= n)
      i0 = n - 1;
    if (i1 >= n)
      i1 = n - 1;
    float smp = samples[i0] * (1.0f - frac) + samples[i1] * frac;
    out_l[f] += smp;
    out_r[f] += smp;

    // Advance phase
    if (loop_mode == LOOP_REV) {
      phase -= rate;
      if (phase < (float)loop_start) {
        phase = (float)loop_end;
      }
    } else {
      phase += rate * (float)dir;

      switch (loop_mode) {
        case LOOP_OFF:
          if (phase >= (float)n) {
            s->playing = false;
          }
          break;
        case LOOP_FWD:
          if (phase >= (float)loop_end) {
            phase = (float)loop_start;
          }
          break;
        case LOOP_PINGPONG:
          if (dir > 0 && phase >= (float)loop_end) {
            phase = (float)loop_end;
            dir = -1;
          } else if (dir < 0 && phase <= (float)loop_start) {
            phase = (float)loop_start;
            dir = 1;
          }
          break;
      }
    }
  }

  s->phase = phase;
  s->direction = dir;
}

static const char* const sampler_loop_names[] = {"OFF", "FWD", "PING", "REV"};

const UnitDef unit_sampler = {
    .id = "sampler",
    .name = "SAMPLER",
    .data_hint = "sample.wav",
    .file_filter = "*.wav *.mp3 *.ogg *.flac",
    .is_source = true,
    .num_params = 5,
    .param_names = {"LOOP", "LSTR", "LEND", "TUNE", "STRT"},
    .param_defaults = {0, 0x00, 0xFF, 0x80, 0x00},
    .param_enums = {sampler_loop_names},
    .param_enum_count = {4},
    .create = sampler_create,
    .destroy = sampler_destroy,
    .set_data = sampler_set_data,
    .preload_data = sampler_preload,
    .note_on = sampler_note_on,
    .note_off = sampler_note_off,
    .kill = sampler_kill,
    .render = sampler_render,
};
