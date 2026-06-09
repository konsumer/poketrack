#pragma once
#include "audio.h"
#include "input.h"
#include "raylib.h"
#include "tracker.h"

#define WIN_W 480
#define WIN_H 320

#define SONG_VIEW_COLS 8  // visible channels at once in song screen

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

extern const Color CH_COLORS[SONG_CHANNELS];

typedef enum {
  SCREEN_SONG = 0,
  SCREEN_PATTERN,
  SCREEN_INSTRUMENT,
  SCREEN_MENU,
} AppScreen;

typedef struct {
  TrackerSong *song;
  AudioEngine *engine;
  AppScreen screen;

  // Cursor state per screen
  int song_row, song_col;        // SONG: row 0-254, col 0-7 (channel)
  int pattern_row, pattern_col;  // PATTERN: step 0-15, col 0-6
  int inst_row;                  // INSTRUMENT: param 0-N
  int menu_row;                  // MENU: item 0-N

  int song_scroll;      // first visible row
  int song_col_scroll;  // first visible channel (0 to SONG_CHANNELS-SONG_VIEW_COLS)

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
} UIState;

void ui_init(UIState *ui, TrackerSong *song, AudioEngine *engine);
void ui_update(UIState *ui);
void ui_draw(UIState *ui);

bool ui_repeat(TrackerButton btn);
const char *note_str(uint8_t note);
const char *fx_cmd_str(uint8_t fx);
void draw_cell(int x, int y, int w, int h, Color bg, const char *text, int fs, Color fg);
