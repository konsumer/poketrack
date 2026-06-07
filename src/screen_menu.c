#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_browser.h"
#include "tracker.h"
#include "ui.h"

typedef enum { MENU_FB_NONE, MENU_FB_LOAD, MENU_FB_SAVE } MenuFileBrowserMode;
static MenuFileBrowserMode g_fb_mode = MENU_FB_NONE;

#define MENU_CONTENT_Y (STATUS_H + 2)
#define SONG_FILE "song.rpt"

typedef enum {
  MENU_BPM = 0,
  MENU_SCALE_ROOT,
  MENU_SCALE,
  MENU_LOOP,
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
    "SAVE",
    "LOAD",
    "NEW SONG",
#ifndef __EMSCRIPTEN__
    "FULLSCREEN",
    "EXIT",
#endif
};

static char status_msg[48] = "";
static int status_timer = 0;

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
    } else if (g_fb_mode == MENU_FB_SAVE) {
      g_fb_mode = MENU_FB_NONE;
      bool ok = tracker_save(ui->song, fb_path);
      if (ok) {
        audio_set_save_dir(ui->engine, fb_path);
        file_browser_download(fb_path, "song.rpt");
      }
      snprintf(status_msg, sizeof(status_msg), ok ? "SAVED" : "SAVE FAILED");
      status_timer = 180;
    }
    return;  // don't re-process the A press that confirmed the dialog
  }

  if (file_browser_active()) return;

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->menu_row > 0)
      ui->menu_row--;
    if (ui_repeat(BTN_DOWN) && ui->menu_row < MENU_COUNT - 1)
      ui->menu_row++;
  } else {
    switch (ui->menu_row) {
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

      case MENU_SAVE:
        if (input_pressed(BTN_A)) {
          g_fb_mode = MENU_FB_SAVE;
          file_browser_save_as("Save song", SONG_FILE);
        }
        break;

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

void screen_menu_draw(UIState *ui) {
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = MENU_CONTENT_Y + i * (CH_H + 2);
    bool cur = (i == ui->menu_row);
    DrawRectangle(0, y, WIN_W, CH_H, cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
    DrawText(menu_labels[i], 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);

    char val[64] = "";
    switch (i) {
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
        DrawText(ui->song->loop ? "ON" : "OFF", 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SAVE:
      case MENU_LOAD:
        DrawText(SONG_FILE, 100, y + (CH_H - FONT_S) / 2, FONT_S - 1, cur ? C_DIM : C_DIM);
        if (cur) DrawText("[holdA+A]", WIN_W - 64, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
        break;
      case MENU_NEW:
        DrawText(cur ? "[holdA+A=confirm]" : "", 100, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
        break;
#ifndef __EMSCRIPTEN__
      case MENU_FULLSCREEN:
        DrawText(IsWindowFullscreen() ? "ON" : "OFF", 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
#endif
    }
  }

  // Status message
  if (status_timer > 0) {
    int y = MENU_CONTENT_Y + MENU_COUNT * (CH_H + 2) + CH_H;
    DrawRectangle(0, y, WIN_W, CH_H, C_BG_ALT);
    DrawText(status_msg, 4, y + (CH_H - FONT_S) / 2, FONT_S,
             strncmp(status_msg, "SAVE", 4) == 0 || strncmp(status_msg, "LOAD", 4) == 0 || strncmp(status_msg, "NEW", 3) == 0
                 ? C_PLAY
                 : C_NOTE_OFF);
  }
}
