// Granular synth — loads a WAV file, plays overlapping grains
// data = path to WAV file (mono or stereo, any bit depth raylib supports)
// P0 GSIZE:  00=5ms    FF=500ms  grain duration
// P1 POS:    00=start  FF=end    read position in file
// P2 SPRAY:  00=none   FF=wide   position scatter per grain
// P3 PITCH:  00=-12st  7F=0      FF=+12st  transpose
// P4 ATK:    00=5%     FF=50%    grain attack (fraction of grain)
// P5 REL:    00=5%     FF=50%    grain release (fraction of grain)
// P6 DENS:   00=sparse FF=dense  grain overlap (1x-8x)
// P7 VOL:    00=0      FF=1.0
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "unit.h"

#define GRAN_POLY 4
#define MAX_GRAINS 32

typedef struct {
  float src_pos;
  float pitch;
  float size;
  float phase;
  float vel;
  bool active;
} Grain;

typedef struct {
  bool active;
  uint8_t note;
  float vel;
  float next_grain;
} GranVoice;

struct UnitState {
  float *buf_l;
  float *buf_r;
  uint32_t buf_len;
  bool stereo;
  float sample_rate;
  char path[512];
  Grain grains[MAX_GRAINS];
  GranVoice voices[GRAN_POLY];
  uint32_t rng;
};

static float gran_rand(uint32_t *r) {
  *r = *r * 1664525u + 1013904223u;
  return (float)(*r & 0x7fffffff) / (float)0x7fffffff;
}

static UnitState *gran_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->rng = 0xdeadbeef;
  return s;
}

static void gran_destroy(UnitState *s) {
  free(s->buf_l);
  free(s->buf_r);
  free(s);
}

static void gran_set_data(UnitState *s, const char *data, const char *base_dir) {
  if (!data || !data[0])
    return;
  unit_resolve_path(base_dir, data, s->path, sizeof(s->path));

  free(s->buf_l);
  s->buf_l = NULL;
  free(s->buf_r);
  s->buf_r = NULL;
  s->buf_len = 0;

  Wave wav = LoadWave(s->path);
  if (!wav.data || !wav.frameCount)
    return;

  // LoadWaveSamples returns interleaved float32 normalised to [-1,1]
  float *samples = LoadWaveSamples(wav);
  uint32_t frames = wav.frameCount;
  int ch = (int)wav.channels;
  s->stereo = (ch >= 2);

  s->buf_l = malloc(frames * sizeof(float));
  s->buf_r = malloc(frames * sizeof(float));
  if (s->buf_l && s->buf_r) {
    for (uint32_t i = 0; i < frames; i++) {
      s->buf_l[i] = samples[i * ch];
      s->buf_r[i] = (ch >= 2) ? samples[i * ch + 1] : s->buf_l[i];
    }
    s->buf_len = frames;
  } else {
    free(s->buf_l);
    s->buf_l = NULL;
    free(s->buf_r);
    s->buf_r = NULL;
  }

  UnloadWaveSamples(samples);
  UnloadWave(wav);
}

static void spawn_grain(UnitState *s, GranVoice *v, const uint8_t *p) {
  if (!s->buf_len)
    return;
  Grain *g = NULL;
  for (int i = 0; i < MAX_GRAINS; i++)
    if (!s->grains[i].active) {
      g = &s->grains[i];
      break;
    }
  if (!g)
    return;

  float pos_center = p2f(p[1], 0.0f, 1.0f) * (s->buf_len - 1);
  float spray = p2f(p[2], 0.0f, 0.5f) * s->buf_len;
  float offset = (gran_rand(&s->rng) * 2.0f - 1.0f) * spray;
  float src_pos = pos_center + offset;
  if (src_pos < 0)
    src_pos = 0;
  if (src_pos >= s->buf_len)
    src_pos = (float)(s->buf_len - 1);

  float det = p2f(p[3], -12.0f, 12.0f);
  float note_pitch = powf(2.0f, (v->note - 60 + det) / 12.0f);

  float grain_ms = p2f(p[0], 5.0f, 500.0f);
  float grain_samp = grain_ms * 0.001f * s->sample_rate;

  *g = (Grain){
      .src_pos = src_pos,
      .pitch = note_pitch,
      .size = grain_samp,
      .phase = 0.0f,
      .vel = v->vel,
      .active = true,
  };
}

