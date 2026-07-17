// SFZ soundfont player unit
// data field = path to .sfz file
// P0 VOL:  00=silent  FF=full
// P1 PAN:  00=L  7F=center  FF=R
// P2 TRAN: 00=-24st  80=0  FF=+24st (translate incoming note → selects region/key)
// P3 TUNE: 00=-100c  80=0  FF=+100c (cents fine tune, resamples pitch)
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <strings.h>
#endif

#include "miniz.c"  // single-file zip reader, used to load .sfz bundled inside a .zip
#include "unit.h"

#define SFZ_MAX_REGIONS 512
#define SFZ_MAX_VOICES 16
#define SFZ_PATH_LEN 512
#define SFZ_LINE_LEN 1024
#define SFZ_MAX_SAMPLE_POOL 256  // distinct sample files per instrument (dedup shared slices)
#define SFZ_MAX_MACROS 64
#define SFZ_MAX_INCLUDE_DEPTH 8

typedef enum { SFZ_LOOP_NONE = 0,
               SFZ_LOOP_CONTINUOUS,
               SFZ_LOOP_ONE_SHOT } SfzLoopMode;

typedef struct {
  float* samples;        // borrowed pointer into the shared sample pool (not owned by the region)
  uint32_t num_samples;  // full length of the underlying file
  uint32_t wav_sr;
  int lokey, hikey;
  int pitch_keycenter;
  int lovel, hivel;
  float volume_db;  // additive dB gain from region
  float pan;        // -1..1
  float tune;       // semitones
  float ampeg_attack;
  float ampeg_hold;
  float ampeg_decay;
  float ampeg_sustain;  // 0..100 (percent of full level)
  float ampeg_release;
  float amp_veltrack;  // -100..100, default 100 (full velocity tracking)
  uint32_t offset;     // first sample frame to play (slice start)
  uint32_t end;        // last sample frame to play, inclusive (slice end)
  bool has_end;
  SfzLoopMode loop_mode;
  uint32_t loop_start;
  uint32_t loop_end;  // inclusive
  bool has_loop_end;
} SfzRegion;

typedef enum { SFZ_ENV_ATTACK,
               SFZ_ENV_HOLD,
               SFZ_ENV_DECAY,
               SFZ_ENV_SUSTAIN,
               SFZ_ENV_RELEASE } SfzEnvState;

typedef struct {
  int region_idx;     // -1 = inactive
  uint8_t note;       // original note (for note_off matching)
  uint8_t play_note;  // TRAN-translated note (for region select + pitch)
  float phase;
  float vel_gain;  // velocity → amplitude, post amp_veltrack
  float env;       // current envelope gain [0..1]
  bool releasing;
  SfzEnvState state;
  float hold_left;     // seconds remaining in the hold stage
  float release_rate;  // set once when entering release, from the env level at that moment
} SfzVoice;

// One decoded sample file, shared by every region that references it (e.g. slice
// regions in a chopped-up breakbeat all pointing at the same underlying .wav).
typedef struct {
  char path[SFZ_PATH_LEN];
  float* samples;
  uint32_t num_samples;
  uint32_t wav_sr;
} SfzSample;

// Shared parsed SFZ data (regions + sample buffers). Multiple UnitState instances
// pointing at the same file share one SfzShared entry via refcounting.
typedef struct SfzShared {
  char path[SFZ_PATH_LEN];
  SfzRegion regions[SFZ_MAX_REGIONS];
  int num_regions;
  SfzSample sample_pool[SFZ_MAX_SAMPLE_POOL];
  int num_pool;
  int refs;
} SfzShared;

#define SFZ_CACHE_MAX 32
static SfzShared* sfz_cache[SFZ_CACHE_MAX];

struct UnitState {
  SfzShared* shared;
  SfzVoice voices[SFZ_MAX_VOICES];
  float engine_sr;
};

// --- audio loading (same pattern as sampler.c) ---

