// Quick sanity tests — run via `make test`.
// Not a full suite: catches glaring breakage cheaply. Covers the unit
// registry, song/instrument file round-trips, WAV export, the recursive file
// scan the SFZ zip loader depends on, and a render smoke test (every unit
// renders finite audio; synth sources actually make sound).
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "clap_host.h"
#include "denormal.h"
#include "raylib.h"
#include "tracker.h"
#include "units/unit_registry.h"

static int fails = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      fails++;                                    \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

// TrackerSong is tens of MB — keep test copies off the stack
static TrackerSong song_a, song_b;

static void test_registry(void) {
  const UnitDef* defs[64];
  int n = 0;
  unit_list(defs, &n);
  CHECK(n > 0, "registry is empty");
  for (int i = 0; i < n; i++) {
    const UnitDef* d = defs[i];
    CHECK(d->id && d->id[0], "unit %d has no id", i);
    CHECK(strlen(d->id) < UNIT_ID_LEN, "%s: id too long for ChainSlot", d->id);
    CHECK(d->name && d->name[0], "%s: no display name", d->id);
    CHECK(d->num_params >= 0 && d->num_params <= UNIT_MAX_PARAMS, "%s: num_params out of range", d->id);
    CHECK(d->create && d->destroy && d->render, "%s: missing required callback", d->id);
    for (int p = 0; p < d->num_params; p++) {
      CHECK(d->param_names[p] != NULL, "%s: param %d unnamed", d->id, p);
      if (d->param_enum_count[p])
        CHECK(d->param_defaults[p] < d->param_enum_count[p], "%s: param %d default outside enum", d->id, p);
    }
    for (int j = 0; j < i; j++)
      CHECK(strcmp(defs[j]->id, d->id) != 0, "duplicate unit id %s", d->id);
  }
}

static void test_song_roundtrip(void) {
  tracker_init(&song_a);
  snprintf(song_a.name, sizeof(song_a.name), "testsong");
  song_a.bpm = 173;
  song_a.swing = 3;
  song_a.scale_root = 5;
  song_a.scale_idx = 2;
  song_a.loop = false;
  song_a.song_len = 4;
  song_a.patterns[0][0] = 3;
  song_a.patterns[2][1] = 7;

  // Unallocated slots read as the shared default empty pattern
  CHECK(tracker_pattern_peek(&song_a, 9)->len == DEFAULT_PATTERN_STEPS, "peek of empty slot");
  CHECK(song_a.pattern_data[9] == NULL, "peek must not allocate");

  Pattern* p = tracker_pattern(&song_a, 3);
  CHECK(p != NULL, "pattern alloc failed");
  p->len = 32;
  p->steps[5][0] = (PatternStep){.note = 0x3C, .velocity = 100, .instrument = 7, .fx = {0x08, TRACKER_EMPTY}, .fxv = {0x80, 0}};
  p->steps[15][31] = (PatternStep){.note = NOTE_OFF, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};

  TrackerInstrument* inst = &song_a.instruments[7];
  tracker_inst_set_slot(inst, 0, "osc", 7);
  tracker_inst_set_slot(inst, 1, "ducker", 7);
  inst->chain[1].params[8] = 0x33;  // 9th param — past the old 8-param file width
  inst->chain[0].cc_map[2] = 0x40;
  snprintf(inst->chain[0].data, sizeof(inst->chain[0].data), "samples/kick.wav");

  CHECK(tracker_save(&song_a, "test_roundtrip.rpt"), "song save failed");
  tracker_init(&song_b);
  CHECK(tracker_load(&song_b, "test_roundtrip.rpt"), "song load failed");

  CHECK(strcmp(song_b.name, "testsong") == 0, "name lost: %s", song_b.name);
  CHECK(song_b.bpm == 173, "bpm lost: %d", song_b.bpm);
  CHECK(song_b.swing == 3 && song_b.scale_root == 5 && song_b.scale_idx == 2 && !song_b.loop, "meta lost");
  CHECK(song_b.song_len == 4 && song_b.patterns[0][0] == 3 && song_b.patterns[2][1] == 7, "arrangement lost");
  Pattern* pb = tracker_pattern_peek(&song_b, 3);
  CHECK(pb->len == 32, "pattern len lost");
  CHECK(memcmp(&pb->steps[5][0], &p->steps[5][0], sizeof(PatternStep)) == 0, "note step lost");
  CHECK(pb->steps[15][31].note == NOTE_OFF, "note-off step lost");
  CHECK(strcmp(song_b.instruments[7].chain[0].unit_id, "osc") == 0, "chain slot 0 lost");
  CHECK(strcmp(song_b.instruments[7].chain[1].unit_id, "ducker") == 0, "chain slot 1 lost");
  CHECK(memcmp(song_b.instruments[7].chain[0].params, song_a.instruments[7].chain[0].params, UNIT_MAX_PARAMS) == 0,
        "slot 0 params lost");
  CHECK(song_b.instruments[7].chain[1].params[8] == 0x33, "9th param lost (v3 wide-param regression)");
  CHECK(song_b.instruments[7].chain[0].cc_map[2] == 0x40, "cc_map lost");
  CHECK(strstr(song_b.instruments[7].chain[0].data, "samples/kick.wav") != NULL,
        "data path lost: %s", song_b.instruments[7].chain[0].data);
  remove("test_roundtrip.rpt");
}

static void test_instrument_roundtrip(void) {
  static TrackerInstrument ia, ib;
  memset(&ia, 0, sizeof(ia));
  snprintf(ia.name, sizeof(ia.name), "leadsynth");
  tracker_inst_set_slot(&ia, 0, "fm", 0);
  tracker_inst_set_slot(&ia, 3, "delay", 0);
  ia.chain[0].params[1] = 0xAB;
  ia.midi_in_channel = 9;

  CHECK(tracker_save_instrument(&ia, "test_inst.rpti", "./"), "instrument save failed");
  CHECK(tracker_load_instrument(&ib, "test_inst.rpti"), "instrument load failed");
  CHECK(strcmp(ib.name, "leadsynth") == 0, "inst name lost");
  CHECK(strcmp(ib.chain[0].unit_id, "fm") == 0 && strcmp(ib.chain[3].unit_id, "delay") == 0, "inst chain lost");
  CHECK(ib.chain[0].params[1] == 0xAB, "inst param lost");
  CHECK(ib.midi_in_channel == 9, "midi channel lost");
  remove("test_inst.rpti");
}

// The SFZ .zip loader relies on MakeDirectory creating a full nested path and
// on LoadDirectoryFilesEx(dir, ".sfz", true) finding a file nested inside it
// (that pairing replaced a hand-rolled mkdir_p + opendir/readdir walk).
static void test_recursive_find(void) {
  const char* root = "test_scan";
  const char* nested = "test_scan/a/b";
  CHECK(MakeDirectory(nested) == 0, "MakeDirectory failed to create nested path");
  CHECK(DirectoryExists(nested), "nested dir missing after MakeDirectory");
  SaveFileText("test_scan/a/b/kit.SFZ", "// sfz");  // uppercase: match must be case-insensitive
  SaveFileText("test_scan/a/notes.txt", "ignored");

  FilePathList found = LoadDirectoryFilesEx(root, ".sfz", true);
  CHECK(found.count == 1, "recursive .sfz scan found %u files, want 1", found.count);
  if (found.count == 1)
    CHECK(strcmp(GetFileName(found.paths[0]), "kit.SFZ") == 0,
          "found wrong file: %s", found.paths[0]);
  UnloadDirectoryFiles(found);

  remove("test_scan/a/b/kit.SFZ");
  remove("test_scan/a/notes.txt");
  remove("test_scan/a/b");
  remove("test_scan/a");
  remove("test_scan");
}

// Offline WAV render: a one-note song must come out as a readable 16-bit
// stereo WAV at the engine sample rate, with actual audio in it.
static void test_wav_export(void) {
  static AudioEngine eng;
  tracker_init(&song_a);
  song_a.song_len = 1;
  song_a.loop = false;
  song_a.patterns[0][0] = 0;
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  Pattern* p = tracker_pattern(&song_a, 0);
  CHECK(p != NULL, "wav: pattern alloc failed");
  p->len = 4;
  p->steps[0][0] = (PatternStep){.note = 60, .velocity = 127, .instrument = 0, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};

  audio_init(&eng, &song_a);
  bool ok = audio_render_wav(&eng, "test_export.wav");
  audio_shutdown(&eng);
  CHECK(ok, "wav: render failed");
  if (!ok)
    return;

  Wave w = LoadWave("test_export.wav");
  CHECK(w.frameCount > 0, "wav: no frames");
  CHECK(w.sampleRate == AUDIO_SAMPLE_RATE, "wav: sample rate %u", w.sampleRate);
  CHECK(w.channels == 2, "wav: channels %u", w.channels);
  CHECK(w.sampleSize == 16, "wav: sample size %u", w.sampleSize);
  const int16_t* pcm = (const int16_t*)w.data;
  bool silent = true;
  for (unsigned i = 0; pcm && i < w.frameCount * w.channels; i++)
    if (pcm[i] != 0) {
      silent = false;
      break;
    }
  CHECK(!silent, "wav: rendered file is all silence");
  UnloadWave(w);
  remove("test_export.wav");
}

// Render a short song to WAV and return its total energy, or -1 on failure.
static double render_song_energy(const char* path) {
  static AudioEngine eng;
  audio_init(&eng, &song_a);
  bool ok = audio_render_wav(&eng, path);
  audio_shutdown(&eng);
  if (!ok)
    return -1.0;

  Wave w = LoadWave(path);
  const int16_t* pcm = (const int16_t*)w.data;
  double energy = 0;
  for (unsigned i = 0; pcm && i < w.frameCount * w.channels; i++)
    energy += (double)pcm[i] * pcm[i];
  UnloadWave(w);
  remove(path);
  return energy;
}

// ROUTE sends a chain's audio into another instrument's chain (a send bus),
// so several instruments can share one reverb. Checks all three things that
// have to hold: the dry half still plays, the sent half arrives, and it
// arrives having actually been processed by the destination's units.
static void test_route_send_bus(void) {
  CHECK(unit_find("route") != NULL, "route unit not registered");

  tracker_init(&song_a);
  song_a.song_len = 1;
  song_a.loop = false;
  song_a.patterns[0][0] = 0;

  // Instrument 0: an oscillator feeding a ROUTE aimed at instrument 2.
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "route", 0);
  uint8_t* route_p = song_a.instruments[0].chain[1].params;

  // Instrument 2: bus-only — no source, just a gain it can be identified by.
  tracker_inst_set_slot(&song_a.instruments[2], 0, "pangain", 2);
  uint8_t* bus_p = song_a.instruments[2].chain[0].params;

  Pattern* p = tracker_pattern(&song_a, 0);
  CHECK(p != NULL, "route: pattern alloc failed");
  if (!p)
    return;
  p->len = 8;
  p->steps[0][0] = (PatternStep){.note = 60, .velocity = 127, .instrument = 0, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};

  route_p[0] = 0x00;  // MIX: all dry, nothing sent
  route_p[1] = 2;     // INST: destination
  bus_p[0] = 0x80;    // bus PAN center
  bus_p[1] = 0x80;    // bus GAIN unity
  double dry = render_song_energy("test_route_dry.wav");
  CHECK(dry > 0, "route: MIX=00 should play dry as usual, got energy %g", dry);

  // All sent, bus at unity: the same audio, arriving via instrument 2.
  route_p[0] = 0xFF;
  double sent = render_song_energy("test_route_sent.wav");
  CHECK(sent > dry * 0.5 && sent < dry * 2.0,
        "route: MIX=FF should sound about as loud through the bus (dry %g, sent %g)", dry, sent);

  // Same, but the bus silences what it is handed. Only reachable if the sent
  // audio really goes THROUGH instrument 2's chain rather than around it.
  bus_p[1] = 0x00;  // bus GAIN mute
  double muted = render_song_energy("test_route_muted.wav");
  CHECK(muted >= 0 && muted < dry * 0.01,
        "route: sent audio bypassed the destination chain (dry %g, through muted bus %g)", dry, muted);

  // A ROUTE pointed at its own instrument must not feed itself back.
  route_p[1] = 0;
  double self = render_song_energy("test_route_self.wav");
  CHECK(self >= 0, "route: self-send render failed");

  tracker_init(&song_a);
}

