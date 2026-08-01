// Quick sanity tests — run via `make test`.
// Not a full suite: catches glaring breakage cheaply. Covers the unit
// registry, song/instrument file round-trips, WAV export, the recursive file
// scan the SFZ zip loader depends on, and a render smoke test (every unit
// renders finite audio; synth sources actually make sound).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "clap_host.h"
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

int main(void) {
  SetTraceLogLevel(LOG_ERROR);  // silence raylib INFO spam
  unit_dsp_init();

  test_registry();
  test_song_roundtrip();
  test_instrument_roundtrip();
  test_recursive_find();
  test_wav_export();
  test_render_smoke();
  test_clap_plugin_pd();
  test_multiple_wclap_teardown_does_not_dangle();
  test_clap_plugin_pd_default_params_are_audible();
  test_clap_param_mapping_reaches_shared_instance();
  test_clap_plugin_pd_supersaw_polyphony();

  if (fails) {
    printf("%d FAILURE(S)\n", fails);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