static float* load_audio(const char* path, uint32_t* out_count, uint32_t* out_sr) {
  Wave w = LoadWave(path);
  if (w.frameCount == 0 || !w.data)
    return NULL;
  *out_sr = w.sampleRate;
  WaveFormat(&w, w.sampleRate, 32, 1);
  float* smp = LoadWaveSamples(w);
  uint32_t n = w.frameCount;
  UnloadWave(w);
  if (!smp)
    return NULL;
  float* out = malloc(n * sizeof(float));
  if (!out) {
    UnloadWaveSamples(smp);
    return NULL;
  }
  memcpy(out, smp, n * sizeof(float));
  UnloadWaveSamples(smp);
  *out_count = n;
  return out;
}

// --- shared sample pool (dedup: same sample= referenced by many slice/velocity regions) ---

static float* sample_pool_load(SfzShared* sh, const char* path, uint32_t* out_count, uint32_t* out_sr) {
  for (int i = 0; i < sh->num_pool; i++) {
    if (strcmp(sh->sample_pool[i].path, path) == 0) {
      *out_count = sh->sample_pool[i].num_samples;
      *out_sr = sh->sample_pool[i].wav_sr;
      return sh->sample_pool[i].samples;
    }
  }
  uint32_t n = 0, sr = 0;
  float* buf = load_audio(path, &n, &sr);
  if (!buf)
    return NULL;
  if (sh->num_pool < SFZ_MAX_SAMPLE_POOL) {
    SfzSample* sp = &sh->sample_pool[sh->num_pool++];
    strncpy(sp->path, path, sizeof(sp->path) - 1);
    sp->samples = buf;
    sp->num_samples = n;
    sp->wav_sr = sr;
  }
  *out_count = n;
  *out_sr = sr;
  return buf;
}

// --- SFZ parser ---

static void trim(char* s) {
  char* e = s + strlen(s) - 1;
  while (e >= s && isspace((unsigned char)*e))
    *e-- = '\0';
  char* p = s;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (p != s)
    memmove(s, p, strlen(p) + 1);
}

