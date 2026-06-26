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

struct UnitState {
  tsf *sf;
  float sample_rate;
  int active_preset;
  int active_bank;
  char path[512];          // resolved absolute path to .sf2 file
  uint8_t note_xlat[128];  // orig note → TRAN-translated note, so note_off matches
};

static UnitState *sf2_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->active_preset = -1;
  s->active_bank = 0;
  return s;
}

static void sf2_destroy(UnitState *s) {
  if (s->sf)
    tsf_close(s->sf);
  free(s);
}

static void sf2_set_data(UnitState *s, const char *data, const char *base_dir) {
  const char *rel = (data && data[0]) ? data : "soundfont.sf2";
  unit_resolve_path(base_dir, rel, s->path, sizeof(s->path));
  if (s->sf) {
    tsf_close(s->sf);
    s->sf = NULL;
  }
  s->sf = tsf_load_filename(s->path);
  if (s->sf)
    tsf_set_output(s->sf, TSF_STEREO_INTERLEAVED, (int)s->sample_rate, 0.0f);
}

static void sf2_ensure_loaded(UnitState *s, int preset, int bank) {
  if (!s->sf) {
    // Try default path if set_data was never called
    if (!s->path[0])
      strncpy(s->path, "soundfont.sf2", sizeof(s->path));
    s->sf = tsf_load_filename(s->path);
    if (s->sf)
      tsf_set_output(s->sf, TSF_STEREO_INTERLEAVED, (int)s->sample_rate, 0.0f);
  }
  if (s->sf && (preset != s->active_preset || bank != s->active_bank)) {
    s->active_preset = preset;
    s->active_bank = bank;
  }
}

static void sf2_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  int preset = p[0] & 0x7F;
  int bank = p[1];
  sf2_ensure_loaded(s, preset, bank);
  if (!s->sf)
    return;
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
  tsf_channel_set_presetnumber(s->sf, 0, preset, bank == 128);
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
