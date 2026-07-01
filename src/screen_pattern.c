#include <stdio.h>
#include <string.h>

#include "file_browser.h"
#include "tracker.h"
#include "ui.h"
#include "units/unit_registry.h"

static int g_pat_fb_mode = 0;  // 0=none 1=save 2=load

// ── Layout ──────────────────────────────────────────────────────────────────
// Each track shows 7 sub-columns: NOTE VEL INST P1 V1 P2 V2.
#define PX_ROW_W 18  // row-number gutter
#define SW_NOTE 32
#define SW_VEL 22
#define SW_INST 18
#define SW_FXT 17
#define SW_FXV 17
#define TRACK_W (SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV + SW_FXT + SW_FXV)  // 140
#define PT_NCOLS 7                                                                // sub-columns per track

// Two sticky header rows: track numbers, then the column legend.
#define PT_HEADER_ROWS 2
#define PT_CONTENT_Y (STATUS_H + PT_HEADER_ROWS * CH_H + 2)

// x offset of sub-column c within a track
static int sub_x(int c) {
  static const int xs[] = {0, SW_NOTE, SW_NOTE + SW_VEL, SW_NOTE + SW_VEL + SW_INST,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV,
                           SW_NOTE + SW_VEL + SW_INST + SW_FXT + SW_FXV + SW_FXT};
  return xs[c];
}
static int sub_w(int c) {
  static const int ws[] = {SW_NOTE, SW_VEL, SW_INST, SW_FXT, SW_FXV, SW_FXT, SW_FXV};
  return ws[c];
}

// Number of tracks that fit horizontally
static int visible_tracks(void) {
  int v = (WIN_W - PX_ROW_W) / TRACK_W;
  if (v < 1)
    v = 1;
  if (v > PATTERN_TRACKS)
    v = PATTERN_TRACKS;
  return v;
}

// Screen x of the left edge of a track, given the current horizontal scroll
static int track_x(UIState* ui, int tr) {
  return PX_ROW_W + (tr - ui->pattern_track_scroll) * TRACK_W;
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255
                                                          : (uint8_t)v; }

static void preview(UIState* ui, uint8_t note) {
  audio_preview_kill(ui->engine);
  if (note == NOTE_EMPTY || note == NOTE_OFF)
    return;
  Pattern* pat = &ui->song->pattern_data[ui->ctx_pattern];
  uint8_t inst = pat->steps[ui->pattern_track][ui->pattern_row].instrument;
  audio_preview_note(ui->engine, inst, note);
}

// Keep the selected track within the horizontal scroll window
static void ensure_track_visible(UIState* ui) {
  int vt = visible_tracks();
  if (ui->pattern_track < ui->pattern_track_scroll)
    ui->pattern_track_scroll = ui->pattern_track;
  if (ui->pattern_track >= ui->pattern_track_scroll + vt)
    ui->pattern_track_scroll = ui->pattern_track - vt + 1;
  if (ui->pattern_track_scroll < 0)
    ui->pattern_track_scroll = 0;
}

