#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bip39_en.h"
#include "file_browser.h"
#include "icons.h"

// Virtual screen size everything is laid out against; main.c renders into a
// texture this size and letterboxes it to the real window. Overridable via
// --width/--height. Defined here rather than in main.c so the test and
// theme-shot targets (which build every source except main.c) still link.
int WIN_W = 480;
int WIN_H = 320;

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

const char* hex2(uint8_t v) {
  // Rotating buffers so a couple of results can be live at once, same contract
  // as raylib's TextFormat — but that one memsets a 1KB buffer and runs
  // vsnprintf per call, and the pattern grid formats ~2000 bytes per frame.
  static const char DIGITS[] = "0123456789ABCDEF";
  static char bufs[4][3];
  static int idx = 0;
  char* b = bufs[idx];
  idx = (idx + 1) & 3;
  b[0] = DIGITS[v >> 4];
  b[1] = DIGITS[v & 0xF];
  b[2] = '\0';
  return b;
}

void draw_cell(int x, int y, int w, int h, Color bg, const char* text, int fs, Color fg) {
  if (bg.a > 0)
    DrawRectangle(x, y, w, h, bg);
  if (text && text[0])
    DrawText(text, x + 2, y + (h - fs) / 2, fs, fg);
}

bool strip_ext(char* name, const char* ext) {
  if (!IsFileExtension(name, ext))
    return false;
  name[strlen(name) - strlen(ext)] = '\0';
  return true;
}

void draw_scrollbar(int x, int y, int w, int h, int scroll, int visible, int total) {
  if (total <= visible || visible <= 0)
    return;
  int th = h * visible / total;
  if (th < 2)
    th = 2;
  DrawRectangle(x, y, w, h, C_DIM);
  DrawRectangle(x, y + h * scroll / total, w, th, C_HEADER);
}

const char* truncate_to_width(const char* s, int pw, int fs) {
  int cw = MeasureText("W", fs);
  int max_chars = pw / (cw > 0 ? cw : 6);
  int len = (int)strlen(s);
  return (len > max_chars) ? s + (len - max_chars) : s;
}

PickerResult list_picker_nav(int* row, int total) {
  if (ui_repeat(BTN_UP) && *row > 0)
    (*row)--;
  if (ui_repeat(BTN_DOWN) && *row < total - 1)
    (*row)++;
  if (input_pressed(BTN_OK))
    return PICKER_CONFIRMED;
  if (input_pressed(BTN_NO))
    return PICKER_CANCELLED;
  return PICKER_NONE;
}