static void test_render_smoke(void) {
  const UnitDef* defs[64];
  int n = 0;
  unit_list(defs, &n);

  enum { BLK = 512,
         BLOCKS = 8 };
  static float in_l[BLK], in_r[BLK], out_l[BLK], out_r[BLK];

  for (int i = 0; i < n; i++) {
    const UnitDef* d = defs[i];
    UnitState* st = d->create(44100.0f);
    CHECK(st != NULL, "%s: create returned NULL", d->id);
    if (!st)
      continue;
    if (d->set_data)
      d->set_data(st, "", "./");

    uint8_t params[UNIT_MAX_PARAMS];
    memcpy(params, d->param_defaults, UNIT_MAX_PARAMS);
    if (d->note_on)
      d->note_on(st, 60, 100, params);

    bool all_finite = true;
    float energy = 0.0f;
    for (int blk = 0; blk < BLOCKS; blk++) {
      for (int f = 0; f < BLK; f++)
        in_l[f] = in_r[f] = unit_sin((blk * BLK + f) * (440.0f / 44100.0f)) * 0.5f;
      if (d->is_source) {
        memset(out_l, 0, sizeof(out_l));
        memset(out_r, 0, sizeof(out_r));
        d->render(st, params, NULL, NULL, out_l, out_r, BLK);
      } else {
        memcpy(out_l, in_l, sizeof(out_l));
        memcpy(out_r, in_r, sizeof(out_r));
        d->render(st, params, out_l, out_r, out_l, out_r, BLK);
      }
      for (int f = 0; f < BLK; f++) {
        if (!isfinite(out_l[f]) || !isfinite(out_r[f]))
          all_finite = false;
        energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
      }
    }
    CHECK(all_finite, "%s: non-finite output", d->id);
    // Self-contained synth sources must actually make sound after note_on;
    // file-backed sources (sf2/sfz/sampler/...) are silent with no file.
    if (strcmp(d->id, "osc") == 0 || strcmp(d->id, "fm") == 0 || strcmp(d->id, "drum") == 0)
      CHECK(energy > 1e-4f, "%s: silent after note_on", d->id);

    if (d->kill)
      d->kill(st);
    d->destroy(st);
  }
}

// The EQ unit: 16 graphic bands, one gain param each.
//
// The load-bearing properties are (a) flat is *exactly* transparent — a
// 16-band cascade at 0dB must not colour anything, and the ADD row's default
// is flat, so every instrument that adds an EQ but doesn't touch it would
// otherwise be subtly filtered; and (b) each param is the band its *name*
// says, i.e. boosting "500" moves a 500Hz tone and boosting "20" doesn't.
// Reversing the band order or the name table would pass the smoke test and be
// almost impossible to spot by ear in a mix.
static double eq_tone_rms(const UnitDef* def, UnitState* st, const uint8_t* params, float hz) {
  enum { BLK = 512,
         BLOCKS = 8 };
  static float l[BLK], r[BLK];
  double phase = 0.0, inc = hz / 44100.0;
  double rms = 0.0;

  def->kill(st);
  for (int blk = 0; blk < BLOCKS; blk++) {
    for (int f = 0; f < BLK; f++) {
      l[f] = r[f] = 0.5f * unit_sin((float)phase);
      phase += inc;
    }
    def->render(st, params, l, r, l, r, BLK);
    if (blk == BLOCKS - 1) {
      double s = 0.0;
      for (int f = 0; f < BLK; f++)
        s += (double)l[f] * l[f];
      rms = sqrt(s / BLK);
    }
  }
  return rms;
}

static void test_eq_unit(void) {
  const UnitDef* def = unit_find("eq");
  CHECK(def != NULL, "eq unit not registered");
  if (!def)
    return;

  CHECK(def->num_params == UNIT_MAX_PARAMS, "eq has %d params, want %d so every band gets one", def->num_params, UNIT_MAX_PARAMS);
  CHECK(!def->is_source, "eq should be an effect");

  UnitState* st = def->create(44100.0f);
  CHECK(st != NULL, "eq create failed");
  if (!st)
    return;

  enum { BLK = 512 };
  static float in_l[BLK], in_r[BLK], out_l[BLK], out_r[BLK];
  uint8_t params[UNIT_MAX_PARAMS];
  memcpy(params, def->param_defaults, UNIT_MAX_PARAMS);

  for (int f = 0; f < BLK; f++) {
    in_l[f] = unit_sin(f * (440.0f / 44100.0f)) * 0.4f;
    in_r[f] = unit_sin(f * (311.1f / 44100.0f)) * 0.4f;
  }

  // 1. Flat = transparent, bit for bit — both in place and into new buffers
  // (the flat path returns early, so it has to do the copy itself).
  memcpy(out_l, in_l, sizeof(out_l));
  memcpy(out_r, in_r, sizeof(out_r));
  def->render(st, params, out_l, out_r, out_l, out_r, BLK);
  CHECK(memcmp(out_l, in_l, sizeof(out_l)) == 0 && memcmp(out_r, in_r, sizeof(out_r)) == 0,
        "eq with flat defaults changed the signal in place — a flat 16-band cascade must be a no-op");

  memset(out_l, 0, sizeof(out_l));
  memset(out_r, 0, sizeof(out_r));
  def->render(st, params, in_l, in_r, out_l, out_r, BLK);
  CHECK(memcmp(out_l, in_l, sizeof(out_l)) == 0 && memcmp(out_r, in_r, sizeof(out_r)) == 0,
        "eq with flat defaults didn't pass the input through to separate output buffers");

  // 2. Each band is the frequency its name says.
  int idx_500 = -1, idx_20 = -1;
  for (int i = 0; i < def->num_params; i++) {
    if (strcmp(def->param_names[i], "500") == 0)
      idx_500 = i;
    if (strcmp(def->param_names[i], "20") == 0)
      idx_20 = i;
  }
  CHECK(idx_500 >= 0 && idx_20 >= 0, "eq: no band named \"500\" / \"20\" — names: %s / %s", def->param_names[0], def->param_names[UNIT_MAX_PARAMS - 1]);
  if (idx_500 < 0 || idx_20 < 0) {
    def->destroy(st);
    return;
  }

  double flat = eq_tone_rms(def, st, params, 500.0f);
  CHECK(flat > 0.1, "eq: flat tone measurement failed (rms %g)", flat);

  params[idx_500] = 0xFF;  // +15 dB
  double in_band = eq_tone_rms(def, st, params, 500.0f) / flat;
  params[idx_500] = 0x80;

  params[idx_20] = 0x00;  // -15 dB, four and a half octaves below the tone
  double out_of_band = eq_tone_rms(def, st, params, 500.0f) / flat;
  params[idx_20] = 0x80;

  // +15dB is x5.62 in amplitude; the tone sits exactly on the band centre, so
  // that's what a correct band gives.
  CHECK(in_band > 4.5 && in_band < 7.0, "eq: boosting the \"500\" band moved a 500Hz tone by x%.2f, want ~x5.6 (+15dB at the band centre) — bands may be miswired", in_band);
  CHECK(out_of_band > 0.95 && out_of_band < 1.05, "eq: cutting the \"20\" band changed a 500Hz tone by x%.2f, want ~x1.0 — a band is affecting the wrong frequency", out_of_band);

  // A cut must go the other way, by the same factor.
  params[idx_500] = 0x00;
  double cut = eq_tone_rms(def, st, params, 500.0f) / flat;
  params[idx_500] = 0x80;
  CHECK(cut > 0.05 && cut < 0.3, "eq: cutting the \"500\" band moved a 500Hz tone by x%.2f, want ~x0.18 (-15dB) — cut and boost may be swapped", cut);

  // 3. Every band at once, both extremes: still finite (a cascade of 16
  // peaking biquads has to stay stable at full boost).
  for (int i = 0; i < UNIT_MAX_PARAMS; i++)
    params[i] = 0xFF;
  for (int blk = 0; blk < 8; blk++) {
    for (int f = 0; f < BLK; f++)
      out_l[f] = out_r[f] = unit_sin(f * (500.0f / 44100.0f)) * 0.2f;
    def->render(st, params, out_l, out_r, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      CHECK(isfinite(out_l[f]) && isfinite(out_r[f]), "eq: non-finite output with every band at +15dB");
  }

  for (int i = 0; i < UNIT_MAX_PARAMS; i++)
    params[i] = 0x00;
  for (int f = 0; f < BLK; f++)
    out_l[f] = out_r[f] = unit_sin(f * (500.0f / 44100.0f)) * 0.2f;
  def->render(st, params, out_l, out_r, out_l, out_r, BLK);
  for (int f = 0; f < BLK; f++)
    CHECK(isfinite(out_l[f]) && isfinite(out_r[f]), "eq: non-finite output with every band at -15dB");

  // 4. The ADD row prints dB, not a bare byte.
  const char* text = def->format_param_val ? def->format_param_val(st, 0, 0x80) : NULL;
  CHECK(text && strcmp(text, "0.0 dB") == 0, "eq: flat displays as \"%s\", want \"0.0 dB\"", text ? text : "(null)");
  text = def->format_param_val ? def->format_param_val(st, 0, 0xFF) : NULL;
  CHECK(text && strcmp(text, "+15.0 dB") == 0, "eq: full boost displays as \"%s\", want \"+15.0 dB\"", text ? text : "(null)");
  text = def->format_param_val ? def->format_param_val(st, 0, 0x00) : NULL;
  CHECK(text && strcmp(text, "-15.0 dB") == 0, "eq: full cut displays as \"%s\", want \"-15.0 dB\"", text ? text : "(null)");

  def->destroy(st);
}

// The EQ inside a real instrument chain, driven by the real playback path:
// osc -> eq, note-on through AudioEngine, same render_channel call the app
// uses (in-place, per tick-aligned sub-block). The unit test above proves the
// DSP; this proves the unit is wired into a chain at all — flat and boosted
// runs have to differ, and a chain that never renders it would make them
// identical.
static double eq_chain_energy(uint8_t band3_value) {
  static AudioEngine eng;
  enum { BLK = 512,
         BLOCKS = 12 };
  static float blk[BLK * 2];

  tracker_init(&song_a);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "eq", 0);
  ChainSlot* eq = &song_a.instruments[0].chain[1];
  // A 261Hz note (C4) sits between the 200 and 315 bands, so lifting the low
  // mids has to move it a lot. Each of 200/315/500 gets +15dB.
  eq->params[5] = band3_value;
  eq->params[6] = band3_value;
  eq->params[7] = band3_value;

  audio_init(&eng, &song_a);
  audio_midi_note_on(&eng, 0, 60);
  AudioDenormalState denorm_prev = audio_denormals_off();
  double energy = 0;
  for (int b = 0; b < BLOCKS; b++) {
    audio_fill_buffer(&eng, blk, BLK);
    for (int f = 0; f < BLK * 2; f++)
      energy += (double)blk[f] * blk[f];
  }
  audio_midi_note_off(&eng, 0, 60);
  audio_shutdown(&eng);
  // audio_fill_buffer() deliberately leaves flush-to-zero on for the calling
  // thread (on the audio thread it stays on for the thread's life). A unit
  // test isn't the audio thread, so hand the process back the way we found it
  // — same discipline as audio_render_wav().
  audio_denormals_restore(denorm_prev);
  return energy;
}

static void test_eq_in_chain(void) {
  double flat = eq_chain_energy(0x80);
  double boosted = eq_chain_energy(0xFF);

  CHECK(flat > 1e-6, "osc->eq chain is silent");
  CHECK(boosted > flat * 1.5, "osc->eq: boosting the 200/315/500 bands changed the chain's energy only from %g to %g — the EQ may not be in the render path", flat, boosted);
}

// End-to-end check of the pd2wclap pipeline: loads the pre-built demo WCLAP
// plugin (plugins/pd2wclap/build/pd-osc.wasm — see plugins/pd2wclap/README.md)
// straight through the real Wasmtime host, same path clap_unit.c uses, sends
// a note on, and checks for actual non-silent finite output. Skips (not
// fails) if the demo hasn't been built, since that requires an external
// toolchain (pd2ast/pdast2wclap/wasi-sdk) not every dev/CI box has.
static void test_clap_plugin_pd(void) {
  const char* path = "../plugins/pd2wclap/build/pd-osc.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_plugin_pd: %s not built (see plugins/pd2wclap/README.md)\n", path);
    return;
  }

  ClapPlugin* p = clap_host_load(path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "pd-osc.wasm: load failed");
  if (!p)
    return;

  CHECK(clap_host_is_instrument(p), "pd-osc.wasm: expected an instrument (notein port)");

  uint32_t total = clap_host_param_count(p);
  CHECK(total == 1, "pd-osc.wasm: expected 1 param (volume), got %u", total);
  uint32_t volume_id = 0;
  char name[24];
  double min = 0, max = 0, def = 0;
  if (total > 0) {
    clap_host_param_info(p, 0, &volume_id, name, sizeof(name), &min, &max, &def);
    CHECK(strcmp(name, "volume") == 0, "pd-osc.wasm: param 0 name %s, want volume", name);
  }
  clap_host_queue_param(p, volume_id, 1.0);

  clap_host_note_on(p, 60, 100, 0);

  enum { BLK = 512,
         BLOCKS = 8 };
  static float out_l[BLK], out_r[BLK];
  bool all_finite = true;
  float energy = 0.0f;
  for (int blk = 0; blk < BLOCKS; blk++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++) {
      if (!isfinite(out_l[f]) || !isfinite(out_r[f]))
        all_finite = false;
      energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
    }
  }
  CHECK(all_finite, "pd-osc.wasm: non-finite output");
  CHECK(energy > 1e-4f, "pd-osc.wasm: silent after note_on + volume param");

  clap_host_unload(p);
}