static int note_name_to_midi(const char* s) {
  // Accept plain integer or note name like C4, C#4, Db4
  if (isdigit((unsigned char)s[0]) || s[0] == '-')
    return atoi(s);
  static const char* names = "C D EF G A B";
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
static void region_defaults(SfzRegion* r) {
  r->lokey = 0;
  r->hikey = 127;
  r->pitch_keycenter = 60;
  r->lovel = 0;
  r->hivel = 127;
  r->volume_db = 0.0f;
  r->pan = 0.0f;
  r->tune = 0.0f;
  r->ampeg_attack = 0.001f;
  r->ampeg_hold = 0.0f;
  r->ampeg_decay = 0.0f;
  r->ampeg_sustain = 100.0f;
  r->ampeg_release = 0.1f;
  r->amp_veltrack = 100.0f;
  r->offset = 0;
  r->end = 0;
  r->has_end = false;
  r->loop_mode = SFZ_LOOP_NONE;
  r->loop_start = 0;
  r->loop_end = 0;
  r->has_loop_end = false;
  r->samples = NULL;
  r->num_samples = 0;
  r->wav_sr = 44100;
}

// Apply one key=value opcode to a region. sfz_dir is the instrument's root directory
// (resolved #include and default_path against this); default_path prefixes "sample=".
static void apply_opcode(SfzRegion* r, const char* key, const char* val,
                         const char* sfz_dir, const char* default_path, SfzShared* sh) {
  if (strcmp(key, "sample") == 0) {
    // SFZ uses backslash on Windows; normalize
    char v[SFZ_PATH_LEN];
    strncpy(v, val, sizeof(v) - 1);
    v[sizeof(v) - 1] = '\0';
    for (char* p = v; *p; p++)
      if (*p == '\\')
        *p = '/';
    char rel[SFZ_PATH_LEN];
    snprintf(rel, sizeof(rel), "%s%s", default_path ? default_path : "", v);
    char path[SFZ_PATH_LEN];
    unit_resolve_path(sfz_dir, rel, path, sizeof(path));
    r->samples = sample_pool_load(sh, path, &r->num_samples, &r->wav_sr);
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
  } else if (strcmp(key, "ampeg_hold") == 0) {
    r->ampeg_hold = (float)atof(val);
    if (r->ampeg_hold < 0.0f)
      r->ampeg_hold = 0.0f;
  } else if (strcmp(key, "ampeg_decay") == 0) {
    r->ampeg_decay = (float)atof(val);
    if (r->ampeg_decay < 0.0f)
      r->ampeg_decay = 0.0f;
  } else if (strcmp(key, "ampeg_sustain") == 0) {
    r->ampeg_sustain = (float)atof(val);
    if (r->ampeg_sustain < 0.0f)
      r->ampeg_sustain = 0.0f;
    if (r->ampeg_sustain > 100.0f)
      r->ampeg_sustain = 100.0f;
  } else if (strcmp(key, "ampeg_release") == 0) {
    r->ampeg_release = (float)atof(val);
    if (r->ampeg_release < 0.001f)
      r->ampeg_release = 0.001f;
  } else if (strcmp(key, "amp_veltrack") == 0) {
    r->amp_veltrack = (float)atof(val);
    if (r->amp_veltrack < -100.0f)
      r->amp_veltrack = -100.0f;
    if (r->amp_veltrack > 100.0f)
      r->amp_veltrack = 100.0f;
  } else if (strcmp(key, "offset") == 0) {
    r->offset = (uint32_t)atol(val);
  } else if (strcmp(key, "end") == 0) {
    long e = atol(val);
    if (e >= 0) {
      r->end = (uint32_t)e;
      r->has_end = true;
    }
  } else if (strcmp(key, "loop_mode") == 0 || strcmp(key, "loopmode") == 0) {
    if (strcmp(val, "loop_continuous") == 0 || strcmp(val, "loop_sustain") == 0)
      r->loop_mode = SFZ_LOOP_CONTINUOUS;
    else if (strcmp(val, "one_shot") == 0)
      r->loop_mode = SFZ_LOOP_ONE_SHOT;
    else
      r->loop_mode = SFZ_LOOP_NONE;
  } else if (strcmp(key, "loop_start") == 0 || strcmp(key, "loopstart") == 0) {
    r->loop_start = (uint32_t)atol(val);
  } else if (strcmp(key, "loop_end") == 0 || strcmp(key, "loopend") == 0) {
    r->loop_end = (uint32_t)atol(val);
    r->has_loop_end = true;
  }
}

// Parse state threaded through the whole file (and any #include'd sub-files):
// macro table, current default_path, and the global → group → region inheritance
// chain (SFZ semantics: <global> opcodes seed every <group>, <group> opcodes seed
// every <region> that follows it).
typedef enum { SFZ_MODE_NONE,
               SFZ_MODE_GLOBAL,
               SFZ_MODE_GROUP,
               SFZ_MODE_CONTROL,
               SFZ_MODE_REGION } SfzMode;

typedef struct {
  char root_dir[SFZ_PATH_LEN];
  char default_path[SFZ_PATH_LEN];
  char macro_name[SFZ_MAX_MACROS][32];
  char macro_val[SFZ_MAX_MACROS][256];
  int num_macros;
  SfzShared* sh;
  SfzRegion global_defaults;
  SfzRegion group_defaults;
  SfzRegion cur;
  SfzMode mode;
} SfzParseCtx;

static void ctx_define(SfzParseCtx* ctx, const char* name, const char* val) {
  for (int i = 0; i < ctx->num_macros; i++) {
    if (strcmp(ctx->macro_name[i], name) == 0) {
      strncpy(ctx->macro_val[i], val, sizeof(ctx->macro_val[i]) - 1);
      return;
    }
  }
  if (ctx->num_macros < SFZ_MAX_MACROS) {
    strncpy(ctx->macro_name[ctx->num_macros], name, sizeof(ctx->macro_name[0]) - 1);
    strncpy(ctx->macro_val[ctx->num_macros], val, sizeof(ctx->macro_val[0]) - 1);
    ctx->num_macros++;
  }
}

// Replace every $NAME with its defined value, in place. Undefined $NAME is left as-is.
static void expand_macros(SfzParseCtx* ctx, char* line, int cap) {
  if (!strchr(line, '$'))
    return;
  char out[SFZ_LINE_LEN];
  int oi = 0;
  for (int i = 0; line[i] && oi < cap - 1;) {
    if (line[i] == '$') {
      int j = i + 1;
      char name[32];
      int ni = 0;
      while (line[j] && (isalnum((unsigned char)line[j]) || line[j] == '_') && ni < 31)
        name[ni++] = line[j++];
      name[ni] = '\0';
      const char* val = NULL;
      for (int m = 0; m < ctx->num_macros; m++) {
        if (strcmp(ctx->macro_name[m], name) == 0) {
          val = ctx->macro_val[m];
          break;
        }
      }
      if (val && ni > 0) {
        for (const char* c = val; *c && oi < cap - 1; c++)
          out[oi++] = *c;
        i = j;
        continue;
      }
    }
    out[oi++] = line[i++];
  }
  out[oi] = '\0';
  strncpy(line, out, cap - 1);
  line[cap - 1] = '\0';
}

// Push cur into the region list if it's a complete region (has a sample).
static void flush_region(SfzParseCtx* ctx) {
  if (ctx->mode != SFZ_MODE_REGION || !ctx->cur.samples)
    return;
  if (ctx->sh->num_regions < SFZ_MAX_REGIONS)
    ctx->sh->regions[ctx->sh->num_regions++] = ctx->cur;
}

static void sfz_parse_file(SfzParseCtx* ctx, const char* path, int depth) {
  if (depth > SFZ_MAX_INCLUDE_DEPTH)
    return;
  FILE* f = fopen(path, "r");
  if (!f)
    return;

  char line[SFZ_LINE_LEN];
  while (fgets(line, sizeof(line), f)) {
    char* comment = strstr(line, "//");
    if (comment)
      *comment = '\0';
    trim(line);
    if (!line[0])
      continue;

    // #define $NAME value — literal, not macro-expanded itself
    if (strncmp(line, "#define", 7) == 0) {
      char* p = line + 7;
      while (*p && isspace((unsigned char)*p))
        p++;
      if (*p == '$') {
        p++;
        char name[32];
        int ni = 0;
        while (*p && !isspace((unsigned char)*p) && ni < 31)
          name[ni++] = *p++;
        name[ni] = '\0';
        while (*p && isspace((unsigned char)*p))
          p++;
        char val[256];
        int vi = 0;
        while (*p && !isspace((unsigned char)*p) && vi < 255)
          val[vi++] = *p++;
        val[vi] = '\0';
        if (name[0])
          ctx_define(ctx, name, val);
      }
      continue;
    }

    expand_macros(ctx, line, sizeof(line));

    // #include "path" — resolved relative to the instrument root, parsed inline
    if (strncmp(line, "#include", 8) == 0) {
      char* p = line + 8;
      while (*p && isspace((unsigned char)*p))
        p++;
      char inc[SFZ_PATH_LEN] = {0};
      if (*p == '"') {
        p++;
        char* e = strchr(p, '"');
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len >= (int)sizeof(inc))
          len = sizeof(inc) - 1;
        strncpy(inc, p, len);
        inc[len] = '\0';
      } else {
        strncpy(inc, p, sizeof(inc) - 1);
        trim(inc);
      }
      for (char* c = inc; *c; c++)
        if (*c == '\\')
          *c = '/';
      if (inc[0]) {
        char full[SFZ_PATH_LEN];
        unit_resolve_path(ctx->root_dir, inc, full, sizeof(full));
        sfz_parse_file(ctx, full, depth + 1);
      }
      continue;
    }

    // Tokenize: headers and opcodes can appear on same line
    char* p = line;
    while (*p) {
      while (*p && isspace((unsigned char)*p))
        p++;
      if (!*p)
        break;

      if (*p == '<') {
        char* end = strchr(p, '>');
        if (!end)
          break;
        *end = '\0';
        char* hdr = p + 1;
        trim(hdr);

        flush_region(ctx);
        if (strcmp(hdr, "region") == 0) {
          ctx->cur = ctx->group_defaults;
          ctx->cur.samples = NULL;
          ctx->cur.num_samples = 0;
          ctx->cur.wav_sr = 44100;
          ctx->mode = SFZ_MODE_REGION;
        } else if (strcmp(hdr, "group") == 0) {
          ctx->group_defaults = ctx->global_defaults;
          ctx->mode = SFZ_MODE_GROUP;
        } else if (strcmp(hdr, "global") == 0) {
          ctx->mode = SFZ_MODE_GLOBAL;
        } else if (strcmp(hdr, "control") == 0) {
          ctx->mode = SFZ_MODE_CONTROL;
        } else {
          // <curve>, <effect>, etc: not supported — swallow trailing key=val on
          // the same line without leaking them into whatever region was active
          ctx->mode = SFZ_MODE_NONE;
        }
        p = end + 1;
        continue;
      }

      // Try to parse key=value
      char* eq = strchr(p, '=');
      if (!eq)
        break;

      char key[64] = {0};
      char* kp = p;
      int ki = 0;
      while (kp < eq && !isspace((unsigned char)*kp) && ki < 63)
        key[ki++] = *kp++;
      key[ki] = '\0';

      char* vs = eq + 1;
      while (*vs && isspace((unsigned char)*vs))
        vs++;

      // Collect value until the next opcode key= (whitespace + non-space-run + '=')
      char val[SFZ_PATH_LEN] = {0};
      char* next_key = NULL;
      for (char* sp = vs; *sp; sp++) {
        if (isspace((unsigned char)*sp)) {
          char* nk = sp + 1;
          while (*nk && isspace((unsigned char)*nk))
            nk++;
          if (*nk && *nk != '<') {
            char* neq = strchr(nk, '=');
            if (neq) {
              int has_space = 0;
              for (char* c = nk; c < neq; c++)
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

      if (!key[0] || !val[0])
        continue;

      if (strcmp(key, "default_path") == 0) {
        char norm[SFZ_PATH_LEN];
        strncpy(norm, val, sizeof(norm) - 1);
        norm[sizeof(norm) - 1] = '\0';
        for (char* c = norm; *c; c++)
          if (*c == '\\')
            *c = '/';
        size_t l = strlen(norm);
        if (l && norm[l - 1] != '/' && l + 1 < sizeof(norm)) {
          norm[l] = '/';
          norm[l + 1] = '\0';
        }
        strncpy(ctx->default_path, norm, sizeof(ctx->default_path) - 1);
      } else if (ctx->mode == SFZ_MODE_REGION) {
        apply_opcode(&ctx->cur, key, val, ctx->root_dir, ctx->default_path, ctx->sh);
      } else if (ctx->mode == SFZ_MODE_GROUP) {
        apply_opcode(&ctx->group_defaults, key, val, ctx->root_dir, ctx->default_path, ctx->sh);
      } else if (ctx->mode == SFZ_MODE_GLOBAL) {
        apply_opcode(&ctx->global_defaults, key, val, ctx->root_dir, ctx->default_path, ctx->sh);
        apply_opcode(&ctx->group_defaults, key, val, ctx->root_dir, ctx->default_path, ctx->sh);
      }
      // SFZ_MODE_CONTROL (other than default_path) and SFZ_MODE_NONE: opcode ignored
    }
  }

  fclose(f);
}

static void sfz_parse(SfzShared* sh, const char* path) {
  SfzParseCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.sh = sh;
  ctx.mode = SFZ_MODE_NONE;

  // Root dir for relative #include, default_path and sample= resolution
  strncpy(ctx.root_dir, path, sizeof(ctx.root_dir) - 1);
  char* slash = strrchr(ctx.root_dir, '/');
#ifdef _WIN32
  char* bslash = strrchr(ctx.root_dir, '\\');
  if (bslash > slash)
    slash = bslash;
#endif
  if (slash)
    *(slash + 1) = '\0';
  else
    ctx.root_dir[0] = '\0';

  region_defaults(&ctx.global_defaults);
  region_defaults(&ctx.group_defaults);
  region_defaults(&ctx.cur);

  sh->num_regions = 0;
  sfz_parse_file(&ctx, path, 0);
  flush_region(&ctx);
}

// --- shared cache ---

static void sfz_shared_free(SfzShared* sh) {
  for (int i = 0; i < sh->num_pool; i++)
    free(sh->sample_pool[i].samples);
  free(sh);
}

static SfzShared* sfz_shared_acquire(const char* path) {
  for (int i = 0; i < SFZ_CACHE_MAX; i++) {
    if (sfz_cache[i] && strcmp(sfz_cache[i]->path, path) == 0) {
      sfz_cache[i]->refs++;
      return sfz_cache[i];
    }
  }
  SfzShared* sh = calloc(1, sizeof(SfzShared));
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
  // Cache full of live entries: evict the one with the fewest current refs
  // (ties broken by first found) to make room, rather than reloading on
  // every acquire/release cycle for whichever file loses the race.
  int evict = 0;
  for (int i = 1; i < SFZ_CACHE_MAX; i++)
    if (sfz_cache[i]->refs < sfz_cache[evict]->refs)
      evict = i;
  sfz_shared_free(sfz_cache[evict]);
  sfz_cache[evict] = sh;
  return sh;
}

// Drops a reference. Entries are kept warm in the cache even at refs==0 —
// tracker tracks routinely alternate between several instruments across
// steps, so a momentary zero-ref dip is normal and shouldn't discard a
// fully-decoded kit just to reload it on the next note. Reclaimed lazily
// by sfz_shared_acquire when the cache is full and a new file needs a slot.
static void sfz_shared_release(SfzShared* sh) {
  if (!sh)
    return;
  sh->refs--;
}

// --- .zip bundle support (data field points at a .zip containing an .sfz + samples) ---

static void mkdir_p(const char* dir) {
  char tmp[SFZ_PATH_LEN];
  strncpy(tmp, dir, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  for (char* p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
#ifdef _WIN32
      _mkdir(tmp);
#else
      mkdir(tmp, 0755);
#endif
      *p = '/';
    }
  }
#ifdef _WIN32
  _mkdir(tmp);
#else
  mkdir(tmp, 0755);
#endif
}

static uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) {
    h ^= (uint8_t)*s;
    h *= 16777619u;
  }
  return h;
}

// Deterministic extraction dir per zip path, so re-loading the same zip reuses it.
static void zip_cache_dir(const char* zip_path, char* out, int sz) {
  const char* tmp = getenv("TMPDIR");
#ifdef _WIN32
  if (!tmp)
    tmp = getenv("TEMP");
  if (!tmp)
    tmp = getenv("TMP");
#endif
  if (!tmp || !tmp[0])
    tmp = "/tmp";
  snprintf(out, sz, "%s/rpt_sfz_%08x", tmp, fnv1a(zip_path));
}

static bool zip_extract_all(const char* zip_path, const char* dest_dir) {
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, zip_path, 0))
    return false;

  int n = (int)mz_zip_reader_get_num_files(&zip);
  for (int i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st))
      continue;

    char norm[SFZ_PATH_LEN];
    strncpy(norm, st.m_filename, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    for (char* p = norm; *p; p++)
      if (*p == '\\')
        *p = '/';

    char out_path[SFZ_PATH_LEN];
    snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, norm);

    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      mkdir_p(out_path);
      continue;
    }

    char parent[SFZ_PATH_LEN];
    strncpy(parent, out_path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char* slash = strrchr(parent, '/');
    if (slash) {
      *slash = '\0';
      mkdir_p(parent);
    }
    mz_zip_reader_extract_to_file(&zip, i, out_path, 0);
  }

  mz_zip_reader_end(&zip);
  return true;
}