void screen_pattern_update(UIState* ui) {
  Pattern* pat = &ui->song->pattern_data[ui->ctx_pattern];
  bool edit = input_held(BTN_A);

  if (input_released(BTN_A))
    audio_preview_kill(ui->engine);

  // Footer row: pat->len  (cols: 0=+ 1=- 2=SAVE 3=LOAD)
  bool in_footer = (ui->pattern_row == (int)pat->len);

  // Poll file browser
  const char* fb = file_browser_poll();
  if (fb) {
    if (g_pat_fb_mode == 1) {
      char fname[24];
      snprintf(fname, sizeof(fname), "PAT%02X.rptp", (uint8_t)ui->ctx_pattern);
      tracker_save_pattern(pat, fb);
      file_browser_download(fb, fname);
    } else if (g_pat_fb_mode == 2) {
      tracker_load_pattern(pat, fb);
      if (ui->pattern_row >= (int)pat->len)
        ui->pattern_row = (int)pat->len - 1;
    }
    g_pat_fb_mode = 0;
    return;
  }
  if (file_browser_active())
    return;

  // L/R shoulder: cycle patterns (not while editing)
  if (!edit) {
    if (ui_repeat(BTN_R) && ui->ctx_pattern < NUM_PATTERNS - 1) {
      ui->ctx_pattern++;
      pat = &ui->song->pattern_data[ui->ctx_pattern];
      if (ui->pattern_row > (int)pat->len)
        ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
    }
    if (ui_repeat(BTN_L) && ui->ctx_pattern > 0) {
      ui->ctx_pattern--;
      pat = &ui->song->pattern_data[ui->ctx_pattern];
      if (ui->pattern_row > (int)pat->len)
        ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
    }
  }

  // Handle footer before edit check — cols: 0=+ 1=- 2=SAVE 3=LOAD
  if (in_footer) {
    if (ui_repeat(BTN_UP)) {
      ui->pattern_row = pat->len > 0 ? (int)pat->len - 1 : 0;
      return;
    }
    if (ui_repeat(BTN_LEFT)) {
      if (ui->pattern_col > 0)
        ui->pattern_col--;
    }
    if (ui_repeat(BTN_RIGHT)) {
      if (ui->pattern_col < 3)
        ui->pattern_col++;
    }
    if (input_pressed(BTN_A)) {
      if (ui->pattern_col == 0 && pat->len < MAX_PATTERN_STEPS)
        pat->len++;
      else if (ui->pattern_col == 1 && pat->len > 1)
        pat->len--;
      else if (ui->pattern_col == 2) {
        char fname[24];
        snprintf(fname, sizeof(fname), "PAT%02X.rptp", (uint8_t)ui->ctx_pattern);
        g_pat_fb_mode = 1;
        file_browser_save_as("Save pattern", fname);
      } else if (ui->pattern_col == 3) {
        g_pat_fb_mode = 2;
        file_browser_open("Load pattern", "*.rptp");
      }
      if (ui->pattern_col < 2)
        ui->pattern_row = (int)pat->len;
    }
    return;
  }

  PatternStep* step = &pat->steps[ui->pattern_track][ui->pattern_row];

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->pattern_row > 0)
      ui->pattern_row--;
    if (ui_repeat(BTN_DOWN)) {
      if (ui->pattern_row < (int)pat->len - 1)
        ui->pattern_row++;
      else
        ui->pattern_row = (int)pat->len;  // enter resize row
    }
    // LEFT/RIGHT walk sub-columns, crossing into adjacent tracks at the edges.
    if (ui_repeat(BTN_LEFT)) {
      if (ui->pattern_col > 0)
        ui->pattern_col--;
      else if (ui->pattern_track > 0) {
        ui->pattern_track--;
        ui->pattern_col = PT_NCOLS - 1;
      }
      ensure_track_visible(ui);
    }
    if (ui_repeat(BTN_RIGHT)) {
      if (ui->pattern_col < PT_NCOLS - 1)
        ui->pattern_col++;
      else if (ui->pattern_track < PATTERN_TRACKS - 1) {
        ui->pattern_track++;
        ui->pattern_col = 0;
      }
      ensure_track_visible(ui);
    }

    if (input_pressed(BTN_B)) {
      switch (ui->pattern_col) {
        case 0:
          step->note = NOTE_EMPTY;
          break;
        case 1:
          step->velocity = 0xFF;
          break;
        case 2:
          step->instrument = 0;
          break;
        case 3:
          step->fx[0] = TRACKER_EMPTY;
          break;
        case 4:
          step->fxv[0] = 0;
          break;
        case 5:
          step->fx[1] = TRACKER_EMPTY;
          break;
        case 6:
          step->fxv[1] = 0;
          break;
      }
    }

    ui->ctx_instrument = step->instrument;

  } else {
    switch (ui->pattern_col) {
      case 0: {
        uint8_t n = step->note;
        uint8_t si = ui->song->scale_idx % NUM_SCALES;
        uint8_t sr = ui->song->scale_root % 12;
        if (ui_repeat(BTN_UP)) {
          if (n == NOTE_EMPTY || n == NOTE_OFF) {
            uint8_t base = ui->last_note ? ui->last_note - 1 : 59;
            step->note = scale_next_note(base, +1, si, sr);
            if (!step->velocity)
              step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, +1, si, sr);
          }
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_DOWN)) {
          if (n == NOTE_EMPTY || n == NOTE_OFF) {
            uint8_t base = ui->last_note ? ui->last_note + 1 : 61;
            step->note = scale_next_note(base, -1, si, sr);
            if (!step->velocity)
              step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, -1, si, sr);
          }
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_RIGHT) && n != NOTE_EMPTY && n != NOTE_OFF && n + 12 <= 127) {
          step->note += 12;
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_LEFT) && n != NOTE_EMPTY && n != NOTE_OFF && n >= 13) {
          step->note -= 12;
          ui->last_note = step->note;
          preview(ui, step->note);
        }
        if (input_pressed(BTN_B))
          step->note = NOTE_OFF;
        break;
      }
      case 1:
        if (ui_repeat(BTN_UP))
          step->velocity = clamp8(step->velocity + 1);
        if (ui_repeat(BTN_DOWN))
          step->velocity = clamp8((int)step->velocity - 1);
        if (ui_repeat(BTN_RIGHT))
          step->velocity = clamp8(step->velocity + 16);
        if (ui_repeat(BTN_LEFT))
          step->velocity = clamp8((int)step->velocity - 16);
        if (input_pressed(BTN_B))
          step->velocity = 0xFF;
        break;
      case 2:
        if (ui_repeat(BTN_UP) && step->instrument < NUM_INSTRUMENTS - 1)
          step->instrument++;
        if (ui_repeat(BTN_DOWN) && step->instrument > 0)
          step->instrument--;
        if (input_pressed(BTN_B))
          step->instrument = 0;
        ui->ctx_instrument = step->instrument;
        break;
      case 3: {
        uint8_t* f = &step->fx[0];
        if (ui_repeat(BTN_UP))
          *f = (*f == TRACKER_EMPTY) ? 0 : (*f < 0xFE ? *f + 1 : TRACKER_EMPTY);
        if (ui_repeat(BTN_DOWN))
          *f = (*f == TRACKER_EMPTY || *f == 0) ? TRACKER_EMPTY : *f - 1;
        if (ui_repeat(BTN_RIGHT))
          *f = (*f == TRACKER_EMPTY) ? 0 : (*f + 16 > 0xFE ? 0xFE : *f + 16);
        if (ui_repeat(BTN_LEFT))
          *f = (*f == TRACKER_EMPTY || *f < 16) ? TRACKER_EMPTY : *f - 16;
        if (input_pressed(BTN_B))
          *f = TRACKER_EMPTY;
        break;
      }
      case 4:
        if (ui_repeat(BTN_UP))
          step->fxv[0]++;
        if (ui_repeat(BTN_DOWN))
          step->fxv[0]--;
        if (ui_repeat(BTN_RIGHT))
          step->fxv[0] = clamp8(step->fxv[0] + 16);
        if (ui_repeat(BTN_LEFT))
          step->fxv[0] = clamp8((int)step->fxv[0] - 16);
        if (input_pressed(BTN_B))
          step->fxv[0] = 0;
        break;
      case 5: {
        uint8_t* f = &step->fx[1];
        if (ui_repeat(BTN_UP))
          *f = (*f == TRACKER_EMPTY) ? 0 : (*f < 0xFE ? *f + 1 : TRACKER_EMPTY);
        if (ui_repeat(BTN_DOWN))
          *f = (*f == TRACKER_EMPTY || *f == 0) ? TRACKER_EMPTY : *f - 1;
        if (ui_repeat(BTN_RIGHT))
          *f = (*f == TRACKER_EMPTY) ? 0 : (*f + 16 > 0xFE ? 0xFE : *f + 16);
        if (ui_repeat(BTN_LEFT))
          *f = (*f == TRACKER_EMPTY || *f < 16) ? TRACKER_EMPTY : *f - 16;
        if (input_pressed(BTN_B))
          *f = TRACKER_EMPTY;
        break;
      }
      case 6:
        if (ui_repeat(BTN_UP))
          step->fxv[1]++;
        if (ui_repeat(BTN_DOWN))
          step->fxv[1]--;
        if (ui_repeat(BTN_RIGHT))
          step->fxv[1] = clamp8(step->fxv[1] + 16);
        if (ui_repeat(BTN_LEFT))
          step->fxv[1] = clamp8((int)step->fxv[1] - 16);
        if (input_pressed(BTN_B))
          step->fxv[1] = 0;
        break;
    }
  }

  // X = fill current column down the current track; Y = clear it.
  if (input_pressed(BTN_X)) {
    for (int i = 0; i < pat->len; i++) {
      PatternStep* s = &pat->steps[ui->pattern_track][i];
      switch (ui->pattern_col) {
        case 0:
          s->note = step->note;
          if (s->note != NOTE_EMPTY && s->note != NOTE_OFF && !s->velocity)
            s->velocity = 0xFF;
          break;
        case 1:
          s->velocity = step->velocity;
          break;
        case 2:
          s->instrument = step->instrument;
          break;
        case 3:
          s->fx[0] = step->fx[0];
          break;
        case 4:
          s->fxv[0] = step->fxv[0];
          break;
        case 5:
          s->fx[1] = step->fx[1];
          break;
        case 6:
          s->fxv[1] = step->fxv[1];
          break;
      }
    }
  }

  if (!file_browser_active() && input_pressed(BTN_Y)) {
    for (int i = 0; i < pat->len; i++) {
      PatternStep* s = &pat->steps[ui->pattern_track][i];
      switch (ui->pattern_col) {
        case 0:
          s->note = NOTE_EMPTY;
          break;
        case 1:
          s->velocity = 0xFF;
          break;
        case 2:
          s->instrument = 0;
          break;
        case 3:
          s->fx[0] = TRACKER_EMPTY;
          break;
        case 4:
          s->fxv[0] = 0;
          break;
        case 5:
          s->fx[1] = TRACKER_EMPTY;
          break;
        case 6:
          s->fxv[1] = 0;
          break;
      }
    }
  }
}