// End-to-end check of the juno1 WCLAP instrument (plugins/juno1 — a
// Roland Juno-1/Alpha Juno DCO synth ported from mikerodd/june-21, see
// plugins/juno1/README.md), same real-Wasmtime-host path as
// test_clap_plugin_pd: loads examples/plugins/juno1.wclap.wasm, checks its
// 16 params are present with Patch first, that a few different factory
// Patch values each produce distinct non-silent finite audio (i.e. the
// bundled preset table decoded correctly and actually drives the engine),
// and that releasing a note lets it decay to silence rather than hanging.
static void test_clap_plugin_juno1(void) {
  const char* path = "../examples/plugins/juno1.wclap.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_plugin_juno1: %s not built (see plugins/juno1/README.md)\n", path);
    return;
  }

  ClapPlugin* p = clap_host_load(path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "juno1.wclap.wasm: load failed");
  if (!p)
    return;

  CHECK(clap_host_is_instrument(p), "juno1.wclap.wasm: expected an instrument");

  uint32_t total = clap_host_param_count(p);
  CHECK(total == 37, "juno1.wclap.wasm: expected 37 params (Patch + 36 real hardware params), got %u", total);
  uint32_t patch_id = 0;
  char name[24];
  double min = 0, max = 0, def = 0;
  if (total > 0) {
    clap_host_param_info(p, 0, &patch_id, name, sizeof(name), &min, &max, &def);
    CHECK(strcmp(name, "Patch") == 0, "juno1.wclap.wasm: param 0 name %s, want Patch", name);
    CHECK(max == 255, "juno1.wclap.wasm: Patch max %g, want 255 (its own natural range — 256 bundled patches; poketrack's ADD row scales its raw byte into whatever range the param declares)", max);
  }

  enum { BLK = 512, BLOCKS = 8 };
  static float out_l[BLK], out_r[BLK];

  // A few spread-out factory patches should each sound distinct — compare
  // total energy across patches as a coarse "not literally identical" check.
  int patch_ids[] = {0, 20, 60, 100};
  float patch_energy[4];
  for (int pi = 0; pi < 4; pi++) {
    clap_host_queue_param(p, patch_id, (double)patch_ids[pi]);
    clap_host_note_on(p, 60, 100, 0);
    bool all_finite = true;
    float energy = 0.0f;
    for (int blk = 0; blk < BLOCKS; blk++) {
      clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
      for (int f = 0; f < BLK; f++) {
        if (!isfinite(out_l[f]) || !isfinite(out_r[f]))
          all_finite = false;
        energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
      }
    }
    CHECK(all_finite, "juno1.wclap.wasm: non-finite output on patch %d", patch_ids[pi]);
    CHECK(energy > 1e-4f, "juno1.wclap.wasm: silent on patch %d", patch_ids[pi]);
    patch_energy[pi] = energy;
    clap_host_note_off(p, 60, 0);
    // Let the voice fully release before the next patch's note-on.
    for (int blk = 0; blk < BLOCKS * 4; blk++)
      clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
  }
  bool any_different = false;
  for (int pi = 1; pi < 4; pi++) {
    float ratio = patch_energy[pi] / patch_energy[0];
    if (ratio < 0.8f || ratio > 1.25f)
      any_different = true;
  }
  CHECK(any_different, "juno1.wclap.wasm: all sampled patches produced near-identical energy — preset table may not be wired up");

  clap_host_unload(p);

  // Note-off should let the voice decay to silence, not hang forever —
  // fresh instance so nothing from the patch-comparison loop above bleeds
  // in (its own release tails may still be well audible after only ~370ms).
  ClapPlugin* p2 = clap_host_load(path, NULL, 44100.0f, 512);
  CHECK(p2 != NULL, "juno1.wclap.wasm: reload failed");
  if (!p2)
    return;
  clap_host_note_on(p2, 60, 100, 0);
  for (int blk = 0; blk < BLOCKS; blk++)
    clap_host_process(p2, NULL, NULL, out_l, out_r, BLK);
  clap_host_note_off(p2, 60, 0);
  for (int blk = 0; blk < 172; blk++)  // ~2s at 512/44100 — longest factory release tails clear well within this
    clap_host_process(p2, NULL, NULL, out_l, out_r, BLK);
  float released_energy = 0.0f;
  for (int blk = 0; blk < BLOCKS; blk++) {
    clap_host_process(p2, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      released_energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(released_energy < 1e-4f, "juno1.wclap.wasm: still loud (%g) well after note-off — envelope/voice not releasing", released_energy);

  clap_host_unload(p2);
}

// Regression test for a real crash: poketrack's PatternStep.velocity is a
// raw 0-255 byte, and screen_pattern.c defaults a freshly-entered note to
// 0xFF (full) — but wclap_host_native.c's clap_host_note_on() normalizes
// by /127.0, not /255.0, so CLAP's nominally-0.0-1.0 velocity comes out
// above 1.0 (up to ~2.0) for a plain default note. juno1's default patch
// ("PolySynth1", vcaEnv=2 "Dyn-Normal") indexes a 128-entry table with
// round(velocity*127) — unclamped, that's index ~255, and AssemblyScript's
// bounds checks stay on in release builds, so this trapped the wasm module
// on essentially every normally-typed pattern note. This is likely what
// "works in preview (moderate velocity) but breaks once you play a
// pattern (default full-velocity notes)" actually was. Drives note_on with
// the real uint8_t max (255) the same way real playback does, both
// directly and through the actual clap unit + AudioEngine pattern-playback
// path, and checks the plugin/engine survives and keeps producing audio.
static void test_clap_plugin_juno1_full_velocity_note(void) {
  const char* path = "../examples/plugins/juno1.wclap.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_plugin_juno1_full_velocity_note: %s not built\n", path);
    return;
  }

  ClapPlugin* p = clap_host_load(path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "juno1.wclap.wasm: load failed");
  if (p) {
    clap_host_note_on(p, 60, 255, 0);  // full byte velocity, default patch (vcaEnv=2)
    enum { BLK = 512, BLOCKS = 8 };
    static float out_l[BLK], out_r[BLK];
    bool all_finite = true;
    float energy = 0.0f;
    for (int blk = 0; blk < BLOCKS; blk++) {
      clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
      for (int f = 0; f < BLK; f++) {
        if (!isfinite(out_l[f]) || !isfinite(out_r[f]))
          all_finite = false;
        energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
      }
    }
    CHECK(all_finite, "juno1.wclap.wasm: non-finite output with velocity=255");
    CHECK(energy > 1e-4f, "juno1.wclap.wasm: silent with velocity=255 (default patch, vcaEnv=2)");
    clap_host_unload(p);
  }

  // Same thing through the real pattern-playback path: a freshly-entered
  // note (default velocity 0xFF, per screen_pattern.c) on the default patch.
  static AudioEngine eng;
  tracker_init(&song_a);
  song_a.song_len = 1;
  song_a.loop = false;
  song_a.bpm = 120;
  song_a.patterns[0][0] = 0;
  ChainSlot* sl = &song_a.instruments[0].chain[0];
  tracker_inst_set_slot(&song_a.instruments[0], 0, "clap", 0);
  strncpy(sl->data, path, sizeof(sl->data) - 1);
  Pattern* pat = tracker_pattern(&song_a, 0);
  CHECK(pat != NULL, "full-velocity: pattern alloc failed");
  if (pat) {
    pat->len = 4;
    pat->steps[0][0] = (PatternStep){.note = 60, .velocity = 0xFF, .instrument = 0, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
    audio_init(&eng, &song_a);
    bool ok = audio_render_wav(&eng, "test_full_velocity.wav");
    audio_shutdown(&eng);
    CHECK(ok, "full-velocity: render failed");
    if (ok) {
      Wave w = LoadWave("test_full_velocity.wav");
      const int16_t* pcm = (const int16_t*)w.data;
      bool silent = true;
      for (unsigned i = 0; pcm && i < w.frameCount * w.channels; i++)
        if (pcm[i] != 0) { silent = false; break; }
      CHECK(!silent, "full-velocity pattern render is all silence");
      UnloadWave(w);
      remove("test_full_velocity.wav");
    }
  }
}

// Checks clap_unit.c's format_param_val (the ADD-row value display in
// screen_instrument.c) actually calls through to the plugin's own
// CLAP_EXT_PARAMS value_to_text via the new clap_host_param_value_to_text,
// instead of only ever showing a raw number — juno1's "Patch" param names
// the currently-selected preset ("JazzGuitar"), which a plain int can't.
// Also checks dyn_param_is_enum, which the UI uses to skip drawing the
// slider bar over that name for CLAP_PARAM_IS_ENUM params like Patch.
static void test_clap_format_param_val_uses_value_to_text(void) {
  const char* path = "../examples/plugins/juno1.wclap.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_format_param_val_uses_value_to_text: %s not built\n", path);
    return;
  }

  const UnitDef* def = unit_find("clap");
  CHECK(def != NULL, "clap unit not registered");
  if (!def)
    return;

  UnitState* s = def->create(44100.0f);
  CHECK(s != NULL, "clap_unit_create failed");
  if (!s)
    return;
  char data[640];
  snprintf(data, sizeof(data), "%s\t\t", path);
  def->set_data(s, data, "./");

  CHECK(def->picker_count(s) == 37, "juno1 exposes %d params via picker, want 37", def->picker_count(s));
  def->picker_add(s, 0);  // "Patch"
  CHECK(def->dyn_num_params(s) == 1, "picker_add didn't map Patch");

  // Patch declares its own natural range (0-127); clap_unit.c's
  // clap_byte_to_value maps a stepped param's ADD-row byte directly onto
  // steps from its min, so byte 1 lands on preset index 1 ("JazzGuitar").
  const char* text = def->format_param_val(s, 0, 1);
  CHECK(text != NULL, "format_param_val returned NULL for a value_to_text-capable param");
  if (text)
    CHECK(strcmp(text, "JazzGuitar") == 0, "format_param_val returned \"%s\", want \"JazzGuitar\"", text);

  // Patch is CLAP_PARAM_IS_ENUM (every value gets a real name), so the UI
  // should treat it like a built-in enum param and skip the slider bar.
  CHECK(def->dyn_param_is_enum != NULL, "clap unit doesn't implement dyn_param_is_enum");
  if (def->dyn_param_is_enum)
    CHECK(def->dyn_param_is_enum(s, 0), "Patch not reported as enum -- slider bar would cover its name in the UI");

  // The actual "1 bump = 1 step" property: for a stepped param whose whole
  // range fits in a byte (256 bundled patches here, filling it exactly),
  // clap_byte_to_value must map every single ADD-row byte 0..255 onto a
  // genuinely different step — proportionally scaling the full 0-255 byte
  // range across a narrower param range (like a plain continuous knob
  // would use) instead means most single bumps land on the same rounded
  // step twice in a row.
  // format_param_val returns a shared static buffer, so copy each result
  // out before the next call overwrites it.
  char prev[64] = {0};
  const char* first = def->format_param_val(s, 0, 0);
  snprintf(prev, sizeof(prev), "%s", first ? first : "");
  for (int b = 1; b <= 255; b++) {
    const char* now = def->format_param_val(s, 0, (uint8_t)b);
    CHECK(now != NULL && strcmp(now, prev) != 0,
          "Patch byte %d..%d didn't change preset (\"%s\" -> \"%s\") -- a single ADD-row bump should always move one patch",
          b - 1, b, prev, now ? now : "(null)");
    snprintf(prev, sizeof(prev), "%s", now ? now : "");
  }

  def->destroy(s);
}

// Regression test for "a statically ADD-mapped param plays in preview but
// not once triggered via real pattern/MIDI playback" — mirrors
// test_clap_param_mapping_reaches_shared_instance's real ADD flow
// (ensure_preview, picker_add, sync_to_data, audio_rebuild_instrument) but
// checks actual rendered audio from both the preview instance and the
// separate shared instance real playback uses, not just that the mapping
// array was copied over. Patch byte 2 ("Xylophone") is picked because it's
// known-loud within a short window (patch 0's own attack ramp makes a tight
// energy comparison noisier) — see plugins/juno1/vendor/FACTORYA.SYX.
static void test_clap_juno1_static_mapping_reaches_shared_instance(void) {
  const char* path = "../examples/plugins/juno1.wclap.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_juno1_static_mapping_reaches_shared_instance: %s not built\n", path);
    return;
  }

  static AudioEngine eng;
  tracker_init(&song_a);
  ChainSlot* sl = &song_a.instruments[0].chain[0];
  tracker_inst_set_slot(&song_a.instruments[0], 0, "clap", 0);
  strncpy(sl->data, path, sizeof(sl->data) - 1);
  audio_init(&eng, &song_a);

  const UnitDef* def = unit_find("clap");
  CHECK(def != NULL, "clap unit not registered");
  if (!def) {
    audio_shutdown(&eng);
    return;
  }

  audio_ensure_preview(&eng, 0);
  UnitState* preview = eng.preview_states[0];
  CHECK(preview != NULL, "preview_states[0] not created");
  if (!preview) {
    audio_shutdown(&eng);
    return;
  }
  CHECK(def->picker_count(preview) == 37, "juno1 exposes %d params, want 37", def->picker_count(preview));
  CHECK(strcmp(def->picker_name(preview, 0), "Patch") == 0,
        "picker index 0 is %s, not Patch — adjust this test", def->picker_name(preview, 0));

  def->picker_add(preview, 0);  // map "Patch"
  CHECK(def->dyn_num_params(preview) == 1, "picker_add didn't add a mapping");

  // Patch's ADD-row byte maps directly onto its 0-255 range (see
  // clap_unit.c's clap_byte_to_value): byte 2 is preset index 2
  // ("Xylophone") out of 256 bundled patches.
  def->set_param_val(preview, 0, 2);
  def->sync_to_data(preview, sl->data, sizeof(sl->data));
  audio_rebuild_instrument(&eng, 0);

  enum { BLK = 512, BLOCKS = 8 };
  static float blk[BLK * 2];

  audio_preview_note(&eng, 0, 60);
  double preview_energy = 0;
  for (int b = 0; b < BLOCKS; b++) {
    audio_fill_buffer(&eng, blk, BLK);
    for (int f = 0; f < BLK * 2; f++)
      preview_energy += (double)blk[f] * blk[f];
  }
  audio_preview_kill(&eng);
  for (int b = 0; b < BLOCKS; b++)  // let the preview voice fully release
    audio_fill_buffer(&eng, blk, BLK);
  CHECK(preview_energy > 1e-4, "juno1: preview silent with Patch statically mapped to 'Xylophone' (energy=%g)", preview_energy);

  // Now the real pattern/MIDI playback path — a separate wasm instance.
  audio_midi_note_on(&eng, 0, 60);
  UnitState* shared = eng.shared_states[0][0];
  CHECK(shared != NULL, "shared_states[0][0] not created by note-on");
  if (shared)
    CHECK(def->dyn_num_params(shared) == 1 && def->get_param_val(shared, 0) == 2,
          "shared instance's Patch mapping is %d params / val %d, want 1 / 2 — mapping never reached the playing instance",
          def->dyn_num_params(shared), def->dyn_num_params(shared) > 0 ? def->get_param_val(shared, 0) : -1);
  double shared_energy = 0;
  for (int b = 0; b < BLOCKS; b++) {
    audio_fill_buffer(&eng, blk, BLK);
    for (int f = 0; f < BLK * 2; f++)
      shared_energy += (double)blk[f] * blk[f];
  }
  CHECK(shared_energy > 1e-4, "juno1: SILENT via real playback (audio_midi_note_on) with Patch statically mapped to 'Xylophone' (energy=%g), even though preview played fine (energy=%g)", shared_energy, preview_energy);

  audio_midi_note_off(&eng, 0, 60);
  audio_shutdown(&eng);
}

