#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bip39_en.h"
#include "file_browser.h"
#include "tracker.h"
#include "ui.h"

typedef enum { MENU_FB_NONE,
               MENU_FB_LOAD } MenuFileBrowserMode;
static MenuFileBrowserMode g_fb_mode = MENU_FB_NONE;

#define MENU_CONTENT_Y (STATUS_H + 2)

typedef enum {
  MENU_BPM = 0,
  MENU_SCALE_ROOT,
  MENU_SCALE,
  MENU_LOOP,
  MENU_SONG_NAME,
  MENU_SAVE,
  MENU_LOAD,
  MENU_NEW,
#ifndef __EMSCRIPTEN__
  MENU_FULLSCREEN,
  MENU_EXIT,
#endif
  MENU_COUNT,
} MenuItem;

static const char *menu_labels[] = {
    "BPM",
    "KEY",
    "SCALE",
    "LOOP",
    "NAME",
    "SAVE",
    "LOAD",
    "NEW",
#ifndef __EMSCRIPTEN__
    "FULLSCREEN",
    "EXIT",
#endif
};

// On-screen keyboard
#define KB_KEY_W     44
#define KB_KEY_H     18
#define KB_GAP       2
#define KB_CHAR_ROWS 4

static const char *KB_CHARS[KB_CHAR_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM._",
};
static const int KB_CHAR_COLS[KB_CHAR_ROWS] = {10, 10, 10, 9};

// Row 4 = special: 0=SHIFT 1=SPACE 2=DEL 3=SUGGEST 4=OK
#define KB_SPECIAL_ROW  4
#define KB_TOTAL_ROWS   5
#define KB_SPECIAL_COLS 5

static char status_msg[48] = "";
static int  status_timer   = 0;
static bool g_name_editing = false;
static int  g_kb_row       = KB_SPECIAL_ROW;
static int  g_kb_col       = 3;  // SUGGEST preselected
static bool g_kb_shift     = false;

static int kb_max_col(int row) {
  return (row < KB_CHAR_ROWS) ? KB_CHAR_COLS[row] : KB_SPECIAL_COLS;
}

static void enter_name_editing(TrackerSong *song) {
  if (!song->name[0])
    strncpy(song->name, "song", sizeof(song->name) - 1);
  g_name_editing = true;
  g_kb_row       = KB_SPECIAL_ROW;
  g_kb_col       = 3;  // SUGGEST
  while (GetCharPressed() > 0) {}
}

static void name_suggest(TrackerSong *song) {
  static bool seeded = false;
  if (!seeded) { srand((unsigned int)time(NULL)); seeded = true; }
  int a = rand() % 2048, b = rand() % 2048;
  snprintf(song->name, sizeof(song->name), "%s-%s", bip39_en[a], bip39_en[b]);
}

// Build save filename from song name, defaulting to "song.rpt"
static void song_save_path(const TrackerSong *song, char *out, size_t sz) {
  const char *n = (song->name[0] && strcmp(song->name, "UNTITLED") != 0) ? song->name : "song";
  size_t nl = strlen(n);
  if (nl >= 4 && strcasecmp(n + nl - 4, ".rpt") == 0)
    snprintf(out, sz, "%s", n);
  else
    snprintf(out, sz, "%s.rpt", n);
}

static void name_append(TrackerSong *song, char c) {
  size_t l = strlen(song->name);
  if (l < sizeof(song->name) - 2) {
    song->name[l]     = c;
    song->name[l + 1] = '\0';
  }
}

static void name_backspace(TrackerSong *song) {
  size_t l = strlen(song->name);
  if (l)
    song->name[l - 1] = '\0';
}

