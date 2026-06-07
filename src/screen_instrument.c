#include "audio.h"
#include "file_browser.h"
#include "ui.h"
#include "units/unit_registry.h"
#include <stdio.h>
#include <string.h>

// Slot awaiting a file-browser result (-1 = none pending)
static int g_file_slot = -1;

#define PANEL_W (WIN_W / 2)
#define INST_CONTENT_Y (STATUS_H + 2)

// inst_row 0-7 = slot list, 8+ = param/data editing
// DATA row index = CHAIN_MAX + def->num_params  (one past last param)

static const UnitDef *slot_def(TrackerInstrument *inst, int s) {
  if (!inst->chain[s].unit_id[0])
    return NULL;
  return unit_find(inst->chain[s].unit_id);
}

void screen_instrument_update(UIState *ui) {
  TrackerInstrument *inst = &ui->song->instruments[ui->ctx_instrument];

  // (inst_data_editing kept for text-fallback path but file dialog is primary)

  bool edit = input_held(BTN_A);
  bool in_params = (ui->inst_row >= CHAIN_MAX);

  // L/R shoulder: cycle instrument (not while editing)
  if (!edit && !in_params) {
    if (ui_repeat(BTN_R) && ui->ctx_instrument < NUM_INSTRUMENTS - 1) {
      ui->ctx_instrument++;
      ui->inst_row = 0;
      inst = &ui->song->instruments[ui->ctx_instrument];
    }
    if (ui_repeat(BTN_L) && ui->ctx_instrument > 0) {
      ui->ctx_instrument--;
      ui->inst_row = 0;
      inst = &ui->song->instruments[ui->ctx_instrument];
    }
  }

  if (!in_params) {
    int slot = ui->inst_row;
    if (!edit) {
      if (ui_repeat(BTN_UP) && slot > 0)
        ui->inst_row--;
      if (ui_repeat(BTN_DOWN) && slot < CHAIN_MAX - 1)
        ui->inst_row++;
      if (ui_repeat(BTN_RIGHT) && slot_def(inst, slot))
        ui->inst_row = CHAIN_MAX;
      if (input_pressed(BTN_B)) {
        audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
        memset(&inst->chain[slot], 0, sizeof(ChainSlot));
      }
    } else {
      const UnitDef *defs[32];
      int nf = 0;
      unit_list(defs, &nf);
      ChainSlot *sl = &inst->chain[slot];

      if (ui_repeat(BTN_UP) || ui_repeat(BTN_DOWN)) {
        int cur_idx = -1;
        for (int i = 0; i < nf; i++)
          if (sl->unit_id[0] && strcmp(defs[i]->id, sl->unit_id) == 0) {
            cur_idx = i;
            break;
          }
        if (ui_repeat(BTN_UP))
          cur_idx = (cur_idx <= 0) ? nf - 1 : cur_idx - 1;
        else
          cur_idx = (cur_idx + 1) % nf;
        audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
        tracker_inst_set_slot(inst, slot, defs[cur_idx]->id);
      }
      if (input_pressed(BTN_B) && sl->unit_id[0])
        sl->enabled = !sl->enabled;
    }
  } else {
    int slot = ui->ctx_instrument_slot;
    const UnitDef *def = slot_def(inst, slot);
    if (!def) {
      ui->inst_row = slot;
      return;
    }

    audio_ensure_preview(ui->engine, (uint8_t)ui->ctx_instrument);
    UnitState *state = ui->engine->preview_states[slot];
    // Prefer chan_states[0] when playing — edits go to the live instance
    // (same instance the draw section reads, so changes are immediately visible)
    if (ui->engine->chan_states[0][slot])
      state = ui->engine->chan_states[0][slot];
    int nparams = (def->dyn_num_params && state) ? def->dyn_num_params(state) : def->num_params;
    bool has_picker = def->picker_count && def->picker_add && state;
    bool has_add_row = has_picker && nparams < UNIT_MAX_PARAMS;
    int add_row  = CHAIN_MAX + nparams;   // virtual row for ADD
    int data_row = CHAIN_MAX + nparams + (has_add_row ? 1 : 0);
    bool has_data = (def->data_hint != NULL);
    int param = ui->inst_row - CHAIN_MAX;
    bool on_add  = has_add_row && (ui->inst_row == add_row);
    bool on_data = has_data && (ui->inst_row == data_row);
    ChainSlot *sl = &inst->chain[slot];

    // Picker mode: browse all plugin params
    if (ui->clap_picker_active) {
      int total = def->picker_count(state);
      if (ui_repeat(BTN_UP)   && ui->clap_picker_row > 0)       ui->clap_picker_row--;
      if (ui_repeat(BTN_DOWN) && ui->clap_picker_row < total-1) ui->clap_picker_row++;
      if (input_pressed(BTN_A)) {
        int before = def->dyn_num_params ? def->dyn_num_params(state) : 0;
        def->picker_add(state, ui->clap_picker_row);
        if (def->sync_to_data) def->sync_to_data(state, sl->data, sizeof(sl->data));
        int after = def->dyn_num_params ? def->dyn_num_params(state) : 0;
        // Keep cursor on ADD row (which shifted up by 1 after the add)
        if (after > before) ui->inst_row = CHAIN_MAX + after;
        ui->clap_picker_active = false;
      }
      if (input_pressed(BTN_B)) ui->clap_picker_active = false;
      return;
    }

    // Poll for file-browser result — must precede any re-open trigger
    const char *chosen = file_browser_poll();
    if (chosen && g_file_slot == slot) {
      g_file_slot = -1;
      const char *rel = chosen;
      const char *sd  = ui->engine->save_dir;
      size_t sdlen = strlen(sd);
      if (sdlen > 0 && strncmp(chosen, sd, sdlen) == 0)
        rel = chosen + sdlen;
      strncpy(sl->data, rel, sizeof(sl->data) - 1);
      sl->data[sizeof(sl->data) - 1] = '\0';
      audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
      return;
    }

    if (file_browser_active()) return;

    // A on FILE row: open file browser
    if (on_data && input_pressed(BTN_A)) {
      g_file_slot = slot;
      file_browser_open("Select file", def->file_filter ? def->file_filter : "");
    }

    // A on ADD row: open picker
    if (on_add && input_pressed(BTN_A)) {
      ui->clap_picker_active = true;
      ui->clap_picker_row    = 0;
    }

    if (!edit) {
      if (ui_repeat(BTN_UP)) {
        if (ui->inst_row > CHAIN_MAX)
          ui->inst_row--;
        else
          ui->inst_row = slot;
      }
      if (ui_repeat(BTN_DOWN)) {
        int max_row = has_data ? data_row : (has_add_row ? add_row : CHAIN_MAX + nparams - 1);
        if (ui->inst_row < max_row) ui->inst_row++;
      }
      if (ui_repeat(BTN_LEFT))
        ui->inst_row = slot;

      if (on_data) {
        if (input_pressed(BTN_B)) {
          audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
          inst->chain[slot].data[0] = '\0';
        }
      } else if (!on_add && param >= 0 && param < nparams) {
        // B on mapped param: remove it (picker units) or reset (static units)
        if (input_pressed(BTN_B)) {
          if (def->mapping_remove && state) {
            def->mapping_remove(state, param);
            if (def->sync_to_data) def->sync_to_data(state, sl->data, sizeof(sl->data));
            if (ui->inst_row > CHAIN_MAX) ui->inst_row--;
          } else {
            uint8_t def_val = (def->get_param_default && state)
                ? def->get_param_default(state, param)
                : (param < UNIT_MAX_PARAMS ? def->param_defaults[param] : 0);
            if (def->set_param_val && state) {
              def->set_param_val(state, param, def_val);
              if (def->sync_to_data) def->sync_to_data(state, sl->data, sizeof(sl->data));
            } else if (param < UNIT_MAX_PARAMS) {
              sl->params[param] = def_val;
            }
          }
        }
      }
    } else {
      if (!on_data && !on_add && param >= 0 && param < nparams) {
        bool use_dyn = def->get_param_val && def->set_param_val && state;
        uint8_t cur_v = use_dyn ? def->get_param_val(state, param)
                                : (param < UNIT_MAX_PARAMS ? sl->params[param] : 0);
        const char *fmt_cur = (def->format_param_val && state) ? def->format_param_val(state, param, cur_v) : NULL;
        bool is_bool_param = fmt_cur && (strcmp(fmt_cur, "ON") == 0 || strcmp(fmt_cur, "OFF") == 0);
        bool is_enum = !use_dyn && param < UNIT_MAX_PARAMS
                       && def->param_enum_count[param] > 0 && def->param_enums[param];
        bool changed = false;
        if (is_bool_param) {
          if (ui_repeat(BTN_UP) || ui_repeat(BTN_DOWN)) { cur_v = (cur_v >= 128) ? 0 : 255; changed = true; }
        } else if (is_enum) {
          int cnt = def->param_enum_count[param];
          if (ui_repeat(BTN_UP))   { cur_v = (uint8_t)((cur_v + 1) % cnt); changed = true; }
          if (ui_repeat(BTN_DOWN)) { cur_v = (uint8_t)((cur_v + cnt - 1) % cnt); changed = true; }
        } else {
          if (ui_repeat(BTN_UP))    { cur_v++; changed = true; }
          if (ui_repeat(BTN_DOWN))  { cur_v--; changed = true; }
          if (ui_repeat(BTN_RIGHT)) { cur_v = (uint8_t)(cur_v + 16 > 255 ? 255 : cur_v + 16); changed = true; }
          if (ui_repeat(BTN_LEFT))  { cur_v = (uint8_t)(cur_v < 16 ? 0 : cur_v - 16); changed = true; }
        }
        if (input_pressed(BTN_B)) {
          cur_v = (def->get_param_default && state)
              ? def->get_param_default(state, param)
              : (param < UNIT_MAX_PARAMS ? def->param_defaults[param] : 0);
          changed = true;
        }
        if (changed) {
          if (use_dyn) {
            def->set_param_val(state, param, cur_v);
            if (def->sync_to_data) def->sync_to_data(state, sl->data, sizeof(sl->data));
          } else if (param < UNIT_MAX_PARAMS) {
            sl->params[param] = cur_v;
          }
        }
      }
    }
  }

  if (ui->inst_row < CHAIN_MAX)
    ui->ctx_instrument_slot = ui->inst_row;
}