// A freshly-added PLUGIN unit in poketrack never explicitly sets a param
// before the first note — it plays with whatever default value the CLAP
// host reports (which comes straight from each GUI slider's saved
// default_value field in the .pd file). If that default sits at a
// degenerate position (a gain of 0, or a filter cutoff of 0 Hz — below
// its own declared 100..5000 range), the plugin is correctly silent by
// design, not broken — but that's indistinguishable from "doesn't work"
// to a user who just added the instrument and played a note. This test
// locks down that both demo patches are actually audible out of the box.
static void test_clap_plugin_pd_default_params_are_audible(void) {
  const char* paths[] = {
      "../plugins/pd2wclap/build/pd-osc.wasm",
      "../plugins/pd2wclap/build/pd-vcf.wasm",
  };
  for (int i = 0; i < 2; i++) {
    const char* path = paths[i];
    if (!FileExists(path)) {
      printf("SKIP test_clap_plugin_pd_default_params_are_audible: %s not built\n", path);
      continue;
    }
    ClapPlugin* p = clap_host_load(path, NULL, 44100.0f, 512);
    CHECK(p != NULL, "%s: load failed", path);
    if (!p)
      continue;

    if (clap_host_is_instrument(p))
      clap_host_note_on(p, 60, 100, 0);

    enum { BLK = 512, BLOCKS = 8 };
    static float out_l[BLK], out_r[BLK];
    float energy = 0.0f;
    for (int blk = 0; blk < BLOCKS; blk++) {
      clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
      for (int f = 0; f < BLK; f++)
        energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
    }
    CHECK(energy > 1e-4f, "%s: silent with default params (no param explicitly set) — check GUI slider default_value fields", path);

    clap_host_unload(p);
  }
}

// Regression test for a real bug in the ADD-param-mapping flow
// (screen_instrument.c): adding a new param mapping to a CLAP unit (via
// def->picker_add + sync_to_data) mutates preview_states[slot], but unless
// audio_rebuild_instrument() runs afterward too, shared_states[inst][slot]
// — the instance actually used for pattern/MIDI playback — keeps its old,
// shorter mappings[] array. The next param-value edit then silently no-ops
// on the live instance (index out of range), with no error: "params don't
// do anything until I add another unit" (adding a unit happens to trigger
// exactly the audio_rebuild_instrument call this path was missing). This
// drives the real AudioEngine/ChainSlot machinery end to end, mirroring
// what screen_instrument.c now does, and checks the newly-added mapping
// and a value change on it both reach the shared instance with no other
// chain edit in between.
static void test_clap_param_mapping_reaches_shared_instance(void) {
  const char* path = "../plugins/pd2wclap/build/pd-osc.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_param_mapping_reaches_shared_instance: %s not built\n", path);
    return;
  }

  static AudioEngine eng;
  tracker_init(&song_a);
  ChainSlot* sl = &song_a.instruments[0].chain[0];
  tracker_inst_set_slot(&song_a.instruments[0], 0, "clap", 0);
  strncpy(sl->data, path, sizeof(sl->data) - 1);  // no tab-suffix: zero mappings yet, like a freshly-loaded plugin
  audio_init(&eng, &song_a);

  const UnitDef* def = unit_find("clap");
  CHECK(def != NULL, "clap unit not registered");
  if (!def) {
    audio_shutdown(&eng);
    return;
  }

  // Mirror screen_instrument.c's ADD-row flow: ensure_preview, picker_add,
  // sync_to_data, then (the fix) rebuild the instrument.
  audio_ensure_preview(&eng, 0);
  UnitState* preview = eng.preview_states[0];
  CHECK(preview != NULL, "preview_states[0] not created");
  if (!preview) {
    audio_shutdown(&eng);
    return;
  }
  CHECK(def->picker_count(preview) > 0, "pd-osc.wasm exposes no pickable params");
  def->picker_add(preview, 0);  // maps pd-osc's one param ("volume")
  CHECK(def->dyn_num_params(preview) == 1, "picker_add didn't add a mapping to preview state");
  def->sync_to_data(preview, sl->data, sizeof(sl->data));
  audio_rebuild_instrument(&eng, 0);

  // Now bring up the shared instance the way real playback does (a MIDI
  // note), with no OTHER chain edit since the mapping was added.
  audio_midi_note_on(&eng, 0, 60);
  UnitState* shared = eng.shared_states[0][0];
  CHECK(shared != NULL, "shared_states[0][0] not created by note-on");
  if (shared) {
    CHECK(def->dyn_num_params(shared) == 1,
          "shared instance has %d mappings, want 1 — new mapping never reached the playing instance",
          def->dyn_num_params(shared));

    audio_set_dyn_param(&eng, 0, 0, 0, 200);
    CHECK(def->get_param_val(shared, 0) == 200,
          "shared instance param value is %d after set, want 200 — edit never reached the playing instance",
          def->get_param_val(shared, 0));
  }

  audio_shutdown(&eng);
}