void screen_menu_update(UIState *ui) {
  bool edit = input_held(BTN_A);

  if (status_timer > 0)
    status_timer--;

  // Poll file browser result
  const char *fb_path = file_browser_poll();
  if (fb_path) {
    if (g_fb_mode == MENU_FB_LOAD) {
      g_fb_mode = MENU_FB_NONE;
      audio_stop(ui->engine);
      TrackerSong *tmp = malloc(sizeof(TrackerSong));
      if (tmp && tracker_load(tmp, fb_path)) {
        *ui->song = *tmp;
        audio_set_save_dir(ui->engine, fb_path);
        snprintf(status_msg, sizeof(status_msg), "LOADED");
      } else {
        snprintf(status_msg, sizeof(status_msg), "LOAD FAILED");
      }
      free(tmp);
      status_timer = 180;
    }
    return;
  }

  if (file_browser_active())
    return;

  // Name editing mode: on-screen keyboard + physical keyboard
  if (g_name_editing) {
    // On-screen keyboard navigation
    if (ui_repeat(BTN_LEFT)) {
      g_kb_col--;
      if (g_kb_col < 0)
        g_kb_col = kb_max_col(g_kb_row) - 1;
    }
    if (ui_repeat(BTN_RIGHT)) {
      g_kb_col++;
      if (g_kb_col >= kb_max_col(g_kb_row))
        g_kb_col = 0;
    }
    if (ui_repeat(BTN_UP)) {
      g_kb_row--;
      if (g_kb_row < 0)
        g_kb_row = KB_TOTAL_ROWS - 1;
      if (g_kb_col >= kb_max_col(g_kb_row))
        g_kb_col = kb_max_col(g_kb_row) - 1;
    }
    if (ui_repeat(BTN_DOWN)) {
      g_kb_row++;
      if (g_kb_row >= KB_TOTAL_ROWS)
        g_kb_row = 0;
      if (g_kb_col >= kb_max_col(g_kb_row))
        g_kb_col = kb_max_col(g_kb_row) - 1;
    }

    // BTN_A: type selected on-screen key; flush char queue so the keypress doesn't also type
    if (input_pressed(BTN_A)) {
      while (GetCharPressed() > 0) {}
      if (g_kb_row < KB_CHAR_ROWS) {
        char c = KB_CHARS[g_kb_row][g_kb_col];
        name_append(ui->song, g_kb_shift ? c : (char)(c | 0x20));
      } else {
        switch (g_kb_col) {
          case 0: g_kb_shift = !g_kb_shift;  break;
          case 1: name_append(ui->song, ' '); break;
          case 2: name_backspace(ui->song);   break;
          case 3: name_suggest(ui->song);     break;
          case 4: g_name_editing = false;     break;
        }
      }
    } else {
      // Physical keyboard input (only when BTN_A not pressed)
      int ch;
      while ((ch = GetCharPressed()) > 0) {
        if (ch >= 32)
          name_append(ui->song, (char)ch);
      }
      if (IsKeyPressed(KEY_BACKSPACE))
        name_backspace(ui->song);
      if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        g_name_editing = false;
        return;
      }
    }

    // BTN_B: exit name editing
    if (input_pressed(BTN_B))
      g_name_editing = false;

    return;
  }

  // BTN_Y anywhere on menu screen enters name editing (works with or without A held)
  if (input_pressed(BTN_Y)) {
    enter_name_editing(ui->song);
    return;
  }

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->menu_row > 0)
      ui->menu_row--;
    if (ui_repeat(BTN_DOWN) && ui->menu_row < MENU_COUNT - 1)
      ui->menu_row++;
  } else {
    switch (ui->menu_row) {
      case MENU_SONG_NAME:
        if (input_pressed(BTN_A))
          enter_name_editing(ui->song);
        break;

      case MENU_BPM:
        if (ui_repeat(BTN_UP) && ui->song->bpm < 250)
          ui->song->bpm++;
        if (ui_repeat(BTN_DOWN) && ui->song->bpm > 40)
          ui->song->bpm--;
        if (ui_repeat(BTN_RIGHT) && ui->song->bpm <= 240)
          ui->song->bpm += 10;
        if (ui_repeat(BTN_LEFT) && ui->song->bpm >= 50)
          ui->song->bpm -= 10;
        break;

      case MENU_SCALE_ROOT:
        if (ui_repeat(BTN_UP))
          ui->song->scale_root = (ui->song->scale_root + 1) % 12;
        if (ui_repeat(BTN_DOWN))
          ui->song->scale_root = (ui->song->scale_root + 11) % 12;
        break;

      case MENU_SCALE:
        if (ui_repeat(BTN_UP))
          ui->song->scale_idx = (ui->song->scale_idx + 1) % NUM_SCALES;
        if (ui_repeat(BTN_DOWN))
          ui->song->scale_idx = (ui->song->scale_idx + NUM_SCALES - 1) % NUM_SCALES;
        break;

      case MENU_LOOP:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_A))
          ui->song->loop = !ui->song->loop;
        break;

      case MENU_SAVE: {
        if (input_pressed(BTN_A)) {
          char fname[64], path[576];
          song_save_path(ui->song, fname, sizeof(fname));
          snprintf(path, sizeof(path), "%s%s", ui->engine->save_dir, fname);
          bool ok = tracker_save(ui->song, path);
          if (ok) {
            audio_set_save_dir(ui->engine, path);
            file_browser_download(path, fname);
          }
          snprintf(status_msg, sizeof(status_msg), ok ? "SAVED" : "SAVE FAILED");
          status_timer = 180;
        }
        break;
      }

      case MENU_LOAD:
        if (input_pressed(BTN_A)) {
          g_fb_mode = MENU_FB_LOAD;
          file_browser_open("Load song", "*.rpt");
        }
        break;

      case MENU_NEW:
        if (input_pressed(BTN_A)) {
          audio_stop(ui->engine);
          tracker_clear(ui->song);
          ui->song_row = ui->song_col = 0;
          ui->pattern_row = ui->pattern_col = 0;
          ui->inst_row = ui->song_scroll = ui->song_col_scroll = 0;
          ui->ctx_pattern = ui->ctx_instrument = 0;
          snprintf(status_msg, sizeof(status_msg), "NEW SONG");
          status_timer = 120;
        }
        break;
