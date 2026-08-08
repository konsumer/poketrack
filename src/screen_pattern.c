#include <stdio.h>
#include <string.h>

#include "file_browser.h"
#include "icons.h"
#include "tracker.h"
#include "ui.h"

static int g_pat_fb_mode = 0;  // 0=none 1=save 2=load

// ── Layout ──────────────────────────────────────────────────────────────────
// Each track shows 7 sub-columns: NOTE VEL INST P1 V1 P2 V2.
#define PX_ROW_W 18  // row-number gutter
#define SW_NOTE 32
#define SW_VEL 22
#define SW_INST 18
#define SW_FXT 17
#define SW_FXV 17
#define TRACK_W (SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV + SW_FXT + SW_FXV)  // 140
#define PT_NCOLS 7                                                                // sub-columns per track

// Two sticky header rows: track numbers, then the column legend.
#define PT_HEADER_ROWS 2
#define PT_CONTENT_Y (STATUS_H + PT_HEADER_ROWS * CH_H + 2)

// x offset of sub-column c within a track
static int sub_x(int c) {
  static const int xs[] = {0, SW_NOTE, SW_NOTE + SW_VEL, SW_NOTE + SW_VEL + SW_INST,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV + SW_FXT};
  return xs[c];
}
static int sub_w(int c) {
  static const int ws[] = {SW_NOTE, SW_VEL, SW_INST, SW_FXT, SW_FXV, SW_FXT, SW_FXV};
  return ws[c];
}

// Number of tracks that fit horizontally
static int visible_tracks(void) {
  int v = (WIN_W - PX_ROW_W) / TRACK_W;
  if (v < 1)
    v = 1;
  if (v > PATTERN_TRACKS)
    v = PATTERN_TRACKS;
  return v;
}

// Screen x of the left edge of a track, given the current horizontal scroll
static int track_x(UIState* ui, int tr) {
  return PX_ROW_W + (tr - ui->pattern_track_scroll) * TRACK_W;
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255
                                                          : (uint8_t)v; }

// ── Sub-column accessors ────────────────────────────────────────────────────
// The 7 sub-columns map onto PatternStep fields; every place that edits, fills
// or clears a column goes through these instead of repeating the same switch.

// Value byte behind sub-column c (VEL/INST/FXV columns). NULL for NOTE and the
// two FX-command columns, which have their own stepping rules.
static uint8_t* col_value(PatternStep* s, int c) {
  switch (c) {
    case 1:
      return &s->velocity;
    case 2:
      return &s->instrument;
    case 4:
      return &s->fxv[0];
    case 6:
      return &s->fxv[1];
    default:
      return NULL;
  }
}

// FX command byte behind sub-column c (P1/P2), or NULL.
static uint8_t* col_fx(PatternStep* s, int c) {
  return c == 3 ? &s->fx[0] : (c == 5 ? &s->fx[1] : NULL);
}

// The value a column resets to when cleared (B, Y, or a fresh row).
static uint8_t col_empty(int c) {
  switch (c) {
    case 0:
      return NOTE_EMPTY;
    case 1:
      return 0xFF;  // full velocity
    case 3:
    case 5:
      return TRACKER_EMPTY;
    default:
      return 0;  // instrument, fx values
  }
}

// Every sub-column is one byte wide, so one pointer covers all seven.
// NULL for a column index outside 0..PT_NCOLS-1.
static uint8_t* col_ptr(PatternStep* s, int c) {
  if (c == 0)
    return &s->note;
  uint8_t* fx = col_fx(s, c);
  return fx ? fx : col_value(s, c);
}

static uint8_t col_get(PatternStep* s, int c) {
  const uint8_t* p = col_ptr(s, c);
  return p ? *p : 0;
}

static void col_set(PatternStep* s, int c, uint8_t v) {
  uint8_t* p = col_ptr(s, c);
  if (p)
    *p = v;
}

// hold-A + dpad on a plain value column: up/down ±1, right/left ±16.
static void edit_value(uint8_t* v) {
  if (ui_repeat(BTN_UP))
    *v = clamp8(*v + 1);
  if (ui_repeat(BTN_DOWN))
    *v = clamp8((int)*v - 1);
  if (ui_repeat(BTN_RIGHT))
    *v = clamp8(*v + 16);
  if (ui_repeat(BTN_LEFT))
    *v = clamp8((int)*v - 16);
}

// hold-A + dpad on an FX command column. Unlike a plain value these wrap
// through TRACKER_EMPTY ("--") at both ends rather than clamping.
static void edit_fx(uint8_t* f) {
  if (ui_repeat(BTN_UP))
    *f = (*f == TRACKER_EMPTY) ? 0 : (*f < 0xFE ? *f + 1 : TRACKER_EMPTY);
  if (ui_repeat(BTN_DOWN))
    *f = (*f == TRACKER_EMPTY || *f == 0) ? TRACKER_EMPTY : *f - 1;
  if (ui_repeat(BTN_RIGHT))
    *f = (*f == TRACKER_EMPTY) ? 0 : (*f + 16 > 0xFE ? 0xFE : *f + 16);
  if (ui_repeat(BTN_LEFT))
    *f = (*f == TRACKER_EMPTY || *f < 16) ? TRACKER_EMPTY : *f - 16;
}

static void preview(UIState* ui, uint8_t note) {
  audio_preview_kill(ui->engine);
  if (note == NOTE_EMPTY || note == NOTE_OFF)
    return;
  Pattern* pat = tracker_pattern(ui->song, ui->ctx_pattern);
  if (!pat)
    return;
  uint8_t inst = pat->steps[ui->pattern_track][ui->pattern_row].instrument;
  audio_preview_note(ui->engine, inst, note);
}