// Same real-Wasmtime-host path as test_clap_plugin_pd, but for the
// polyphonic supersaw patch (notein -> poly 4 1 -> 4 [voice] sub-patch
// instances) — checks it's actually silent with no notes held, and
// produces real overlapping-note polyphony: two simultaneously-held
// notes must sound *louder* (more energy) than either one alone, which
// is only possible if poly is correctly assigning them to separate
// voices rather than one voice stomping on the other.
static void test_clap_plugin_pd_supersaw_polyphony(void) {
  const char* path = "../plugins/pd2wclap/build/pd-supersaw.wasm";
  if (!FileExists(path)) {
    printf("SKIP test_clap_plugin_pd_supersaw_polyphony: %s not built\n", path);
    return;
  }

  ClapPlugin* p = clap_host_load(path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "pd-supersaw.wasm: load failed");
  if (!p)
    return;

  enum { BLK = 512 };
  static float out_l[BLK], out_r[BLK];

  // Silent with nothing held.
  float silent_energy = 0.0f;
  for (int blk = 0; blk < 4; blk++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      silent_energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(silent_energy < 1e-6f, "pd-supersaw.wasm: not silent with no notes held (energy=%g)", silent_energy);

  // One note, let the attack ramp settle, measure.
  clap_host_note_on(p, 60, 100, 0);
  for (int blk = 0; blk < 8; blk++)
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
  float one_note_energy = 0.0f;
  for (int blk = 0; blk < 4; blk++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      one_note_energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(one_note_energy > 1e-4f, "pd-supersaw.wasm: silent after single note_on");

  // Add a second, different, overlapping note (not choked off — this is
  // the real overlapping-note path clap_unit.c doesn't exercise).
  clap_host_note_on(p, 67, 100, 0);
  for (int blk = 0; blk < 8; blk++)
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
  float two_note_energy = 0.0f;
  for (int blk = 0; blk < 4; blk++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      two_note_energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(two_note_energy > one_note_energy * 1.3f,
        "pd-supersaw.wasm: two held notes (%g) not louder than one (%g) — poly isn't assigning separate voices",
        two_note_energy, one_note_energy);

  clap_host_note_off(p, 60, 0);
  clap_host_note_off(p, 67, 0);
  for (int blk = 0; blk < 20; blk++)
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
  float released_energy = 0.0f;
  for (int blk = 0; blk < 4; blk++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, BLK);
    for (int f = 0; f < BLK; f++)
      released_energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(released_energy < one_note_energy * 0.05f,
        "pd-supersaw.wasm: still loud (%g) well after both notes released", released_energy);

  clap_host_unload(p);
}

// Regression test for a dangling-pointer crash on save. lib_release() in
// wclap_host_native.c used to compact its s_libs[] array by shifting entries
// down, but every ClapPlugin holds a raw LibEntry* into that array — so
// tearing down the FIRST of several loaded WCLAPs silently repointed the
// others at the wrong entry, and the next unload closed a wclap whose plugin
// instance was still alive (wclap-bridge detects that and abort()s).
//
// Needs three or more distinct WCLAP files loaded at once to show up, which
// is exactly what a real song with several plugin instruments does, and what
// audio_set_save_dir()'s full teardown after every save triggers.
static void test_multiple_wclap_teardown_does_not_dangle(void) {
  const char* plugins[] = {
      "../examples/plugins/karp.wclap.wasm",
      "../examples/plugins/pd-osc.wclap.wasm",
      "../examples/plugins/pd-supersaw.wclap.wasm",
  };
  const int n = 3;
  for (int i = 0; i < n; i++) {
    if (!FileExists(plugins[i])) {
      printf("SKIP test_multiple_wclap_teardown_does_not_dangle: %s not built\n", plugins[i]);
      return;
    }
  }

  ClapPlugin* p[3];
  for (int i = 0; i < n; i++) {
    p[i] = clap_host_load(plugins[i], NULL, 44100.0f, 512);
    CHECK(p[i] != NULL, "%s: load failed", plugins[i]);
    if (!p[i])
      return;
  }
  // Unload front-to-back: this is the order that used to corrupt the entries
  // still held by the not-yet-unloaded plugins.
  for (int i = 0; i < n; i++)
    clap_host_unload(p[i]);

  // Re-load afterwards to prove freed slots are genuinely reusable (the fix
  // marks slots free in place rather than compacting).
  ClapPlugin* again = clap_host_load(plugins[0], NULL, 44100.0f, 512);
  CHECK(again != NULL, "reload after teardown failed — freed slot not reusable");
  if (again)
    clap_host_unload(again);
}

// The whole point of CHOPPER is that its repeat length comes from the song
// BPM, so verify the actual period of the looped slice rather than just that
// it makes finite noise (test_render_smoke already covers that). 1/8 note at
// 120 BPM = 16 lines / 8 = 2 lines.
// A tempo-synced LFO must derive its phase from song position, not from a
// per-instance accumulator. The chain is instantiated per lane/track, so an
// instrument playing on several tracks has several copies of its LFO, all
// writing the same target param — copies created at different times used to
// sit at different phases and the value juddered between them (last writer
// wins). Also checks the cycle actually lands on the advertised division.
// The audio callback must leave denormals flushed to zero. filter.c's SVF
// state decays into the denormal range and STAYS there while its input is
// quiet (a low cutoff is a long time constant), and on x86-64 every denormal
// operation takes a microcode assist costing ~100x a normal one — enough to
// blow a block deadline and stutter. This is why an LFO on filter cutoff
// stuttered on Linux/x86 but never on ARM, where denormals are near-free.
//
// Tests the wiring, not just denormal.h: it clears the mode, checks the check
// itself can see denormals, then runs one real audio_fill_buffer() and
// requires the mode to be set afterwards.
static bool makes_denormal(void) {
  volatile float tiny = FLT_MIN, half = 0.5f;
  volatile float out = tiny * half;
  return out != 0.0f;
}
// A track keeps its chain loaded once it has played a note, so the engine used
// to re-render every chain forever even when it was producing silence. The
// gate skips those, but it must be invisible: a later note has to wake the
// track, and a chain holding a unit with side effects (LFO here) must never be
// gated at all or its modulation would stop with it.
static void test_idle_track_gate_wakes_on_note(void) {
  static AudioEngine eng;
  tracker_init(&song_a);
  song_a.bpm = 120;
  song_a.song_len = 1;
  song_a.loop = false;
  song_a.patterns[0][0] = 0;

  // Instrument 0: plain osc — gateable. Instrument 1: osc + lfo — never gated.
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  song_a.instruments[0].chain[0].enabled = 1;
  tracker_inst_set_slot(&song_a.instruments[1], 0, "osc", 1);
  song_a.instruments[1].chain[0].enabled = 1;
  tracker_inst_set_slot(&song_a.instruments[1], 1, "lfo", 1);
  song_a.instruments[1].chain[1].enabled = 1;

  Pattern* p = tracker_pattern(&song_a, 0);
  CHECK(p != NULL, "gate: pattern alloc failed");
  if (!p)
    return;
  p->len = 64;
  // track 0: note at step 0, released at step 1, note again at step 32
  p->steps[0][0] = (PatternStep){.note = 60, .velocity = 200, .instrument = 0, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  p->steps[0][1] = (PatternStep){.note = NOTE_OFF, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  p->steps[0][32] = (PatternStep){.note = 64, .velocity = 200, .instrument = 0, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  // track 1: one note, never again — its chain has an LFO so it stays awake
  p->steps[1][0] = (PatternStep){.note = 48, .velocity = 200, .instrument = 1, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  p->steps[1][1] = (PatternStep){.note = NOTE_OFF, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};

  audio_init(&eng, &song_a);
  audio_play(&eng);

  enum { BLK = 512 };
  static float buf[BLK * 2];
  const uint32_t spl = AUDIO_SAMPLE_RATE * 60u / (120u * 4u);  // samples per line

  // Render up to just before the second note, tracking whether the gate ever
  // engaged on track 0 and how loud things were once it did.
  bool gated_seen = false;
  double energy_after_gate = 0;
  uint32_t rendered = 0;
  while (rendered < spl * 30) {
    audio_fill_buffer(&eng, buf, BLK);
    rendered += BLK;
    if (eng.chan_gated[0][0]) {
      gated_seen = true;
      for (int i = 0; i < BLK * 2; i++) energy_after_gate += (double)buf[i] * buf[i];
    }
  }
  CHECK(gated_seen, "gate: a track silent for 30 lines was never gated");
  CHECK(energy_after_gate < 1e-6,
        "gate: gated track still contributed audio (energy %g)", energy_after_gate);
  CHECK(!eng.chan_gated[0][1],
        "gate: a chain holding an LFO must never be gated — its modulation would stop");

  // Now cross the second note and confirm the track came back.
  double energy_after_note = 0;
  while (rendered < spl * 36) {
    audio_fill_buffer(&eng, buf, BLK);
    rendered += BLK;
    for (int i = 0; i < BLK * 2; i++) energy_after_note += (double)buf[i] * buf[i];
  }
  CHECK(energy_after_note > 1e-3,
        "gate: track did not wake on its next note (energy %g) — notes are being swallowed",
        energy_after_note);
  CHECK(!eng.chan_gated[0][0], "gate: track still marked gated after a note");

  audio_shutdown(&eng);
}

static void test_audio_callback_flushes_denormals(void) {
#if !AUDIO_DENORMALS_CONTROLLED
  return;  // no per-thread denormal control on this target (wasm)
#else
  // Clear the flush bits, leaving rounding/exception-mask bits alone — and
  // clear them *explicitly* rather than round-tripping whatever the thread is
  // in now: audio_fill_buffer() sets the mode on whatever thread calls it (on
  // the audio thread, correctly, for that thread's whole life), so any earlier
  // test that rendered audio leaves this one starting from "flushing on". It
  // would then report "not sensitive" — which reads like a broken test helper
  // or a broken CPU, not like state left over from the test above.
  audio_denormals_on();
  CHECK(makes_denormal(),
        "test is not sensitive: FLT_MIN*0.5 flushed even with the mode cleared");

  static AudioEngine eng;
  tracker_init(&song_a);
  song_a.song_len = 1;
  song_a.loop = false;
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  audio_init(&eng, &song_a);

  static float buf[AUDIO_BLOCK_SIZE * 2];
  audio_fill_buffer(&eng, buf, AUDIO_BLOCK_SIZE);

  CHECK(!makes_denormal(),
        "audio_fill_buffer() left denormals enabled — the audio thread will hit "
        "the x86 denormal penalty (see src/denormal.h)");

  audio_shutdown(&eng);
#endif
}

// ARP and MICRO are note modifiers: they sit ahead of the source and rewrite
// the note stream for the rest of the chain, so the source only ever hears
// notes the modifier emits. Verified end-to-end through the real engine.
#define NM_LINES 9
#define NM_SPL 6000  // one pattern line at 120 BPM, 48kHz

// Configure song as a single-lane, single-track one-note pattern (note 60 held
// for 8 lines), then render NM_LINES lines and count distinct amplitude bursts
// — each burst is one note attack that decayed back to silence.
static void nm_configure(TrackerSong* song, uint8_t vel) {
  tracker_init(song);
  song->bpm = 120;
  song->song_len = 1;
  song->loop = false;
  song->patterns[0][0] = 0;
  Pattern* pat = tracker_pattern(song, 0);
  pat->len = 16;
  pat->steps[0][0] = (PatternStep){.note = 60, .velocity = vel, .instrument = 0,
                                   .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  pat->steps[0][8] = (PatternStep){.note = NOTE_OFF, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
}

static int nm_count_bursts(TrackerSong* song) {
  static AudioEngine eng;
  audio_init(&eng, song);
  audio_play(&eng);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  int bursts = 0;
  // A note counts when sound returns after a real gap. A plain silent-sample
  // count would miscount every zero crossing of a tone.
  uint32_t quiet = UINT32_MAX;
  for (uint32_t done = 0; done < NM_LINES * NM_SPL; done += BLK) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK * 2; i++) {
      if (fabsf(buf[i]) > 1e-3f) {
        if (quiet >= 256)
          bursts++;
        quiet = 0;
      } else if (quiet < UINT32_MAX) {
        quiet++;
      }
    }
  }
  audio_shutdown(&eng);
  return bursts;
}

// Frequency proxy for a sustained tone: sign changes per second, sampled after
// the attack has settled. For a sine this is ~2x the pitch in Hz.
static double nm_zero_cross_rate(TrackerSong* song) {
  static AudioEngine eng;
  audio_init(&eng, song);
  audio_play(&eng);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  const uint32_t skip = AUDIO_SAMPLE_RATE / 3, want = AUDIO_SAMPLE_RATE;
  int crossings = 0;
  float prev = 0;
  bool have = false;
  for (uint32_t done = 0; done < skip + want; done += BLK) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK; i++) {
      uint32_t t = done + (uint32_t)i;
      float v = buf[i * 2];
      if (t >= skip) {
        if (have && ((v >= 0.0f) != (prev >= 0.0f)))
          crossings++;
        have = true;
      }
      prev = v;
    }
  }
  audio_shutdown(&eng);
  return (double)crossings;
}

// Hold one live-MIDI note (no transport running) and render: counts amplitude
// bursts and total energy. Live input and preview share the note pipeline with
// pattern playback, so this exercises the modifiers on that path.
static void nm_midi_run(TrackerSong* song, uint8_t inst, uint8_t note,
                        int* bursts, double* energy) {
  static AudioEngine eng;
  audio_init(&eng, song);
  audio_midi_note_on(&eng, inst, note);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  *bursts = 0;
  *energy = 0;
  uint32_t quiet = UINT32_MAX;
  for (uint32_t done = 0; done < NM_LINES * NM_SPL; done += BLK) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK * 2; i++) {
      if (fabsf(buf[i]) > 1e-3f) {
        if (quiet >= 256)
          (*bursts)++;
        quiet = 0;
      } else if (quiet < UINT32_MAX) {
        quiet++;
      }
      *energy += (double)buf[i] * buf[i];
    }
  }
  audio_midi_kill_all(&eng);
  audio_shutdown(&eng);
}

// Energy in the last `tail` samples of NM_LINES lines (used to check a note
// really stopped, not just decayed).
static double nm_tail_energy(TrackerSong* song, uint32_t tail) {
  static AudioEngine eng;
  audio_init(&eng, song);
  audio_play(&eng);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  const uint32_t total = NM_LINES * NM_SPL;
  double e = 0;
  for (uint32_t done = 0; done < total; done += BLK) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK; i++) {
      uint32_t t = done + (uint32_t)i;
      if (t >= total - tail)
        e += (double)buf[i * 2] * buf[i * 2];
    }
  }
  audio_shutdown(&eng);
  return e;
}

// Total rendered energy over NM_LINES lines.
static double nm_render_energy(TrackerSong* song) {
  static AudioEngine eng;
  audio_init(&eng, song);
  audio_play(&eng);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  double e = 0;
  for (uint32_t done = 0; done < NM_LINES * NM_SPL; done += BLK) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK * 2; i++)
      e += (double)buf[i] * buf[i];
  }
  audio_shutdown(&eng);
  return e;
}

