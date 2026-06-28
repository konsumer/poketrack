// SFZ soundfont player unit
// data field = path to .sfz file
// P0 VOL:  00=silent  FF=full
// P1 PAN:  00=L  7F=center  FF=R
// P2 TRAN: 00=-24st  80=0  FF=+24st (translate incoming note → selects region/key)
// P3 TUNE: 00=-100c  80=0  FF=+100c (cents fine tune, resamples pitch)
#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

#define SFZ_MAX_REGIONS 512
#define SFZ_MAX_VOICES 16
#define SFZ_PATH_LEN 512
#define SFZ_LINE_LEN 1024

typedef struct {
  float *samples;
  uint32_t num_samples;
  uint32_t wav_sr;
  int lokey, hikey;
  int pitch_keycenter;
  int lovel, hivel;
  float volume_db;  // additive dB gain from region
  float pan;        // -1..1
  float tune;       // semitones
  float ampeg_attack;
  float ampeg_release;
} SfzRegion;

typedef struct {
  int region_idx;  // -1 = inactive
  uint8_t note;       // original note (for note_off matching)
  uint8_t play_note;  // TRAN-translated note (for region select + pitch)
  float phase;
  float vel_gain;  // velocity → amplitude [0..1]
  float env;       // current envelope gain [0..1]
  bool releasing;
  float release_rate;
} SfzVoice;

// Shared parsed SFZ data (regions + sample buffers). Multiple UnitState instances
// pointing at the same file share one SfzShared entry via refcounting.
typedef struct SfzShared {
  char path[SFZ_PATH_LEN];
  SfzRegion regions[SFZ_MAX_REGIONS];
  int num_regions;
  int refs;
} SfzShared;

#define SFZ_CACHE_MAX 32
static SfzShared *sfz_cache[SFZ_CACHE_MAX];

struct UnitState {
  SfzShared *shared;
  SfzVoice voices[SFZ_MAX_VOICES];
  float engine_sr;
};

// --- audio loading (same pattern as sampler.c) ---

static float *load_audio(const char *path, uint32_t *out_count, uint32_t *out_sr) {
  Wave w = LoadWave(path);
  if (w.frameCount == 0 || !w.data)
    return NULL;
  *out_sr = w.sampleRate;
  WaveFormat(&w, w.sampleRate, 32, 1);
  float *smp = LoadWaveSamples(w);
  uint32_t n = w.frameCount;
  UnloadWave(w);
  if (!smp)
    return NULL;
  float *out = malloc(n * sizeof(float));
  if (!out) {
    UnloadWaveSamples(smp);
    return NULL;
  }
  memcpy(out, smp, n * sizeof(float));
  UnloadWaveSamples(smp);
  *out_count = n;
  return out;
}

// --- SFZ parser ---

static void trim(char *s) {
  char *e = s + strlen(s) - 1;
  while (e >= s && isspace((unsigned char)*e))
    *e-- = '\0';
  char *p = s;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (p != s)
    memmove(s, p, strlen(p) + 1);
}

static int note_name_to_midi(const char *s) {
  // Accept plain integer or note name like C4, C#4, Db4
  if (isdigit((unsigned char)s[0]) || s[0] == '-')
    return atoi(s);
  static const char *names = "C D EF G A B";
  int note = -1;
  for (int i = 0; i < 12; i++) {
    if (names[i] != ' ' && tolower((unsigned char)s[0]) == tolower((unsigned char)names[i])) {
      note = i;
      break;
    }
  }
  if (note < 0)
    return atoi(s);
  int p = 1;
  if (s[p] == '#') {
    note++;
    p++;
  } else if (s[p] == 'b' || s[p] == 'B') {
    note--;
    p++;
  }
  int octave = atoi(s + p);
  return (octave + 1) * 12 + note;
}

// Defaults for a region (before group/global opcodes applied)
static void region_defaults(SfzRegion *r) {
  r->lokey = 0;
  r->hikey = 127;
  r->pitch_keycenter = 60;
  r->lovel = 0;
  r->hivel = 127;
  r->volume_db = 0.0f;
  r->pan = 0.0f;
  r->tune = 0.0f;
  r->ampeg_attack = 0.001f;
  r->ampeg_release = 0.1f;
  r->samples = NULL;
  r->num_samples = 0;
  r->wav_sr = 44100;
}

