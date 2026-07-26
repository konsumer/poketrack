#pragma once
#include "audio.h"
#include "input.h"
#include "raylib.h"
#include "theme.h"
#include "tracker.h"

extern int WIN_W;
extern int WIN_H;

// visible arrangement lanes at once in the song screen (all of them, capped at 8)
#define SONG_VIEW_COLS (SONG_CHANNELS < 8 ? SONG_CHANNELS : 8)

#define FONT_S 10
#define CH_H 14
#define STATUS_H 15  // top and bottom bar height

typedef enum {
  SCREEN_SONG = 0,
  SCREEN_PATTERN,
  SCREEN_INSTRUMENT,
  SCREEN_MENU,
} AppScreen;

typedef struct {
  TrackerSong* song;
  AudioEngine* engine;
  AppScreen screen;

  // Cursor state per screen
  int song_row, song_col;        // SONG: row 0-254, col 0..SONG_CHANNELS-1 (lane)
  int pattern_row, pattern_col;  // PATTERN: step 0-N, col 0-6 (sub-column within track)
  int pattern_track;             // PATTERN: current track 0..PATTERN_TRACKS-1
  int inst_row;                  // INSTRUMENT: param 0-N
  int menu_row;                  // MENU: item 0-N

  int song_scroll;           // first visible row
  int song_col_scroll;       // first visible channel (0 to SONG_CHANNELS-SONG_VIEW_COLS)
  int pattern_track_scroll;  // first visible track in pattern screen

  // Editing context (shared across screens)
  int ctx_channel;          // which channel
  int ctx_pattern;          // which pattern index
  int ctx_instrument;       // which instrument index
  int ctx_instrument_slot;  // which chain slot in instrument screen

  int blink;
  bool inst_data_editing;  // true while typing in the data/file field

  // CLAP param picker sub-mode
  bool clap_picker_active;
  int clap_picker_row;  // cursor in full plugin param list

  // Device picker sub-mode (data row A, for units with dev_picker_*)
  bool dev_picker_active;
  int dev_picker_row;

  // MIDI-in device picker (instrument-level, left panel DEV row)
  bool midi_in_picker_active;
  int midi_in_picker_row;
  bool inst_param_cc_col;  // true = cursor is on the CC field of a param row

  uint8_t last_note;     // last note entered in pattern screen
  uint8_t last_pattern;  // last pattern index entered in song screen
} UIState;

void ui_init(UIState* ui, TrackerSong* song, AudioEngine* engine);
void ui_update(UIState* ui);
void ui_draw(UIState* ui);

bool ui_repeat(TrackerButton btn);

// Shared on-screen keyboard modal (SHIFT SPACE DEL [SUGGEST] OK).
// Used by any screen that needs joystick-driven text entry.
#define KBM_KEY_W 44
#define KBM_KEY_H 18
#define KBM_GAP 2
#define KBM_CHAR_ROWS 4
#define KBM_SPECIAL_ROW 4
#define KBM_TOTAL_ROWS 5

typedef struct {
  bool active;
  bool confirmed;  // true if closed via OK/Enter, false if closed via cancel
  char* buf;       // pointer to target string (edited in place)
  int buf_sz;      // sizeof(buf)
  int row, col;
  bool shift;
  int suggest_words;  // >0 shows a SUGGEST key that fills buf with N random bip39 words
} KBModal;

// suggest_words: 0 to disable the SUGGEST key, or a word count (e.g. 2) to enable it
void kb_modal_open(KBModal* kb, char* buf, int buf_sz, int suggest_words);
// Returns true when modal closes (OK/Enter or cancel) — check kb->confirmed for which.
bool kb_modal_update(KBModal* kb);
void kb_modal_draw(KBModal* kb, const char* label);
// Two-digit uppercase hex, no printf. Preferred over TextFormat("%02X", v) in
// per-cell draw loops — TextFormat clears a 1KB buffer on every call.
const char* hex2(uint8_t v);
const char* note_str(uint8_t note);
const char* fx_cmd_str(uint8_t fx);
void draw_cell(int x, int y, int w, int h, Color bg, const char* text, int fs, Color fg);

// Vertical scrollbar for a list of `total` rows showing `visible` of them
// starting at `scroll`. Draws nothing when everything already fits.
void draw_scrollbar(int x, int y, int w, int h, int scroll, int visible, int total);

// Strips a trailing extension from name in place, if it matches. ext includes
// the dot and is matched case-insensitively, same as raylib's IsFileExtension.
// Returns true if something was stripped.
bool strip_ext(char* name, const char* ext);

// Truncates s to its trailing portion so it fits within pw pixels at font
// size fs (keeps the tail, e.g. for a long file path — no ellipsis).
const char* truncate_to_width(const char* s, int pw, int fs);

// Shared scrollable list-picker overlay (CLAP param picker, device picker,
// MIDI-in device picker all use this — same nav/scroll/highlight, different
// row content and confirm actions).
typedef enum { PICKER_NONE,
               PICKER_CONFIRMED,
               PICKER_CANCELLED } PickerResult;

// Up/down repeat-nav + OK/NO detection, clamping *row to [0, total-1].
// Caller applies its own confirm/cancel side effects and clears its own
// active flag based on the result.
PickerResult list_picker_nav(int* row, int total);

typedef const char* (*PickerLabelFn)(void* ctx, int idx);
// Optional (may be NULL to use the default cur ? C_TITLE : C_TEXT) — returns
// the row's final color, already resolved for whether it's the cursor row.
typedef Color (*PickerColorFn)(void* ctx, int idx, bool cur);

// Draws a scrollable list-picker overlay at (x, y, w, h): title bar + hint
// line, then rows scrolled to keep `row` visible. empty_msg (may be NULL) is
// shown instead of the list when total == 0.
void list_picker_draw(int x, int y, int w, int h, const char* title,
                      int row, int total, PickerLabelFn get_label,
                      PickerColorFn get_color, const char* empty_msg, void* ctx);