static void test_note_modifiers(void) {
  // A plain plucky osc, so each note is one burst that decays before the next.
  uint8_t pluck[UNIT_MAX_PARAMS] = {0, 0, 0x10, 0, 0, 0x80, 0x80, 0xFF};

  // --- ARP: one held note becomes one note per 1/16 (one pattern line) ---
  nm_configure(&song_a, 100);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "arp", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
  memcpy(song_a.instruments[0].chain[1].params, pluck, UNIT_MAX_PARAMS);
  song_a.instruments[0].chain[0].params[0] = 1;     // ON
  song_a.instruments[0].chain[0].params[1] = 4;     // RATE = 1/16
  song_a.instruments[0].chain[0].params[2] = 0;     // MODE = UP
  song_a.instruments[0].chain[0].params[3] = 0x20;  // GATE ~17%
  song_a.instruments[0].chain[0].params[4] = 0;     // OCT = 1
  int arp_bursts = nm_count_bursts(&song_a);

  // The same note without the arp: a single burst.
  nm_configure(&song_a, 100);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  memcpy(song_a.instruments[0].chain[0].params, pluck, UNIT_MAX_PARAMS);
  int plain_bursts = nm_count_bursts(&song_a);

  CHECK(arp_bursts >= 7 && arp_bursts <= 9,
        "arp: held note produced %d retriggers, want 8 (one per 1/16)", arp_bursts);
  CHECK(plain_bursts <= 2,
        "arp control: plain note produced %d bursts, want 1", plain_bursts);
  CHECK(arp_bursts > plain_bursts,
        "arp: %d bursts with arp vs %d without — notes are not being subdivided",
        arp_bursts, plain_bursts);

  // --- MICRO: a tracker note is a step of an N-per-octave scale, so its
  // range shrinks. Note 72 (C5 in 12-EDO) through a 24-step scale with the
  // C4 anchor is a tritone above it (F#4), not an octave. ---
  uint8_t sine[UNIT_MAX_PARAMS] = {0, 0, 0, 0xFF, 0, 0x80, 0x80, 0x30};
  double zcr[2];
  for (int mode = 0; mode < 2; mode++) {
    nm_configure(&song_a, 20);
    Pattern* pat = tracker_pattern(&song_a, 0);
    pat->steps[0][0].note = 72;
    pat->steps[0][8] = (PatternStep){.fx = {TRACKER_EMPTY, TRACKER_EMPTY}};  // hold it
    tracker_inst_set_slot(&song_a.instruments[0], 0, "micro", 0);
    tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
    memcpy(song_a.instruments[0].chain[1].params, sine, UNIT_MAX_PARAMS);
    song_a.instruments[0].chain[0].params[0] = 1;   // ON
    song_a.instruments[0].chain[0].params[1] = mode ? 4 : 0;  // 24 steps vs 12 (bypass)
    song_a.instruments[0].chain[0].params[2] = 60;  // ROOT = C4
    zcr[mode] = nm_zero_cross_rate(&song_a);
  }
  double ratio = zcr[1] / zcr[0];
  CHECK(fabs(ratio - 0.70711) < 0.03,
        "micro: note 72 at 24 steps/octave runs at %.3fx the 12-EDO frequency (%0.f vs "
        "%0.f Hz) — want 0.707, i.e. a tritone above the anchor, not an octave",
        ratio, zcr[1], zcr[0]);

  // --- CHORD: one note becomes a triad, so the source runs several voices ---
  uint8_t pad[UNIT_MAX_PARAMS] = {0, 0, 0, 0xFF, 0, 0x80, 0x80, 0x20};
  nm_configure(&song_a, 20);
  Pattern* cpat = tracker_pattern(&song_a, 0);
  cpat->steps[0][8] = (PatternStep){.fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  tracker_inst_set_slot(&song_a.instruments[0], 0, "chord", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
  memcpy(song_a.instruments[0].chain[1].params, pad, UNIT_MAX_PARAMS);
  song_a.instruments[0].chain[0].params[0] = 1;  // ON
  song_a.instruments[0].chain[0].params[1] = 0;  // MAJ
  double chord_e = nm_render_energy(&song_a);

  nm_configure(&song_a, 20);
  cpat = tracker_pattern(&song_a, 0);
  cpat->steps[0][8] = (PatternStep){.fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  memcpy(song_a.instruments[0].chain[0].params, pad, UNIT_MAX_PARAMS);
  double single_e = nm_render_energy(&song_a);

  CHECK(chord_e > single_e * 2.0,
        "chord: triad energy %.6g vs single note %.6g — the chord did not reach the "
        "source as separate voices",
        chord_e, single_e);

  // --- BEND: 00/80/FF are -1/0/+1 semitones, MIDI-wheel style ---
  uint8_t held[UNIT_MAX_PARAMS] = {0, 0, 0, 0xFF, 0, 0x80, 0x80, 0x30};
  const uint8_t bends[3] = {0x80, 0x00, 0xFF};  // centre, down a semitone, up
  double bz[3];
  for (int m = 0; m < 3; m++) {
    nm_configure(&song_a, 20);
    Pattern* bp = tracker_pattern(&song_a, 0);
    bp->steps[0][8] = (PatternStep){.fx = {TRACKER_EMPTY, TRACKER_EMPTY}};  // hold
    tracker_inst_set_slot(&song_a.instruments[0], 0, "bend", 0);
    tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
    memcpy(song_a.instruments[0].chain[1].params, held, UNIT_MAX_PARAMS);
    song_a.instruments[0].chain[0].params[0] = bends[m];
    bz[m] = nm_zero_cross_rate(&song_a);
  }
  CHECK(fabs(bz[1] / bz[0] - 0.94387) < 0.02,
        "bend: BEND=00 should be a semitone down (%.3fx centre)", bz[1] / bz[0]);
  CHECK(fabs(bz[2] / bz[0] - 1.05946) < 0.02,
        "bend: BEND=FF should be a semitone up (%.3fx centre)", bz[2] / bz[0]);

  // Moving BEND while a note is held must not strand it: the note-off has to
  // release the pitch it started on, not the one BEND points at now.
  nm_configure(&song_a, 20);
  Pattern* bp = tracker_pattern(&song_a, 0);
  bp->steps[0][0] = (PatternStep){.note = 60, .velocity = 20, .instrument = 0,
                                  .fx = {0, TRACKER_EMPTY}, .fxv = {0x00, 0}};  // bend down
  bp->steps[0][4] = (PatternStep){.note = NOTE_EMPTY, .instrument = 0,
                                  .fx = {0, TRACKER_EMPTY}, .fxv = {0xFF, 0}};  // bend up
  bp->steps[0][8] = (PatternStep){.note = NOTE_OFF, .fx = {TRACKER_EMPTY, TRACKER_EMPTY}};
  tracker_inst_set_slot(&song_a.instruments[0], 0, "bend", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
  memcpy(song_a.instruments[0].chain[1].params, held, UNIT_MAX_PARAMS);
  double tail = nm_tail_energy(&song_a, 4000);  // well after the note-off
  CHECK(tail < 0.05,
        "bend: note kept ringing after note-off (tail energy %.6g) — the release "
        "used the moved BEND instead of the one the note started with",
        tail);
}

// A key press on a MIDI keyboard runs through the same CHORD/ARP/MICRO note
// modifiers as pattern playback — checked here with the transport stopped,
// which is also how the UI preview behaves.
static void test_midi_note_modifiers(void) {
  uint8_t pluck[UNIT_MAX_PARAMS] = {0, 0, 0x10, 0, 0, 0x80, 0x80, 0xFF};
  int bursts;
  double energy;

  // One held key through ARP: retriggers on the arp grid.
  nm_configure(&song_a, 100);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "arp", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
  memcpy(song_a.instruments[0].chain[1].params, pluck, UNIT_MAX_PARAMS);
  song_a.instruments[0].chain[0].params[0] = 1;     // ON
  song_a.instruments[0].chain[0].params[1] = 4;     // RATE = 1/16
  song_a.instruments[0].chain[0].params[2] = 0;     // UP
  song_a.instruments[0].chain[0].params[3] = 0x20;  // GATE short
  song_a.instruments[0].chain[0].params[4] = 0;     // OCT 1
  nm_midi_run(&song_a, 0, 60, &bursts, &energy);
  int arp_bursts = bursts;

  // The same key with no arp: a single burst.
  nm_configure(&song_a, 100);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  memcpy(song_a.instruments[0].chain[0].params, pluck, UNIT_MAX_PARAMS);
  nm_midi_run(&song_a, 0, 60, &bursts, &energy);
  int plain_bursts = bursts;

  CHECK(arp_bursts >= 7 && arp_bursts <= 9,
        "midi arp: one held key produced %d retriggers, want 8 (one per 1/16)", arp_bursts);
  CHECK(plain_bursts <= 2,
        "midi arp control: %d bursts with no arp, want 1", plain_bursts);

  // One held key through CHORD: a triad, so more voices than a single note.
  uint8_t pad[UNIT_MAX_PARAMS] = {0, 0, 0, 0xFF, 0, 0x80, 0x80, 0x20};
  nm_configure(&song_a, 20);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "chord", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "osc", 0);
  memcpy(song_a.instruments[0].chain[1].params, pad, UNIT_MAX_PARAMS);
  song_a.instruments[0].chain[0].params[0] = 1;  // ON
  song_a.instruments[0].chain[0].params[1] = 0;  // MAJ
  nm_midi_run(&song_a, 0, 60, &bursts, &energy);
  double chord_e = energy;

  nm_configure(&song_a, 20);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "osc", 0);
  memcpy(song_a.instruments[0].chain[0].params, pad, UNIT_MAX_PARAMS);
  nm_midi_run(&song_a, 0, 60, &bursts, &energy);
  double single_e = energy;

  CHECK(chord_e > single_e * 2.0,
        "midi chord: triad energy %.6g vs single %.6g — CHORD not applied to MIDI input",
        chord_e, single_e);
}

// TURNTABLE drives the playhead from an internal audio-rate gesture. On a
// rising ramp the direction of travel is unambiguous, so we can tell a steady
// record from a scratched one and see the fader cutting the forward half.
#define TT_WIN 3000

static void tt_run(uint8_t depth, uint8_t cut, double* trend, int* downs, int* zeros) {
  static AudioEngine eng;
  nm_configure(&song_a, 100);
  tracker_inst_set_slot(&song_a.instruments[0], 0, "turntab", 0);
  ChainSlot* sl = &song_a.instruments[0].chain[0];
  snprintf(sl->data, sizeof(sl->data), "nm_ramp.wav");
  sl->params[0] = 0x00;   // LSTR
  sl->params[1] = 0xFF;   // LEND (whole sample)
  sl->params[2] = 0x80;   // TUNE = 0
  sl->params[3] = depth;  // DPTH
  sl->params[4] = 0xFF;   // RATE = 16 Hz
  sl->params[5] = 0;      // SHPE = SINE
  sl->params[6] = cut;    // CUT
  sl->params[7] = 0xFF;   // VOL

  audio_init(&eng, &song_a);
  audio_play(&eng);
  enum { BLK = 512 };
  static float buf[BLK * 2];
  float first[TT_WIN];
  int got = 0;
  while (got < TT_WIN) {
    audio_fill_buffer(&eng, buf, BLK);
    for (int i = 0; i < BLK && got < TT_WIN; i++)
      first[got++] = buf[i * 2];
  }
  audio_shutdown(&eng);

  double head = 0, tail = 0;
  int h = TT_WIN / 3;
  for (int i = 0; i < h; i++) {
    head += first[i];
    tail += first[TT_WIN - 1 - i];
  }
  *trend = (tail - head) / h;

  // Direction of travel, measured on 64-sample window means: a backwards
  // stretch shows up as a run of windows whose mean falls. (Per-sample diffs
  // are too small on a ramp to threshold reliably.)
  enum { BW = 64 };
  const int nw = TT_WIN / BW;
  double prev = 0;
  int d = 0, z = 0;
  for (int wi = 0; wi < nw; wi++) {
    double mean = 0;
    for (int i = 0; i < BW; i++)
      mean += first[wi * BW + i];
    mean /= BW;
    if (wi > 0 && mean < prev - 0.005)
      d++;
    prev = mean;
  }
  for (int i = 0; i < TT_WIN; i++)
    if (fabsf(first[i]) < 1e-4f)
      z++;
  *downs = d;
  *zeros = z;
}

static void test_turntable_scratch(void) {
  enum { N = 4800 };
  static float ramp[N];
  for (int i = 0; i < N; i++)
    ramp[i] = -1.0f + 2.0f * (float)i / (float)(N - 1);
  Wave w = {0};
  w.frameCount = N;
  w.sampleRate = 48000;
  w.sampleSize = 32;
  w.channels = 1;
  w.data = ramp;
  CHECK(ExportWave(w, "nm_ramp.wav"), "turntable: couldn't write the test ramp");

  double trend_plain, trend_scratch, trend_cut;
  int downs_plain, downs_scratch, downs_cut;
  int zeros_plain, zeros_cut;
  tt_run(0x00, 0x00, &trend_plain, &downs_plain, &zeros_plain);
  tt_run(0xFF, 0x00, &trend_scratch, &downs_scratch, &zeros_cut);  // zeros unused here
  tt_run(0x00, 0xFF, &trend_cut, &downs_cut, &zeros_cut);
  remove("nm_ramp.wav");

  // DPTH=0: the record just plays, one direction.
  CHECK(trend_plain > 0.2, "turntable: DPTH=0 should play forward (trend %.3f)", trend_plain);
  CHECK(downs_plain <= 2,
        "turntable: DPTH=0 shouldn't travel backwards (%d backward windows)", downs_plain);

  // DPTH=FF: the gesture swings the platter back, so the window contains a
  // long backwards stretch (DPTH=0's window has none).
  CHECK(downs_scratch > 8,
        "turntable: DPTH=FF should scratch the playhead backwards (%d backward windows, "
        "was %d with DPTH=0)",
        downs_scratch, downs_plain);

  // CUT=FF with a steady platter: the fader silences the forward half.
  CHECK(zeros_plain < TT_WIN / 20,
        "turntable: CUT=0 should never gate (%d silent samples)", zeros_plain);
  CHECK(zeros_cut > TT_WIN / 3 && zeros_cut < TT_WIN * 3 / 4,
        "turntable: CUT=FF should silence about half the output (%d/%d samples)",
        zeros_cut, TT_WIN);
}

static void test_lfo_sync_is_position_locked(void) {
  const UnitDef* d = unit_find("lfo");
  CHECK(d != NULL, "lfo unit not registered");
  if (!d)
    return;

  // A real engine, so the LFO's audio_mod_set_param() lands somewhere readable:
  // instrument 0 is [filter, lfo], and the LFO drives the filter's CUTF
  // (global param index 1 — slot 0 owns 0..2).
  static AudioEngine eng;
  tracker_init(&song_a);
  song_a.song_len = 1;
  song_a.loop = false;
  tracker_inst_set_slot(&song_a.instruments[0], 0, "filter", 0);
  tracker_inst_set_slot(&song_a.instruments[0], 1, "lfo", 0);
  audio_init(&eng, &song_a);
  uint8_t* cutf = &song_a.instruments[0].chain[0].params[1];

  const uint32_t spl = 5512;  // one pattern line at 120 BPM, 44.1kHz
  g_unit_samples_per_line = spl;
  g_unit_render_samples = 0;

  uint8_t p[UNIT_MAX_PARAMS];
  memcpy(p, d->param_defaults, UNIT_MAX_PARAMS);
  p[1] = 2;     // SAW — monotonic within a cycle, so the period is easy to spot
  p[2] = 0;     // INST: target instrument 0
  p[3] = 1;     // PARAM: filter CUTF
  p[4] = 0x80;  // centre
  p[5] = 0xFF;  // full depth
  p[6] = 1;     // ON
  p[7] = 5;     // SYNC = 1/4 (one cycle per beat = 4 lines)

  enum { BLK = 512 };
  static float l[BLK], r[BLK];
  memset(l, 0, sizeof(l));
  memset(r, 0, sizeof(r));

  // "Track A" has been playing a while when "track B" starts.
  UnitState* a = d->create(44100.0f);
  CHECK(a != NULL, "lfo: create failed");
  if (!a)
    return;
  for (int b = 0; b < 37; b++) {
    d->render(a, p, l, r, l, r, BLK);
    g_unit_render_samples += BLK;
  }
  UnitState* late = d->create(44100.0f);
  CHECK(late != NULL, "lfo: create failed");
  if (!late) {
    d->destroy(a);
    return;
  }

  int disagree = 0;
  for (int b = 0; b < 16; b++) {
    d->render(a, p, l, r, l, r, BLK);
    uint8_t va = *cutf;
    d->render(late, p, l, r, l, r, BLK);
    uint8_t vb = *cutf;
    if (va != vb)
      disagree++;
    g_unit_render_samples += BLK;
  }
  CHECK(disagree == 0,
        "lfo: a copy created mid-song disagrees with its sibling in %d/16 blocks "
        "— synced phase is not position-locked",
        disagree);

  // One cycle per beat: sampling a saw at the start of successive beats must
  // give the same value every time, and NOT the same value half a beat later.
  const uint64_t beat = (uint64_t)spl * 4;
  uint8_t on_beat[4], off_beat;
  for (int i = 0; i < 4; i++) {
    g_unit_render_samples = beat * (uint64_t)(i + 3);
    d->render(a, p, l, r, l, r, 2);  // tiny block: phase ~ exactly at the beat
    on_beat[i] = *cutf;
  }
  g_unit_render_samples = beat * 3 + beat / 2;
  d->render(a, p, l, r, l, r, 2);
  off_beat = *cutf;
  CHECK(on_beat[0] == on_beat[1] && on_beat[1] == on_beat[2] && on_beat[2] == on_beat[3],
        "lfo: SYNC=1/4 not repeating once per beat (%02X %02X %02X %02X)",
        on_beat[0], on_beat[1], on_beat[2], on_beat[3]);
  CHECK(on_beat[0] != off_beat,
        "lfo: value is constant across the beat — not actually modulating");

  d->destroy(a);
  d->destroy(late);
  audio_shutdown(&eng);
  g_unit_samples_per_line = 0;
  g_unit_render_samples = 0;
}

static void test_chopper_repeats_at_tempo_derived_length(void) {
  const UnitDef* d = unit_find("chopper");
  CHECK(d != NULL, "chopper unit not registered");
  if (!d)
    return;

  const uint32_t spl = 5512;  // one pattern line at 120 BPM, 44.1kHz
  g_unit_samples_per_line = spl;
  UnitState* st = d->create(44100.0f);
  CHECK(st != NULL, "chopper: create failed");
  if (!st)
    return;

  uint8_t p[UNIT_MAX_PARAMS];
  memcpy(p, d->param_defaults, UNIT_MAX_PARAMS);  // ON=0 MODE=EVEN SIZE=1/8 SYNC=BEAT

  enum { BLK = 512 };
  static float in_l[BLK], in_r[BLK], out_l[BLK], out_r[BLK];

  // Prime the capture buffer with a ramp that never repeats within a slice,
  // so periodicity in the output can only come from looping (not from the
  // input happening to be periodic). Bypassed, so this also checks ON=0.
  long t = 0;
  bool passthrough_ok = true;
  for (int b = 0; b < 80; b++) {
    for (int i = 0; i < BLK; i++)
      in_l[i] = in_r[i] = (float)((t++ % 40009)) * 0.00001f;
    d->render(st, p, in_l, in_r, out_l, out_r, BLK);
    for (int i = 0; i < BLK; i++)
      if (out_l[i] != in_l[i])
        passthrough_ok = false;
  }
  CHECK(passthrough_ok, "chopper: ON=0 must pass audio through untouched");

  // Engage, feeding silence: anything that comes out now must be the
  // captured slice replaying.
  p[0] = 1;
  const int expect = (int)(spl * 16 / 8);
  static float cap[40000];
  int n = 0;
  while (n + BLK <= (int)(sizeof(cap) / sizeof(cap[0])) && n < 3 * expect) {
    memset(in_l, 0, sizeof(in_l));
    memset(in_r, 0, sizeof(in_r));
    d->render(st, p, in_l, in_r, out_l, out_r, BLK);
    for (int i = 0; i < BLK; i++)
      cap[n++] = out_l[i];
  }

  double energy = 0;
  for (int i = 0; i < n; i++)
    energy += cap[i] * cap[i];
  CHECK(energy > 1e-6, "chopper: engaged output is silent — slice never captured");

  int mismatches = 0;
  for (int i = 0; i + expect < n; i++)
    if (cap[i] != cap[i + expect])
      mismatches++;
  CHECK(mismatches == 0,
        "chopper: output not periodic at %d samples (1/8 @120BPM) — %d/%d samples differ",
        expect, mismatches, n - expect);

  // A wrong-but-plausible period must NOT also match, otherwise the check
  // above would pass for a stuck/DC output.
  int off_by_matches = 0;
  for (int i = 0; i + expect / 2 < n; i++)
    if (cap[i] == cap[i + expect / 2])
      off_by_matches++;
  CHECK(off_by_matches < (n - expect / 2),
        "chopper: output is periodic at half the expected length too — looks constant, not chopped");

  // SYNC=LINE subdivides one line instead of a whole note, so the same
  // divisor must give a 16x shorter slice.
  d->kill(st);
  p[3] = 1;
  for (int b = 0; b < 80; b++) {
    for (int i = 0; i < BLK; i++)
      in_l[i] = in_r[i] = (float)((t++ % 40009)) * 0.00001f;
    uint8_t off[UNIT_MAX_PARAMS];
    memcpy(off, p, sizeof(off));
    off[0] = 0;
    d->render(st, off, in_l, in_r, out_l, out_r, BLK);
  }
  const int expect_line = (int)(spl / 8);
  n = 0;
  while (n + BLK <= (int)(sizeof(cap) / sizeof(cap[0])) && n < 3 * expect_line) {
    memset(in_l, 0, sizeof(in_l));
    memset(in_r, 0, sizeof(in_r));
    d->render(st, p, in_l, in_r, out_l, out_r, BLK);
    for (int i = 0; i < BLK; i++)
      cap[n++] = out_l[i];
  }
  int line_mismatches = 0;
  for (int i = 0; i + expect_line < n; i++)
    if (cap[i] != cap[i + expect_line])
      line_mismatches++;
  CHECK(line_mismatches == 0,
        "chopper: SYNC=LINE not periodic at %d samples — %d differ", expect_line, line_mismatches);

  d->destroy(st);
  g_unit_samples_per_line = 0;
}

// --- Dexed (plugins/dexed: dexed's own engine compiled to WCLAP) -----------
//
// The tests below are the load-bearing ones for that plugin: its preset table
// is compiled in rather than read from disk, so "the table decoded, the
// voices got unpacked, and the engine is actually driven by them" is the
// property worth pinning, along with the param surface poketrack's ADD row
// talks to.

static const char* dexed_wasm_path = "../examples/plugins/dexed.wclap.wasm";

// Finds a param by name; returns false when the plugin doesn't declare it.
static bool dexed_find_param(ClapPlugin* p, const char* want, uint32_t* out_id, double* out_min, double* out_max, bool* out_stepped, bool* out_enum) {
  uint32_t total = clap_host_param_count(p);
  char name[24];
  for (uint32_t i = 0; i < total; i++) {
    double min = 0, max = 0, def = 0;
    if (!clap_host_param_info(p, i, out_id, name, sizeof(name), &min, &max, &def))
      continue;
    if (strcmp(name, want) != 0)
      continue;
    if (out_min)
      *out_min = min;
    if (out_max)
      *out_max = max;
    if (out_stepped)
      *out_stepped = clap_host_param_is_stepped(p, i);
    if (out_enum)
      *out_enum = clap_host_param_is_enum(p, i);
    return true;
  }
  return false;
}

// Renders `blocks` blocks after a note-on and reports total energy, draining
// the voice afterwards so the next case starts clean.
static float dexed_render_note(ClapPlugin* p, uint8_t key, int blocks) {
  static float out_l[512], out_r[512];
  clap_host_note_on(p, key, 100, 0);
  float energy = 0.0f;
  for (int b = 0; b < blocks; b++) {
    clap_host_process(p, NULL, NULL, out_l, out_r, 512);
    for (int f = 0; f < 512; f++)
      energy += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  clap_host_note_off(p, key, 0);
  for (int b = 0; b < 96; b++)  // let the release tail clear (~1.1s)
    clap_host_process(p, NULL, NULL, out_l, out_r, 512);
  return energy;
}

// Loads dexed, checks its param surface, and checks that the bundled presets
// actually drive the engine: distinct programs must produce distinct audio,
// and a released note must decay to silence rather than hang.
static void test_clap_plugin_dexed(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_plugin_dexed: %s not built (see plugins/dexed/README.md)\n", dexed_wasm_path);
    return;
  }

  ClapPlugin* p = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "dexed.wclap.wasm: load failed");
  if (!p)
    return;

  CHECK(clap_host_is_instrument(p), "dexed.wclap.wasm: expected an instrument");

  // 156 params from dexed's own Ctrl list (24 globals + 6 operators x 22),
  // plus Cartridge/Program/Engine.
  uint32_t total = clap_host_param_count(p);
  CHECK(total == 159, "dexed.wclap.wasm: expected 159 params, got %u", total);

  uint32_t cart_id = 0, prog_id = 0, algo_id = 0, out_id = 0;
  double min = 0, max = 0;
  bool stepped = false, is_enum = false;
  CHECK(dexed_find_param(p, "ALGORITHM", &algo_id, &min, &max, &stepped, &is_enum), "dexed: no ALGORITHM param");
  CHECK(min == 1 && max == 32, "dexed: ALGORITHM range %g-%g, want 1-32 (the DX7's own range, not a 0-31 byte)", min, max);
  CHECK(dexed_find_param(p, "Cartridge", &cart_id, &min, &max, &stepped, &is_enum), "dexed: no Cartridge param");
  CHECK(max == 33 - 1, "dexed: Cartridge max %g, want 32 (33 bundled banks)", max);
  CHECK(is_enum && stepped, "dexed: Cartridge should be a stepped enum (one byte bump = one bank)");
  CHECK(dexed_find_param(p, "Program", &prog_id, &min, &max, &stepped, &is_enum), "dexed: no Program param");
  CHECK(max == 31, "dexed: Program max %g, want 31 (32 voices per DX7 cartridge)", max);
  CHECK(is_enum && stepped, "dexed: Program should be a stepped enum (one byte bump = one voice)");
  CHECK(dexed_find_param(p, "Output", &out_id, &min, &max, &stepped, &is_enum), "dexed: no Output param");
  CHECK(!stepped, "dexed: Output is a continuous gain, not stepped");

  // Program 0 of cartridge 0 vs a program 20 voices later: the DX7's own
  // ROM banks are full of very different sounds, so these must not come out
  // the same — which is what a preset table wired to nothing would do.
  float e0 = dexed_render_note(p, 60, 8);
  clap_host_queue_param(p, prog_id, 20);
  float e20 = dexed_render_note(p, 60, 8);
  CHECK(e0 > 1e-4f, "dexed.wclap.wasm: silent on the startup program");
  CHECK(e20 > 1e-4f, "dexed.wclap.wasm: silent on program 20 of cartridge 0");
  CHECK(fabsf(e20 - e0) > 1e-4f, "dexed.wclap.wasm: program 0 and 20 rendered identically (%g vs %g) — preset table not wired up", e0, e20);

  clap_host_unload(p);

  // Note-off must reach silence. Fresh instance so the tail from the
  // comparison above can't bleed in.
  ClapPlugin* p2 = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(p2 != NULL, "dexed.wclap.wasm: reload failed");
  if (!p2)
    return;
  static float out_l[512], out_r[512];
  clap_host_note_on(p2, 60, 100, 0);
  for (int b = 0; b < 8; b++)
    clap_host_process(p2, NULL, NULL, out_l, out_r, 512);
  clap_host_note_off(p2, 60, 0);
  // The DX7's longest factory release is ~8s of slow decay; 8s of blocks is
  // far past anything a DX7 voice can hold after key-up.
  for (int b = 0; b < 700; b++)
    clap_host_process(p2, NULL, NULL, out_l, out_r, 512);
  float tail = 0.0f;
  for (int b = 0; b < 4; b++) {
    clap_host_process(p2, NULL, NULL, out_l, out_r, 512);
    for (int f = 0; f < 512; f++)
      tail += out_l[f] * out_l[f] + out_r[f] * out_r[f];
  }
  CHECK(tail < 1e-4f, "dexed.wclap.wasm: still loud (%g) 8s after note-off — envelope/voice not releasing", tail);

  clap_host_unload(p2);
}

// Every bundled cartridge must load: the banks are compiled in as raw 4104-
// byte SysEx and unpacked at runtime by dexed's own parser, so a bank that
// decoded wrong (bad checksum path, wrong voice count) shows up here as a
// silent or missing cartridge.
static void test_clap_plugin_dexed_all_cartridges(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_plugin_dexed_all_cartridges: %s not built\n", dexed_wasm_path);
    return;
  }

  ClapPlugin* p = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(p != NULL, "dexed.wclap.wasm: load failed");
  if (!p)
    return;

  uint32_t cart_id = 0, prog_id = 0;
  if (!dexed_find_param(p, "Cartridge", &cart_id, NULL, NULL, NULL, NULL) || !dexed_find_param(p, "Program", &prog_id, NULL, NULL, NULL, NULL)) {
    CHECK(0, "dexed: Cartridge/Program params missing");
    clap_host_unload(p);
    return;
  }

  float energies[33];
  int silent = 0;
  for (int cart = 0; cart < 33; cart++) {
    clap_host_queue_param(p, prog_id, 0);
    clap_host_queue_param(p, cart_id, cart);
    energies[cart] = dexed_render_note(p, 60, 6);
    if (!(energies[cart] > 1e-4f)) {
      printf("  dexed: cartridge %d is silent (energy %g)\n", cart, energies[cart]);
      silent++;
    }
  }
  CHECK(silent == 0, "dexed.wclap.wasm: %d/33 bundled cartridges produced no sound — preset table/parser broke", silent);

  // ...and they must not all be the same bank copied 33 times.
  float lo = energies[0], hi = energies[0];
  for (int i = 1; i < 33; i++) {
    if (energies[i] < lo)
      lo = energies[i];
    if (energies[i] > hi)
      hi = energies[i];
  }
  CHECK(hi > lo * 1.5f, "dexed.wclap.wasm: all 33 cartridges rendered near-identical energy (%.5f..%.5f) — every bank may be loading the same data", lo, hi);

  clap_host_unload(p);
}