void list_picker_draw(int x, int y, int w, int h, const char* title,
                      int row, int total, PickerLabelFn get_label,
                      PickerColorFn get_color, const char* empty_msg, void* ctx) {
  DrawRectangle(x, y, w, h, C_BG);
  DrawText(TextFormat("%s  [A]=select  [B]=cancel", title),
           x + 4, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_HEADER);
  DrawLine(x, y + CH_H, x + w, y + CH_H, C_SEP);

  if (total == 0) {
    if (empty_msg)
      DrawText(empty_msg, x + 4, y + CH_H + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
    return;
  }

  int visible = (h - CH_H) / CH_H;
  int scroll = 0;
  if (row >= visible)
    scroll = row - visible + 1;

  for (int i = 0; i < visible && (scroll + i) < total; i++) {
    int idx = scroll + i;
    int py = y + CH_H + i * CH_H;
    bool cur = (idx == row);
    DrawRectangle(x, py, w, CH_H, cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
    Color tc = get_color ? get_color(ctx, idx, cur) : (cur ? C_TITLE : C_TEXT);
    DrawText(get_label(ctx, idx), x + 4, py + (CH_H - FONT_S) / 2, FONT_S, tc);
  }
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
    // hold START (+ SELECT) + direction = play from cursor row (consumes
    // this START hold so the release below doesn't also toggle play/stop)
    if (input_pressed(BTN_PLAY) && input_held(BTN_SCREEN) && !audio_is_playing(ui->engine)) {
      if (ui->screen == SCREEN_SONG)
        audio_play_from(ui->engine, (uint16_t)ui->song_row);
      else
        audio_play(ui->engine);
      ui->play_chord_used = true;
    }

    // hold START + dpad = toggle mute on an arrangement lane (song screen
    // lanes are fixed at 4, one per dpad direction, in BTN_UP/DOWN/LEFT/RIGHT
    // enum order). Also consumes this START hold.
    _Static_assert(SONG_CHANNELS == 4, "dpad-to-lane mute mapping assumes 4 lanes");
    if (input_held(BTN_PLAY) && !input_held(BTN_SCREEN)) {
      static const TrackerButton DPAD[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
      for (int ch = 0; ch < 4; ch++) {
        if (input_pressed(DPAD[ch])) {
          audio_toggle_mute(ui->engine, ch);
          ui->play_chord_used = true;
        }
      }
    }

    // START tap (no chord used during the hold) = play/stop; pattern screen
    // loops current pattern only. Fires on release, not press, so a mute
    // chord (above) gets first refusal — a plain tap still feels instant
    // since release follows press within a frame or two.
    if (input_released(BTN_PLAY)) {
      if (!ui->play_chord_used) {
        if (audio_is_playing(ui->engine)) {
          audio_stop(ui->engine);
        } else if (ui->screen == SCREEN_PATTERN) {
          audio_play_pattern(ui->engine, (uint8_t)ui->ctx_pattern);
        } else {
          audio_play(ui->engine);
        }
      }
      ui->play_chord_used = false;
    }

    // SELECT + direction = switch screen (takes priority, no A held)
    if (input_held(BTN_SCREEN) && !input_held(BTN_OK)) {
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

static void draw_status(UIState* ui) {
  bool edit = input_held(BTN_OK);
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

  if (THEME_HIDE_HELP)
    return;

  // Bottom hint bar
  DrawRectangle(0, WIN_H - STATUS_H, WIN_W, STATUS_H, C_BG_ALT);
  DrawLine(0, WIN_H - STATUS_H, WIN_W, WIN_H - STATUS_H, C_SEP);
  const char* hint;
  if (edit) {
    switch (ui->screen) {
      case SCREEN_SONG:
        hint = "{OK}+{UP}/{DOWN}: pattern# +-1  {OK}+{LEFT}/{RIGHT}: +-16  {OK}+{NO}: clear cell";
        break;
      case SCREEN_PATTERN:
        switch (ui->pattern_col) {
          case 0:
            hint = "{OK}+{UP}/{DOWN}: note semitone  {OK}+{LEFT}/{RIGHT}: octave  {OK}+{NO}: note-off  {OK}+{SCREEN}+{NO}: clear";
            break;
          case 1:
            hint = "{OK}+{UP}/{DOWN}: velocity +-1   {OK}+{LEFT}/{RIGHT}: +-16";
            break;
          case 2:
            hint = "{OK}+{UP}/{DOWN}: instrument# +-1   {OK}+{LEFT}/{RIGHT}: +-16   {NO}: reset to 0";
            break;
          default:
            hint = "{OK}+{UP}/{DOWN}: fx value  {OK}+{LEFT}/{RIGHT}: coarse";
            break;
        }
        break;
      case SCREEN_INSTRUMENT:
        hint = "{OK}+{UP}/{DOWN}: value +-fine   {OK}+{LEFT}/{RIGHT}: coarse";
        break;
      case SCREEN_MENU:
        hint = "{OK}+{UP}/{DOWN}: change value";
        break;
    }
  } else if (ui->screen == SCREEN_PATTERN) {
    hint = "hold{OK}+dpad: edit   {NO}: clear/back   {PREV} prev  {NEXT} next   {PLAY}: play/stop";
  } else if (ui->screen == SCREEN_INSTRUMENT) {
    hint = "hold{OK}+dpad: edit   {NO}: clear/back   {PREV} prev  {NEXT} next   {PLAY}: play/stop";
  } else if (ui->screen == SCREEN_SONG) {
    hint = "hold{OK}+dpad: edit   hold{PLAY}+dpad: mute   {NO}: clear   {PLAY}: play/stop";
  } else {
    hint = "hold{OK}+dpad: edit   {NO}: clear/back   {PLAY}: play/stop";
  }
  hint_draw(hint, 4, WIN_H - STATUS_H + (STATUS_H - (FONT_S - 1)) / 2, FONT_S - 1, C_DIM);
}

// ---- Shared keyboard modal -------------------------------------------------

static const char* KBM_CHARS[KBM_CHAR_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM._",
};
static const int KBM_CHAR_COLS[KBM_CHAR_ROWS] = {10, 10, 10, 9};

// Special row layout: SHIFT(0) SPACE(1) DEL(2) [SUGGEST(3)] OK(last)
static int kbm_ok_col(const KBModal* kb) {
  return kb->suggest_words > 0 ? 4 : 3;
}

static int kbm_max_col(const KBModal* kb, int row) {
  return (row < KBM_CHAR_ROWS) ? KBM_CHAR_COLS[row] : kbm_ok_col(kb) + 1;
}

static void kbm_suggest(KBModal* kb) {
  kb->buf[0] = '\0';
  for (int i = 0; i < kb->suggest_words; i++) {
    if (i)
      strncat(kb->buf, "-", kb->buf_sz - strlen(kb->buf) - 1);
    strncat(kb->buf, bip39_en[GetRandomValue(0, 2047)], kb->buf_sz - strlen(kb->buf) - 1);
  }
}

void kb_modal_open(KBModal* kb, char* buf, int buf_sz, int suggest_words) {
  kb->buf = buf;
  kb->buf_sz = buf_sz;
  kb->suggest_words = suggest_words;
  kb->row = KBM_SPECIAL_ROW;
  kb->col = suggest_words > 0 ? 3 : kbm_ok_col(kb);  // SUGGEST (if any) else OK preselected
  kb->shift = false;
  kb->active = true;
  kb->confirmed = false;
  while (GetCharPressed() > 0) {
  }
}

bool kb_modal_update(KBModal* kb) {
  if (!kb->active)
    return true;

  if (ui_repeat(BTN_LEFT)) {
    kb->col--;
    if (kb->col < 0)
      kb->col = kbm_max_col(kb, kb->row) - 1;
  }
  if (ui_repeat(BTN_RIGHT)) {
    kb->col++;
    if (kb->col >= kbm_max_col(kb, kb->row))
      kb->col = 0;
  }
  if (ui_repeat(BTN_UP)) {
    kb->row--;
    if (kb->row < 0)
      kb->row = KBM_TOTAL_ROWS - 1;
    if (kb->col >= kbm_max_col(kb, kb->row))
      kb->col = kbm_max_col(kb, kb->row) - 1;
  }
  if (ui_repeat(BTN_DOWN)) {
    kb->row++;
    if (kb->row >= KBM_TOTAL_ROWS)
      kb->row = 0;
    if (kb->col >= kbm_max_col(kb, kb->row))
      kb->col = kbm_max_col(kb, kb->row) - 1;
  }

  if (input_pressed(BTN_OK)) {
    while (GetCharPressed() > 0) {
    }
    if (kb->row < KBM_CHAR_ROWS) {
      char c = KBM_CHARS[kb->row][kb->col];
      if (!kb->shift && c >= 'A' && c <= 'Z')
        c = (char)(c + 32);
      size_t l = strlen(kb->buf);
      if (l < (size_t)(kb->buf_sz - 2)) {
        kb->buf[l] = c;
        kb->buf[l + 1] = '\0';
      }
    } else if (kb->col == 0) {
      kb->shift = !kb->shift;
    } else if (kb->col == 1) {
      size_t l = strlen(kb->buf);
      if (l < (size_t)(kb->buf_sz - 2)) {
        kb->buf[l] = ' ';
        kb->buf[l + 1] = '\0';
      }
    } else if (kb->col == 2) {
      size_t l = strlen(kb->buf);
      if (l)
        kb->buf[l - 1] = '\0';
    } else if (kb->suggest_words > 0 && kb->col == 3) {
      kbm_suggest(kb);
    } else if (kb->col == kbm_ok_col(kb)) {
      kb->active = false;
      kb->confirmed = true;
      return true;
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
      kb->confirmed = true;
      return true;
    }
  }

  if (input_pressed(BTN_NO)) {
    kb->active = false;
    kb->confirmed = false;
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
  DrawRectangle(0, name_y, WIN_W, CH_H + 2, C_FB_INPUT_BG);
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
      bool is_letter = raw >= 'A' && raw <= 'Z';
      char lbl[2] = {(!kb->shift && is_letter) ? (char)(raw + 32) : raw, 0};
      int tw = MeasureText(lbl, FONT_S);
      DrawText(lbl, x + (KBM_KEY_W - tw) / 2, y + (KBM_KEY_H - FONT_S) / 2, FONT_S,
               cur ? C_TITLE : C_TEXT);
    }
  }

  bool has_suggest = kb->suggest_words > 0;
  int sy = y0 + KBM_CHAR_ROWS * (KBM_KEY_H + KBM_GAP);
  int sh_x = 8, sh_w = 56;
  int sp_x = sh_x + sh_w + 2, sp_w = 88;
  int del_x = sp_x + sp_w + 2, del_w = 56;
  int sug_x = del_x + del_w + 2, sug_w = has_suggest ? 108 : 0;
  int ok_x = sug_x + sug_w + (has_suggest ? 2 : 0), ok_w = WIN_W - ok_x - 8;

  bool sh_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 0);
  bool sp_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 1);
  bool del_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == 2);
  bool sug_cur = has_suggest && (kb->row == KBM_SPECIAL_ROW && kb->col == 3);
  bool ok_cur = (kb->row == KBM_SPECIAL_ROW && kb->col == kbm_ok_col(kb));

  Color sh_bg = kb->shift ? (Color){0x60, 0x40, 0x00, 0xFF} : key_bg;
  DrawRectangle(sh_x, sy, sh_w, KBM_KEY_H, sh_cur ? key_cur : sh_bg);
  DrawRectangle(sp_x, sy, sp_w, KBM_KEY_H, sp_cur ? key_cur : key_bg);
  DrawRectangle(del_x, sy, del_w, KBM_KEY_H, del_cur ? key_cur : key_bg);
  if (has_suggest)
    DrawRectangle(sug_x, sy, sug_w, KBM_KEY_H, sug_cur ? key_cur : (Color){0x00, 0x28, 0x40, 0xFF});
  DrawRectangle(ok_x, sy, ok_w, KBM_KEY_H, ok_cur ? key_cur : key_bg);

  Color sh_txt = sh_cur ? C_TITLE : (kb->shift ? (Color){0xFF, 0xC0, 0x00, 0xFF} : C_TEXT);
  DrawText("SHIFT", sh_x + (sh_w - MeasureText("SHIFT", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, sh_txt);
  DrawText("SPACE", sp_x + (sp_w - MeasureText("SPACE", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, sp_cur ? C_TITLE : C_TEXT);
  DrawText("DEL", del_x + (del_w - MeasureText("DEL", FONT_S)) / 2,
           sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, del_cur ? C_NOTE_OFF : C_TEXT);
  if (has_suggest)
    DrawText("SUGGEST", sug_x + (sug_w - MeasureText("SUGGEST", FONT_S)) / 2,
             sy + (KBM_KEY_H - FONT_S) / 2, FONT_S, sug_cur ? C_TITLE : (Color){0x40, 0xC0, 0xFF, 0xFF});
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