// Apply one key=value opcode to a region
static void apply_opcode(SfzRegion *r, const char *key, const char *val,
                         const char *sfz_dir) {
  if (strcmp(key, "sample") == 0) {
    char path[SFZ_PATH_LEN];
    // SFZ uses backslash on Windows; normalize
    char v[SFZ_PATH_LEN];
    strncpy(v, val, sizeof(v) - 1);
    for (char *p = v; *p; p++)
      if (*p == '\\')
        *p = '/';
    unit_resolve_path(sfz_dir, v, path, sizeof(path));
    if (r->samples)
      free(r->samples);
    r->samples = NULL;
    r->num_samples = 0;
    r->wav_sr = 44100;
    r->samples = load_audio(path, &r->num_samples, &r->wav_sr);
  } else if (strcmp(key, "lokey") == 0) {
    r->lokey = note_name_to_midi(val);
  } else if (strcmp(key, "hikey") == 0) {
    r->hikey = note_name_to_midi(val);
  } else if (strcmp(key, "key") == 0) {
    int k = note_name_to_midi(val);
    r->lokey = r->hikey = r->pitch_keycenter = k;
  } else if (strcmp(key, "pitch_keycenter") == 0) {
    r->pitch_keycenter = note_name_to_midi(val);
  } else if (strcmp(key, "lovel") == 0) {
    r->lovel = atoi(val);
  } else if (strcmp(key, "hivel") == 0) {
    r->hivel = atoi(val);
  } else if (strcmp(key, "volume") == 0) {
    r->volume_db = (float)atof(val);
  } else if (strcmp(key, "pan") == 0) {
    r->pan = (float)atof(val) / 100.0f;  // SFZ pan: -100..100
  } else if (strcmp(key, "tune") == 0) {
    r->tune = (float)atof(val) / 100.0f;  // SFZ tune: cents → semitones
  } else if (strcmp(key, "ampeg_attack") == 0) {
    r->ampeg_attack = (float)atof(val);
    if (r->ampeg_attack < 0.001f)
      r->ampeg_attack = 0.001f;
  } else if (strcmp(key, "ampeg_release") == 0) {
    r->ampeg_release = (float)atof(val);
    if (r->ampeg_release < 0.001f)
      r->ampeg_release = 0.001f;
  }
}

static void sfz_parse(SfzShared *sh, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return;

  // Extract dir from path for relative sample paths
  char sfz_dir[SFZ_PATH_LEN] = {0};
  strncpy(sfz_dir, path, sizeof(sfz_dir) - 1);
  char *slash = strrchr(sfz_dir, '/');
#ifdef _WIN32
  char *bslash = strrchr(sfz_dir, '\\');
  if (bslash > slash)
    slash = bslash;
#endif
  if (slash)
    *(slash + 1) = '\0';
  else
    sfz_dir[0] = '\0';

  sh->num_regions = 0;

  // group/global opcodes inherit into each new region
  SfzRegion group_defaults;
  region_defaults(&group_defaults);

  SfzRegion cur;
  region_defaults(&cur);
  int in_region = 0;

  char line[SFZ_LINE_LEN];
  while (fgets(line, sizeof(line), f)) {
    // Strip comment
    char *comment = strstr(line, "//");
    if (comment)
      *comment = '\0';
    trim(line);
    if (!line[0])
      continue;

    // Tokenize: headers and opcodes can appear on same line
    char *p = line;
    while (*p) {
      // Skip whitespace
      while (*p && isspace((unsigned char)*p))
        p++;
      if (!*p)
        break;

      if (*p == '<') {
        // Header
        char *end = strchr(p, '>');
        if (!end)
          break;
        *end = '\0';
        char *hdr = p + 1;
        trim(hdr);

        if (strcmp(hdr, "region") == 0) {
          if (in_region && cur.samples && sh->num_regions < SFZ_MAX_REGIONS) {
            sh->regions[sh->num_regions++] = cur;
          } else if (in_region && cur.samples) {
            // at max; free this one
            free(cur.samples);
          }
          // Copy group defaults as starting point
          cur = group_defaults;
          cur.samples = NULL;
          cur.num_samples = 0;
          cur.wav_sr = 44100;
          in_region = 1;
        } else if (strcmp(hdr, "group") == 0 || strcmp(hdr, "global") == 0) {
          if (in_region && cur.samples && sh->num_regions < SFZ_MAX_REGIONS) {
            sh->regions[sh->num_regions++] = cur;
          } else if (in_region && cur.samples) {
            free(cur.samples);
          }
          in_region = 0;
          region_defaults(&group_defaults);
        }
        p = end + 1;
        continue;
      }

      // Try to parse key=value
      char *eq = strchr(p, '=');
      if (!eq)
        break;

      // Key is everything before '=' (no spaces, stop at space before '=')
      char key[64] = {0};
      char *kp = p;
      int ki = 0;
      while (kp < eq && !isspace((unsigned char)*kp) && ki < 63)
        key[ki++] = *kp++;
      key[ki] = '\0';

      // Value: after '=', up to next key= or header or end of string
      char *vs = eq + 1;
      while (*vs && isspace((unsigned char)*vs))
        vs++;

      // Find end of value: look for next unquoted space-then-word=
      // Simple: scan forward for pattern: whitespace + non-space-chars + '='
      char *ve = vs;
      char val[SFZ_PATH_LEN] = {0};
      // Collect value until next opcode key= or end of token
      // A new opcode starts when we see \s+\S+=
      char *next_key = NULL;
      for (char *sp = vs; *sp; sp++) {
        if (isspace((unsigned char)*sp)) {
          // Check if next non-space chars form a key=
          char *nk = sp + 1;
          while (*nk && isspace((unsigned char)*nk))
            nk++;
          if (*nk && *nk != '<') {
            char *neq = strchr(nk, '=');
            if (neq) {
              // Make sure there's no space between nk and neq
              int has_space = 0;
              for (char *c = nk; c < neq; c++)
                if (isspace((unsigned char)*c)) {
                  has_space = 1;
                  break;
                }
              if (!has_space) {
                next_key = sp;
                break;
              }
            }
          }
        }
      }
      (void)ve;
      if (next_key) {
        int len = (int)(next_key - vs);
        if (len >= (int)sizeof(val))
          len = sizeof(val) - 1;
        strncpy(val, vs, len);
        val[len] = '\0';
        trim(val);
        p = next_key;
      } else {
        strncpy(val, vs, sizeof(val) - 1);
        trim(val);
        p = vs + strlen(vs);
      }

      if (key[0] && val[0]) {
        if (in_region)
          apply_opcode(&cur, key, val, sfz_dir);
        else
          apply_opcode(&group_defaults, key, val, sfz_dir);
      }
    }
  }

  // Flush last region
  if (in_region && cur.samples && sh->num_regions < SFZ_MAX_REGIONS) {
    sh->regions[sh->num_regions++] = cur;
  } else if (in_region && cur.samples) {
    free(cur.samples);
  }

  fclose(f);
}

