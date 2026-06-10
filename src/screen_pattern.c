#include <stdio.h>

#include "ui.h"
#include "units/unit_registry.h"

// Column layout
#define PX_ROW  0
#define PW_ROW  24
#define PX_NOTE 24
#define PW_NOTE 40
#define PX_VEL  64
#define PW_VEL  28
#define PX_INST 92
#define PW_INST 26
#define PX_F1T  118
#define PW_FXT  24
#define PX_F1V  142
#define PW_FXV  24
#define PX_F2T  166
#define PX_F2V  190
#define PT_NCOLS 7

#define PT_CONTENT_Y (STATUS_H + CH_H + 2)

static int col_x(int c) {
  static const int xs[] = {PX_NOTE, PX_VEL, PX_INST, PX_F1T, PX_F1V, PX_F2T, PX_F2V};
  return xs[c];
}
static int col_w(int c) {
  static const int ws[] = {PW_NOTE, PW_VEL, PW_INST, PW_FXT, PW_FXV, PW_FXT, PW_FXV};
  return ws[c];
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static void preview(UIState *ui, uint8_t note) {
  audio_preview_kill(ui->engine);
  if (note == NOTE_EMPTY || note == NOTE_OFF)
    return;
  uint8_t inst = ui->song->pattern_data[ui->ctx_pattern].steps[ui->pattern_row].instrument;
  audio_preview_note(ui->engine, inst, note);
}

void screen_pattern_update(UIState *ui) {
  Pattern *pat = &ui->song->pattern_data[ui->ctx_pattern];
  bool edit = input_held(BTN_A);

  if (input_released(BTN_A))
    audio_preview_kill(ui->engine);

  // Footer row is the virtual row at index pat->len (below last step)
  bool in_footer = (ui->pattern_row == (int)pat->len);

  // L/R shoulder: cycle patterns (not while editing)
  if (!edit) {
    if (ui_repeat(BTN_R) && ui->ctx_pattern < NUM_PATTERNS - 1) {
      ui->ctx_pattern++;
      pat = &ui->song->pattern_data[ui->ctx_pattern];
      if (ui->pattern_row > (int)pat->len) ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
    }
    if (ui_repeat(BTN_L) && ui->ctx_pattern > 0) {
      ui->ctx_pattern--;
      pat = &ui->song->pattern_data[ui->ctx_pattern];
      if (ui->pattern_row > (int)pat->len) ui->pattern_row = pat->len;
      in_footer = (ui->pattern_row == (int)pat->len);
    }
  }

  // Handle footer before edit check — A press would otherwise be eaten by edit mode
  if (in_footer) {
    if (ui_repeat(BTN_UP))   { ui->pattern_row = pat->len > 0 ? pat->len - 1 : 0; return; }
    if (ui_repeat(BTN_LEFT))  ui->pattern_col = 0;
    if (ui_repeat(BTN_RIGHT)) ui->pattern_col = 1;
    if (ui->pattern_col > 1)  ui->pattern_col = 1;

    // col 0 = "+", col 1 = "-"
    if (input_pressed(BTN_A)) {
      if (ui->pattern_col == 0 && pat->len < MAX_PATTERN_STEPS)
        pat->len++;
      else if (ui->pattern_col == 1 && pat->len > 1)
        pat->len--;
      ui->pattern_row = pat->len;  // stay in footer
    }
    return;
  }

  PatternStep *step = &pat->steps[ui->pattern_row];

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->pattern_row > 0)
      ui->pattern_row--;
    if (ui_repeat(BTN_DOWN)) {
      if (ui->pattern_row < (int)pat->len - 1) ui->pattern_row++;
      else ui->pattern_row = pat->len;  // enter footer
    }
    if (ui_repeat(BTN_LEFT)  && ui->pattern_col > 0)          ui->pattern_col--;
    if (ui_repeat(BTN_RIGHT) && ui->pattern_col < PT_NCOLS-1) ui->pattern_col++;

    if (input_pressed(BTN_B)) {
      switch (ui->pattern_col) {
        case 0: step->note = NOTE_EMPTY;        break;
        case 1: step->velocity = 0xFF;          break;
        case 2: step->instrument = 0;           break;
        case 3: step->fx[0] = TRACKER_EMPTY;   break;
        case 4: step->fxv[0] = 0;              break;
        case 5: step->fx[1] = TRACKER_EMPTY;   break;
        case 6: step->fxv[1] = 0;              break;
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
            step->note = scale_next_note(59, +1, si, sr);
            if (!step->velocity) step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, +1, si, sr);
          }
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_DOWN)) {
          if (n == NOTE_EMPTY || n == NOTE_OFF) {
            step->note = scale_next_note(61, -1, si, sr);
            if (!step->velocity) step->velocity = 0xFF;
          } else {
            step->note = scale_next_note(n, -1, si, sr);
          }
          preview(ui, step->note);
        }
        if (ui_repeat(BTN_RIGHT) && n != NOTE_EMPTY && n != NOTE_OFF && n + 12 <= 127) {
          step->note += 12; preview(ui, step->note);
        }
        if (ui_repeat(BTN_LEFT)  && n != NOTE_EMPTY && n != NOTE_OFF && n >= 13) {
          step->note -= 12; preview(ui, step->note);
        }
        if (input_pressed(BTN_B)) step->note = NOTE_OFF;
        break;
      }
      case 1:
        if (ui_repeat(BTN_UP))    step->velocity = clamp8(step->velocity + 1);
        if (ui_repeat(BTN_DOWN))  step->velocity = clamp8((int)step->velocity - 1);
        if (ui_repeat(BTN_RIGHT)) step->velocity = clamp8(step->velocity + 16);
        if (ui_repeat(BTN_LEFT))  step->velocity = clamp8((int)step->velocity - 16);
        if (input_pressed(BTN_B)) step->velocity = 0xFF;
        break;
      case 2:
        if (ui_repeat(BTN_UP)   && step->instrument < NUM_INSTRUMENTS-1) step->instrument++;
        if (ui_repeat(BTN_DOWN) && step->instrument > 0)                 step->instrument--;
        if (input_pressed(BTN_B)) step->instrument = 0;
        ui->ctx_instrument = step->instrument;
        break;
      case 3: {
        uint8_t *f = &step->fx[0];
        if (ui_repeat(BTN_UP))    *f = (*f==TRACKER_EMPTY)?0:(*f<0xFE?*f+1:TRACKER_EMPTY);
        if (ui_repeat(BTN_DOWN))  *f = (*f==TRACKER_EMPTY||*f==0)?TRACKER_EMPTY:*f-1;
        if (ui_repeat(BTN_RIGHT)) *f = (*f==TRACKER_EMPTY)?0:(*f+16>0xFE?0xFE:*f+16);
        if (ui_repeat(BTN_LEFT))  *f = (*f==TRACKER_EMPTY||*f<16)?TRACKER_EMPTY:*f-16;
        if (input_pressed(BTN_B)) *f = TRACKER_EMPTY;
        break;
      }
      case 4:
        if (ui_repeat(BTN_UP))    step->fxv[0]++;
        if (ui_repeat(BTN_DOWN))  step->fxv[0]--;
        if (ui_repeat(BTN_RIGHT)) step->fxv[0] = clamp8(step->fxv[0] + 16);
        if (ui_repeat(BTN_LEFT))  step->fxv[0] = clamp8((int)step->fxv[0] - 16);
        if (input_pressed(BTN_B)) step->fxv[0] = 0;
        break;
      case 5: {
        uint8_t *f = &step->fx[1];
        if (ui_repeat(BTN_UP))    *f = (*f==TRACKER_EMPTY)?0:(*f<0xFE?*f+1:TRACKER_EMPTY);
        if (ui_repeat(BTN_DOWN))  *f = (*f==TRACKER_EMPTY||*f==0)?TRACKER_EMPTY:*f-1;
        if (ui_repeat(BTN_RIGHT)) *f = (*f==TRACKER_EMPTY)?0:(*f+16>0xFE?0xFE:*f+16);
        if (ui_repeat(BTN_LEFT))  *f = (*f==TRACKER_EMPTY||*f<16)?TRACKER_EMPTY:*f-16;
        if (input_pressed(BTN_B)) *f = TRACKER_EMPTY;
        break;
      }
      case 6:
        if (ui_repeat(BTN_UP))    step->fxv[1]++;
        if (ui_repeat(BTN_DOWN))  step->fxv[1]--;
        if (ui_repeat(BTN_RIGHT)) step->fxv[1] = clamp8(step->fxv[1] + 16);
        if (ui_repeat(BTN_LEFT))  step->fxv[1] = clamp8((int)step->fxv[1] - 16);
        if (input_pressed(BTN_B)) step->fxv[1] = 0;
        break;
    }
  }

  if (input_pressed(BTN_X)) {
    for (int i = 0; i < pat->len; i++) {
      PatternStep *s = &pat->steps[i];
      switch (ui->pattern_col) {
        case 0: s->note = step->note;
                if (s->note != NOTE_EMPTY && s->note != NOTE_OFF && !s->velocity)
                  s->velocity = 0xFF;
                break;
        case 1: s->velocity   = step->velocity;   break;
        case 2: s->instrument = step->instrument; break;
        case 3: s->fx[0]  = step->fx[0];  break;
        case 4: s->fxv[0] = step->fxv[0]; break;
        case 5: s->fx[1]  = step->fx[1];  break;
        case 6: s->fxv[1] = step->fxv[1]; break;
      }
    }
  }

  if (input_pressed(BTN_Y)) {
    for (int i = 0; i < pat->len; i++) {
      PatternStep *s = &pat->steps[i];
      switch (ui->pattern_col) {
        case 0: s->note = NOTE_EMPTY;        break;
        case 1: s->velocity = 0xFF;          break;
        case 2: s->instrument = 0;           break;
        case 3: s->fx[0] = TRACKER_EMPTY;   break;
        case 4: s->fxv[0] = 0;              break;
        case 5: s->fx[1] = TRACKER_EMPTY;   break;
        case 6: s->fxv[1] = 0;              break;
      }
    }
  }
}