void screen_instrument_draw(UIState *ui) {
  TrackerInstrument *inst = &ui->song->instruments[ui->ctx_instrument];
  bool in_params = (ui->inst_row >= CHAIN_MAX);
  int cur_slot = in_params ? ui->ctx_instrument_slot : ui->inst_row;

  // Left panel: chain slots
  for (int s = 0; s < CHAIN_MAX; s++) {
    int y = INST_CONTENT_Y + s * CH_H;
    bool cur = !in_params && (s == ui->inst_row);
    bool sel = (s == cur_slot);
    Color bg = cur ? C_CURSOR : (sel ? C_BG_ALT : (s % 2 == 0 ? C_BG_ALT : C_BG));
    DrawRectangle(0, y, PANEL_W - 1, CH_H, bg);
    DrawText(TextFormat("%d", s), 2, y + (CH_H - FONT_S) / 2, FONT_S, C_HEADER);

    ChainSlot *sl = &inst->chain[s];
    const UnitDef *def = slot_def(inst, s);
    if (def) {
      Color nc = sl->enabled ? (def->is_source ? C_NOTE : C_FX) : C_DIM;
      DrawText(def->name, 16, y + (CH_H - FONT_S) / 2, FONT_S, nc);
      DrawText(sl->enabled ? "ON" : "OF", PANEL_W - 22, y + (CH_H - FONT_S) / 2, FONT_S, nc);
    } else {
      DrawText("--", 16, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TEXT : C_DIM);
    }
  }
  DrawLine(PANEL_W, INST_CONTENT_Y, PANEL_W, WIN_H - STATUS_H, C_SEP);

  // Right panel
  const UnitDef *def = slot_def(inst, cur_slot);
  if (!def) {
    DrawText("empty slot", PANEL_W + 4, INST_CONTENT_Y + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
    DrawText("holdA+UP/DN=set unit", PANEL_W + 4, INST_CONTENT_Y + CH_H + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
    return;
  }

  DrawText(TextFormat("%s  %s",
                      def->name, def->is_source ? "SOURCE" : "EFFECT"),
           PANEL_W + 4, INST_CONTENT_Y + (CH_H - FONT_S) / 2, FONT_S, def->is_source ? C_NOTE : C_FX);

  ChainSlot *sl = &inst->chain[cur_slot];
  int bar_x = PANEL_W + 94;
  int bar_w = WIN_W - bar_x - 4;
  UnitState *cur_state = ui->engine->preview_states[cur_slot];
  // Prefer live chan_states[0] for display — reflects sequencer param automation
  if (ui->engine->chan_states[0][cur_slot])
    cur_state = ui->engine->chan_states[0][cur_slot];
  int nparams = (def->dyn_num_params && cur_state) ? def->dyn_num_params(cur_state) : def->num_params;
  bool has_picker_draw = def->picker_count && def->picker_add && cur_state;
  bool has_add_row_draw = has_picker_draw && nparams < UNIT_MAX_PARAMS;

  int param_offset = 0;
  for (int s = 0; s < cur_slot; s++) {
    const UnitDef *sd = slot_def(inst, s);
    if (!sd) continue;
    UnitState *ss = ui->engine->preview_states[s];
    param_offset += (sd->dyn_num_params && ss) ? sd->dyn_num_params(ss) : sd->num_params;
  }

  // Scrolling: keep cursor row visible
  int max_visible = (WIN_H - STATUS_H - INST_CONTENT_Y - CH_H) / CH_H - 1;  // -1 for FILE row
  int param_idx   = in_params ? (ui->inst_row - CHAIN_MAX) : 0;
  int scroll_off  = 0;
  if (param_idx >= max_visible) scroll_off = param_idx - max_visible + 1;

  // Params
  for (int pi = scroll_off; pi < nparams; pi++) {
    int row = pi - scroll_off;
    int y   = INST_CONTENT_Y + CH_H + row * CH_H;
    if (y + CH_H > WIN_H - STATUS_H) break;
    int param_row = CHAIN_MAX + pi;
    bool cur = in_params && (param_row == ui->inst_row);
    DrawRectangle(PANEL_W, y, WIN_W - PANEL_W, CH_H, cur ? C_CURSOR : (pi % 2 == 0 ? C_BG_ALT : C_BG));

    // Clipped name: "XX name..." truncated to fit before value column
    const char *pname = (def->dyn_param_name && cur_state) ? def->dyn_param_name(cur_state, pi) : (pi < UNIT_MAX_PARAMS ? def->param_names[pi] : NULL);
    char name_buf[16];
    snprintf(name_buf, sizeof(name_buf), "%02X %.8s", param_offset + pi, pname ? pname : "");
    DrawText(name_buf, PANEL_W + 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_TEXT);

    bool use_dyn_val = def->get_param_val && cur_state;
    uint8_t pv = use_dyn_val ? def->get_param_val(cur_state, pi)
                             : (pi < UNIT_MAX_PARAMS ? sl->params[pi] : 0);
    bool is_enum = !use_dyn_val && pi < UNIT_MAX_PARAMS
                   && def->param_enum_count[pi] > 0 && def->param_enums[pi];

    const char *fmt = (def->format_param_val && cur_state) ? def->format_param_val(cur_state, pi, pv) : NULL;
    bool is_bool_disp = fmt && (strcmp(fmt, "ON") == 0 || strcmp(fmt, "OFF") == 0);

    if (is_enum) {
      uint8_t idx = pv % def->param_enum_count[pi];
      DrawText(def->param_enums[pi][idx], PANEL_W + 72, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_VEL);
    } else if (fmt) {
      // Stepped/bool: show actual value, bar only if not boolean
      DrawText(fmt, PANEL_W + 72, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_VEL);
      if (!is_bool_disp) {
        float frac = pv / 255.0f;
        DrawRectangle(bar_x, y + 3, bar_w, CH_H - 6, C_DIM);
        DrawRectangle(bar_x, y + 3, (int)(frac * bar_w), CH_H - 6, cur ? C_NOTE : C_HEADER);
      }
    } else {
      DrawText(TextFormat("%02X", pv), PANEL_W + 72, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_VEL);
      float frac = pv / 255.0f;
      DrawRectangle(bar_x, y + 3, bar_w, CH_H - 6, C_DIM);
      DrawRectangle(bar_x, y + 3, (int)(frac * bar_w), CH_H - 6, cur ? C_NOTE : C_HEADER);
    }
  }

  // ADD row (for picker-capable units with room for more mappings)
  if (has_add_row_draw) {
    int add_row_idx = CHAIN_MAX + nparams;
    int add_visible  = nparams - scroll_off;
    int y_add = INST_CONTENT_Y + CH_H + add_visible * CH_H;
    if (y_add + CH_H <= WIN_H - STATUS_H) {
      bool cur = in_params && (ui->inst_row == add_row_idx);
      DrawRectangle(PANEL_W, y_add, WIN_W - PANEL_W, CH_H,
                    cur ? C_CURSOR : (nparams % 2 == 0 ? C_BG_ALT : C_BG));
      DrawText("ADD", PANEL_W + 4, y_add + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_DIM);
      DrawText("PARAM", PANEL_W + 32, y_add + (CH_H - FONT_S) / 2, FONT_S, cur ? C_VEL : C_DIM);
    }
  }

  // FILE row for units that load a file
  if (def->data_hint) {
    int data_row = CHAIN_MAX + nparams + (has_add_row_draw ? 1 : 0);
    int file_row = nparams - scroll_off + (has_add_row_draw ? 1 : 0);
    int y        = INST_CONTENT_Y + CH_H + file_row * CH_H;
    if (y + CH_H <= WIN_H - STATUS_H) {
      bool cur = in_params && (ui->inst_row == data_row);
      bool editing = cur && ui->inst_data_editing;
      Color bg = editing ? C_CURSOR2 : (cur ? C_CURSOR : (nparams % 2 == 0 ? C_BG_ALT : C_BG));
      DrawRectangle(PANEL_W, y, WIN_W - PANEL_W, CH_H, bg);
      DrawText("FILE", PANEL_W + 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_TEXT);

      // Show path (right-justify so end is visible when long)
      // If empty, show hint grayed out
      bool using_hint = !sl->data[0];
      // Clip at first tab — units like CLAP append \tplugin_id\thex after path
      static char path_buf[512];
      const char *path;
      if (using_hint) {
        path = def->data_hint;
      } else {
        strncpy(path_buf, sl->data, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        char *tab = strchr(path_buf, '\t');
        if (tab) *tab = '\0';
        path = path_buf;
      }
      int px = PANEL_W + 38;
      int pw = WIN_W - px - 4;
      // Measure and right-clip
      int cw = MeasureText("W", FONT_S);  // approx char width
      int max_chars = pw / (cw > 0 ? cw : 6);
      int plen = (int)strlen(path);
      const char *display = (plen > max_chars) ? path + (plen - max_chars) : path;
      Color tc = editing ? C_STATUS : (using_hint ? C_HEADER : (cur ? C_NOTE : C_VEL));
      DrawText(display, px, y + (CH_H - FONT_S) / 2, FONT_S, tc);

      // Blinking cursor when editing
      if (editing && (ui->blink & 16)) {
        int cx = px + MeasureText(display, FONT_S);
        DrawRectangle(cx, y + 2, 2, CH_H - 4, C_STATUS);
      }
    }
  }

  // Picker overlay
  if (ui->clap_picker_active && has_picker_draw) {
    int total = def->picker_count(cur_state);
    int overlay_x = PANEL_W;
    int overlay_y = INST_CONTENT_Y;
    int overlay_w = WIN_W - PANEL_W;
    int overlay_h = WIN_H - STATUS_H - overlay_y;
    DrawRectangle(overlay_x, overlay_y, overlay_w, overlay_h, C_BG);
    DrawText("ADD PARAM  [A]=add  [B]=cancel",
             overlay_x + 4, overlay_y + (CH_H - FONT_S) / 2, FONT_S - 1, C_HEADER);
    DrawLine(overlay_x, overlay_y + CH_H, WIN_W, overlay_y + CH_H, C_SEP);

    int visible = (overlay_h - CH_H) / CH_H;
    int scroll = 0;
    if (ui->clap_picker_row >= visible) scroll = ui->clap_picker_row - visible + 1;

    for (int i = 0; i < visible && (scroll + i) < total; i++) {
      int pi = scroll + i;
      int py = overlay_y + CH_H + i * CH_H;
      bool cur = (pi == ui->clap_picker_row);
      DrawRectangle(overlay_x, py, overlay_w, CH_H,
                    cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
      const char *pname = def->picker_name(cur_state, pi);
      char label[32];
      snprintf(label, sizeof(label), "%d %.20s", pi, pname ? pname : "");
      DrawText(label, overlay_x + 4, py + (CH_H - FONT_S) / 2, FONT_S,
               cur ? C_TITLE : C_TEXT);
    }
  }
}