// --- shared cache ---

static SfzShared *sfz_shared_acquire(const char *path) {
  for (int i = 0; i < SFZ_CACHE_MAX; i++) {
    if (sfz_cache[i] && strcmp(sfz_cache[i]->path, path) == 0) {
      sfz_cache[i]->refs++;
      return sfz_cache[i];
    }
  }
  SfzShared *sh = calloc(1, sizeof(SfzShared));
  if (!sh)
    return NULL;
  strncpy(sh->path, path, sizeof(sh->path) - 1);
  sh->refs = 1;
  sfz_parse(sh, path);
  for (int i = 0; i < SFZ_CACHE_MAX; i++) {
    if (!sfz_cache[i]) {
      sfz_cache[i] = sh;
      return sh;
    }
  }
  // Cache full: return detached (won't be shared but still works)
  return sh;
}

static void sfz_shared_release(SfzShared *sh) {
  if (!sh)
    return;
  if (--sh->refs > 0)
    return;
  for (int i = 0; i < SFZ_MAX_REGIONS; i++)
    free(sh->regions[i].samples);
  for (int i = 0; i < SFZ_CACHE_MAX; i++) {
    if (sfz_cache[i] == sh) {
      sfz_cache[i] = NULL;
      break;
    }
  }
  free(sh);
}

// --- unit lifecycle ---

static UnitState *sfz_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->engine_sr = sr;
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
  return s;
}

static void sfz_destroy(UnitState *s) {
  sfz_shared_release(s->shared);
  free(s);
}

static void sfz_set_data(UnitState *s, const char *data, const char *base_dir) {
  const char *rel = (data && data[0]) ? data : "instrument.sfz";
  char path[SFZ_PATH_LEN];
  unit_resolve_path(base_dir, rel, path, sizeof(path));
  if (s->shared && strcmp(s->shared->path, path) == 0)
    return;
  sfz_shared_release(s->shared);
  s->shared = sfz_shared_acquire(path);
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
}

static void sfz_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  if (!s->shared || s->shared->num_regions == 0)
    return;

  // P2 TRAN: integer semitone translation of the incoming note. This shifts which
  // region/key is selected (e.g. retarget a drum pattern onto a kit mapped an
  // octave lower), rather than resampling the pitch like TUNE does. The original
  // note is kept for note_off matching (note_off isn't passed params).
  int trans = (int)lroundf(p2f(p[2], -24.0f, 24.0f));
  int tnote = (int)note + trans;
  if (tnote < 0)
    tnote = 0;
  if (tnote > 127)
    tnote = 127;
  uint8_t pnote = (uint8_t)tnote;

  // Tracker uses 0-255 velocity; SFZ lovel/hivel are 0-127
  int vel127 = (int)vel * 127 / 255;

  // Find matching region(s) — play all that match (SFZ can have multiple)
  for (int ri = 0; ri < s->shared->num_regions; ri++) {
    SfzRegion *r = &s->shared->regions[ri];
    if (!r->samples)
      continue;
    if (pnote < r->lokey || pnote > r->hikey)
      continue;
    if (vel127 < r->lovel || vel127 > r->hivel)
      continue;

    // Find free voice (steal oldest if none)
    int vi = -1;
    for (int i = 0; i < SFZ_MAX_VOICES; i++) {
      if (s->voices[i].region_idx < 0) {
        vi = i;
        break;
      }
    }
    if (vi < 0)
      vi = 0;  // steal voice 0

    SfzVoice *v = &s->voices[vi];
    v->region_idx = ri;
    v->note = note;
    v->play_note = pnote;
    v->phase = 0.0f;
    v->vel_gain = vel127 / 127.0f;  // velocity → amplitude
    v->env = 0.0f;
    v->releasing = false;
    // Pitch/rate is recomputed in render to track the live TUNE param; only the
    // release rate (fixed at note-on by the region's envelope) is cached here.
    v->release_rate = 1.0f / (r->ampeg_release * s->engine_sr);
  }
}