// Keep the selected track within the horizontal scroll window
static void ensure_track_visible(UIState* ui) {
  int vt = visible_tracks();
  if (ui->pattern_track < ui->pattern_track_scroll)
    ui->pattern_track_scroll = ui->pattern_track;
  if (ui->pattern_track >= ui->pattern_track_scroll + vt)
    ui->pattern_track_scroll = ui->pattern_track - vt + 1;
  if (ui->pattern_track_scroll < 0)
    ui->pattern_track_scroll = 0;
}

// ── "GEN" euclidean drum + melody/bass generator ───────────────────────────
// Bjorklund-distributed drum grooves, in the spirit of
// https://github.com/stackdump/beats-bitwrap-io — a popup picks a drum, bass
// and melody instrument plus one of 19 genre presets, then writes a 5-track
// groove (kick/snare/hihat/bass/melody) starting at the current track.
//
// The melody/bass walk is a straight port of that project's legacy
// (non-"cohesion") engine (internal/generator/markov.go): each step lands on
// the nearest chord tone on strong beats, moves stepwise or repeats on weak
// beats, and density thins the walk into rests, always protecting beat 0.
// Unlike bitwrap (which carries its own per-genre scale/root), this walks
// the tracker's own song-wide scale (MENU screen KEY/SCALE) via
// scale_next_note(), so generated notes always match the song's key.
typedef struct {
  uint8_t hits, steps, rotation, note;
} GenRole;

typedef struct {
  const char* name;
  GenRole kick, snare, hihat;
  uint8_t melody_density;  // 0-100
  uint8_t bass_density;    // 0-100
} GenStyle;

// (hits, steps, rotation, note) per drum role, then melody/bass density %,
// one row per genre — all values ported from bitwrap's genre table.
static const GenStyle GEN_STYLES[] = {
    {"TECHNO", {4, 16, 0, 36}, {2, 16, 4, 38}, {5, 8, 0, 42}, 40, 50},
    {"HOUSE", {4, 16, 0, 36}, {3, 8, 0, 38}, {6, 8, 0, 42}, 50, 60},
    {"JAZZ", {3, 12, 0, 36}, {2, 12, 3, 38}, {5, 12, 0, 42}, 60, 40},
    {"AMBIENT", {2, 16, 0, 36}, {1, 16, 8, 38}, {3, 16, 0, 42}, 30, 20},
    {"DNB", {3, 16, 0, 36}, {2, 8, 2, 38}, {7, 8, 0, 42}, 50, 70},
    {"EDM", {4, 16, 0, 36}, {2, 16, 4, 40}, {8, 16, 0, 42}, 60, 50},
    {"SPEEDCORE", {8, 16, 0, 36}, {4, 16, 2, 40}, {12, 16, 0, 42}, 70, 80},
    {"DUBSTEP", {3, 16, 0, 36}, {2, 16, 4, 38}, {5, 16, 0, 42}, 40, 60},
    {"COUNTRY", {4, 16, 0, 36}, {4, 16, 4, 38}, {8, 16, 0, 42}, 50, 50},
    {"BLUES", {3, 16, 0, 36}, {2, 16, 4, 38}, {6, 16, 0, 42}, 50, 40},
    {"SYNTHWAVE", {4, 16, 0, 36}, {2, 16, 4, 38}, {4, 8, 0, 42}, 40, 60},
    {"TRANCE", {4, 16, 0, 36}, {2, 16, 4, 40}, {8, 16, 0, 42}, 50, 60},
    {"LOFI", {3, 16, 0, 36}, {2, 16, 4, 38}, {5, 8, 0, 42}, 35, 30},
    {"REGGAE", {3, 16, 0, 36}, {2, 16, 6, 37}, {8, 16, 0, 42}, 40, 50},
    {"FUNK", {5, 16, 0, 36}, {4, 16, 4, 38}, {8, 16, 0, 42}, 55, 60},
    {"BOSSA", {3, 16, 0, 36}, {5, 16, 3, 37}, {6, 8, 0, 42}, 45, 50},
    {"TRAP", {3, 16, 0, 36}, {2, 16, 4, 40}, {10, 16, 0, 42}, 35, 50},
    {"GARAGE", {3, 16, 0, 36}, {3, 16, 2, 38}, {7, 8, 0, 42}, 45, 60},
    {"METAL", {8, 16, 0, 36}, {4, 16, 4, 38}, {8, 8, 0, 42}, 60, 70},
};
#define GEN_STYLE_COUNT ((int)(sizeof(GEN_STYLES) / sizeof(GEN_STYLES[0])))
#define GEN_MAXN 32       // longest drum `steps` used by any style above
#define GEN_SCALE_MAX 12  // longest ScaleDef.intervals[]
#define GEN_MEL_STEPS 16  // melody/bass walk unit, tiled across the pattern

// Popup rows. The first 4 are hold-{OK}+dpad value fields (matching how
// every other screen edits a value); GENERATE is an action row, triggered
// by a plain {OK} press while selected — same convention as the MENU
// screen's SAVE/LOAD/NEW rows.
typedef enum {
  GEN_ROW_DRUM = 0,
  GEN_ROW_BASS,
  GEN_ROW_MELODY,
  GEN_ROW_STYLE,
  GEN_ROW_GENERATE,
  GEN_ROW_COUNT,
} GenRow;

