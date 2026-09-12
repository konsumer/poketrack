// SF2 soundfont player unit using TinySoundFont
// data field = path to .sf2 file (defaults to "soundfont.sf2")
// P0 PRESET: 00-FF (GM preset 0-127)
// P1 BANK:   00-FF
// P2 VOL:    00=silent  FF=full
// P3 PAN:    00=L  80=center  FF=R
// P4 TRANS:  00=-128st  80=0  FF=+127st (translate incoming note → selects key/sample)
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
  tsf* master;
  int refs;
} Sf2Cache;

static Sf2Cache sf2_cache[SF2_CACHE_MAX];

// Returns a tsf_copy() of the shared master (caller owns the copy).
// Returns NULL on load failure.
static tsf* sf2_cache_acquire(const char* path) {
  for (int i = 0; i < SF2_CACHE_MAX; i++) {
    if (sf2_cache[i].master && strcmp(sf2_cache[i].path, path) == 0) {
      sf2_cache[i].refs++;
      return tsf_copy(sf2_cache[i].master);
    }
  }
  tsf* master = tsf_load_filename(path);
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
  tsf* copy = tsf_copy(master);
  tsf_close(master);
  return copy;
}

static void sf2_cache_release(const char* path) {
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

// Whole-number notes play on channel 0 and let TSF track their polyphony.
// A fractional pitch (from a note modifier like MICRO) can't be named by a
// SoundFont key, so each one gets its own channel with a per-channel tuning —
// TSF's tuning is per channel, not per voice. Channel i+1 holds slot i.
#define SF2_PITCH_CH 15

typedef struct {
  bool used;
  float pitch;  // the fractional pitch we were asked to play
  int tnote;    // TRAN-translated key actually sent to TSF
} Sf2PitchVoice;

struct UnitState {
  tsf* sf;
  float sample_rate;
  char path[512];          // resolved absolute path to .sf2 file
  uint8_t note_xlat[128];  // orig note → TRAN-translated note, so note_off matches
  Sf2PitchVoice micro[SF2_PITCH_CH];
  uint32_t micro_next;
};

static void sf2_clear_micro(UnitState* s) { memset(s->micro, 0, sizeof(s->micro)); }

static UnitState* sf2_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}

static void sf2_release(UnitState* s) {
  if (s->sf) {
    tsf_close(s->sf);
    s->sf = NULL;
  }
  sf2_clear_micro(s);
  if (s->path[0]) {
    sf2_cache_release(s->path);
    s->path[0] = '\0';
  }
}

static void sf2_destroy(UnitState* s) {
  sf2_release(s);
  free(s);
}

