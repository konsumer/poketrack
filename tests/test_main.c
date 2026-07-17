// Quick sanity tests — run via `make test`.
// Not a full suite: catches glaring breakage cheaply. Covers the unit
// registry, song/instrument file round-trips, and a render smoke test
// (every unit renders finite audio; synth sources actually make sound).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "tracker.h"
#include "units/unit_registry.h"

static int fails = 0;
#define CHECK(cond, ...)                                \
  do {                                                  \
    if (!(cond)) {                                      \
      fails++;                                          \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
      printf(__VA_ARGS__);                              \
      printf("\n");                                     \
    }                                                   \
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

static void test_render_smoke(void) {
  const UnitDef* defs[64];
  int n = 0;
  unit_list(defs, &n);

  enum { BLK = 512, BLOCKS = 8 };
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

int main(void) {
  SetTraceLogLevel(LOG_ERROR);  // silence raylib INFO spam
  unit_dsp_init();

  test_registry();
  test_song_roundtrip();
  test_instrument_roundtrip();
  test_render_smoke();

  if (fails) {
    printf("%d FAILURE(S)\n", fails);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