static bool g_gen_active = false;
static int g_gen_field = 0;
// -1 = "--" (disabled — that role's tracks are left untouched on generate).
static int g_gen_drum_inst = 0;
static int g_gen_bass_inst = 0;
static int g_gen_melody_inst = 0;
static int g_gen_style = 0;

// Distributes `k` hits as evenly as possible across `n` steps (n <= GEN_MAXN),
// via the standard Bjorklund group-merging algorithm.
static void bjorklund(int k, int n, bool* out) {
  if (n <= 0)
    return;
  if (k <= 0) {
    for (int i = 0; i < n; i++) out[i] = false;
    return;
  }
  if (k >= n) {
    for (int i = 0; i < n; i++) out[i] = true;
    return;
  }

  typedef struct {
    uint8_t bits[GEN_MAXN];
    int len;
  } Group;
  Group groups[GEN_MAXN];
  int ngroups = n;
  for (int i = 0; i < n; i++) {
    groups[i].len = 1;
    groups[i].bits[0] = (i < k) ? 1 : 0;
  }

  int kk = k;
  for (;;) {
    int remainder = ngroups - kk;
    if (remainder <= 1)
      break;
    int merges = kk < remainder ? kk : remainder;
    Group merged[GEN_MAXN];
    int mc = 0;
    for (int i = 0; i < merges; i++) {
      Group* g = &merged[mc++];
      g->len = 0;
      for (int j = 0; j < groups[i].len; j++)
        g->bits[g->len++] = groups[i].bits[j];
      Group* tail = &groups[ngroups - 1 - i];
      for (int j = 0; j < tail->len; j++)
        g->bits[g->len++] = tail->bits[j];
    }
    for (int i = merges; i < ngroups - merges; i++)
      merged[mc++] = groups[i];
    memcpy(groups, merged, sizeof(Group) * mc);
    ngroups = mc;
    kk = merges;
    if (ngroups - kk <= 1)
      break;
  }

  int idx = 0;
  for (int i = 0; i < ngroups; i++)
    for (int j = 0; j < groups[i].len; j++)
      out[idx++] = groups[i].bits[j] != 0;
}

// Writes one role's euclidean hit-mask into a track, tiled across the full
// pattern length and rotated per the style's `rotation`.
static void gen_write_track(Pattern* pat, int tr, uint8_t inst, const GenRole* role) {
  int n = role->steps > GEN_MAXN ? GEN_MAXN : role->steps;
  bool hits[GEN_MAXN];
  bjorklund(role->hits, n, hits);
  int rot = ((role->rotation % n) + n) % n;

  for (int i = 0; i < pat->len; i++) {
    bool hit = hits[(i + rot) % n];
    PatternStep* s = &pat->steps[tr][i];
    s->note = hit ? role->note : NOTE_EMPTY;
    s->velocity = 0xFF;
    s->instrument = inst;
    s->fx[0] = s->fx[1] = TRACKER_EMPTY;
    s->fxv[0] = s->fxv[1] = 0;
  }
}

// ── Melody/bass walk (ported from bitwrap's markov.go) ─────────────────────

// Chord tones are always scale degrees {0, 2, 4} (root/third/fifth) — the
// same "no chords provided" default bitwrap's engine falls back to, which is
// what most of its genre chord tables reduce to in practice anyway.
static const int GEN_CHORD_DEGREES[3] = {0, 2, 4};

// Closest chord-tone degree to `current`, preferring the tone itself.
static int gen_nearest_chord_tone(int current, int scale_len, const bool* chord) {
  if (chord[current])
    return current;
  for (int dist = 1; dist < scale_len; dist++) {
    int up = current + dist, down = current - dist;
    if (up < scale_len && chord[up])
      return up;
    if (down >= 0 && chord[down])
      return down;
  }
  return 0;
}

// Moves `current` by `interval` scale-degrees, up or down at random,
// clamping into [0, scale_len) rather than wrapping octaves.
static int gen_stepwise(int current, int scale_len, int interval) {
  if (GetRandomValue(0, 1)) {
    int next = current + interval;
    if (next >= scale_len)
      next = current - interval;
    return next < 0 ? 0 : next;
  }
  int next = current - interval;
  if (next < 0)
    next = current + interval;
  return next >= scale_len ? scale_len - 1 : next;
}

// Strong beat (0): nearest chord tone. Medium beat (2): mostly chord tone,
// sometimes a step. Weak beats: step of 1, step of 2, or repeat.
static int gen_melody_step(int current, int beat_pos, int scale_len, const bool* chord) {
  if (beat_pos == 0)
    return gen_nearest_chord_tone(current, scale_len, chord);
  if (beat_pos == 2) {
    if (GetRandomValue(0, 99) < 70)
      return gen_nearest_chord_tone(current, scale_len, chord);
    return gen_stepwise(current, scale_len, 1);
  }
  int r = GetRandomValue(0, 99);
  if (r < 60)
    return gen_stepwise(current, scale_len, 1);
  if (r < 85)
    return gen_stepwise(current, scale_len, 2);
  return current;
}

// Bass emphasizes root (beat 0) and fifth (beat 2), stepping or holding
// on weak beats.
static int gen_bass_step(int current, int beat_pos, int scale_len) {
  if (beat_pos == 0)
    return 0;
  if (beat_pos == 2)
    return (scale_len > 4 && GetRandomValue(0, 99) < 60) ? 4 : 0;
  return GetRandomValue(0, 99) < 50 ? current : gen_stepwise(current, scale_len, 1);
}