static void sfz_note_off(UnitState *s, uint8_t note) {
  for (int i = 0; i < SFZ_MAX_VOICES; i++) {
    SfzVoice *v = &s->voices[i];
    if (v->region_idx >= 0 && v->note == note && !v->releasing) {
      v->releasing = true;
    }
  }
}

static void sfz_kill(UnitState *s) {
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
}

static void sfz_render(UnitState *s, const uint8_t *p,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->shared || s->shared->num_regions == 0)
    return;

  float vol = p2f(p[0], 0.0f, 1.0f);
  float pan = p2f(p[1], -1.0f, 1.0f);
  // P2 TRAN is applied at note-on (translates note → region select), not here.
  float tune = p2f(p[3], -100.0f, 100.0f) / 100.0f;  // semitones

  float pan_l = (pan <= 0.0f) ? 1.0f : 1.0f - pan;
  float pan_r = (pan >= 0.0f) ? 1.0f : 1.0f + pan;

  for (int vi = 0; vi < SFZ_MAX_VOICES; vi++) {
    SfzVoice *v = &s->voices[vi];
    if (v->region_idx < 0)
      continue;
    SfzRegion *r = &s->shared->regions[v->region_idx];
    if (!r->samples || r->num_samples == 0) {
      v->region_idx = -1;
      continue;
    }

    float rpan_l = (r->pan <= 0.0f) ? 1.0f : 1.0f - r->pan;
    float rpan_r = (r->pan >= 0.0f) ? 1.0f : 1.0f + r->pan;
    float rvol = powf(10.0f, r->volume_db / 20.0f);

    // Recompute rate with global fine tune (TRAN already baked into v->play_note)
    float pitch_ratio = powf(2.0f, (v->play_note - r->pitch_keycenter + r->tune + tune) / 12.0f);
    float rate = pitch_ratio * ((float)r->wav_sr / s->engine_sr);

    float attack_rate = 1.0f / (r->ampeg_attack * s->engine_sr);
    float release_rate = v->release_rate;

    float phase = v->phase;
    float env = v->env;
    bool releasing = v->releasing;

    for (uint32_t f = 0; f < frames; f++) {
      // Envelope
      if (releasing) {
        env -= release_rate;
        if (env <= 0.0f) {
          env = 0.0f;
          v->region_idx = -1;
          break;
        }
      } else {
        if (env < 1.0f) {
          env += attack_rate;
          if (env > 1.0f)
            env = 1.0f;
        }
      }

      // Sample read with linear interpolation
      uint32_t i0 = (uint32_t)phase;
      uint32_t i1 = i0 + 1;
      if (i0 >= r->num_samples) {
        v->region_idx = -1;
        break;
      }
      if (i1 >= r->num_samples)
        i1 = r->num_samples - 1;
      float frac = phase - (float)i0;
      float smp = r->samples[i0] * (1.0f - frac) + r->samples[i1] * frac;

      float out = smp * env * rvol * vol * v->vel_gain;
      out_l[f] += out * pan_l * rpan_l;
      out_r[f] += out * pan_r * rpan_r;

      phase += rate;
      if (phase >= (float)r->num_samples) {
        v->region_idx = -1;
        break;
      }
    }

    v->phase = phase;
    v->env = env;
    v->releasing = releasing;
  }
}

const UnitDef unit_sfz = {
    .id = "sfz",
    .name = "SFZ",
    .data_hint = "instrument.sfz",
    .file_filter = "*.sfz",
    .is_source = true,
    .num_params = 4,
    .param_names = {"VOL", "PAN", "TRAN", "TUNE"},
    .param_defaults = {200, 128, 128, 128},
    .create = sfz_create,
    .destroy = sfz_destroy,
    .set_data = sfz_set_data,
    .note_on = sfz_note_on,
    .note_off = sfz_note_off,
    .kill = sfz_kill,
    .render = sfz_render,
};