// Recursively search dir for the first *.sfz file (zips commonly nest it one folder deep).
static bool find_sfz_file(const char* dir, char* out, int sz, int depth) {
  if (depth > 6)
    return false;
  DIR* d = opendir(dir);
  if (!d)
    return false;
  struct dirent* de;
  bool found = false;
  while (!found && (de = readdir(d))) {
    if (de->d_name[0] == '.')
      continue;
    char full[SFZ_PATH_LEN];
    snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
    struct stat st;
    if (stat(full, &st))
      continue;
    if (S_ISDIR(st.st_mode)) {
      found = find_sfz_file(full, out, sz, depth + 1);
    } else {
      const char* ext = strrchr(de->d_name, '.');
      if (ext && strcasecmp(ext, ".sfz") == 0) {
        strncpy(out, full, sz - 1);
        out[sz - 1] = '\0';
        found = true;
      }
    }
  }
  closedir(d);
  return found;
}

// If path points at a .zip, extract it (once) to a stable cache dir and return the
// path to the .sfz file found inside. Returns false (path untouched) for a plain .sfz.
static bool zip_resolve_sfz(const char* path, char* out, int sz) {
  const char* ext = strrchr(path, '.');
  if (!ext || strcasecmp(ext, ".zip") != 0)
    return false;

  char cache_dir[SFZ_PATH_LEN];
  zip_cache_dir(path, cache_dir, sizeof(cache_dir));

  struct stat st;
  if (stat(cache_dir, &st) != 0) {
    mkdir_p(cache_dir);
    zip_extract_all(path, cache_dir);
  }

  return find_sfz_file(cache_dir, out, sz, 0);
}