static void sf2_set_data(UnitState* s, const char* data, const char* base_dir) {
  const char* rel = (data && data[0]) ? data : "soundfont.sf2";
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

// Load a font into the cache and hold a reference that is never released, so
// it stays resident for the rest of the run. Used by the engine to warm every
// font the song references before playback starts (see UnitDef.preload_data).
static void sf2_preload(const char* data, const char* base_dir) {
  const char* rel = (data && data[0]) ? data : "soundfont.sf2";
  char path[512];
  unit_resolve_path(base_dir, rel, path, sizeof(path));
  for (int i = 0; i < SF2_CACHE_MAX; i++)
    if (sf2_cache[i].master && strcmp(sf2_cache[i].path, path) == 0)
      return;  // already resident
  for (int i = 0; i < SF2_CACHE_MAX; i++) {
    if (!sf2_cache[i].master) {
      tsf* master = tsf_load_filename(path);
      if (!master)
        return;  // missing/bad file: leave it to fail the same way it does now
      strncpy(sf2_cache[i].path, path, sizeof(sf2_cache[i].path) - 1);
      sf2_cache[i].master = master;
      sf2_cache[i].refs = 1;  // the pin; acquire/release balance on top of it
      return;
    }
  }
  // Cache full — nothing to pin into. acquire() still works, just unshared.
}

static void sf2_ensure_loaded(UnitState* s) {
  if (!s->sf) {
    if (!s->path[0])
      strncpy(s->path, "soundfont.sf2", sizeof(s->path) - 1);
    s->sf = sf2_cache_acquire(s->path);
    if (s->sf)
      tsf_set_output(s->sf, TSF_STEREO_INTERLEAVED, (int)s->sample_rate, 0.0f);
  }
}

// P1 BANK: select the (bank, preset) pair directly. If the font has no such
// pair, fall back to GM-style lookup (drum rules when bank >= 128).
static void sf2_apply_preset(UnitState* s, int ch, int bank, int preset) {
  if (!tsf_channel_set_bank_preset(s->sf, ch, bank, preset))
    tsf_channel_set_presetnumber(s->sf, ch, preset, bank >= 128);
}

// The slot already playing this exact pitch, or NULL.
static Sf2PitchVoice* sf2_find_micro(UnitState* s, float pitch) {
  for (int i = 0; i < SF2_PITCH_CH; i++)
    if (s->micro[i].used && fabsf(s->micro[i].pitch - pitch) < 0.001f)
      return &s->micro[i];
  return NULL;
}

static void sf2_micro_off(UnitState* s, Sf2PitchVoice* v) {
  int ch = (int)(v - s->micro) + 1;
  tsf_channel_note_off(s->sf, ch, v->tnote);
  v->used = false;
}

static void sf2_note_on(UnitState* s, float pitch, uint8_t vel, const uint8_t* p) {
  int key = (int)lrintf(pitch);
  if (key < 0)
    key = 0;
  if (key > 127)
    key = 127;
  float frac = pitch - (float)key;  // < 0.5 semitones; nonzero = microtonal
  int preset = p[0] & 0x7F;
  int bank = p[1];
  // P4 TRANS: integer semitone translation of the incoming note (selects a
  // different key/sample, like a drum kit mapped an octave lower). Remember the
  // translated key so note_off (not passed params) can match it in TSF.
  // 1 LSB = 1 semitone, 0x80 = center (0x00=-128 .. 0xFF=+127).
  int tnote = key + (int)p[4] - 128;
  if (tnote < 0)
    tnote = 0;
  if (tnote > 127)
    tnote = 127;

  sf2_ensure_loaded(s);
  if (!s->sf)
    return;

  if (fabsf(frac) < 1e-4f) {  // whole-number note: channel 0, TSF polyphony
    sf2_apply_preset(s, 0, bank, preset);
    s->note_xlat[key] = (uint8_t)tnote;
    tsf_channel_note_on(s->sf, 0, (uint8_t)tnote, vel / 255.0f);
    return;
  }

  Sf2PitchVoice* v = sf2_find_micro(s, pitch);
  if (v)
    sf2_micro_off(s, v);  // retrigger the same pitch cleanly
  for (int i = 0; i < SF2_PITCH_CH && !v; i++) {
    int idx = (int)((s->micro_next + i) % SF2_PITCH_CH);
    if (!s->micro[idx].used)
      v = &s->micro[idx];
  }
  if (!v) {  // all busy: steal the next one round-robin
    v = &s->micro[s->micro_next % SF2_PITCH_CH];
    sf2_micro_off(s, v);
  }
  s->micro_next = (uint32_t)(v - s->micro) + 1;

  int ch = (int)(v - s->micro) + 1;
  sf2_apply_preset(s, ch, bank, preset);
  tsf_channel_set_pitchwheel(s->sf, ch, 8192);
  tsf_channel_set_tuning(s->sf, ch, frac);
  v->used = true;
  v->pitch = pitch;
  v->tnote = tnote;
  tsf_channel_note_on(s->sf, ch, (uint8_t)tnote, vel / 255.0f);
}

static void sf2_note_off(UnitState* s, float pitch) {
  if (!s->sf)
    return;
  Sf2PitchVoice* v = sf2_find_micro(s, pitch);
  if (v) {
    sf2_micro_off(s, v);
    return;
  }
  int key = (int)lrintf(pitch);
  if (key < 0)
    key = 0;
  if (key > 127)
    key = 127;
  tsf_channel_note_off(s->sf, 0, s->note_xlat[key]);
}

static void sf2_kill(UnitState* s) {
  if (!s->sf)
    return;
  tsf_channel_sounds_off_all(s->sf, 0);  // immediate, no release tail
  for (int i = 0; i < SF2_PITCH_CH; i++)
    if (s->micro[i].used)
      tsf_channel_sounds_off_all(s->sf, i + 1);
  sf2_clear_micro(s);
}

static void sf2_render(UnitState* s, const uint8_t* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  if (!s->sf)
    return;

  float vol = p2f(p[2], 0.0f, 1.0f);
  float pan = p2f_center(p[3], -1.0f, 1.0f);  // -1=L, 0=center, +1=R
  // P4 TRANS is applied at note-on (translates note → key select), not here.
  float tune = p2f_center(p[5], -100.0f, 100.0f);  // cents

  tsf_channel_set_pitchwheel(s->sf, 0, 8192);       // center
  tsf_channel_set_tuning(s->sf, 0, tune / 100.0f);  // cents → semitones
  tsf_channel_set_volume(s->sf, 0, vol);
  tsf_channel_set_pan(s->sf, 0, (pan + 1.0f) * 0.5f);  // 0-1

  // Microtonal voices live on their own channels with their own tuning, so
  // keep them in step with the live VOL/PAN/TUNE params too.
  for (int i = 0; i < SF2_PITCH_CH; i++) {
    if (!s->micro[i].used)
      continue;
    int ch = i + 1;
    int key = (int)lrintf(s->micro[i].pitch);
    tsf_channel_set_pitchwheel(s->sf, ch, 8192);
    tsf_channel_set_tuning(s->sf, ch, (s->micro[i].pitch - (float)key) + tune / 100.0f);
    tsf_channel_set_volume(s->sf, ch, vol);
    tsf_channel_set_pan(s->sf, ch, (pan + 1.0f) * 0.5f);
  }

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
    .preload_data = sf2_preload,
    .note_on = sf2_note_on,
    .note_off = sf2_note_off,
    .kill = sf2_kill,
    .render = sf2_render,
};