// Walks GEN_MEL_STEPS scale-degree steps, then thins it to `density_pct`
// by resting the weakest beats first (beat 0 is never rested). seq[i] is a
// scale-degree index, or -1 for a rest.
static void gen_compose_sequence(int scale_len, const bool* chord, bool is_bass, int density_pct, int* seq) {
  int current = 0;
  seq[0] = 0;
  for (int i = 1; i < GEN_MEL_STEPS; i++) {
    int beat_pos = i % 4;
    current = is_bass ? gen_bass_step(current, beat_pos, scale_len)
                      : gen_melody_step(current, beat_pos, scale_len, chord);
    seq[i] = current;
  }

  int note_count = (density_pct * GEN_MEL_STEPS) / 100;
  if (note_count < 1)
    note_count = 1;
  if (note_count >= GEN_MEL_STEPS)
    return;

  int rests_needed = GEN_MEL_STEPS - note_count;
  int order[GEN_MEL_STEPS], score[GEN_MEL_STEPS];
  for (int i = 0; i < GEN_MEL_STEPS; i++) {
    order[i] = i;
    score[i] = (i == 0) ? 200 : (i % 4 == 0) ? 100
                            : (i % 4 == 2)   ? 50
                                             : 0;
  }
  for (int i = 1; i < GEN_MEL_STEPS; i++)
    for (int j = i; j > 0 && score[order[j]] < score[order[j - 1]]; j--) {
      int t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  for (int i = 0; i < rests_needed; i++) {
    int idx = order[i];
    if (idx != 0)
      seq[idx] = -1;
  }
}

// Beat-position accent + a small chord-tone bonus, scaled to the tracker's
// 0-255 velocity range (bitwrap works in 0-127 with a ~100 base velocity).
static uint8_t gen_compute_velocity(int beat_pos, int deg, const bool* chord) {
  int vel = 200 + (beat_pos == 0 ? 40 : beat_pos == 2 ? 15
                                                      : -30);
  if (chord[deg])
    vel += 15;
  return (uint8_t)(vel < 40 ? 40 : vel > 255 ? 255
                                             : vel);
}

// Writes a melody or bass line into a track: builds a one-octave table of
// the song's own scale (MENU screen KEY/SCALE) anchored at `reg_root`,
// composes a GEN_MEL_STEPS-long walk over it, then tiles that walk across
// the full pattern length.
static void gen_write_melodic_track(Pattern* pat, int tr, uint8_t inst, uint8_t scale_idx,
                                    uint8_t scale_root, uint8_t reg_root, int density_pct, bool is_bass) {
  int scale_len = (scale_idx == 0) ? GEN_SCALE_MAX : SCALES[scale_idx].len;
  if (scale_len > GEN_SCALE_MAX)
    scale_len = GEN_SCALE_MAX;
  if (scale_len < 1)
    scale_len = 1;

  uint8_t table[GEN_SCALE_MAX];
  table[0] = scale_next_note((uint8_t)(reg_root > 0 ? reg_root - 1 : 0), +1, scale_idx, scale_root);
  for (int i = 1; i < scale_len; i++)
    table[i] = scale_next_note(table[i - 1], +1, scale_idx, scale_root);

  bool chord[GEN_SCALE_MAX] = {0};
  for (int i = 0; i < 3; i++)
    if (GEN_CHORD_DEGREES[i] < scale_len)
      chord[GEN_CHORD_DEGREES[i]] = true;

  int seq[GEN_MEL_STEPS];
  gen_compose_sequence(scale_len, chord, is_bass, density_pct, seq);

  for (int i = 0; i < pat->len; i++) {
    int beat_pos = i % GEN_MEL_STEPS;
    int deg = seq[beat_pos];
    PatternStep* s = &pat->steps[tr][i];
    if (deg < 0) {
      s->note = NOTE_EMPTY;
      s->velocity = 0xFF;
    } else {
      s->note = table[deg];
      s->velocity = gen_compute_velocity(beat_pos % 4, deg, chord);
    }
    s->instrument = inst;
    s->fx[0] = s->fx[1] = TRACKER_EMPTY;
    s->fxv[0] = s->fxv[1] = 0;
  }
}

// First of the 5 consecutive tracks (kick, snare, hihat, bass, melody) a
// generate at `track` would write, clamped so all 5 stay within range.
static int gen_base_track(int track) {
  if (track > PATTERN_TRACKS - 5)
    track = PATTERN_TRACKS - 5;
  if (track < 0)
    track = 0;
  return track;
}

// Any of the 3 instruments can be -1 ("--"): that role's tracks are then
// left untouched, so e.g. melody can be (re)generated alone.
static void gen_apply(Pattern* pat, TrackerSong* song, int track, int drum_inst,
                      int bass_inst, int melody_inst, const GenStyle* gs) {
  int base = gen_base_track(track);
  if (drum_inst >= 0) {
    gen_write_track(pat, base + 0, (uint8_t)drum_inst, &gs->kick);
    gen_write_track(pat, base + 1, (uint8_t)drum_inst, &gs->snare);
    gen_write_track(pat, base + 2, (uint8_t)drum_inst, &gs->hihat);
  }
  // Bass anchored ~C2, melody ~C4, both snapped onto the song's key/scale.
  if (bass_inst >= 0)
    gen_write_melodic_track(pat, base + 3, (uint8_t)bass_inst, song->scale_idx, song->scale_root,
                            (uint8_t)(song->scale_root + 36), gs->bass_density, true);
  if (melody_inst >= 0)
    gen_write_melodic_track(pat, base + 4, (uint8_t)melody_inst, song->scale_idx, song->scale_root,
                            (uint8_t)(song->scale_root + 60), gs->melody_density, false);
}

// hold-{OK} + dpad on an instrument-select field. Mirrors edit_fx's wrap
// behavior but with -1 ("--", disabled) as the empty sentinel instead of
// TRACKER_EMPTY, since 0..255 are all valid instrument indices here.
static void gen_edit_inst(int* v) {
  if (ui_repeat(BTN_UP))
    *v = (*v < 0) ? 0 : (*v < NUM_INSTRUMENTS - 1 ? *v + 1 : -1);
  if (ui_repeat(BTN_DOWN))
    *v = (*v <= 0) ? -1 : *v - 1;
  if (ui_repeat(BTN_RIGHT))
    *v = (*v < 0) ? 0 : (*v + 16 > NUM_INSTRUMENTS - 1 ? NUM_INSTRUMENTS - 1 : *v + 16);
  if (ui_repeat(BTN_LEFT))
    *v = (*v < 0 || *v < 16) ? -1 : *v - 16;
}

void screen_pattern_update(UIState* ui) {
  Pattern* pat = tracker_pattern(ui->song, ui->ctx_pattern);
  if (!pat)
    return;
  bool edit = input_held(BTN_OK);

  if (input_released(BTN_OK))
    audio_preview_kill(ui->engine);

  // ctx_pattern can change from the song screen; if the new pattern is
  // shorter than where the cursor was, land on the footer instead of an
  // undrawn row past it.
  if (ui->pattern_row > (int)pat->len)
    ui->pattern_row = (int)pat->len;

  // Footer row: pat->len  (cols: 0=+ 1=- 2=SAVE 3=LOAD)
  bool in_footer = (ui->pattern_row == (int)pat->len);

  // Poll file browser
  const char* fb = file_browser_poll();
  if (fb) {
    if (g_pat_fb_mode == 1) {
      char fname[24];
      snprintf(fname, sizeof(fname), "PAT%02X.rptp", (uint8_t)ui->ctx_pattern);
      tracker_save_pattern(pat, fb);
      file_browser_download(fb, fname);
    } else if (g_pat_fb_mode == 2) {
      tracker_load_pattern(pat, fb);
      if (ui->pattern_row >= (int)pat->len)
        ui->pattern_row = (int)pat->len - 1;
    }
    g_pat_fb_mode = 0;
    return;
  }
  if (file_browser_active())
    return;

  // GEN popup: SELECT+B always cancels, matching the file browser convention.
  // Otherwise it follows the same convention as every other screen: plain
  // {UP}/{DOWN} moves between rows, hold {OK}+dpad edits the selected row's
  // value. GENERATE is an action row (like MENU's SAVE/LOAD/NEW) — a plain
  // {OK} press while it's selected runs the generator.
  if (g_gen_active) {
    if (input_held(BTN_SCREEN) && input_pressed(BTN_NO)) {
      g_gen_active = false;
      return;
    }
    if (!edit) {
      if (ui_repeat(BTN_UP) && g_gen_field > 0)
        g_gen_field--;
      if (ui_repeat(BTN_DOWN) && g_gen_field < GEN_ROW_COUNT - 1)
        g_gen_field++;
    } else {
      switch (g_gen_field) {
        case GEN_ROW_DRUM:
          gen_edit_inst(&g_gen_drum_inst);
          break;
        case GEN_ROW_BASS:
          gen_edit_inst(&g_gen_bass_inst);
          break;
        case GEN_ROW_MELODY:
          gen_edit_inst(&g_gen_melody_inst);
          break;
        case GEN_ROW_STYLE:
          if (ui_repeat(BTN_UP) || ui_repeat(BTN_RIGHT))
            g_gen_style = (g_gen_style + 1) % GEN_STYLE_COUNT;
          if (ui_repeat(BTN_DOWN) || ui_repeat(BTN_LEFT))
            g_gen_style = (g_gen_style + GEN_STYLE_COUNT - 1) % GEN_STYLE_COUNT;
          break;
        case GEN_ROW_GENERATE:
          if (input_pressed(BTN_OK)) {
            gen_apply(pat, ui->song, ui->pattern_track, g_gen_drum_inst, g_gen_bass_inst,
                      g_gen_melody_inst, &GEN_STYLES[g_gen_style]);
            g_gen_active = false;
          }
          break;
      }
    }
    return;
  }

  // L/R shoulder: cycle patterns (not while editing)
  if (!edit) {
    bool switched = false;
    if (ui_repeat(BTN_NEXT) && ui->ctx_pattern < NUM_PATTERNS - 1) {
      ui->ctx_pattern++;
      pat = tracker_pattern(ui->song, ui->ctx_pattern);
      if (!pat)
        return;
      if (ui->pattern_row > (int)pat->len)
        ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
      switched = true;
    }
    if (ui_repeat(BTN_PREV) && ui->ctx_pattern > 0) {
      ui->ctx_pattern--;
      pat = tracker_pattern(ui->song, ui->ctx_pattern);
      if (!pat)
        return;
      if (ui->pattern_row > (int)pat->len)
        ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
      switched = true;
    }
    // If a pattern is currently looping (PLAY on this screen), follow along
    // to the newly-selected pattern instead of continuing to loop the old one.
    if (switched && audio_is_playing(ui->engine) && ui->engine->pattern_loop)
      audio_play_pattern(ui->engine, (uint8_t)ui->ctx_pattern);
  }

  // Handle footer before edit check — cols: 0=+ 1=- 2=SAVE 3=LOAD 4=GEN
  if (in_footer) {
    // The grid has more sub-columns than the footer's 5 cells; arriving from
    // one of them would leave the cursor on a nonexistent cell — land on GEN.
    if (ui->pattern_col > 4)
      ui->pattern_col = 4;
    if (ui_repeat(BTN_UP)) {
      ui->pattern_row = pat->len > 0 ? (int)pat->len - 1 : 0;
      return;
    }
    if (ui_repeat(BTN_LEFT)) {
      if (ui->pattern_col > 0)
        ui->pattern_col--;
    }
    if (ui_repeat(BTN_RIGHT)) {
      if (ui->pattern_col < 4)
        ui->pattern_col++;
    }
    if (input_pressed(BTN_OK)) {
      if (ui->pattern_col == 0 && pat->len < MAX_PATTERN_STEPS)
        pat->len++;
      else if (ui->pattern_col == 1 && pat->len > 1)
        pat->len--;
      else if (ui->pattern_col == 2) {
        char fname[24];
        snprintf(fname, sizeof(fname), "PAT%02X.rptp", (uint8_t)ui->ctx_pattern);
        g_pat_fb_mode = 1;
        file_browser_save_as("Save pattern", fname);
      } else if (ui->pattern_col == 3) {
        g_pat_fb_mode = 2;
        file_browser_open("Load pattern", "*.rptp");
      } else if (ui->pattern_col == 4) {
        g_gen_drum_inst = g_gen_bass_inst = g_gen_melody_inst = ui->ctx_instrument;
        g_gen_field = 0;
        g_gen_active = true;
      }
      if (ui->pattern_col < 2)
        ui->pattern_row = (int)pat->len;
    }
    return;
  }

  PatternStep* step = &pat->steps[ui->pattern_track][ui->pattern_row];

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->pattern_row > 0)
      ui->pattern_row--;
    if (ui_repeat(BTN_DOWN)) {
      if (ui->pattern_row < (int)pat->len - 1)
        ui->pattern_row++;
      else
        ui->pattern_row = (int)pat->len;  // enter resize row
    }
    // LEFT/RIGHT walk sub-columns, crossing into adjacent tracks at the edges.
    if (ui_repeat(BTN_LEFT)) {
      if (ui->pattern_col > 0)
        ui->pattern_col--;
      else if (ui->pattern_track > 0) {
        ui->pattern_track--;
        ui->pattern_col = PT_NCOLS - 1;
      }
      ensure_track_visible(ui);
    }
    if (ui_repeat(BTN_RIGHT)) {
      if (ui->pattern_col < PT_NCOLS - 1)
        ui->pattern_col++;
      else if (ui->pattern_track < PATTERN_TRACKS - 1) {
        ui->pattern_track++;
        ui->pattern_col = 0;
      }
      ensure_track_visible(ui);
    }

    if (input_pressed(BTN_NO))
      col_set(step, ui->pattern_col, col_empty(ui->pattern_col));

    ui->ctx_instrument = step->instrument;

  } else {
    switch (ui->pattern_col) {
      case 0: {
        uint8_t n = step->note;
        uint8_t si = ui->song->scale_idx % NUM_SCALES;
        uint8_t sr = ui->song->scale_root % 12;
        if (ui_repeat(BTN_UP)) {
          if (n == NOTE_EMPTY || n == NOTE_OFF) {
            uint8_t base = ui->last_note ? ui->last_note - 1 : 59;
            step->note = scale_next_note(base, +1, si, sr);
            if (!step->velocity)
              step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, +1, si, sr);
          }
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_DOWN)) {
          if (n == NOTE_EMPTY || n == NOTE_OFF) {
            uint8_t base = ui->last_note ? ui->last_note + 1 : 61;
            step->note = scale_next_note(base, -1, si, sr);
            if (!step->velocity)
              step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, -1, si, sr);
          }
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_RIGHT) && n != NOTE_EMPTY && n != NOTE_OFF && n + 12 <= 127) {
          step->note += 12;
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_LEFT) && n != NOTE_EMPTY && n != NOTE_OFF && n >= 13) {
          step->note -= 12;
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (input_pressed(BTN_NO))
          step->note = NOTE_OFF;
        break;
      }
      // VEL / INST / FXV: plain ±1 / ±16 value columns.
      // FX cmd (P1/P2): same dpad layout, but wraps through "--".
      default: {
        int c = ui->pattern_col;
        uint8_t* fx = col_fx(step, c);
        uint8_t* val = fx ? NULL : col_value(step, c);
        if (fx)
          edit_fx(fx);
        else if (val)
          edit_value(val);
        else
          break;  // column index out of range — nothing to edit
        if (input_pressed(BTN_NO))
          col_set(step, c, col_empty(c));
        if (c == 2)
          ui->ctx_instrument = step->instrument;
        break;
      }
    }
  }

  // X = fill current column down the current track with the cursor cell's
  // value; Y = clear it. Both write the same byte to every step.
  bool fill = input_pressed(BTN_ALL);
  bool clear = !file_browser_active() && input_pressed(BTN_NONE);
  if (fill || clear) {
    int c = ui->pattern_col;
    uint8_t v = fill ? col_get(step, c) : col_empty(c);
    for (int i = 0; i < pat->len; i++) {
      PatternStep* s = &pat->steps[ui->pattern_track][i];
      col_set(s, c, v);
      // A filled note with no velocity would be silent — give it full velocity,
      // matching what entering the note by hand does.
      if (fill && c == 0 && v != NOTE_EMPTY && v != NOTE_OFF && !s->velocity)
        s->velocity = 0xFF;
    }
  }
}

