// SF2 soundfont player unit using TinySoundFont
// data field = path to .sf2 file (defaults to "soundfont.sf2")
// P0 PRESET: 00-FF (GM preset 0-127)
// P1 BANK:   00-FF
// P2 VOL:    00=silent  FF=full
// P3 PAN:    00=L  7F=center  FF=R
// P4 TRANS:  00=-24st  80=0  FF=+24st (translate incoming note → selects key/sample)
// P5 TUNE:   00=-100c  80=0  FF=+100c (cents fine tune, resamples pitch)
#define TSF_IMPLEMENTATION
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tsf.h"
#include "unit.h"

// Shared font cache: multiple units pointing at the same file share one master tsf
// loaded via tsf_load_filename; each unit gets a tsf_copy() with independent voice state.
#define SF2_CACHE_MAX 16

typedef struct {
  char path[512];
  tsf *master;
  int refs;
} Sf2Cache;

static Sf2Cache sf2_cache[SF2_CACHE_MAX];

// Returns a tsf_copy() of the shared master (caller owns the copy).
// Returns NULL on load failure.
static tsf *sf2_cache_acquire(const char *path) {
  for (int i = 0; i < SF2_CACHE_MAX; i++) {
    if (sf2_cache[i].master && strcmp(sf2_cache[i].path, path) == 0) {
      sf2_cache[i].refs++;
      return tsf_copy(sf2_cache[i].master);
    }
  }
  tsf *master = tsf_load_filename(path);
  if (!master)
    return NULL;
  for (int i = 0; i < SF2_CACHE_MAX; i++) {
    if (!sf2_cache[i].master) {
      strncpy(sf2_cache[i].path, path, sizeof(sf2_cache[i].path) - 1);
      sf2_cache[i].master = master;
      sf2_cache[i].refs = 1;
      return tsf_copy(master);
    }
  }
  // Cache full: load without sharing
  tsf *copy = tsf_copy(master);
  tsf_close(master);
  return copy;
}

static void sf2_cache_release(const char *path) {
  for (int i = 0; i < SF2_CACHE_MAX; i++) {
    if (sf2_cache[i].master && strcmp(sf2_cache[i].path, path) == 0) {
      if (--sf2_cache[i].refs == 0) {
        tsf_close(sf2_cache[i].master);
        sf2_cache[i].master = NULL;
        sf2_cache[i].path[0] = '\0';
      }
      return;
    }
  }
}

struct UnitState {
  tsf *sf;
  float sample_rate;
  char path[512];          // resolved absolute path to .sf2 file
  uint8_t note_xlat[128];  // orig note → TRAN-translated note, so note_off matches
};

static UnitState *sf2_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}

static void sf2_release(UnitState *s) {
  if (s->sf) {
    tsf_close(s->sf);
    s->sf = NULL;
  }
  if (s->path[0]) {
    sf2_cache_release(s->path);
    s->path[0] = '\0';
  }
}

static void sf2_destroy(UnitState *s) {
  sf2_release(s);
  free(s);
}

static void sf2_set_data(UnitState *s, const char *data, const char *base_dir) {
  const char *rel = (data && data[0]) ? data : "soundfont.sf2";
  char path[512];
  unit_resolve_path(base_dir, rel, path, sizeof(path));
  if (s->sf && strcmp(s->path, path) == 0)
    return;
  sf2_release(s);
  strncpy(s->path, path, sizeof(s->path) - 1);
  s->sf = sf2_cache_acquire(path);
  if (s->sf)
    tsf_set_output(s->sf, TSF_STEREO_INTERLEAVED, (int)s->sample_rate, 0.0f);
}

static void sf2_ensure_loaded(UnitState *s) {
  if (!s->sf) {
    if (!s->path[0])
      strncpy(s->path, "soundfont.sf2", sizeof(s->path) - 1);
    s->sf = sf2_cache_acquire(s->path);
    if (s->sf)
      tsf_set_output(s->sf, TSF_STEREO_INTERLEAVED, (int)s->sample_rate, 0.0f);
  }
}

static void sf2_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  int preset = p[0] & 0x7F;
  int bank = p[1];
  sf2_ensure_loaded(s);
  if (!s->sf)
    return;
  // P1 BANK: select the (bank, preset) pair directly. If the font has no such
  // pair, fall back to GM-style lookup (drum rules when bank >= 128).
  if (!tsf_channel_set_bank_preset(s->sf, 0, bank, preset))
    tsf_channel_set_presetnumber(s->sf, 0, preset, bank >= 128);
  // P4 TRANS: integer semitone translation of the incoming note (selects a
  // different key/sample, like a drum kit mapped an octave lower). Remember the
  // translated key so note_off (not passed params) can match it in TSF.
  int trans = (int)lroundf(p2f(p[4], -24.0f, 24.0f));
  int tnote = (int)note + trans;
  if (tnote < 0)
    tnote = 0;
  if (tnote > 127)
    tnote = 127;
  s->note_xlat[note] = (uint8_t)tnote;
  tsf_channel_note_on(s->sf, 0, (uint8_t)tnote, vel / 255.0f);
}

static void sf2_note_off(UnitState *s, uint8_t note) {
  if (s->sf)
    tsf_channel_note_off(s->sf, 0, s->note_xlat[note]);
}

static void sf2_kill(UnitState *s) {
  if (s->sf)
    tsf_channel_sounds_off_all(s->sf, 0);  // immediate, no release tail
}

static void sf2_render(UnitState *s, const uint8_t *p,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->sf)
    return;

  float vol = p2f(p[2], 0.0f, 1.0f);
  float pan = p2f(p[3], -1.0f, 1.0f);  // -1=L, 0=center, +1=R
  // P4 TRANS is applied at note-on (translates note → key select), not here.
  float tune = p2f(p[5], -100.0f, 100.0f);  // cents

  tsf_channel_set_pitchwheel(s->sf, 0, 8192);          // center
  tsf_channel_set_tuning(s->sf, 0, tune / 100.0f);     // cents → semitones
  tsf_channel_set_volume(s->sf, 0, vol);
  tsf_channel_set_pan(s->sf, 0, (pan + 1.0f) * 0.5f);  // 0-1

  // Render interleaved stereo (512 frames max from AUDIO_BLOCK_SIZE)
  float ibuf[1024];  // 512 * 2 channels — matches AUDIO_BLOCK_SIZE
  if (frames > 512)
    frames = 512;
  memset(ibuf, 0, frames * 2 * sizeof(float));
  tsf_render_float(s->sf, ibuf, (int)frames, 0);

  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] += ibuf[f * 2];
    out_r[f] += ibuf[f * 2 + 1];
  }
}

const UnitDef unit_sf2 = {
    .id = "sf2",
    .name = "SF2",
    .data_hint = "soundfont.sf2",
    .file_filter = "*.sf2",
    .is_source = true,
    .num_params = 6,
    .param_names = {"PRST", "BANK", "VOL", "PAN", "TRAN", "TUNE"},
    .param_defaults = {0, 0, 200, 128, 128, 128},
    .create = sf2_create,
    .destroy = sf2_destroy,
    .set_data = sf2_set_data,
    .note_on = sf2_note_on,
    .note_off = sf2_note_off,
    .kill = sf2_kill,
    .render = sf2_render,
};
