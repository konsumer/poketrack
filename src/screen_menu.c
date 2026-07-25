#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_browser.h"
#include "tracker.h"
#include "ui.h"

typedef enum { MENU_FB_NONE,
               MENU_FB_LOAD,
               MENU_FB_SAVE,
               MENU_FB_EXPORT } MenuFileBrowserMode;
static MenuFileBrowserMode g_fb_mode = MENU_FB_NONE;

#define MENU_CONTENT_Y (STATUS_H + 2)

typedef enum {
  MENU_BPM = 0,
  MENU_SCALE_ROOT,
  MENU_SCALE,
  MENU_LOOP,
  MENU_PREVIEW,
  MENU_SAVE,
  MENU_EXPORT,
  MENU_LOAD,
  MENU_NEW,
#ifndef __EMSCRIPTEN__
  MENU_FULLSCREEN,
  MENU_EXIT,
#endif
  MENU_COUNT,
} MenuItem;

static const char* menu_labels[] = {
    "BPM",
    "KEY",
    "SCALE",
    "LOOP",
    "PREVIEW",
    "SAVE",
    "EXPORT WAV",
    "LOAD",
    "NEW",
#ifndef __EMSCRIPTEN__
    "FULLSCREEN",
    "EXIT",
#endif
};

static char status_msg[48] = "";
static int status_timer = 0;

// Last path component, e.g. "/foo/bar/song.rpt" -> "song.rpt"
static const char* path_basename(const char* path) {
  const char* base = strrchr(path, '/');
  return base ? base + 1 : path;
}

// Save (and export) go through the same dir-then-name file browser dialog
// patterns/instruments use, so the chosen filename becomes the song's name —
// there's no separate rename step.
static void set_song_name_from_path(TrackerSong* song, const char* path) {
  strncpy(song->name, path_basename(path), sizeof(song->name) - 1);
  song->name[sizeof(song->name) - 1] = '\0';
  size_t l = strlen(song->name);
  if (l >= 4 && strcasecmp(song->name + l - 4, ".rpt") == 0)
    song->name[l - 4] = '\0';
}

// Build save filename from song name, defaulting to "song.rpt"
static void song_save_path(const TrackerSong* song, char* out, size_t sz) {
  const char* n = (song->name[0] && strcmp(song->name, "UNTITLED") != 0) ? song->name : "song";
  size_t nl = strlen(n);
  if (nl >= 4 && strcasecmp(n + nl - 4, ".rpt") == 0)
    snprintf(out, sz, "%s", n);
  else
    snprintf(out, sz, "%s.rpt", n);
}

// Build export filename from song name, defaulting to "song.wav"
static void song_export_path(const TrackerSong* song, char* out, size_t sz) {
  const char* n = (song->name[0] && strcmp(song->name, "UNTITLED") != 0) ? song->name : "song";
  char base[64];
  snprintf(base, sizeof(base), "%s", n);
  size_t bl = strlen(base);
  // Strip a trailing .rpt so we don't produce "song.rpt.wav"
  if (bl >= 4 && strcasecmp(base + bl - 4, ".rpt") == 0)
    base[bl - 4] = '\0';
  snprintf(out, sz, "%s.wav", base);
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
        file_browser_download(fb_path, path_basename(fb_path));
      }
      snprintf(status_msg, sizeof(status_msg), ok ? "SAVED" : "SAVE FAILED");
      status_timer = 180;
    } else if (g_fb_mode == MENU_FB_EXPORT) {
      bool ok = audio_render_wav(ui->engine, fb_path);
      if (ok)
        file_browser_download(fb_path, path_basename(fb_path));
      snprintf(status_msg, sizeof(status_msg), ok ? "EXPORTED WAV" : "EXPORT FAILED");
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

      case MENU_PREVIEW:
        if (input_pressed(BTN_UP) || input_pressed(BTN_DOWN) || input_pressed(BTN_OK))
          g_preview_disabled = !g_preview_disabled;
        break;

      case MENU_SAVE:
        if (input_pressed(BTN_OK)) {
          char fname[64];
          song_save_path(ui->song, fname, sizeof(fname));
          g_fb_mode = MENU_FB_SAVE;
          file_browser_save_as("Save song", fname);
        }
        break;

      case MENU_EXPORT:
        if (input_pressed(BTN_OK)) {
          char fname[64];
          song_export_path(ui->song, fname, sizeof(fname));
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
      case MENU_PREVIEW:
        DrawText(g_preview_disabled ? "OFF" : "ON",
                 100, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_TEXT);
        break;
      case MENU_SAVE:
      case MENU_EXPORT:
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