// Draw one track's 7 sub-cells for step `s` at screen (tx, y).
// cur_col = highlighted sub-column (0-6), or -1 if this isn't the cursor track/row.
static void draw_track_cells(int tx, int y, PatternStep* s, int cur_col) {
  for (int c = 0; c < PT_NCOLS; c++) {
    bool cur_cell = (cur_col == c);
    int cx = tx + sub_x(c), cw = sub_w(c);
    if (cur_cell)
      DrawRectangle(cx, y, cw - 1, CH_H, C_CURSOR);
    Color fc2;
    const char* txt;
    char tmp[8];
    switch (c) {
      case 0:
        txt = note_str(s->note);
        fc2 = s->note == NOTE_EMPTY ? (cur_cell ? C_TEXT : C_DIM)
              : s->note == NOTE_OFF ? C_NOTE_OFF
                                    : C_NOTE;
        break;
      case 1:
        if (s->note == NOTE_EMPTY || s->note == NOTE_OFF) {
          txt = "--";
          fc2 = cur_cell ? C_TEXT : C_DIM;
        } else {
          snprintf(tmp, 8, "%02X", s->velocity);
          txt = tmp;
          fc2 = C_VEL;
        }
        break;
      case 2:
        snprintf(tmp, 8, "%02X", s->instrument);
        txt = tmp;
        fc2 = C_INST;
        break;
      case 3:
        if (s->fx[0] == TRACKER_EMPTY) {
          txt = "--";
          fc2 = cur_cell ? C_TEXT : C_DIM;
        } else {
          snprintf(tmp, 8, "%02X", s->fx[0]);
          txt = tmp;
          fc2 = C_FX;
        }
        break;
      case 4:
        if (s->fx[0] == TRACKER_EMPTY) {
          txt = "--";
          fc2 = cur_cell ? C_TEXT : C_DIM;
        } else {
          snprintf(tmp, 8, "%02X", s->fxv[0]);
          txt = tmp;
          fc2 = C_FX;
        }
        break;
      case 5:
        if (s->fx[1] == TRACKER_EMPTY) {
          txt = "--";
          fc2 = cur_cell ? C_TEXT : C_DIM;
        } else {
          snprintf(tmp, 8, "%02X", s->fx[1]);
          txt = tmp;
          fc2 = C_FX;
        }
        break;
      case 6:
        if (s->fx[1] == TRACKER_EMPTY) {
          txt = "--";
          fc2 = cur_cell ? C_TEXT : C_DIM;
        } else {
          snprintf(tmp, 8, "%02X", s->fxv[1]);
          txt = tmp;
          fc2 = C_FX;
        }
        break;
      default:
        txt = "?";
        fc2 = C_TEXT;
        break;
    }
    DrawText(txt, cx + 2, y + (CH_H - FONT_S) / 2, FONT_S, fc2);
  }
}

