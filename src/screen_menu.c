#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_browser.h"
#include "tracker.h"
#include "ui.h"

typedef enum { MENU_FB_NONE,
               MENU_FB_LOAD,
               MENU_FB_SAVE,
               MENU_FB_EXPORT,
               MENU_FB_THEME } MenuFileBrowserMode;
static MenuFileBrowserMode g_fb_mode = MENU_FB_NONE;

#define MENU_CONTENT_Y (STATUS_H + 2)

typedef enum {
  MENU_BPM = 0,
  MENU_LEN,
  MENU_SCALE_ROOT,
  MENU_SCALE,
  MENU_LOOP,
  MENU_PATLOOP,
  MENU_PREVIEW,
  MENU_SAVE,
  MENU_EXPORT,
  MENU_LOAD,
  MENU_NEW,
  MENU_THEME,
#ifndef __EMSCRIPTEN__
  MENU_FULLSCREEN,
  MENU_EXIT,
#endif
  MENU_COUNT,
} MenuItem;

static const char* menu_labels[] = {
    "BPM",
    "LEN",
    "KEY",
    "SCALE",
    "LOOP",
    "PAT-LOOP",
    "PREVIEW",
    "SAVE",
    "EXPORT WAV",
    "LOAD",
    "NEW",
    "THEME",
#ifndef __EMSCRIPTEN__
    "FULLSCREEN",
    "EXIT",
#endif
};

static char status_msg[48] = "";
static int status_timer = 0;

// Save (and export) go through the same dir-then-name file browser dialog
// patterns/instruments use, so the chosen filename becomes the song's name —
// there's no separate rename step.
static void set_song_name_from_path(TrackerSong* song, const char* path) {
  strncpy(song->name, GetFileName(path), sizeof(song->name) - 1);
  song->name[sizeof(song->name) - 1] = '\0';
  strip_ext(song->name, ".rpt");
}

// Default filename offered by a save/export dialog: the song's name with the
// given extension (".rpt", ".wav"), falling back to "song".
static void song_filename(const TrackerSong* song, const char* ext, char* out, size_t sz) {
  const char* n = (song->name[0] && strcmp(song->name, "UNTITLED") != 0) ? song->name : "song";
  char base[64];
  snprintf(base, sizeof(base), "%s", n);
  strip_ext(base, ".rpt");
  snprintf(out, sz, "%s%s", base, ext);
}