static void draw_gen_popup(UIState* ui) {
  if (!g_gen_active)
    return;

  int y0 = STATUS_H;
  int h = WIN_H - STATUS_H * 2;
  DrawRectangle(0, y0, WIN_W, h, (Color){0x00, 0x00, 0x10, 0xF0});
  DrawLine(0, y0, WIN_W, y0, C_SEP);
  DrawText("GENERATE", 4, y0 + 4, FONT_S, C_HEADER);

  const GenStyle* gs = &GEN_STYLES[g_gen_style];
  static const char* FIELD_LABELS[GEN_ROW_COUNT] = {"DRUM INST", "BASS INST", "MELODY INST", "STYLE", ""};
  int val_x = 100;
  int ry0 = y0 + 4 + CH_H + 4;

  for (int f = 0; f < GEN_ROW_COUNT; f++) {
    int ry = ry0 + f * (CH_H + 2);
    bool sel = (g_gen_field == f);
    DrawRectangle(4, ry, WIN_W - 8, CH_H, sel ? C_CURSOR : C_BG_ALT);
    if (f == GEN_ROW_GENERATE) {
      const char* label = "GENERATE";
      int tw = MeasureText(label, FONT_S);
      DrawText(label, (WIN_W - tw) / 2, ry + (CH_H - FONT_S) / 2, FONT_S, sel ? C_TITLE : C_PLAY);
      continue;
    }
    DrawText(FIELD_LABELS[f], 8, ry + (CH_H - FONT_S) / 2, FONT_S, sel ? C_TITLE : C_HEADER);
    if (f == GEN_ROW_STYLE) {
      DrawText(TextFormat("%s (%d/%d)", gs->name, g_gen_style + 1, GEN_STYLE_COUNT),
               val_x, ry + (CH_H - FONT_S) / 2, FONT_S, sel ? C_TITLE : C_NOTE);
    } else {
      int inst = f == GEN_ROW_DRUM ? g_gen_drum_inst : f == GEN_ROW_BASS ? g_gen_bass_inst
                                                                         : g_gen_melody_inst;
      const char* txt = inst < 0 ? "--" : hex2((uint8_t)inst);
      Color c = inst < 0 ? (sel ? C_TITLE : C_DIM) : (sel ? C_TITLE : C_INST);
      DrawText(txt, val_x, ry + (CH_H - FONT_S) / 2, FONT_S, c);
    }
  }

  int ry3 = ry0 + GEN_ROW_COUNT * (CH_H + 2) + 8;
  DrawText(TextFormat("K %d/%d   S %d/%d   H %d/%d", gs->kick.hits, gs->kick.steps,
                      gs->snare.hits, gs->snare.steps, gs->hihat.hits, gs->hihat.steps),
           8, ry3, FONT_S, C_DIM);
  DrawText(TextFormat("BASS %d%%   MELODY %d%%", gs->bass_density, gs->melody_density),
           8, ry3 + CH_H, FONT_S, C_DIM);

  int base = gen_base_track(ui->pattern_track);
  DrawText(TextFormat("writes tracks %X %X %X %X %X (kick/snare/hihat/bass/melody)",
                      base, base + 1, base + 2, base + 3, base + 4),
           8, ry3 + CH_H * 2, FONT_S, C_DIM);

  hint_draw("{UP}/{DOWN}: row   hold{OK}+dpad: edit   {SCREEN}+{NO}: cancel",
            4, y0 + h - FONT_S - 4, FONT_S - 1, C_DIM);
}

