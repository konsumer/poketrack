#include "tracker.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "units/unit_registry.h"

const ScaleDef SCALES[NUM_SCALES] = {
    // --- Chromatic ---
    {"CHROM", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    // --- Western diatonic modes ---
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"PHRYG", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"LYDIAN", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"MIXOLY", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"LOCRIAN", 7, {0, 1, 3, 5, 6, 8, 10}},
    // --- Harmonic / melodic variants ---
    {"HM.MIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"ML.MIN", 7, {0, 2, 3, 5, 7, 9, 11}},
    {"HM.MAJ", 7, {0, 2, 4, 5, 7, 8, 11}},
    // --- Jazz / altered modes ---
    {"LYD.DOM", 7, {0, 2, 4, 6, 7, 9, 10}},
    {"PHR.DOM", 7, {0, 1, 4, 5, 7, 8, 10}},
    {"LYD.AUG", 7, {0, 2, 4, 6, 8, 9, 11}},
    {"ALTERED", 7, {0, 1, 3, 4, 6, 8, 10}},
    {"UK.DORI", 7, {0, 2, 3, 6, 7, 9, 10}},
    // --- Pentatonic / blues ---
    {"PENT.MJ", 5, {0, 2, 4, 7, 9}},
    {"PENT.MN", 5, {0, 3, 5, 7, 10}},
    {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
    {"MJ.BLUE", 6, {0, 2, 3, 4, 7, 9}},
    {"EGYPT", 5, {0, 2, 5, 7, 10}},
    // --- Symmetric ---
    {"W.TONE", 6, {0, 2, 4, 6, 8, 10}},
    {"DIM.H-W", 8, {0, 1, 3, 4, 6, 7, 9, 10}},
    {"DIM.W-H", 8, {0, 2, 3, 5, 6, 8, 9, 11}},
    {"AUGMENT", 6, {0, 3, 4, 7, 8, 11}},
    {"TRITONE", 6, {0, 1, 4, 6, 7, 10}},
    {"PROMETH", 6, {0, 2, 4, 6, 9, 10}},
    // --- Middle Eastern ---
    {"PERSIAN", 7, {0, 1, 4, 5, 6, 8, 11}},
    {"BYZANT", 7, {0, 1, 4, 5, 7, 8, 11}},
    {"ENIGMA", 7, {0, 1, 4, 6, 8, 10, 11}},
    // --- Japanese ---
    {"HIRAJSH", 5, {0, 2, 3, 7, 8}},
    {"IN", 5, {0, 1, 5, 7, 8}},
    {"INSEN", 5, {0, 1, 5, 7, 10}},
    {"IWATO", 5, {0, 1, 5, 6, 10}},
    {"YO", 5, {0, 2, 5, 7, 9}},
    {"MAN.GON", 5, {0, 3, 5, 8, 10}},
    // --- Hungarian ---
    {"HUNG.MN", 7, {0, 2, 3, 6, 7, 8, 11}},
    {"HUNG.MJ", 7, {0, 3, 4, 6, 7, 9, 10}},
    // --- Neapolitan ---
    {"NAPL.MJ", 7, {0, 1, 3, 5, 7, 9, 11}},
    {"NAPL.MN", 7, {0, 1, 3, 5, 7, 8, 11}},
    // --- Indian ragas ---
    {"TODI", 7, {0, 1, 3, 6, 7, 8, 11}},
    {"MARWA", 7, {0, 1, 4, 6, 7, 9, 11}},
    {"PURVI", 7, {0, 1, 4, 6, 7, 8, 11}},
    // --- 8-tone / bebop ---
    {"SPANISH", 8, {0, 1, 3, 4, 5, 6, 8, 10}},
    {"BEBOP.D", 8, {0, 2, 4, 5, 7, 9, 10, 11}},
};

const char *const SCALE_ROOT_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

uint8_t scale_next_note(uint8_t note, int dir, uint8_t scale_idx, uint8_t root) {
  if (scale_idx == 0) {
    // Chromatic: straight semitone step
    int n = (int)note + dir;
    if (n < 1)
      return 1;
    if (n > 127)
      return 127;
    return (uint8_t)n;
  }
  const ScaleDef *sc = &SCALES[scale_idx];
  int n = (int)note;
  for (int i = 0; i < 128; i++) {
    n += dir;
    if (n < 1)
      return 1;
    if (n > 127)
      return 127;
    int pc = ((n - (int)root) % 12 + 12) % 12;
    for (int j = 0; j < sc->len; j++)
      if (sc->intervals[j] == pc)
        return (uint8_t)n;
  }
  return note;
}

void tracker_inst_set_slot(TrackerInstrument *inst, int slot, const char *unit_id) {
  if (slot < 0 || slot >= CHAIN_MAX)
    return;
  ChainSlot *s = &inst->chain[slot];
  // Clear file data when switching to a different unit type — old paths are meaningless
  if (strncmp(s->unit_id, unit_id, UNIT_ID_LEN) != 0)
    s->data[0] = '\0';
  strncpy(s->unit_id, unit_id, UNIT_ID_LEN - 1);
  s->unit_id[UNIT_ID_LEN - 1] = '\0';
  s->enabled = true;
  const UnitDef *def = unit_find(unit_id);
  if (def) {
    for (int i = 0; i < UNIT_MAX_PARAMS; i++)
      s->params[i] = def->param_defaults[i];
  }
}

void tracker_init(TrackerSong *song) {
  memset(song, 0, sizeof(TrackerSong));
  memset(song->patterns, TRACKER_EMPTY, sizeof(song->patterns));
  // fx fields default to TRACKER_EMPTY (no param automation)
  for (int pi = 0; pi < NUM_PATTERNS; pi++)
    for (int si = 0; si < PATTERN_STEPS; si++)
      for (int fi = 0; fi < FX_PER_STEP; fi++)
        song->pattern_data[pi].steps[si].fx[fi] = TRACKER_EMPTY;
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    snprintf(song->instruments[i].name, 16, "INST%02X", i);
    for (int s = 0; s < CHAIN_MAX; s++)
      memset(song->instruments[i].chain[s].cc_map, 0xFF, UNIT_MAX_PARAMS);
  }
  song->bpm = 120;
  song->swing = 0;
  song->loop = true;
  strncpy(song->name, "UNTITLED", 32);
}

void tracker_clear(TrackerSong *song) {
  uint8_t bpm = song->bpm;
  char name[32];
  strncpy(name, song->name, 32);
  tracker_init(song);
  song->bpm = bpm;
  strncpy(song->name, name, 32);
}

#include <stdlib.h>

static void song_dir(const char *file_path, char *dir, int sz) {
  strncpy(dir, file_path, sz - 1);
  char *sep = strrchr(dir, '/');
  if (!sep)
    sep = strrchr(dir, '\\');
  if (sep)
    *(sep + 1) = '\0';
  else {
    dir[0] = '.';
    dir[1] = '/';
    dir[2] = '\0';
  }
}

// Convert abs_path to path relative to base_dir (must end with '/').
// Falls back to abs_path if no common prefix.
static void path_make_relative(const char *base_dir, const char *abs_path, char *out, int out_sz) {
  int last_sep = 0;
  for (int i = 0; base_dir[i] && abs_path[i]; i++) {
    if (base_dir[i] != abs_path[i])
      break;
    if (base_dir[i] == '/')
      last_sep = i + 1;
  }
  if (last_sep == 0) {
    strncpy(out, abs_path, out_sz - 1);
    out[out_sz - 1] = '\0';
    return;
  }
  int ups = 0;
  for (int i = last_sep; base_dir[i]; i++)
    if (base_dir[i] == '/')
      ups++;
  out[0] = '\0';
  for (int i = 0; i < ups; i++) strncat(out, "../", out_sz - (int)strlen(out) - 1);
  strncat(out, abs_path + last_sep, out_sz - (int)strlen(out) - 1);
}

static void path_resolve(const char *base_dir, const char *rel, char *out, int out_sz) {
  if (rel[0] == '/' || rel[0] == '\\' || rel[1] == ':')
    strncpy(out, rel, out_sz - 1);
  else
    snprintf(out, out_sz, "%s%s", base_dir, rel);
  out[out_sz - 1] = '\0';
}

// Rewrite data field path in-place using path_fn(dir, old_path) -> new_path.
// Handles CLAP "path\tplugin_id" by only transforming the path part.
static void rewrite_path(char *d, const char *dir,
                         void (*path_fn)(const char *, const char *, char *, int)) {
  if (!d[0])
    return;
  char fpath[512];
  strncpy(fpath, d, sizeof(fpath) - 1);
  char *tab = strchr(fpath, '\t');
  if (tab)
    *tab = '\0';
  char result[512];
  path_fn(dir, fpath, result, sizeof(result));
  if (tab)
    snprintf(d, 239, "%s\t%s", result, tab + 1);
  else
    strncpy(d, result, 238);
}

bool tracker_save(const TrackerSong *song, const char *path) {
  char dir[512];
  song_dir(path, dir, sizeof(dir));

  TrackerSong *out = malloc(sizeof(TrackerSong));
  if (!out)
    return false;
  memcpy(out, song, sizeof(TrackerSong));

  for (int i = 0; i < NUM_INSTRUMENTS; i++)
    for (int s = 0; s < CHAIN_MAX; s++) {
      char *d = out->instruments[i].chain[s].data;
      // Only relativise absolute paths
      if (d[0] == '/' || d[0] == '\\' || d[1] == ':')
        rewrite_path(d, dir, path_make_relative);
    }

  bool ok = SaveFileData(path, out, (int)sizeof(TrackerSong));
  free(out);
  return ok;
}

bool tracker_load(TrackerSong *song, const char *path) {
  int size = 0;
  unsigned char *data = LoadFileData(path, &size);
  if (!data)
    return false;

  bool ok = ((size_t)size >= sizeof(TrackerSong));
  if (ok) {
    char dir[512];
    song_dir(path, dir, sizeof(dir));
    memcpy(song, data, sizeof(TrackerSong));
    for (int i = 0; i < NUM_INSTRUMENTS; i++)
      for (int s = 0; s < CHAIN_MAX; s++) {
        char *d = song->instruments[i].chain[s].data;
        // Only resolve relative paths
        if (d[0] && d[0] != '/' && d[0] != '\\' && d[1] != ':')
          rewrite_path(d, dir, path_resolve);
      }
  }

  UnloadFileData(data);
  return ok;
}