void screen_pattern_draw(UIState *ui) {
  Pattern *pat = &ui->song->pattern_data[ui->ctx_pattern];
  bool in_footer = (ui->pattern_row == (int)pat->len);

  int hy = STATUS_H + 1;
  DrawRectangle(0, hy, WIN_W, CH_H, C_BG_ALT);
  DrawText("NOTE", PX_NOTE + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("VEL",  PX_VEL  + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("INS",  PX_INST + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("P1",   PX_F1T  + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("V",    PX_F1V  + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("P2",   PX_F2T  + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawText("V",    PX_F2V  + 2, hy + (CH_H-FONT_S)/2, FONT_S, C_HEADER);
  DrawLine(0, hy + CH_H, WIN_W, hy + CH_H, C_SEP);

  int playing_step = -1;
  if (audio_is_playing(ui->engine)) {
    AudioEngine *eng = ui->engine;
    if (eng->pattern_loop) {
      if (eng->loop_pattern_idx == ui->ctx_pattern)
        playing_step = eng->cursors[0].pattern_step;
    } else {
      for (int ch = 0; ch < SONG_CHANNELS; ch++) {
        ChannelCursor *cur = &eng->cursors[ch];
        if (ui->song->patterns[ch][cur->song_row] == (uint8_t)ui->ctx_pattern) {
          playing_step = cur->pattern_step;
          break;
        }
      }
    }
  }

  int pt_visible  = (WIN_H - PT_CONTENT_Y - STATUS_H) / CH_H;
  int total_rows  = pat->len + 1;

  int scroll = 0;
  if (ui->pattern_row >= pt_visible)
    scroll = ui->pattern_row - pt_visible + 1;
  if (scroll + pt_visible > total_rows)
    scroll = total_rows - pt_visible;
  if (scroll < 0) scroll = 0;

  for (int vi = 0; vi < pt_visible; vi++) {
    int i = scroll + vi;
    if (i > (int)pat->len) break;
    int y = PT_CONTENT_Y + vi * CH_H;

    if (i == (int)pat->len) {
      // Footer: "[len_hex]  [+]  [-]"
      bool fc = in_footer;
      DrawRectangle(0, y, WIN_W, CH_H, C_BG_ALT);

      // Length in 2-digit hex (in the row-number column)
      DrawText(TextFormat("%02X", pat->len), PX_ROW + 2, y + (CH_H-FONT_S)/2, FONT_S,
               fc ? C_TEXT : C_HEADER);

      // col 0 = "+"
      bool plus_sel  = fc && ui->pattern_col == 0;
      if (plus_sel)  DrawRectangle(PX_NOTE, y, PW_NOTE - 1, CH_H, C_CURSOR);
      DrawText("+", PX_NOTE + 2, y + (CH_H-FONT_S)/2, FONT_S, plus_sel  ? C_TITLE : C_DIM);

      // col 1 = "-"
      bool minus_sel = fc && ui->pattern_col == 1;
      if (minus_sel) DrawRectangle(PX_VEL, y, PW_VEL - 1, CH_H, C_CURSOR);
      DrawText("-", PX_VEL  + 2, y + (CH_H-FONT_S)/2, FONT_S, minus_sel ? C_TITLE : C_DIM);
      continue;
    }

    PatternStep *s = &pat->steps[i];
    bool is_cur = (i == ui->pattern_row);

    Color row_bg = (i % 4 == 0) ? C_BG_ALT : C_BG;
    if (i == playing_step) row_bg = C_CURSOR2;
    DrawRectangle(0, y, WIN_W, CH_H, row_bg);
    DrawText(TextFormat("%02X", (uint8_t)i), PX_ROW + 2, y + (CH_H-FONT_S)/2, FONT_S,
             is_cur ? C_TITLE : C_HEADER);

    for (int c = 0; c < PT_NCOLS; c++) {
      bool cur_cell = is_cur && c == ui->pattern_col;
      int cx = col_x(c), cw = col_w(c);
      if (cur_cell) DrawRectangle(cx, y, cw-1, CH_H, C_CURSOR);
      Color fc2;
      const char *txt;
      char tmp[8];
      switch (c) {
        case 0:
          txt = note_str(s->note);
          fc2 = s->note == NOTE_EMPTY ? (cur_cell ? C_TEXT : C_DIM)
              : s->note == NOTE_OFF   ? C_NOTE_OFF : C_NOTE;
          break;
        case 1:
          if (s->note == NOTE_EMPTY || s->note == NOTE_OFF) {
            txt = "--"; fc2 = cur_cell ? C_TEXT : C_DIM;
          } else {
            snprintf(tmp, 8, "%02X", s->velocity); txt = tmp; fc2 = C_VEL;
          }
          break;
        case 2:
          snprintf(tmp, 8, "%02X", s->instrument); txt = tmp; fc2 = C_INST;
          break;
        case 3:
          if (s->fx[0] == TRACKER_EMPTY) { txt = "--"; fc2 = cur_cell ? C_TEXT : C_DIM; }
          else { snprintf(tmp,8,"%02X",s->fx[0]);  txt=tmp; fc2=C_FX; }
          break;
        case 4:
          if (s->fx[0] == TRACKER_EMPTY) { txt = "--"; fc2 = cur_cell ? C_TEXT : C_DIM; }
          else { snprintf(tmp,8,"%02X",s->fxv[0]); txt=tmp; fc2=C_FX; }
          break;
        case 5:
          if (s->fx[1] == TRACKER_EMPTY) { txt = "--"; fc2 = cur_cell ? C_TEXT : C_DIM; }
          else { snprintf(tmp,8,"%02X",s->fx[1]);  txt=tmp; fc2=C_FX; }
          break;
        case 6:
          if (s->fx[1] == TRACKER_EMPTY) { txt = "--"; fc2 = cur_cell ? C_TEXT : C_DIM; }
          else { snprintf(tmp,8,"%02X",s->fxv[1]); txt=tmp; fc2=C_FX; }
          break;
        default: txt = "?"; fc2 = C_TEXT; break;
      }
      DrawText(txt, cx + 2, y + (CH_H-FONT_S)/2, FONT_S, fc2);
    }
  }

  if (total_rows > pt_visible) {
    int bar_h = pt_visible * CH_H;
    int th = bar_h * pt_visible / total_rows;
    if (th < 2) th = 2;
    int ty = PT_CONTENT_Y + bar_h * scroll / total_rows;
    DrawRectangle(WIN_W-3, PT_CONTENT_Y, 3, bar_h, C_DIM);
    DrawRectangle(WIN_W-3, ty, 3, th, C_HEADER);
  }
}