void screen_pattern_draw(UIState* ui) {
  Pattern* pat = &ui->song->pattern_data[ui->ctx_pattern];
  bool in_footer = (ui->pattern_row == (int)pat->len);
  int vt = visible_tracks();

  // Sticky header: row 1 = track number (0-F, colored), row 2 = column legend.
  static const char* SUBLBL[PT_NCOLS] = {"NOT", "V", "I", "P1", "V1", "P2", "V2"};
  int hy = STATUS_H + 1;
  int hy2 = hy + CH_H;
  DrawRectangle(0, hy, WIN_W, CH_H * 2, C_BG_ALT);
  DrawText("PT", 2, hy + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  for (int i = 0; i < vt; i++) {
    int tr = ui->pattern_track_scroll + i;
    if (tr >= PATTERN_TRACKS)
      break;
    int tx = track_x(ui, tr);
    bool cur = (tr == ui->pattern_track);
    DrawText(TextFormat("%X", tr), tx + 2, hy + (CH_H - FONT_S) / 2, FONT_S,
             cur ? C_TITLE : CH_COLORS[tr]);
    for (int c = 0; c < PT_NCOLS; c++)
      DrawText(SUBLBL[c], tx + sub_x(c) + 2, hy2 + (CH_H - FONT_S) / 2, FONT_S,
               cur ? C_HEADER : C_DIM);
  }
  if (ui->pattern_track_scroll > 0)
    DrawText("<", PX_ROW_W - 8, hy + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
  if (ui->pattern_track_scroll + vt < PATTERN_TRACKS)
    DrawText(">", WIN_W - 8, hy + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
  DrawLine(0, hy2 + CH_H, WIN_W, hy2 + CH_H, C_SEP);

  // Which step is currently playing (shared across all tracks of the pattern)
  int playing_step = -1;
  if (audio_is_playing(ui->engine)) {
    AudioEngine* eng = ui->engine;
    if (eng->pattern_loop) {
      if (eng->loop_pattern_idx == ui->ctx_pattern)
        playing_step = eng->cursors[0].pattern_step;
    } else {
      for (int ch = 0; ch < SONG_CHANNELS; ch++) {
        ChannelCursor* cur = &eng->cursors[ch];
        if (ui->song->patterns[ch][cur->song_row] == (uint8_t)ui->ctx_pattern) {
          playing_step = cur->pattern_step;
          break;
        }
      }
    }
  }

  int pt_visible = (WIN_H - PT_CONTENT_Y - STATUS_H) / CH_H;
  int total_rows = pat->len + 1;

  int scroll = 0;
  if (ui->pattern_row >= pt_visible)
    scroll = ui->pattern_row - pt_visible + 1;
  if (scroll + pt_visible > total_rows)
    scroll = total_rows - pt_visible;
  if (scroll < 0)
    scroll = 0;

  for (int vi = 0; vi < pt_visible; vi++) {
    int i = scroll + vi;
    if (i > (int)pat->len)
      break;
    int y = PT_CONTENT_Y + vi * CH_H;

    if (i == (int)pat->len) {
      // Footer: "[len_hex]  [+][-][SAVE][LOAD]" — packed into the first track's
      // width so no track separator line cuts through the controls.
      DrawRectangle(0, y, WIN_W, CH_H, C_BG_ALT);
      DrawText(TextFormat("%02X", pat->len), 2, y + (CH_H - FONT_S) / 2, FONT_S,
               in_footer ? C_TEXT : C_HEADER);
      const char* labels[4] = {"+", "-", "SAVE", "LOAD"};
      const int fw[4] = {16, 16, 52, 52};  // 18+136 = 154 < first line at PX_ROW_W+TRACK_W
      int x = PX_ROW_W;
      for (int c = 0; c < 4; c++) {
        bool sel = in_footer && ui->pattern_col == c;
        if (sel)
          DrawRectangle(x, y, fw[c] - 2, CH_H, C_CURSOR);
        DrawText(labels[c], x + 2, y + (CH_H - FONT_S) / 2, FONT_S, sel ? C_TITLE : C_DIM);
        x += fw[c];
      }
      continue;
    }

    bool is_cur = (i == ui->pattern_row);
    Color row_bg = (i % 4 == 0) ? C_BG_ALT : C_BG;
    if (i == playing_step)
      row_bg = C_CURSOR2;
    DrawRectangle(0, y, WIN_W, CH_H, row_bg);
    DrawText(TextFormat("%02X", (uint8_t)i), 2, y + (CH_H - FONT_S) / 2, FONT_S,
             is_cur ? C_TITLE : C_HEADER);

    for (int ti = 0; ti < vt; ti++) {
      int tr = ui->pattern_track_scroll + ti;
      if (tr >= PATTERN_TRACKS)
        break;
      int tx = track_x(ui, tr);
      PatternStep* s = &pat->steps[tr][i];
      int cur_col = (is_cur && !in_footer && tr == ui->pattern_track) ? ui->pattern_col : -1;
      draw_track_cells(tx, y, s, cur_col);
    }
  }

  // Vertical separators between tracks (and the gutter), spanning header + grid.
  int grid_bottom = PT_CONTENT_Y + pt_visible * CH_H;
  if (grid_bottom > WIN_H - STATUS_H)
    grid_bottom = WIN_H - STATUS_H;
  for (int k = 0; k <= vt; k++) {
    int x = PX_ROW_W + k * TRACK_W;
    if (x >= WIN_W - 3)
      break;
    DrawLine(x, hy, x, grid_bottom, C_SEP);
  }

  if (total_rows > pt_visible) {
    int bar_h = pt_visible * CH_H;
    int th = bar_h * pt_visible / total_rows;
    if (th < 2)
      th = 2;
    int ty = PT_CONTENT_Y + bar_h * scroll / total_rows;
    DrawRectangle(WIN_W - 3, PT_CONTENT_Y, 3, bar_h, C_DIM);
    DrawRectangle(WIN_W - 3, ty, 3, th, C_HEADER);
  }
}
