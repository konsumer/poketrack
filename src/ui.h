#pragma once
#include "audio.h"
#include "input.h"
#include "raylib.h"
#include "tracker.h"

#define WIN_W 480
#define WIN_H 320

// visible arrangement lanes at once in the song screen (all of them, capped at 8)
#define SONG_VIEW_COLS (SONG_CHANNELS < 8 ? SONG_CHANNELS : 8)

#define FONT_S 10
#define CH_H 14
#define STATUS_H 15  // top and bottom bar height

// M8-inspired color palette
#define C_BG (Color){0x00, 0x00, 0x08, 0xFF}
#define C_BG_ALT (Color){0x06, 0x06, 0x10, 0xFF}
#define C_CURSOR (Color){0x18, 0x28, 0x68, 0xFF}
#define C_CURSOR2 (Color){0x28, 0x10, 0x40, 0xFF}
#define C_SEP (Color){0x20, 0x20, 0x30, 0xFF}
#define C_TEXT (Color){0xB8, 0xB8, 0xC8, 0xFF}
#define C_DIM (Color){0x28, 0x28, 0x38, 0xFF}
#define C_HEADER (Color){0x50, 0x50, 0x70, 0xFF}
#define C_NOTE (Color){0x40, 0xFF, 0xC0, 0xFF}
#define C_NOTE_OFF (Color){0xFF, 0x50, 0x50, 0xFF}
#define C_VEL (Color){0xA0, 0xFF, 0x60, 0xFF}
#define C_INST (Color){0xFF, 0xA0, 0x30, 0xFF}
#define C_FX (Color){0x80, 0xA0, 0xFF, 0xFF}
#define C_PLAY (Color){0x00, 0xFF, 0x60, 0xFF}
#define C_STATUS (Color){0xE0, 0xFF, 0x00, 0xFF}
#define C_TITLE (Color){0xFF, 0xFF, 0xFF, 0xFF}
#define C_EDIT_TAG (Color){0xFF, 0xC0, 0x00, 0xFF}

extern const Color CH_COLORS[PATTERN_TRACKS];

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

// Shared on-screen keyboard modal (SHIFT SPACE DEL OK, no suggest)
#define KBM_KEY_W 44
#define KBM_KEY_H 18
#define KBM_GAP 2
#define KBM_CHAR_ROWS 4
#define KBM_SPECIAL_ROW 4
#define KBM_TOTAL_ROWS 5
// Special cols: 0=SHIFT 1=SPACE 2=DEL 3=OK
#define KBM_SPECIAL_COLS 4

typedef struct {
  bool active;
  char* buf;   // pointer to target string (edited in place)
  int buf_sz;  // sizeof(buf)
  int row, col;
  bool shift;
} KBModal;

void kb_modal_open(KBModal* kb, char* buf, int buf_sz);
// Returns true when modal closes (OK or BTN_B). Caller checks buf for result.
bool kb_modal_update(KBModal* kb);
void kb_modal_draw(KBModal* kb, const char* label);
const char* note_str(uint8_t note);
const char* fx_cmd_str(uint8_t fx);
void draw_cell(int x, int y, int w, int h, Color bg, const char* text, int fs, Color fg);