// Draw one track's 7 sub-cells for step `s` at screen (tx, y).
// cur_col = highlighted sub-column (0-6), or -1 if this isn't the cursor track/row.
static void draw_track_cells(int tx, int y, PatternStep* s, int cur_col) {
  for (int c = 0; c < PT_NCOLS; c++) {
    bool cur_cell = (cur_col == c);
    int cx = tx + sub_x(c), cw = sub_w(c);
    if (cur_cell)
      DrawRectangle(cx, y, cw - 1, CH_H, C_CURSOR);
    // A column reads as blank ("--", dimmed) when the thing it qualifies is
    // absent: velocity without a note, an fx value without its fx command.
    bool has_note = (s->note != NOTE_EMPTY && s->note != NOTE_OFF);
    bool blank = (c == 1 && !has_note) ||
                 ((c >= 3) && s->fx[(c - 3) / 2] == TRACKER_EMPTY);

    const char* txt;
    Color fc2;
    if (c == 0) {
      txt = note_str(s->note);
      fc2 = s->note == NOTE_EMPTY ? (cur_cell ? C_TEXT : C_DIM)
            : s->note == NOTE_OFF ? C_NOTE_OFF
                                  : C_NOTE;
    } else if (blank) {
      txt = "--";
      fc2 = cur_cell ? C_TEXT : C_DIM;
    } else {
      txt = hex2(col_get(s, c));
      fc2 = (c == 1) ? C_VEL : (c == 2 ? C_INST : C_FX);
    }
    DrawText(txt, cx + 2, y + (CH_H - FONT_S) / 2, FONT_S, fc2);
  }
}