// The poketrack-side behaviour of the two preset params, through clap_unit's
// ADD row: one byte bump is exactly one program, the plugin's own value_to_text
// names it, and Program is reported as a CLAP enum so the UI shows the name
// instead of drawing a slider over it.
static void test_clap_dexed_program_param_stepping(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_dexed_program_param_stepping: %s not built\n", dexed_wasm_path);
    return;
  }

  const UnitDef* def = unit_find("clap");
  CHECK(def != NULL, "clap unit not registered");
  if (!def)
    return;

  UnitState* s = def->create(44100.0f);
  CHECK(s != NULL, "clap_unit_create failed");
  if (!s)
    return;
  char data[640];
  snprintf(data, sizeof(data), "%s\t\t", dexed_wasm_path);
  def->set_data(s, data, "./");

  CHECK(def->picker_count(s) == 159, "dexed exposes %d params via picker, want 159", def->picker_count(s));

  int cart_idx = -1, prog_idx = -1, algo_idx = -1;
  for (int i = 0; i < def->picker_count(s); i++) {
    const char* n = def->picker_name(s, i);
    if (n && strcmp(n, "Cartridge") == 0)
      cart_idx = i;
    else if (n && strcmp(n, "Program") == 0)
      prog_idx = i;
    else if (n && strcmp(n, "ALGORITHM") == 0)
      algo_idx = i;
  }
  CHECK(prog_idx >= 0 && cart_idx >= 0 && algo_idx >= 0, "dexed: picker is missing Cartridge/Program/ALGORITHM");

  def->picker_add(s, prog_idx);
  CHECK(def->dyn_num_params(s) == 1, "picker_add didn't map Program");

  // Program is 0-31 across a 0-255 ADD-row byte: byte N is program N, and
  // bytes past the last program hold there rather than wrapping or spreading.
  char prev[64] = {0};
  for (int b = 0; b <= 31; b++) {
    const char* now = def->format_param_val(s, 0, (uint8_t)b);
    CHECK(now != NULL && now[0], "dexed: no name for program byte %d", b);
    if (now) {
      CHECK(strcmp(now, prev) != 0, "dexed: program byte %d..%d gave the same name (\"%s\")", b - 1, b, now);
      snprintf(prev, sizeof(prev), "%s", now);
    }
  }
  const char* last = def->format_param_val(s, 0, 31);
  char last_name[64];
  snprintf(last_name, sizeof(last_name), "%s", last ? last : "");
  const char* wrapped = def->format_param_val(s, 0, 255);
  CHECK(wrapped && strcmp(wrapped, last_name) == 0, "dexed: byte 255 gave \"%s\", want program 31 (\"%s\") — bytes past the last program must clamp", wrapped ? wrapped : "(null)", last_name);

  CHECK(def->dyn_param_is_enum != NULL, "clap unit doesn't implement dyn_param_is_enum");
  if (def->dyn_param_is_enum)
    CHECK(def->dyn_param_is_enum(s, 0), "Program not reported as enum — its name would be hidden behind the ADD row's slider bar");

  def->destroy(s);
}