#ifndef __EMSCRIPTEN__
      case MENU_FULLSCREEN:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_A))
          ToggleFullscreen();
        break;
      case MENU_EXIT:
        if (input_pressed(BTN_A))
          exit(0);
        break;
#endif
    }
  }
}

static void draw_keyboard(const TrackerSong *song) {
  // Full modal: cover everything below top status bar
  int modal_y = STATUS_H;
  int modal_h = WIN_H - STATUS_H * 2;
  DrawRectangle(0, modal_y, WIN_W, modal_h, (Color){0x00, 0x00, 0x10, 0xFF});
  DrawLine(0, modal_y, WIN_W, modal_y, C_SEP);

  // Name field at top of modal
  int name_y = modal_y + 4;
  DrawRectangle(0, name_y, WIN_W, CH_H + 2, (Color){0x08, 0x08, 0x28, 0xFF});
  DrawText("NAME:", 4, name_y + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  const char *nm = song->name[0] ? song->name : "";
  int nx = 44;
  DrawText(nm, nx, name_y + (CH_H - FONT_S) / 2, FONT_S, C_TITLE);
  // Cursor blink
  double t = GetTime();
  if ((t - (int)t) < 0.5) {
    int cx = nx + MeasureText(nm, FONT_S);
    DrawRectangle(cx, name_y + 2, 1, FONT_S + 1, C_TITLE);
  }

  // Keyboard grid
  int kb_top  = name_y + CH_H + 6;
  int avail_h = WIN_H - STATUS_H - kb_top;
  int kb_h    = KB_TOTAL_ROWS * (KB_KEY_H + KB_GAP);
  int y0      = kb_top + (avail_h - kb_h) / 2;

  Color key_bg  = {0x28, 0x28, 0x50, 0xFF};
  Color key_cur = {0x20, 0x60, 0xC0, 0xFF};

  for (int r = 0; r < KB_CHAR_ROWS; r++) {
    int ncols   = KB_CHAR_COLS[r];
    int total_w = ncols * KB_KEY_W + (ncols - 1) * KB_GAP;
    int sx      = (WIN_W - total_w) / 2;
    int y       = y0 + r * (KB_KEY_H + KB_GAP);
    for (int c = 0; c < ncols; c++) {
      int  x   = sx + c * (KB_KEY_W + KB_GAP);
      bool cur = (g_kb_row == r && g_kb_col == c);
      DrawRectangle(x, y, KB_KEY_W, KB_KEY_H, cur ? key_cur : key_bg);
      char raw = KB_CHARS[r][c];
      char label[2] = {g_kb_shift ? raw : (char)(raw | 0x20), 0};
      int  tw       = MeasureText(label, FONT_S);
      DrawText(label, x + (KB_KEY_W - tw) / 2, y + (KB_KEY_H - FONT_S) / 2, FONT_S,
               cur ? C_TITLE : C_TEXT);
    }
  }

  // Special row: SHIFT | SPACE | DEL | SUGGEST | OK
  int sy     = y0 + KB_CHAR_ROWS * (KB_KEY_H + KB_GAP);
  int sh_x   = 8,              sh_w  = 56;
  int sp_x   = sh_x + sh_w  + 2, sp_w  = 88;
  int del_x  = sp_x + sp_w  + 2, del_w = 56;
  int sug_x  = del_x + del_w + 2, sug_w = 108;
  int ok_x   = sug_x + sug_w + 2, ok_w  = WIN_W - ok_x - 8;

  bool sh_cur  = (g_kb_row == KB_SPECIAL_ROW && g_kb_col == 0);
  bool sp_cur  = (g_kb_row == KB_SPECIAL_ROW && g_kb_col == 1);
  bool del_cur = (g_kb_row == KB_SPECIAL_ROW && g_kb_col == 2);
  bool sug_cur = (g_kb_row == KB_SPECIAL_ROW && g_kb_col == 3);
  bool ok_cur  = (g_kb_row == KB_SPECIAL_ROW && g_kb_col == 4);

  Color sh_bg = g_kb_shift ? (Color){0x60, 0x40, 0x00, 0xFF} : key_bg;
  DrawRectangle(sh_x,  sy, sh_w,  KB_KEY_H, sh_cur  ? key_cur : sh_bg);
  DrawRectangle(sp_x,  sy, sp_w,  KB_KEY_H, sp_cur  ? key_cur : key_bg);
  DrawRectangle(del_x, sy, del_w, KB_KEY_H, del_cur ? key_cur : key_bg);
  DrawRectangle(sug_x, sy, sug_w, KB_KEY_H, sug_cur ? key_cur : (Color){0x00, 0x28, 0x40, 0xFF});
  DrawRectangle(ok_x,  sy, ok_w,  KB_KEY_H, ok_cur  ? key_cur : key_bg);

  Color sh_txt = sh_cur ? C_TITLE : (g_kb_shift ? (Color){0xFF, 0xC0, 0x00, 0xFF} : C_TEXT);
  DrawText("SHIFT",   sh_x  + (sh_w  - MeasureText("SHIFT",   FONT_S)) / 2,
           sy + (KB_KEY_H - FONT_S) / 2, FONT_S, sh_txt);
  DrawText("SPACE",   sp_x  + (sp_w  - MeasureText("SPACE",   FONT_S)) / 2,
           sy + (KB_KEY_H - FONT_S) / 2, FONT_S, sp_cur  ? C_TITLE : C_TEXT);
  DrawText("DEL",     del_x + (del_w - MeasureText("DEL",     FONT_S)) / 2,
           sy + (KB_KEY_H - FONT_S) / 2, FONT_S, del_cur ? C_NOTE_OFF : C_TEXT);
  DrawText("SUGGEST", sug_x + (sug_w - MeasureText("SUGGEST", FONT_S)) / 2,
           sy + (KB_KEY_H - FONT_S) / 2, FONT_S, sug_cur ? C_TITLE : (Color){0x40, 0xC0, 0xFF, 0xFF});
  DrawText("OK",      ok_x  + (ok_w  - MeasureText("OK",      FONT_S)) / 2,
           sy + (KB_KEY_H - FONT_S) / 2, FONT_S, ok_cur  ? C_PLAY : C_TEXT);
}

void screen_menu_draw(UIState *ui) {
  if (g_name_editing) {
    draw_keyboard(ui->song);
    return;
  }

  for (int i = 0; i < MENU_COUNT; i++) {
    int  y   = MENU_CONTENT_Y + i * (CH_H + 2);
    bool cur = (i == ui->menu_row);
    DrawRectangle(0, y, WIN_W, CH_H, cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
    DrawText(menu_labels[i], 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);

    char val[64] = "";
    switch (i) {
      case MENU_SONG_NAME: {
        int  nx      = 60;
        bool editing = cur && g_name_editing;
        const char *nm = ui->song->name[0] ? ui->song->name : "song";
        DrawRectangle(nx, y, WIN_W - nx - 4, CH_H,
                      editing ? C_CURSOR2 : (Color){0, 0, 0, 0});
        DrawText(nm, nx + 2, y + (CH_H - FONT_S) / 2, FONT_S,
                 editing ? C_TITLE : (cur ? C_NOTE : C_TEXT));
        if (editing) {
          double t  = GetTime();
          if ((t - (int)t) < 0.5) {
            int cx = nx + 2 + MeasureText(nm, FONT_S);
            DrawRectangle(cx, y + 2, 1, FONT_S + 1, C_TITLE);
          }
        } else if (cur) {
          DrawText("[Y=edit]", WIN_W - 56, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
        }
        break;
      }
      case MENU_BPM:
        snprintf(val, sizeof(val), "%d", ui->song->bpm);
        DrawText(val, 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SCALE_ROOT:
        DrawText(SCALE_ROOT_NAMES[ui->song->scale_root % 12],
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SCALE:
        DrawText(SCALES[ui->song->scale_idx % NUM_SCALES].name,
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_LOOP:
        DrawText(ui->song->loop ? "ON" : "OFF",
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SAVE:
      case MENU_LOAD:
        if (cur)
          DrawText("[holdA+A]", WIN_W - 64, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
        break;
      case MENU_NEW:
        DrawText(cur ? "[holdA+A=confirm]" : "",
                 100, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
        break;
#ifndef __EMSCRIPTEN__
      case MENU_FULLSCREEN:
        DrawText(IsWindowFullscreen() ? "ON" : "OFF",
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
#endif
    }
  }

  // Status message in bottom toolbar
  if (status_timer > 0 && !g_name_editing) {
    int    y   = WIN_H - STATUS_H;
    Color  col = (strncmp(status_msg, "SAVE", 4) == 0 || strncmp(status_msg, "LOAD", 4) == 0 || strncmp(status_msg, "NEW", 3) == 0)
                     ? C_PLAY
                     : C_NOTE_OFF;
    DrawRectangle(0, y, WIN_W, STATUS_H, C_BG_ALT);
    DrawLine(0, y, WIN_W, y, C_SEP);
    DrawText(status_msg, 4, y + (STATUS_H - FONT_S) / 2, FONT_S, col);
  }
}
