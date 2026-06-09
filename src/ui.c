#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "file_browser.h"

const Color CH_COLORS[SONG_CHANNELS] = {
    {0xFF, 0x40, 0x40, 0xFF},
    {0xFF, 0x90, 0x20, 0xFF},
    {0xFF, 0xFF, 0x20, 0xFF},
    {0x40, 0xFF, 0x60, 0xFF},
    {0x20, 0xD0, 0xFF, 0xFF},
    {0x60, 0x60, 0xFF, 0xFF},
    {0xC0, 0x40, 0xFF, 0xFF},
    {0xFF, 0x40, 0xC0, 0xFF},
    {0xFF, 0x80, 0x80, 0xFF},
    {0x80, 0xFF, 0x80, 0xFF},
    {0x80, 0x80, 0xFF, 0xFF},
    {0xFF, 0xFF, 0x80, 0xFF},
    {0x80, 0xFF, 0xFF, 0xFF},
    {0xFF, 0x80, 0xFF, 0xFF},
    {0xC0, 0xC0, 0x40, 0xFF},
    {0x40, 0xC0, 0xC0, 0xFF},
};

void ui_init(UIState *ui, TrackerSong *song, AudioEngine *engine) {
  memset(ui, 0, sizeof(UIState));
  ui->song = song;
  ui->engine = engine;
}

bool ui_repeat(TrackerButton btn) {
  if (input_pressed(btn))
    return true;
  int f = input_held_frames(btn);
  return (f > 20) && ((f % 4) == 0);
}

const char *note_str(uint8_t note) {
  static char buf[8];
  if (note == NOTE_EMPTY)
    return "---";
  if (note == NOTE_OFF)
    return "OFF";
  static const char *names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  int oct = (note / 12) - 1;
  if (oct < 0)
    oct = 0;
  snprintf(buf, sizeof(buf), "%s%d", names[note % 12], oct);
  return buf;
}

const char *fx_cmd_str(uint8_t fx) {
  switch (fx) {
    case FX_NONE:
      return "--";
    case FX_ARP:
      return "AR";
    case FX_CHA:
      return "CH";
    case FX_DEL:
      return "DL";
    case FX_HOP:
      return "HP";
    case FX_KIL:
      return "KL";
    case FX_RET:
      return "RT";
    case FX_TEM:
      return "TM";
    case FX_TSP:
      return "TP";
    case FX_VIB:
      return "VB";
    case FX_VOL:
      return "VL";
    default:
      return "??";
  }
}

void draw_cell(int x, int y, int w, int h, Color bg, const char *text, int fs, Color fg) {
  if (bg.a > 0)
    DrawRectangle(x, y, w, h, bg);
  if (text && text[0])
    DrawText(text, x + 2, y + (h - fs) / 2, fs, fg);
}

// Forward declarations
void screen_song_update(UIState *ui);
void screen_song_draw(UIState *ui);
void screen_pattern_update(UIState *ui);
void screen_pattern_draw(UIState *ui);
void screen_instrument_update(UIState *ui);
void screen_instrument_draw(UIState *ui);
void screen_menu_update(UIState *ui);
void screen_menu_draw(UIState *ui);

void ui_update(UIState *ui) {
  ui->blink++;
  file_browser_tick();

  if (!file_browser_active()) {
    // START = play/stop; pattern screen loops current pattern only
    if (input_pressed(BTN_START) && !input_held(BTN_SELECT)) {
      if (audio_is_playing(ui->engine)) {
        audio_stop(ui->engine);
      } else if (ui->screen == SCREEN_PATTERN) {
        audio_play_pattern(ui->engine, (uint8_t)ui->ctx_pattern);
      } else {
        audio_play(ui->engine);
      }
    }

    // SELECT + direction = switch screen (takes priority, no A held)
    if (input_held(BTN_SELECT) && !input_held(BTN_A)) {
      AppScreen prev = ui->screen;
      if (input_pressed(BTN_LEFT))
        ui->screen = SCREEN_SONG;
      if (input_pressed(BTN_UP))
        ui->screen = SCREEN_PATTERN;
      if (input_pressed(BTN_DOWN))
        ui->screen = SCREEN_INSTRUMENT;
      if (input_pressed(BTN_RIGHT))
        ui->screen = SCREEN_MENU;
      if (ui->screen != prev) {
        if (prev == SCREEN_PATTERN)
          audio_preview_kill(ui->engine);
        return;
      }
    }
  }

  switch (ui->screen) {
    case SCREEN_SONG:
      screen_song_update(ui);
      break;
    case SCREEN_PATTERN:
      screen_pattern_update(ui);
      break;
    case SCREEN_INSTRUMENT:
      screen_instrument_update(ui);
      break;
    case SCREEN_MENU:
      screen_menu_update(ui);
      break;
  }
}

