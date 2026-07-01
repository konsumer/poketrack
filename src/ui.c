#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "file_browser.h"

// Indexed by track (0-F) in the pattern screen and by lane (0..SONG_CHANNELS-1) in the song screen.
const Color CH_COLORS[PATTERN_TRACKS] = {
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

void ui_init(UIState* ui, TrackerSong* song, AudioEngine* engine) {
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

const char* note_str(uint8_t note) {
  static char buf[8];
  if (note == NOTE_EMPTY)
    return "---";
  if (note == NOTE_OFF)
    return "OFF";
  static const char* names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  int oct = (note / 12) - 1;
  if (oct < 0)
    oct = 0;
  snprintf(buf, sizeof(buf), "%s%d", names[note % 12], oct);
  return buf;
}

const char* fx_cmd_str(uint8_t fx) {
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

void draw_cell(int x, int y, int w, int h, Color bg, const char* text, int fs, Color fg) {
  if (bg.a > 0)
    DrawRectangle(x, y, w, h, bg);
  if (text && text[0])
    DrawText(text, x + 2, y + (h - fs) / 2, fs, fg);
}

// Forward declarations
void screen_song_update(UIState* ui);
void screen_song_draw(UIState* ui);
void screen_pattern_update(UIState* ui);
void screen_pattern_draw(UIState* ui);
void screen_instrument_update(UIState* ui);
void screen_instrument_draw(UIState* ui);
void screen_menu_update(UIState* ui);
void screen_menu_draw(UIState* ui);

void ui_update(UIState* ui) {
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
    if (input_pressed(BTN_START) && input_held(BTN_SELECT) && !audio_is_playing(ui->engine)) {
      if (ui->screen == SCREEN_SONG)
        audio_play_from(ui->engine, (uint16_t)ui->song_row);
      else
        audio_play(ui->engine);
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

static const char* screen_label(AppScreen s) {
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

static void draw_status(UIState* ui) {
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
    static const char* names[] = {"SONG", NULL, NULL, "MENU"};
    snprintf(left, sizeof(left), "%s", names[ui->screen]);
  }
  DrawText(left, 4, (STATUS_H - FONT_S) / 2, FONT_S, C_STATUS);

  // Right: play state + edit indicator only — BPM visible as menu item on MENU screen
  const char* play = audio_is_playing(ui->engine) ? ">>" : "[]";
  char right[16];
  snprintf(right, sizeof(right), "%s", edit ? "E" : play);
  int rw = MeasureText(right, FONT_S);
  DrawText(right, WIN_W - rw - 4, (STATUS_H - FONT_S) / 2, FONT_S,
           edit ? C_EDIT_TAG : C_TEXT);

  DrawLine(0, STATUS_H, WIN_W, STATUS_H, C_SEP);

  // Bottom hint bar
  DrawRectangle(0, WIN_H - STATUS_H, WIN_W, STATUS_H, C_BG_ALT);
  DrawLine(0, WIN_H - STATUS_H, WIN_W, WIN_H - STATUS_H, C_SEP);
  const char* hint;
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

// ---- Shared keyboard modal -------------------------------------------------

static const char* KBM_CHARS[KBM_CHAR_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM._",
};
static const int KBM_CHAR_COLS[KBM_CHAR_ROWS] = {10, 10, 10, 9};

static int kbm_max_col(int row) {
  return (row < KBM_CHAR_ROWS) ? KBM_CHAR_COLS[row] : KBM_SPECIAL_COLS;
}

void kb_modal_open(KBModal* kb, char* buf, int buf_sz) {
  kb->buf = buf;
  kb->buf_sz = buf_sz;
  kb->row = KBM_SPECIAL_ROW;
  kb->col = 3;  // OK preselected
  kb->shift = false;
  kb->active = true;
  while (GetCharPressed() > 0) {
  }
}

bool kb_modal_update(KBModal* kb) {
  if (!kb->active)
    return true;

  if (ui_repeat(BTN_LEFT)) {
    kb->col--;
    if (kb->col < 0)
      kb->col = kbm_max_col(kb->row) - 1;
  }
  if (ui_repeat(BTN_RIGHT)) {
    kb->col++;
    if (kb->col >= kbm_max_col(kb->row))
      kb->col = 0;
  }
  if (ui_repeat(BTN_UP)) {
    kb->row--;
    if (kb->row < 0)
      kb->row = KBM_TOTAL_ROWS - 1;
    if (kb->col >= kbm_max_col(kb->row))
      kb->col = kbm_max_col(kb->row) - 1;
  }
  if (ui_repeat(BTN_DOWN)) {
    kb->row++;
    if (kb->row >= KBM_TOTAL_ROWS)
      kb->row = 0;
    if (kb->col >= kbm_max_col(kb->row))
      kb->col = kbm_max_col(kb->row) - 1;
  }

  if (input_pressed(BTN_A)) {
    while (GetCharPressed() > 0) {
    }
    if (kb->row < KBM_CHAR_ROWS) {
      char c = KBM_CHARS[kb->row][kb->col];
      if (!kb->shift)
        c = (char)(c | 0x20);
      size_t l = strlen(kb->buf);
      if (l < (size_t)(kb->buf_sz - 2)) {
        kb->buf[l] = c;
        kb->buf[l + 1] = '\0';
      }
    } else {
      switch (kb->col) {
        case 0:
          kb->shift = !kb->shift;
          break;
        case 1: {
          size_t l = strlen(kb->buf);
          if (l < (size_t)(kb->buf_sz - 2)) {
            kb->buf[l] = ' ';
            kb->buf[l + 1] = '\0';
          }
          break;
        }
        case 2: {
          size_t l = strlen(kb->buf);
          if (l)
            kb->buf[l - 1] = '\0';
          break;
        }
        case 3:  // OK
          kb->active = false;
          return true;
      }
    }
  } else {
    int ch;
    while ((ch = GetCharPressed()) > 0) {
      if (ch >= 32) {
        size_t l = strlen(kb->buf);
        if (l < (size_t)(kb->buf_sz - 2)) {
          kb->buf[l] = (char)ch;
          kb->buf[l + 1] = '\0';
        }
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
      size_t l = strlen(kb->buf);
      if (l)
        kb->buf[l - 1] = '\0';
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
      kb->active = false;
      return true;
    }
  }

  if (input_pressed(BTN_B)) {
    kb->active = false;
    return true;
  }
  return false;
}

void kb_modal_draw(KBModal* kb, const char* label) {
  if (!kb->active)
    return;

  int modal_y = STATUS_H;
  int modal_h = WIN_H - STATUS_H * 2;
  DrawRectangle(0, modal_y, WIN_W, modal_h, (Color){0x00, 0x00, 0x10, 0xFF});
  DrawLine(0, modal_y, WIN_W, modal_y, C_SEP);

  int name_y = modal_y + 4;
  DrawRectangle(0, name_y, WIN_W, CH_H + 2, (Color){0x08, 0x08, 0x28, 0xFF});
  DrawText(label, 4, name_y + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  const char* nm = kb->buf && kb->buf[0] ? kb->buf : "";
  int nx = MeasureText(label, FONT_S) + 10;
  DrawText(nm, nx, name_y + (CH_H - FONT_S) / 2, FONT_S, C_TITLE);
  double t = GetTime();
  if ((t - (int)t) < 0.5) {
    int cx = nx + MeasureText(nm, FONT_S);
    DrawRectangle(cx, name_y + 2, 1, FONT_S + 1, C_TITLE);
  }

  int kb_top = name_y + CH_H + 6;
  int avail_h = WIN_H - STATUS_H - kb_top;
  int kb_h = KBM_TOTAL_ROWS * (KBM_KEY_H + KBM_GAP);
  int y0 = kb_top + (avail_h - kb_h) / 2;

  Color key_bg = {0x28, 0x28, 0x50, 0xFF};
  Color key_cur = {0x20, 0x60, 0xC0, 0xFF};

  for (int r = 0; r < KBM_CHAR_ROWS; r++) {
    int ncols = KBM_CHAR_COLS[r];
    int total_w = ncols * KBM_KEY_W + (ncols - 1) * KBM_GAP;
    int sx = (WIN_W - total_w) / 2;
    int y = y0 + r * (KBM_KEY_H + KBM_GAP);
    for (int c = 0; c < ncols; c++) {
      int x = sx + c * (KBM_KEY_W + KBM_GAP);
      bool cur = (kb->row == r && kb->col == c);
      DrawRectangle(x, y, KBM_KEY_W, KBM_KEY_H, cur ? key_cur : key_bg);
      char raw = KBM_CHARS[r][c];
      char lbl[2] = {kb->shift ? raw : (char)(raw | 0x20), 0};
      int tw = MeasureText(lbl, FONT_S);
      DrawText(lbl, x + (KBM_KEY_W - tw) / 2, y + (KBM_KEY_H - FONT_S) / 2, FONT_S,
               cur ? C_TITLE : C_TEXT);
    }
  }

  int sy = y0 + KBM_CHAR_ROWS * (KBM_KEY_H + KBM_GAP);
  int sh_x = 8, sh_w = 56;
  int sp_x = sh_x + sh_w + 2, sp_w = 88;
  int del_x = sp_x + sp_w + 2, del_w = 56;
  int ok_x = del_x + del_w + 2, ok_w = WIN_W - ok_x - 8;

  bool sh_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 0);
  bool sp_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 1);
  bool del_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 2);
  bool ok_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 3);

  Color sh_bg = kb->shift ? (Color){0x60, 0x40, 0x00, 0xFF} : key_bg;
  DrawRectangle(sh_x, sy, sh_w, KBM_KEY_H, sh_cur ? key_cur : sh_bg);
  DrawRectangle(sp_x, sy, sp_w, KBM_KEY_H, sp_cur ? key_cur : key_bg);
  DrawRectangle(del_x, sy, del_w, KBM_KEY_H, del_cur ? key_cur : key_bg);
  DrawRectangle(ok_x, sy, ok_w, KBM_KEY_H, ok_cur ? key_cur : key_bg);

  Color sh_txt = sh_cur ? C_TITLE : (kb->shift ? (Color){0xFF, 0xC0, 0x00, 0xFF} : C_TEXT);
  DrawText("SHIFT", sh_x + (sh_w - MeasureText("SHIFT", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, sh_txt);
  DrawText("SPACE", sp_x + (sp_w - MeasureText("SPACE", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, sp_cur ? C_TITLE : C_TEXT);
  DrawText("DEL", del_x + (del_w - MeasureText("DEL", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, del_cur ? C_NOTE_OFF : C_TEXT);
  DrawText("OK", ok_x + (ok_w - MeasureText("OK", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, ok_cur ? C_PLAY : C_TEXT);
}

void ui_draw(UIState* ui) {
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