static void gran_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  GranVoice *v = NULL;
  for (int i = 0; i < GRAN_POLY; i++)
    if (!s->voices[i].active) {
      v = &s->voices[i];
      break;
    }
  if (!v)
    v = &s->voices[0];
  *v = (GranVoice){.active = true, .note = note, .vel = vel / 127.0f, .next_grain = 0};
  spawn_grain(s, v, p);
}

static void gran_note_off(UnitState *s, uint8_t note) {
  for (int i = 0; i < GRAN_POLY; i++)
    if (s->voices[i].active && s->voices[i].note == note)
      s->voices[i].active = false;
}

static void gran_kill(UnitState *s) {
  memset(s->voices, 0, sizeof(s->voices));
  memset(s->grains, 0, sizeof(s->grains));
}

static inline float gran_env(float phase, float atk_f, float rel_f) {
  if (atk_f > 0.0f && phase < atk_f)
    return phase / atk_f;
  if (rel_f > 0.0f && phase > (1.0f - rel_f))
    return (1.0f - phase) / rel_f;
  return 1.0f;
}

static void gran_render(UnitState *s, const uint8_t *p,
                        const float *in_l, const float *in_r,
                        float *out_l, float *out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->buf_len)
    return;

  float grain_ms = p2f(p[0], 5.0f, 500.0f);
  float grain_samp = grain_ms * 0.001f * s->sample_rate;
  float dens = p2f(p[6], 0.125f, 1.0f);  // spawn interval as fraction of grain size
  float spawn_int = grain_samp * dens;
  float atk_f = p2f(p[4], 0.05f, 0.50f);
  float rel_f = p2f(p[5], 0.05f, 0.50f);
  if (atk_f + rel_f > 0.95f) {
    float s2 = 0.95f / (atk_f + rel_f);
    atk_f *= s2;
    rel_f *= s2;
  }
  float vol = p2f(p[7], 0.0f, 1.0f);

  for (uint32_t f = 0; f < frames; f++) {
    // Advance voice spawn timers
    for (int i = 0; i < GRAN_POLY; i++) {
      GranVoice *v = &s->voices[i];
      if (!v->active)
        continue;
      v->next_grain -= 1.0f;
      if (v->next_grain <= 0.0f) {
        spawn_grain(s, v, p);
        v->next_grain = spawn_int;
      }
    }

    // Render grains
    float sl = 0, sr_val = 0;
    for (int i = 0; i < MAX_GRAINS; i++) {
      Grain *g = &s->grains[i];
      if (!g->active)
        continue;

      float env = gran_env(g->phase, atk_f, rel_f);

      // Bounds-check before array access (NaN/overshoot guard)
      if (g->src_pos < 0.0f || g->src_pos >= (float)s->buf_len) {
        g->active = false;
        continue;
      }
      uint32_t idx = (uint32_t)g->src_pos;
      if (idx >= s->buf_len) {
        g->active = false;
        continue;
      }
      float frac = g->src_pos - idx;
      uint32_t idx2 = (idx + 1 < s->buf_len) ? idx + 1 : idx;
      float sl1 = s->buf_l[idx] + frac * (s->buf_l[idx2] - s->buf_l[idx]);
      float sr1 = s->buf_r[idx] + frac * (s->buf_r[idx2] - s->buf_r[idx]);

      sl += sl1 * env * g->vel;
      sr_val += sr1 * env * g->vel;

      g->src_pos += g->pitch;
      g->phase += 1.0f / g->size;
      if (g->phase >= 1.0f || g->src_pos >= s->buf_len)
        g->active = false;
    }

    out_l[f] += sl * vol;
    out_r[f] += sr_val * vol;
  }
}

const UnitDef unit_gran = {
    .id = "gran",
    .name = "GRAN",
    .data_hint = "sample.wav",
    .file_filter = "*.wav *.mp3 *.ogg *.flac",
    .is_source = true,
    .num_params = 8,
    .param_names = {"GSIZ", "POS", "SPRY", "PTCH", "ATK", "REL", "DENS", "VOL"},
    .param_defaults = {25, 128, 20, 128, 12, 12, 64, 200},
    .create = gran_create,
    .destroy = gran_destroy,
    .set_data = gran_set_data,
    .note_on = gran_note_on,
    .note_off = gran_note_off,
    .kill = gran_kill,
    .render = gran_render,
};