// --- unit lifecycle ---

static UnitState* sfz_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->engine_sr = sr;
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
  return s;
}

static void sfz_destroy(UnitState* s) {
  sfz_shared_release(s->shared);
  free(s);
}

static void sfz_set_data(UnitState* s, const char* data, const char* base_dir) {
  const char* rel = (data && data[0]) ? data : "instrument.sfz";
  char path[SFZ_PATH_LEN];
  unit_resolve_path(base_dir, rel, path, sizeof(path));

  char sfz_from_zip[SFZ_PATH_LEN];
  if (zip_resolve_sfz(path, sfz_from_zip, sizeof(sfz_from_zip)))
    strncpy(path, sfz_from_zip, sizeof(path) - 1);

  if (s->shared && strcmp(s->shared->path, path) == 0)
    return;
  sfz_shared_release(s->shared);
  s->shared = sfz_shared_acquire(path);
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
}

static void sfz_note_on(UnitState* s, uint8_t note, uint8_t vel, const uint8_t* p) {
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
    SfzRegion* r = &s->shared->regions[ri];
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

    // amp_veltrack (-100..100, default 100): how much velocity affects amplitude.
    // At the default of 100 this reduces to the plain vel/127 scaling used before.
    float velfrac = vel127 / 127.0f;
    float vt = r->amp_veltrack / 100.0f;
    float vgain = 1.0f - vt * (1.0f - velfrac);
    if (vgain < 0.0f)
      vgain = 0.0f;

    SfzVoice* v = &s->voices[vi];
    v->region_idx = ri;
    v->note = note;
    v->play_note = pnote;
    v->phase = (float)r->offset;  // slice start, for regions sharing one sample file
    v->vel_gain = vgain;
    v->env = 0.0f;
    v->releasing = false;
    v->state = SFZ_ENV_ATTACK;
    v->hold_left = 0.0f;
    v->release_rate = 0.0f;  // computed from the live env level once release starts
  }
}

