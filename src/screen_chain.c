#include "ui.h"

#define CHAIN_CONTENT_Y (STATUS_H + CH_H + 2)
// Columns: row#, phrase#, transpose
#define COL_ROW_W 28
#define COL_PH_W 56
#define COL_TP_W 52

static int col_x(int col) {
  if (col == 0)
    return COL_ROW_W;
  return COL_ROW_W + COL_PH_W;
}

// Find lowest unused phrase index
static uint8_t next_free_phrase(TrackerSong *song) {
  bool used[NUM_PHRASES] = {0};
  for (int ci = 0; ci < NUM_CHAINS; ci++)
    for (int s = 0; s < CHAIN_LENGTH; s++)
      if (song->chain_data[ci].phrase[s] != TRACKER_EMPTY)
        used[song->chain_data[ci].phrase[s]] = true;
  for (int i = 0; i < NUM_PHRASES; i++)
    if (!used[i])
      return (uint8_t)i;
  return TRACKER_EMPTY;
}

void screen_chain_update(UIState *ui) {
  TrackerSong *song = ui->song;
  TrackerChain *chain = &song->chain_data[ui->ctx_chain];
  bool sel = input_held(BTN_SCREEN);

  bool edit = input_held(BTN_OK);

  if (!edit) {
    if (ui_repeat(BTN_UP) && ui->chain_row > 0)
      ui->chain_row--;
    if (ui_repeat(BTN_DOWN) && ui->chain_row < CHAIN_LENGTH - 1)
      ui->chain_row++;
    if (ui_repeat(BTN_LEFT) && ui->chain_col > 0)
      ui->chain_col--;
    if (ui_repeat(BTN_RIGHT) && ui->chain_col < 1)
      ui->chain_col++;

    // A: enter phrase editor (not edit mode — quick tap)
    if (input_pressed(BTN_OK) && ui->chain_col == 0) {
      uint8_t pi = chain->phrase[ui->chain_row];
      if (pi == TRACKER_EMPTY) {
        pi = next_free_phrase(song);
        if (pi != TRACKER_EMPTY)
          chain->phrase[ui->chain_row] = pi;
      }
      if (pi != TRACKER_EMPTY) {
        ui->ctx_phrase = pi;
        ui->phrase_row = 0;
        ui->phrase_col = 0;
        ui->screen = SCREEN_PHRASE;
      }
    }

    // B: back to song
    if (input_pressed(BTN_NO))
      ui->screen = SCREEN_SONG;

    // SELECT+B: clear current row
  } else {
    // A held + DPAD: edit values
    int row = ui->chain_row;
    if (ui->chain_col == 0) {
      uint8_t pi = chain->phrase[row];
      if (ui_repeat(BTN_UP)) {
        if (pi == TRACKER_EMPTY)
          chain->phrase[row] = 0;
        else if (pi > 0)
          chain->phrase[row]--;
      }
      if (ui_repeat(BTN_DOWN)) {
        if (pi == TRACKER_EMPTY)
          chain->phrase[row] = 0;
        else if (pi < NUM_PHRASES - 1)
          chain->phrase[row]++;
      }
      if (input_pressed(BTN_NO))
        chain->phrase[row] = TRACKER_EMPTY;
    } else {
      // Transpose
      int8_t tp = chain->transpose[row];
      if (ui_repeat(BTN_UP) && tp < 36)
        chain->transpose[row]++;
      if (ui_repeat(BTN_DOWN) && tp > -36)
        chain->transpose[row]--;
      if (input_pressed(BTN_NO))
        chain->transpose[row] = 0;
    }
  }
}

void screen_chain_draw(UIState *ui) {
  TrackerSong *song = ui->song;
  TrackerChain *chain = &song->chain_data[ui->ctx_chain];

  // Header
  int hy = STATUS_H + 1;
  DrawRectangle(0, hy, WIN_W, CH_H, C_BG_ALT);
  DrawText("##", 2, hy + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  DrawText("PHRASE", col_x(0) + 2, hy + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  DrawText("TRANSPOSE", col_x(1) + 2, hy + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);
  DrawLine(0, hy + CH_H, WIN_W, hy + CH_H, C_SEP);

  for (int i = 0; i < CHAIN_LENGTH; i++) {
    int y = CHAIN_CONTENT_Y + i * CH_H;
    Color row_bg = (i % 4 == 0) ? C_BG_ALT : C_BG;

    // Playing indicator
    if (audio_is_playing(ui->engine)) {
      ChannelCursor *cur = &ui->engine->cursors[ui->ctx_channel];
      uint8_t ci = ui->song->chains[ui->ctx_channel][cur->song_row];
      if (ci == (uint8_t)ui->ctx_chain && cur->chain_step == i)
        row_bg = C_CURSOR2;
    }
    DrawRectangle(0, y, WIN_W, CH_H, row_bg);

    // Row number
    DrawText(TextFormat("%02X", i), 2, y + (CH_H - FONT_S) / 2, FONT_S,
             i == ui->chain_row ? C_TITLE : C_HEADER);

    // Phrase cell
    bool cur_ph = (i == ui->chain_row && ui->chain_col == 0);
    uint8_t pi = chain->phrase[i];
    if (cur_ph)
      DrawRectangle(col_x(0), y, COL_PH_W - 1, CH_H, C_CURSOR);
    DrawText(pi == TRACKER_EMPTY ? "--" : TextFormat("%02X", pi),
             col_x(0) + 2, y + (CH_H - FONT_S) / 2, FONT_S,
             pi == TRACKER_EMPTY ? (cur_ph ? C_TEXT : C_DIM) : CH_COLORS[ui->ctx_channel]);

    // Transpose cell
    bool cur_tp = (i == ui->chain_row && ui->chain_col == 1);
    int8_t tp = chain->transpose[i];
    if (cur_tp)
      DrawRectangle(col_x(1), y, COL_TP_W - 1, CH_H, C_CURSOR);
    Color tp_col = (tp == 0) ? C_DIM : (tp > 0 ? C_NOTE : C_NOTE_OFF);
    if (cur_tp)
      tp_col = C_TEXT;
    DrawText(tp == 0 ? " 00" : TextFormat("%+03d", tp),
             col_x(1) + 2, y + (CH_H - FONT_S) / 2, FONT_S, tp_col);
  }
}