void screen_menu_update(UIState* ui) {
  bool edit = input_held(BTN_OK);

  if (status_timer > 0)
    status_timer--;

  // Poll file browser result
  const char* fb_path = file_browser_poll();
  if (fb_path) {
    if (g_fb_mode == MENU_FB_LOAD) {
      audio_stop(ui->engine);
      TrackerSong* tmp = calloc(1, sizeof(TrackerSong));  // zeroed: tracker_load inits it
      if (tmp && tracker_load(tmp, fb_path)) {
        tracker_free_patterns(ui->song);  // release the old song's patterns
        *ui->song = *tmp;                 // takes ownership of tmp's patterns
        audio_set_save_dir(ui->engine, fb_path);
        snprintf(status_msg, sizeof(status_msg), "LOADED");
      } else {
        if (tmp)
          tracker_free_patterns(tmp);
        snprintf(status_msg, sizeof(status_msg), "LOAD FAILED");
      }
      free(tmp);
      status_timer = 180;
    } else if (g_fb_mode == MENU_FB_SAVE) {
      set_song_name_from_path(ui->song, fb_path);
      bool ok = tracker_save(ui->song, fb_path);
      if (ok) {
        audio_set_save_dir(ui->engine, fb_path);
        file_browser_download(fb_path, GetFileName(fb_path));
      }
      snprintf(status_msg, sizeof(status_msg), ok ? "SAVED" : "SAVE FAILED");
      status_timer = 180;
    } else if (g_fb_mode == MENU_FB_EXPORT) {
      bool ok = audio_render_wav(ui->engine, fb_path);
      if (ok)
        file_browser_download(fb_path, GetFileName(fb_path));
      snprintf(status_msg, sizeof(status_msg), ok ? "EXPORTED WAV" : "EXPORT FAILED");
      status_timer = 180;
    } else if (g_fb_mode == MENU_FB_THEME) {
      bool ok = theme_load(fb_path);
      snprintf(status_msg, sizeof(status_msg), ok ? "THEME LOADED" : "THEME LOAD FAILED");
      status_timer = 180;
    }
    g_fb_mode = MENU_FB_NONE;
    return;
  }

  if (file_browser_active())
    return;

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->menu_row > 0)
      ui->menu_row--;
    if (ui_repeat(BTN_DOWN) && ui->menu_row < MENU_COUNT - 1)
      ui->menu_row++;
  } else {
    switch (ui->menu_row) {
      case MENU_BPM:
        if (ui_repeat(BTN_UP) && ui->song->bpm < 999)
          ui->song->bpm++;
        if (ui_repeat(BTN_DOWN) && ui->song->bpm > 1)
          ui->song->bpm--;
        if (ui_repeat(BTN_RIGHT) && ui->song->bpm <= 989)
          ui->song->bpm += 10;
        if (ui_repeat(BTN_LEFT) && ui->song->bpm > 10)
          ui->song->bpm -= 10;
        else if (ui_repeat(BTN_LEFT) && ui->song->bpm > 1)
          ui->song->bpm = 1;
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
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_OK))
          ui->song->loop = !ui->song->loop;
        break;

      case MENU_PATLOOP:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_OK)) {
          audio_lock(ui->engine);
          ui->engine->pat_loop = !ui->engine->pat_loop;
          audio_unlock(ui->engine);
        }
        break;

      case MENU_PREVIEW:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_OK))
          g_preview_disabled = !g_preview_disabled;
        break;

      case MENU_SAVE:
        if (input_pressed(BTN_OK)) {
          char fname[64];
          song_filename(ui->song, ".rpt", fname, sizeof(fname));
          g_fb_mode = MENU_FB_SAVE;
          file_browser_save_as("Save song", fname);
        }
        break;

      case MENU_EXPORT:
        if (input_pressed(BTN_OK)) {
          char fname[64];
          song_filename(ui->song, ".wav", fname, sizeof(fname));
          g_fb_mode = MENU_FB_EXPORT;
          file_browser_save_as("Export WAV", fname);
        }
        break;

      case MENU_LOAD:
        if (input_pressed(BTN_OK)) {
          g_fb_mode = MENU_FB_LOAD;
          file_browser_open("Load song", "*.rpt");
        }
        break;

      case MENU_NEW:
        if (input_pressed(BTN_OK)) {
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

      case MENU_THEME:
        if (input_pressed(BTN_OK)) {
          g_fb_mode = MENU_FB_THEME;
          file_browser_open("Load theme", "*.ptt");
        }
        break;
#ifndef __EMSCRIPTEN__
      case MENU_FULLSCREEN:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_OK))
          ToggleFullscreen();
        break;
      case MENU_EXIT:
        if (input_pressed(BTN_OK))
          exit(0);
        break;
#endif
    }
  }
}

void screen_menu_draw(UIState* ui) {
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
      case MENU_LEN: {
        int total_sec = (int)(tracker_song_length_seconds(ui->song) + 0.5f);
        snprintf(val, sizeof(val), "%02d:%02d", (total_sec / 60) % 60, total_sec % 60);
        DrawText(val, 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      }
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
      case MENU_PATLOOP:
        DrawText(ui->engine->pat_loop ? "ON" : "OFF",
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_PREVIEW:
        DrawText(g_preview_disabled ? "OFF" : "ON",
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SAVE:
      case MENU_EXPORT:
      case MENU_LOAD:
      case MENU_THEME:
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
  if (status_timer > 0) {
    int y = WIN_H - STATUS_H;
    Color col = (strncmp(status_msg, "SAVE", 4) == 0 || strncmp(status_msg, "LOAD", 4) == 0 ||
                 strncmp(status_msg, "NEW", 3) == 0 || strncmp(status_msg, "EXPORTED", 8) == 0)
                    ? C_PLAY
                    : C_NOTE_OFF;
    DrawRectangle(0, y, WIN_W, STATUS_H, C_BG_ALT);
    DrawLine(0, y, WIN_W, y, C_SEP);
    DrawText(status_msg, 4, y + (STATUS_H - FONT_S) / 2, FONT_S, col);
  }
}