static const char *screen_label(AppScreen s) {
  switch (s) {
    case SCREEN_SONG:
      return "SONG";
    case SCREEN_PATTERN:
      return "PATTERN";
    case SCREEN_INSTRUMENT:
      return "INSTRUMENT";
    case SCREEN_MENU:
      return "MENU";
  }
  return "";
}

static void draw_status(UIState *ui) {
  bool edit = input_held(BTN_A);
  Color bar = edit ? (Color){0x14, 0x0C, 0x28, 0xFF} : C_BG_ALT;

  // Top bar
  DrawRectangle(0, 0, WIN_W, STATUS_H, bar);

  // Left: screen name; pattern screen includes pattern number
  char left[32];
  if (ui->screen == SCREEN_PATTERN)
    snprintf(left, sizeof(left), "PATTERN %02X", ui->ctx_pattern);
  else if (ui->screen == SCREEN_INSTRUMENT)
    snprintf(left, sizeof(left), "INSTRUMENT %02X", ui->ctx_instrument);
  else {
    static const char *names[] = {"SONG", NULL, NULL, "MENU"};
    snprintf(left, sizeof(left), "%s", names[ui->screen]);
  }
  DrawText(left, 4, (STATUS_H - FONT_S) / 2, FONT_S, C_STATUS);

  // Right: play state + edit indicator only — BPM visible as menu item on MENU screen
  const char *play = audio_is_playing(ui->engine) ? ">>" : "[]";
  char right[16];
  snprintf(right, sizeof(right), "%s", edit ? "E" : play);
  int rw = MeasureText(right, FONT_S);
  DrawText(right, WIN_W - rw - 4, (STATUS_H - FONT_S) / 2, FONT_S,
           edit ? C_EDIT_TAG : C_TEXT);

  DrawLine(0, STATUS_H, WIN_W, STATUS_H, C_SEP);

  // Bottom hint bar
  DrawRectangle(0, WIN_H - STATUS_H, WIN_W, STATUS_H, C_BG_ALT);
  DrawLine(0, WIN_H - STATUS_H, WIN_W, WIN_H - STATUS_H, C_SEP);
  const char *hint;
  if (edit) {
    switch (ui->screen) {
      case SCREEN_SONG:
        hint = "A+UP/DN: set pattern#  A+B: clear cell";
        break;
      case SCREEN_PATTERN:
        switch (ui->pattern_col) {
          case 0:
            hint = "A+UP/DN: note semitone  A+LT/RT: octave  A+B: note-off  A+SEL+B: clear";
            break;
          case 1:
            hint = "A+UP/DN: velocity +-1   A+LT/RT: +-16";
            break;
          case 2:
            hint = "A+UP/DN: instrument#   B: reset to 0";
            break;
          default:
            hint = "A+UP/DN: fx value  A+LT/RT: coarse";
            break;
        }
        break;
      case SCREEN_INSTRUMENT:
        hint = "A+UP/DN: value +-fine   A+LT/RT: coarse";
        break;
      case SCREEN_MENU:
        hint = "A+UP/DN: change value";
        break;
    }
  } else {
    hint = "DPAD: move   holdA+DPAD: edit   B: clear/back   START: play/stop";
  }
  DrawText(hint, 4, WIN_H - STATUS_H + (STATUS_H - (FONT_S - 1)) / 2, FONT_S - 1, C_DIM);
}

void ui_draw(UIState *ui) {
  ClearBackground(C_BG);
  draw_status(ui);
  switch (ui->screen) {
    case SCREEN_SONG:
      screen_song_draw(ui);
      break;
    case SCREEN_PATTERN:
      screen_pattern_draw(ui);
      break;
    case SCREEN_INSTRUMENT:
      screen_instrument_draw(ui);
      break;
    case SCREEN_MENU:
      screen_menu_draw(ui);
      break;
  }
  file_browser_draw();
}