void screen_pattern_draw(UIState* ui) {
  Pattern* pat = tracker_pattern(ui->song, ui->ctx_pattern);
  if (!pat)
    return;
  bool in_footer = (ui->pattern_row == (int)pat->len);
  int vt = visible_tracks();

  // Sticky header: row 1 = track number (0-F, colored), row 2 = column legend.
  static const char* SUBLBL[PT_NCOLS] = {"NOT", "V", "I", "P1", "V1", "P2", "V2"};
  int hy = STATUS_H + 1;
  int hy2 = hy + CH_H;
  DrawRectangle(0, hy, WIN_W, CH_H * 2, C_BG_ALT);
  DrawText("PT", 2, hy + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  for (int i = 0; i < vt; i++) {
    int tr = ui->pattern_track_scroll + i;
    if (tr >= PATTERN_TRACKS)
      break;
    int tx = track_x(ui, tr);
    bool cur = (tr == ui->pattern_track);
    DrawText(TextFormat("%X", tr), tx + 2, hy + (CH_H - FONT_S) / 2, FONT_S,
             cur ? C_TITLE : CH_COLORS[tr]);
    for (int c = 0; c < PT_NCOLS; c++)
      DrawText(SUBLBL[c], tx + sub_x(c) + 2, hy2 + (CH_H - FONT_S) / 2, FONT_S,
               cur ? C_HEADER : C_DIM);
  }
  if (ui->pattern_track_scroll > 0)
    DrawText("<", PX_ROW_W - 8, hy + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
  if (ui->pattern_track_scroll + vt < PATTERN_TRACKS)
    DrawText(">", WIN_W - 8, hy + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
  DrawLine(0, hy2 + CH_H, WIN_W, hy2 + CH_H, C_SEP);

  // Which step is currently playing (shared across all tracks of the pattern)
  int playing_step = -1;
  if (audio_is_playing(ui->engine)) {
    AudioEngine* eng = ui->engine;
    if (eng->pattern_loop) {
      if (eng->loop_pattern_idx == ui->ctx_pattern)
        playing_step = eng->cursors[0].pattern_step;
    } else {
      for (int ch = 0; ch < SONG_CHANNELS; ch++) {
        ChannelCursor* cur = &eng->cursors[ch];
        if (ui->song->patterns[ch][cur->song_row] == (uint8_t)ui->ctx_pattern) {
          playing_step = cur->pattern_step;
          break;
        }
      }
    }
  }

  int pt_visible = (WIN_H - PT_CONTENT_Y - STATUS_H) / CH_H;
  int total_rows = pat->len + 1;

  int scroll = 0;
  if (ui->pattern_row >= pt_visible)
    scroll = ui->pattern_row - pt_visible + 1;
  if (scroll + pt_visible > total_rows)
    scroll = total_rows - pt_visible;
  if (scroll < 0)
    scroll = 0;

  for (int vi = 0; vi < pt_visible; vi++) {
    int i = scroll + vi;
    if (i > (int)pat->len)
      break;
    int y = PT_CONTENT_Y + vi * CH_H;

    if (i == (int)pat->len) {
      // Footer: "[len_hex]  [+][-][SAVE][LOAD][GEN]" — packed into the first
      // track's width so no track separator line cuts through the controls.
      DrawRectangle(0, y, WIN_W, CH_H, C_BG_ALT);
      DrawText(TextFormat("%02X", pat->len), 2, y + (CH_H - FONT_S) / 2, FONT_S,
               in_footer ? C_TEXT : C_HEADER);
      const char* labels[5] = {"+", "-", "SAVE", "LOAD", "GEN"};
      const int fw[5] = {14, 14, 40, 40, 32};  // 18+140 = 158 == PX_ROW_W+TRACK_W
      int x = PX_ROW_W;
      for (int c = 0; c < 5; c++) {
        bool sel = in_footer && ui->pattern_col == c;
        if (sel)
          DrawRectangle(x, y, fw[c] - 2, CH_H, C_CURSOR);
        DrawText(labels[c], x + 2, y + (CH_H - FONT_S) / 2, FONT_S, sel ? C_TITLE : C_DIM);
        x += fw[c];
      }
      continue;
    }

    bool is_cur = (i == ui->pattern_row);
    Color row_bg = (i % 4 == 0) ? C_BG_ALT : C_BG;
    if (i == playing_step)
      row_bg = C_CURSOR2;
    DrawRectangle(0, y, WIN_W, CH_H, row_bg);
    DrawText(hex2((uint8_t)i), 2, y + (CH_H - FONT_S) / 2, FONT_S,
             is_cur ? C_TITLE : C_HEADER);

    for (int ti = 0; ti < vt; ti++) {
      int tr = ui->pattern_track_scroll + ti;
      if (tr >= PATTERN_TRACKS)
        break;
      int tx = track_x(ui, tr);
      PatternStep* s = &pat->steps[tr][i];
      int cur_col = (is_cur && !in_footer && tr == ui->pattern_track) ? ui->pattern_col : -1;
      draw_track_cells(tx, y, s, cur_col);
    }
  }

  // Vertical separators between tracks (and the gutter), spanning header + grid.
  int grid_bottom = PT_CONTENT_Y + pt_visible * CH_H;
  if (grid_bottom > WIN_H - STATUS_H)
    grid_bottom = WIN_H - STATUS_H;
  for (int k = 0; k <= vt; k++) {
    int x = PX_ROW_W + k * TRACK_W;
    if (x >= WIN_W - 3)
      break;
    DrawLine(x, hy, x, grid_bottom, C_SEP);
  }

  draw_scrollbar(WIN_W - 3, PT_CONTENT_Y, 3, pt_visible * CH_H,
                 scroll, pt_visible, total_rows);

  draw_gen_popup(ui);
}