static void sfz_note_off(UnitState* s, uint8_t note) {
  for (int i = 0; i < SFZ_MAX_VOICES; i++) {
    SfzVoice* v = &s->voices[i];
    if (v->region_idx >= 0 && v->note == note && !v->releasing) {
      v->releasing = true;
    }
  }
}

static void sfz_kill(UnitState* s) {
  for (int i = 0; i < SFZ_MAX_VOICES; i++)
    s->voices[i].region_idx = -1;
}

static void sfz_render(UnitState* s, const uint8_t* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r, uint32_t frames) {
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
    SfzVoice* v = &s->voices[vi];
    if (v->region_idx < 0)
      continue;
    SfzRegion* r = &s->shared->regions[v->region_idx];
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

    // Playable slice bounds — offset/end let many regions share one sample file
    // (e.g. a breakbeat chopped into per-key slices).
    uint32_t play_end = (r->has_end && r->end < r->num_samples) ? r->end : r->num_samples - 1;
    uint32_t loop_end = (r->has_loop_end && r->loop_end <= play_end) ? r->loop_end : play_end;

    float attack_rate = 1.0f / (r->ampeg_attack * s->engine_sr);
    float decay_rate = (r->ampeg_decay > 0.0f)
                           ? (1.0f - r->ampeg_sustain / 100.0f) / (r->ampeg_decay * s->engine_sr)
                           : 0.0f;
    float sustain_level = r->ampeg_sustain / 100.0f;
    float inv_sr = 1.0f / s->engine_sr;

    float phase = v->phase;
    float env = v->env;
    SfzEnvState state = v->state;
    float hold_left = v->hold_left;
    float release_rate = v->release_rate;

    for (uint32_t f = 0; f < frames; f++) {
      if (v->releasing && state != SFZ_ENV_RELEASE) {
        state = SFZ_ENV_RELEASE;
        release_rate = env / (r->ampeg_release * s->engine_sr);
      }

      switch (state) {
        case SFZ_ENV_ATTACK:
          env += attack_rate;
          if (env >= 1.0f) {
            env = 1.0f;
            if (r->ampeg_hold > 0.0f) {
              state = SFZ_ENV_HOLD;
              hold_left = r->ampeg_hold;
            } else {
              state = (r->ampeg_decay > 0.0f) ? SFZ_ENV_DECAY : SFZ_ENV_SUSTAIN;
            }
          }
          break;
        case SFZ_ENV_HOLD:
          hold_left -= inv_sr;
          if (hold_left <= 0.0f)
            state = (r->ampeg_decay > 0.0f) ? SFZ_ENV_DECAY : SFZ_ENV_SUSTAIN;
          break;
        case SFZ_ENV_DECAY:
          env -= decay_rate;
          if (env <= sustain_level) {
            env = sustain_level;
            state = SFZ_ENV_SUSTAIN;
          }
          break;
        case SFZ_ENV_SUSTAIN:
          env = sustain_level;
          break;
        case SFZ_ENV_RELEASE:
          env -= release_rate;
          if (env <= 0.0f) {
            env = 0.0f;
            v->region_idx = -1;
          }
          break;
      }
      if (v->region_idx < 0)
        break;

      // Sample read with linear interpolation, clamped to the region's slice
      uint32_t i0 = (uint32_t)phase;
      if (i0 > play_end) {
        v->region_idx = -1;
        break;
      }
      uint32_t i1 = i0 + 1;
      if (i1 > play_end)
        i1 = play_end;
      float frac = phase - (float)i0;
      float smp = r->samples[i0] * (1.0f - frac) + r->samples[i1] * frac;

      float out = smp * env * rvol * vol * v->vel_gain;
      out_l[f] += out * pan_l * rpan_l;
      out_r[f] += out * pan_r * rpan_r;

      phase += rate;
      if (r->loop_mode == SFZ_LOOP_CONTINUOUS) {
        if (phase >= (float)(loop_end + 1)) {
          float loop_len = (float)(loop_end + 1) - (float)r->loop_start;
          phase = (loop_len > 0.0f) ? (phase - loop_len) : (float)r->loop_start;
        }
      } else if (phase >= (float)(play_end + 1)) {
        v->region_idx = -1;
        break;
      }
    }

    v->phase = phase;
    v->env = env;
    v->state = state;
    v->hold_left = hold_left;
    v->release_rate = release_rate;
  }
}

const UnitDef unit_sfz = {
    .id = "sfz",
    .name = "SFZ",
    .data_hint = "instrument.sfz",
    .file_filter = "*.sfz *.zip",
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