// A param edited while a note is held must reach that note immediately —
// dexed's processBlock re-inits every live voice when a DX param changes
// (its `refreshVoice` path), which is what makes parameter automation audible
// in a tracker instead of only from the next note-on.
//
// Compared against a control instance left alone for the same number of
// blocks, so the DX7 envelope's own evolution cancels out.
static void test_clap_plugin_dexed_param_change_reaches_held_note(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_plugin_dexed_param_change_reaches_held_note: %s not built\n", dexed_wasm_path);
    return;
  }

  static float out_l[512], out_r[512];
  uint32_t algo_id = 0;

  float energy[2];
  for (int variant = 0; variant < 2; variant++) {
    ClapPlugin* p = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
    CHECK(p != NULL, "dexed.wclap.wasm: load failed");
    if (!p)
      return;
    CHECK(dexed_find_param(p, "ALGORITHM", &algo_id, NULL, NULL, NULL, NULL), "dexed: no ALGORITHM param");

    clap_host_note_on(p, 60, 100, 0);
    for (int b = 0; b < 4; b++)
      clap_host_process(p, NULL, NULL, out_l, out_r, 512);
    if (variant == 1)
      clap_host_queue_param(p, algo_id, 5);  // a different algorithm, same voice
    float e = 0.0f;
    for (int b = 0; b < 4; b++) {
      clap_host_process(p, NULL, NULL, out_l, out_r, 512);
      for (int f = 0; f < 512; f++) {
        CHECK(isfinite(out_l[f]) && isfinite(out_r[f]), "dexed: non-finite output after a mid-note ALGORITHM change");
        e += out_l[f] * out_l[f] + out_r[f] * out_r[f];
      }
    }
    energy[variant] = e;
    clap_host_unload(p);
  }

  CHECK(energy[0] > 1e-4f, "dexed: silent on a held note");
  CHECK(fabsf(energy[1] - energy[0]) > energy[0] * 0.05f,
        "dexed: changing ALGORITHM mid-note left the audio essentially unchanged (%g vs %g) — live voices aren't being refreshed on param change", energy[1],
        energy[0]);
}

// Host block size must not change the sound: dexed renders in fixed 64-sample
// quanta and carries the remainder in `extra_buf`, so any split of the same
// frames has to come out sample-identical. This pins the port's quantum
// buffering (a wrong remainder offset or a lost quantum still "makes sound",
// which the other tests wouldn't notice).
static void test_clap_plugin_dexed_block_size_independent(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_plugin_dexed_block_size_independent: %s not built\n", dexed_wasm_path);
    return;
  }

  static float whole[512], split_l[512], split_r[512];
  int bad = 0;

  ClapPlugin* a = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(a != NULL, "dexed.wclap.wasm: load failed");
  if (!a)
    return;
  clap_host_note_on(a, 60, 100, 0);
  clap_host_process(a, NULL, NULL, whole, split_r, 512);
  clap_host_unload(a);

  // 2 x 256
  ClapPlugin* b = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(b != NULL, "dexed.wclap.wasm: load failed");
  if (!b) {
    return;
  }
  clap_host_note_on(b, 60, 100, 0);
  clap_host_process(b, NULL, NULL, split_l, split_r, 256);
  clap_host_process(b, NULL, NULL, split_l + 256, split_r + 256, 256);
  clap_host_unload(b);
  for (int f = 0; f < 512; f++)
    if (fabsf(split_l[f] - whole[f]) > 1e-9f)
      bad++;

  // 5 x 100 — a size that isn't a multiple of the engine's 64-sample quantum,
  // so this is the case that actually exercises the carry-over buffer.
  ClapPlugin* c = clap_host_load(dexed_wasm_path, NULL, 44100.0f, 512);
  CHECK(c != NULL, "dexed.wclap.wasm: load failed");
  if (!c) {
    return;
  }
  clap_host_note_on(c, 60, 100, 0);
  for (int i = 0; i < 5; i++)
    clap_host_process(c, NULL, NULL, split_l + i * 100, split_r, 100);
  clap_host_unload(c);
  for (int f = 0; f < 500; f++)
    if (fabsf(split_l[f] - whole[f]) > 1e-9f)
      bad++;

  CHECK(bad == 0, "dexed.wclap.wasm: %d samples differ between one 512-frame block and the same frames split into 256/100-frame blocks — quantum buffering is off", bad);
}

// End-to-end: dexed in a real poketrack instrument, played by the real
// playback path (audio_midi_note_on → the shared instance), with a preset
// param statically ADD-mapped — the flow screen_instrument.c drives. A plugin
// can pass every host-level test above and still be silent here if the
// mapping never reaches the playing instance or the voice doesn't survive
// poketrack's note handling.
static void test_clap_dexed_static_mapping_reaches_shared_instance(void) {
  if (!FileExists(dexed_wasm_path)) {
    printf("SKIP test_clap_dexed_static_mapping_reaches_shared_instance: %s not built\n", dexed_wasm_path);
    return;
  }

  static AudioEngine eng;
  tracker_init(&song_a);
  ChainSlot* sl = &song_a.instruments[0].chain[0];
  tracker_inst_set_slot(&song_a.instruments[0], 0, "clap", 0);
  strncpy(sl->data, dexed_wasm_path, sizeof(sl->data) - 1);
  audio_init(&eng, &song_a);

  const UnitDef* def = unit_find("clap");
  CHECK(def != NULL, "clap unit not registered");
  if (!def) {
    audio_shutdown(&eng);
    return;
  }

  audio_ensure_preview(&eng, 0);
  UnitState* preview = eng.preview_states[0];
  CHECK(preview != NULL, "preview_states[0] not created");
  if (!preview) {
    audio_shutdown(&eng);
    return;
  }

  int prog_idx = -1;
  for (int i = 0; i < def->picker_count(preview); i++) {
    const char* n = def->picker_name(preview, i);
    if (n && strcmp(n, "Program") == 0)
      prog_idx = i;
  }
  CHECK(prog_idx >= 0, "dexed: picker has no Program param");
  if (prog_idx < 0) {
    audio_shutdown(&eng);
    return;
  }

  def->picker_add(preview, prog_idx);
  CHECK(def->dyn_num_params(preview) == 1, "picker_add didn't add a mapping");
  def->set_param_val(preview, 0, 20);  // byte 20 = program 20 (stepped param: byte == step)
  def->sync_to_data(preview, sl->data, sizeof(sl->data));
  audio_rebuild_instrument(&eng, 0);

  enum { BLK = 512, BLOCKS = 8 };
  static float blk[BLK * 2];

  audio_preview_note(&eng, 0, 60);
  double preview_energy = 0;
  for (int b = 0; b < BLOCKS; b++) {
    audio_fill_buffer(&eng, blk, BLK);
    for (int f = 0; f < BLK * 2; f++)
      preview_energy += (double)blk[f] * blk[f];
  }
  audio_preview_kill(&eng);
  for (int b = 0; b < 96; b++)  // let the DX7 release tail clear
    audio_fill_buffer(&eng, blk, BLK);
  CHECK(preview_energy > 1e-4, "dexed: preview silent with Program statically mapped to 20 (energy=%g)", preview_energy);

  audio_midi_note_on(&eng, 0, 60);
  UnitState* shared = eng.shared_states[0][0];
  CHECK(shared != NULL, "shared_states[0][0] not created by note-on");
  if (shared)
    CHECK(def->dyn_num_params(shared) == 1 && def->get_param_val(shared, 0) == 20,
          "shared instance's Program mapping is %d params / val %d, want 1 / 20 — mapping never reached the playing instance",
          def->dyn_num_params(shared), def->dyn_num_params(shared) > 0 ? def->get_param_val(shared, 0) : -1);
  double shared_energy = 0;
  for (int b = 0; b < BLOCKS; b++) {
    audio_fill_buffer(&eng, blk, BLK);
    for (int f = 0; f < BLK * 2; f++)
      shared_energy += (double)blk[f] * blk[f];
  }
  CHECK(shared_energy > 1e-4, "dexed: SILENT via real playback (audio_midi_note_on) with Program mapped to 20 (energy=%g), even though preview played fine (energy=%g)",
        shared_energy, preview_energy);

  audio_midi_note_off(&eng, 0, 60);
  audio_shutdown(&eng);
}

// Prints before each test runs, so a hard crash (which loses any buffered
// stdout) still tells you which test it died in from the last line printed.
#define RUN(fn)                \
  do {                         \
    printf("-- " #fn "\n");    \
    fn();                      \
  } while (0)

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);  // unbuffered: a crash must not eat RUN()'s output
  SetTraceLogLevel(LOG_ERROR);       // silence raylib INFO spam
  unit_dsp_init();

  RUN(test_registry);
  RUN(test_song_roundtrip);
  RUN(test_instrument_roundtrip);
  RUN(test_recursive_find);
  RUN(test_wav_export);
  RUN(test_render_smoke);
  RUN(test_eq_unit);
  RUN(test_eq_in_chain);
  RUN(test_chopper_repeats_at_tempo_derived_length);
  RUN(test_audio_callback_flushes_denormals);
  RUN(test_idle_track_gate_wakes_on_note);
  RUN(test_note_modifiers);
  RUN(test_midi_note_modifiers);
  RUN(test_turntable_scratch);
  RUN(test_lfo_sync_is_position_locked);
  RUN(test_route_send_bus);
  RUN(test_clap_plugin_pd);
  RUN(test_clap_plugin_juno1);
  RUN(test_clap_plugin_juno1_full_velocity_note);
  RUN(test_clap_juno1_static_mapping_reaches_shared_instance);
  RUN(test_clap_format_param_val_uses_value_to_text);
  RUN(test_multiple_wclap_teardown_does_not_dangle);
  RUN(test_clap_plugin_pd_default_params_are_audible);
  RUN(test_clap_param_mapping_reaches_shared_instance);
  RUN(test_clap_plugin_pd_supersaw_polyphony);
  RUN(test_clap_plugin_dexed);
  RUN(test_clap_plugin_dexed_all_cartridges);
  RUN(test_clap_dexed_program_param_stepping);
  RUN(test_clap_plugin_dexed_param_change_reaches_held_note);
  RUN(test_clap_plugin_dexed_block_size_independent);
  RUN(test_clap_dexed_static_mapping_reaches_shared_instance);

  if (fails) {
    printf("%d FAILURE(S)\n", fails);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
