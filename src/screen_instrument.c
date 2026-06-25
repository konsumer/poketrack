#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "file_browser.h"
#include "midi_in.h"
#include "tracker.h"
#include "ui.h"
#include "units/unit_registry.h"

// Slot awaiting a file-browser result (-1 = none pending)
static int g_file_slot = -1;

typedef enum { INST_FB_NONE, INST_FB_SAVE, INST_FB_LOAD } InstFBMode;
static InstFBMode g_inst_fb_mode = INST_FB_NONE;


#define PANEL_W (WIN_W / 2)
#define INST_CONTENT_Y (STATUS_H + 2)

// Left-panel row layout:
//   0..CHAIN_MAX-1       : chain slot rows
//   INST_MIDI_DEV_ROW    : MIDI-in device
//   INST_MIDI_CH_ROW     : MIDI-in channel
//   INST_PARAM_BASE+     : right-panel param rows
#define INST_MIDI_DEV_ROW CHAIN_MAX
#define INST_MIDI_CH_ROW (CHAIN_MAX + 1)
#define INST_SAVE_ROW (CHAIN_MAX + 2)
#define INST_LOAD_ROW (CHAIN_MAX + 3)
#define INST_PARAM_BASE (CHAIN_MAX + 4)

static const UnitDef *slot_def(TrackerInstrument *inst, int s) {
  if (!inst->chain[s].unit_id[0])
    return NULL;
  return unit_find(inst->chain[s].unit_id);
}

void screen_instrument_update(UIState *ui) {
  TrackerInstrument *inst = &ui->song->instruments[ui->ctx_instrument];

#ifdef __EMSCRIPTEN__
  midi_web_request_access();  // idempotent — fires once, gives promise time to resolve before picker opens
#endif


  bool edit = input_held(BTN_A);
  bool in_params = (ui->inst_row >= INST_PARAM_BASE);
  bool in_io   = (!in_params && ui->inst_row >= INST_SAVE_ROW);
  bool in_midi = (!in_params && !in_io && ui->inst_row >= INST_MIDI_DEV_ROW);

  // L/R shoulder: cycle instrument (not while editing or in param/midi rows)
  if (!edit && !in_params && !in_midi) {
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

  // ---- MIDI-in device picker overlay ----
  if (ui->midi_in_picker_active) {
    int total = midi_in_port_count() + 1;  // +1 for NONE at index 0
    if (ui_repeat(BTN_UP) && ui->midi_in_picker_row > 0)
      ui->midi_in_picker_row--;
    if (ui_repeat(BTN_DOWN) && ui->midi_in_picker_row < total - 1)
      ui->midi_in_picker_row++;
    if (input_pressed(BTN_A)) {
      if (ui->midi_in_picker_row == 0) {
        inst->midi_in_device[0] = '\0';
      } else {
        const char *name = midi_in_port_name(ui->midi_in_picker_row - 1);
        strncpy(inst->midi_in_device, name ? name : "", sizeof(inst->midi_in_device) - 1);
        inst->midi_in_device[sizeof(inst->midi_in_device) - 1] = '\0';
      }
      ui->midi_in_picker_active = false;
    }
    if (input_pressed(BTN_B))
      ui->midi_in_picker_active = false;
    return;
  }

  // ---- Slot rows (0..CHAIN_MAX-1) ----
  if (!in_params && !in_midi && !in_io) {
    int slot = ui->inst_row;
    if (!edit) {
      if (ui_repeat(BTN_UP) && slot > 0)
        ui->inst_row--;
      if (ui_repeat(BTN_DOWN)) {
        if (slot < CHAIN_MAX - 1)
          ui->inst_row++;
        else if (slot == CHAIN_MAX - 1)
          ui->inst_row = INST_MIDI_DEV_ROW;
      }
      if (ui_repeat(BTN_RIGHT) && slot_def(inst, slot))
        ui->inst_row = INST_PARAM_BASE;
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
        tracker_inst_set_slot(inst, slot, defs[cur_idx]->id, ui->ctx_instrument);
      }
      if (input_pressed(BTN_B) && sl->unit_id[0])
        sl->enabled = !sl->enabled;
    }

    // ---- MIDI-in rows (INST_MIDI_DEV_ROW, INST_MIDI_CH_ROW) ----
  } else if (in_midi) {
    // A on DEVICE row: open picker (must be outside !edit block — same as DATA/ADD row pattern)
    if (ui->inst_row == INST_MIDI_DEV_ROW && input_pressed(BTN_A)) {
#ifdef __EMSCRIPTEN__
      midi_web_request_access();
#endif
      ui->midi_in_picker_active = true;
      ui->midi_in_picker_row = 0;
    }

    if (!edit) {
      if (ui_repeat(BTN_UP))
        ui->inst_row--;  // DEV → slot 7, CHN → DEV
      if (ui->inst_row == INST_MIDI_DEV_ROW && ui_repeat(BTN_DOWN))
        ui->inst_row = INST_MIDI_CH_ROW;
      if (ui->inst_row == INST_MIDI_CH_ROW && ui_repeat(BTN_DOWN))
        ui->inst_row = INST_SAVE_ROW;
      if (ui_repeat(BTN_RIGHT) && slot_def(inst, ui->ctx_instrument_slot))
        ui->inst_row = INST_PARAM_BASE;

      // DEV row: B = clear device
      if (ui->inst_row == INST_MIDI_DEV_ROW && input_pressed(BTN_B))
        inst->midi_in_device[0] = '\0';

      // CHN row: B = reset to 0 (all)
      if (ui->inst_row == INST_MIDI_CH_ROW && input_pressed(BTN_B))
        inst->midi_in_channel = 0;
    } else {
      // Hold A + UP/DOWN on CHN row to change channel (0-16)
      if (ui->inst_row == INST_MIDI_CH_ROW) {
        if (ui_repeat(BTN_UP) && inst->midi_in_channel < 16)
          inst->midi_in_channel++;
        if (ui_repeat(BTN_DOWN) && inst->midi_in_channel > 0)
          inst->midi_in_channel--;
      }
    }

    // ---- Save/Load rows ----
  } else if (in_io) {
    // Poll file browser
    const char *fb = file_browser_poll();
    if (fb) {
      if (g_inst_fb_mode == INST_FB_SAVE) {
        char fname[64];
        const char *nm = inst->name[0] ? inst->name : "inst";
        snprintf(fname, sizeof(fname), "%s.rpti", nm);
        tracker_save_instrument(inst, fb, ui->engine->save_dir);
        file_browser_download(fb, fname);
      } else if (g_inst_fb_mode == INST_FB_LOAD) {
        TrackerInstrument tmp;
        if (tracker_load_instrument(&tmp, fb)) {
          *inst = tmp;
          audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
        }
      }
      g_inst_fb_mode = INST_FB_NONE;
      return;
    }
    if (file_browser_active()) return;

    if (!edit) {
      if (ui_repeat(BTN_UP))
        ui->inst_row--;  // LOAD → SAVE, SAVE → CHN
      if (ui->inst_row == INST_SAVE_ROW && ui_repeat(BTN_DOWN))
        ui->inst_row = INST_LOAD_ROW;
      if (ui_repeat(BTN_RIGHT) && slot_def(inst, ui->ctx_instrument_slot))
        ui->inst_row = INST_PARAM_BASE;
    }
    if (input_pressed(BTN_A)) {
      if (ui->inst_row == INST_SAVE_ROW) {
        char fname[64];
        snprintf(fname, sizeof(fname), "%s.rpti", inst->name[0] ? inst->name : "inst");
        g_inst_fb_mode = INST_FB_SAVE;
        file_browser_save_as("Save instrument", fname);
      } else if (ui->inst_row == INST_LOAD_ROW) {
        g_inst_fb_mode = INST_FB_LOAD;
        file_browser_open("Load instrument", "*.rpti");
      }
    }

    // ---- Param rows (INST_PARAM_BASE+) ----
  } else {
    int slot = ui->ctx_instrument_slot;
    const UnitDef *def = slot_def(inst, slot);
    if (!def) {
      ui->inst_row = slot;
      return;
    }

    audio_ensure_preview(ui->engine, (uint8_t)ui->ctx_instrument);
    UnitState *state = ui->engine->preview_states[slot];
    if (ui->engine->chan_states[0][slot])
      state = ui->engine->chan_states[0][slot];
    int nparams = (def->dyn_num_params && state) ? def->dyn_num_params(state) : def->num_params;
    bool has_picker = def->picker_count && def->picker_add && state;
    bool has_add_row = has_picker && nparams < UNIT_MAX_PARAMS;
    int add_row = INST_PARAM_BASE + nparams;
    int data_row = INST_PARAM_BASE + nparams + (has_add_row ? 1 : 0);
    bool has_data = (def->data_hint != NULL);
    int param = ui->inst_row - INST_PARAM_BASE;
    bool on_add = has_add_row && (ui->inst_row == add_row);
    bool on_data = has_data && (ui->inst_row == data_row);
    ChainSlot *sl = &inst->chain[slot];

    // Device picker mode: select MIDI/audio device
    if (ui->dev_picker_active) {
      int total = def->dev_picker_count(state);
      if (ui_repeat(BTN_UP) && ui->dev_picker_row > 0)
        ui->dev_picker_row--;
      if (ui_repeat(BTN_DOWN) && ui->dev_picker_row < total - 1)
        ui->dev_picker_row++;
      if (input_pressed(BTN_A)) {
        def->dev_picker_set(state, ui->dev_picker_row);
        if (def->sync_to_data)
          def->sync_to_data(state, sl->data, sizeof(sl->data));
        audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
        ui->dev_picker_active = false;
      }
      if (input_pressed(BTN_B))
        ui->dev_picker_active = false;
      return;
    }

    // Picker mode: browse all plugin params
    if (ui->clap_picker_active) {
      int total = def->picker_count(state);
      if (ui_repeat(BTN_UP) && ui->clap_picker_row > 0)
        ui->clap_picker_row--;
      if (ui_repeat(BTN_DOWN) && ui->clap_picker_row < total - 1)
        ui->clap_picker_row++;
      if (input_pressed(BTN_A)) {
        int before = def->dyn_num_params ? def->dyn_num_params(state) : 0;
        def->picker_add(state, ui->clap_picker_row);
        if (def->sync_to_data)
          def->sync_to_data(state, sl->data, sizeof(sl->data));
        int after = def->dyn_num_params ? def->dyn_num_params(state) : 0;
        if (after > before)
          ui->inst_row = INST_PARAM_BASE + after;
        ui->clap_picker_active = false;
      }
      if (input_pressed(BTN_B))
        ui->clap_picker_active = false;
      return;
    }

    // Poll for file-browser result
    const char *chosen = file_browser_poll();
    if (chosen && g_file_slot == slot) {
      g_file_slot = -1;
      const char *rel = chosen;
      const char *sd = ui->engine->save_dir;
      size_t sdlen = strlen(sd);
      if (sdlen > 0 && strncmp(chosen, sd, sdlen) == 0)
        rel = chosen + sdlen;
      strncpy(sl->data, rel, sizeof(sl->data) - 1);
      sl->data[sizeof(sl->data) - 1] = '\0';
      audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
      return;
    }

    if (file_browser_active())
      return;

    // A on DATA row: open device picker or file browser
    if (on_data && input_pressed(BTN_A)) {
      if (def->dev_picker_count && def->dev_picker_set && state) {
#ifdef __EMSCRIPTEN__
        midi_web_request_access();
#endif
        ui->dev_picker_active = true;
        ui->dev_picker_row = 0;
      } else {
        g_file_slot = slot;
        file_browser_open("Select file", def->file_filter ? def->file_filter : "");
      }
    }

    // A on ADD row: open picker
    if (on_add && input_pressed(BTN_A)) {
      ui->clap_picker_active = true;
      ui->clap_picker_row = 0;
    }

    bool on_cc_col = ui->inst_param_cc_col && !on_data && !on_add && param >= 0 && param < nparams && param < UNIT_MAX_PARAMS;

    if (!edit) {
      if (ui_repeat(BTN_UP)) {
        if (ui->inst_row > INST_PARAM_BASE)
          ui->inst_row--;
        else {
          ui->inst_row = slot;
          ui->inst_param_cc_col = false;
        }
      }
      if (ui_repeat(BTN_DOWN)) {
        int max_row = has_data ? data_row : (has_add_row ? add_row : INST_PARAM_BASE + nparams - 1);
        if (ui->inst_row < max_row)
          ui->inst_row++;
      }
      // Left: exit CC col → value col, or exit params → slot panel
      if (ui_repeat(BTN_LEFT)) {
        if (ui->inst_param_cc_col)
          ui->inst_param_cc_col = false;
        else {
          ui->inst_row = slot;
          ui->inst_param_cc_col = false;
        }
      }
      // Right: enter CC col from a normal param row
      if (ui_repeat(BTN_RIGHT) && !on_data && !on_add && param >= 0 && param < nparams && param < UNIT_MAX_PARAMS)
        ui->inst_param_cc_col = true;

      if (on_data) {
        if (input_pressed(BTN_B)) {
          audio_rebuild_instrument(ui->engine, (uint8_t)ui->ctx_instrument);
          inst->chain[slot].data[0] = '\0';
        }
      } else if (!on_add && param >= 0 && param < nparams) {
        if (input_pressed(BTN_B)) {
          if (on_cc_col) {
            // B on CC col: back to value col
            ui->inst_param_cc_col = false;
          } else if (def->mapping_remove && state) {
            def->mapping_remove(state, param);
            if (def->sync_to_data)
              def->sync_to_data(state, sl->data, sizeof(sl->data));
            if (ui->inst_row > INST_PARAM_BASE)
              ui->inst_row--;
          } else {
            uint8_t def_val = (def->get_param_default && state)
                                  ? def->get_param_default(state, param)
                                  : (param < UNIT_MAX_PARAMS ? def->param_defaults[param] : 0);
            if (def->set_param_val && state) {
              def->set_param_val(state, param, def_val);
              if (def->sync_to_data)
                def->sync_to_data(state, sl->data, sizeof(sl->data));
            } else if (param < UNIT_MAX_PARAMS) {
              sl->params[param] = def_val;
            }
          }
        }
      }
    } else if (on_cc_col) {
      // A held + up/down: cycle CC value -- → 00 → ... → 7F → -- (loops)
      uint8_t cc = sl->cc_map[param];
      bool changed = false;
      if (ui_repeat(BTN_UP)) {
        cc = (cc == 0xFF) ? 0x00 : (cc >= 0x7F ? 0xFF : cc + 1);
        changed = true;
      }
      if (ui_repeat(BTN_DOWN)) {
        cc = (cc == 0xFF) ? 0x7F : (cc == 0x00 ? 0xFF : cc - 1);
        changed = true;
      }
      if (changed)
        sl->cc_map[param] = cc;
    } else {
      if (!on_data && !on_add && param >= 0 && param < nparams) {
        bool use_dyn = def->get_param_val && def->set_param_val && state;
        uint8_t cur_v = use_dyn ? def->get_param_val(state, param)
                                : (param < UNIT_MAX_PARAMS ? sl->params[param] : 0);
        const char *fmt_cur = (def->format_param_val && state) ? def->format_param_val(state, param, cur_v) : NULL;
        bool is_bool_param = fmt_cur && (strcmp(fmt_cur, "ON") == 0 || strcmp(fmt_cur, "OFF") == 0);
        bool is_enum = !use_dyn && param < UNIT_MAX_PARAMS && def->param_enum_count[param] > 0 && def->param_enums[param];
        bool changed = false;
        if (is_bool_param) {
          if (ui_repeat(BTN_UP) || ui_repeat(BTN_DOWN)) {
            cur_v = (cur_v >= 128) ? 0 : 255;
            changed = true;
          }
        } else if (is_enum) {
          int cnt = def->param_enum_count[param];
          if (ui_repeat(BTN_UP)) {
            cur_v = (uint8_t)((cur_v + 1) % cnt);
            changed = true;
          }
          if (ui_repeat(BTN_DOWN)) {
            cur_v = (uint8_t)((cur_v + cnt - 1) % cnt);
            changed = true;
          }
        } else {
          if (ui_repeat(BTN_UP)) {
            cur_v++;
            changed = true;
          }
          if (ui_repeat(BTN_DOWN)) {
            cur_v--;
            changed = true;
          }
          if (ui_repeat(BTN_RIGHT)) {
            cur_v = (uint8_t)(cur_v + 16 > 255 ? 255 : cur_v + 16);
            changed = true;
          }
          if (ui_repeat(BTN_LEFT)) {
            cur_v = (uint8_t)(cur_v < 16 ? 0 : cur_v - 16);
            changed = true;
          }
        }
        if (input_pressed(BTN_B)) {
          cur_v = (def->get_param_default && state)
                      ? def->get_param_default(state, param)
                      : (param < UNIT_MAX_PARAMS ? def->param_defaults[param] : 0);
          changed = true;
        }
        if (changed) {
          if (use_dyn) {
            audio_set_dyn_param(ui->engine, (uint8_t)ui->ctx_instrument, slot, param, cur_v);
            if (def->sync_to_data)
              def->sync_to_data(state, sl->data, sizeof(sl->data));
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
  bool in_params = (ui->inst_row >= INST_PARAM_BASE);
  bool in_io   = (!in_params && ui->inst_row >= INST_SAVE_ROW);
  bool in_midi = (!in_params && !in_io && ui->inst_row >= INST_MIDI_DEV_ROW);
  int cur_slot = (in_params || in_midi || in_io) ? ui->ctx_instrument_slot : ui->inst_row;

  // Left panel: chain slots
  for (int s = 0; s < CHAIN_MAX; s++) {
    int y = INST_CONTENT_Y + s * CH_H;
    bool cur = (!in_params && !in_midi && !in_io && s == ui->inst_row);
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

  // MIDI-in DEV row
  {
    int y = INST_CONTENT_Y + INST_MIDI_DEV_ROW * CH_H;
    bool cur = (ui->inst_row == INST_MIDI_DEV_ROW);
    DrawRectangle(0, y, PANEL_W - 1, CH_H, cur ? C_CURSOR : C_BG_ALT);
    DrawText("DEVICE", 2, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);
    const char *dname = inst->midi_in_device[0] ? inst->midi_in_device : "none";
    int px = 46;
    int pw = PANEL_W - px - 4;
    int cw = MeasureText("W", FONT_S);
    int max_chars = pw / (cw > 0 ? cw : 6);
    int dlen = (int)strlen(dname);
    const char *display = (dlen > max_chars) ? dname + (dlen - max_chars) : dname;
    DrawText(display, px, y + (CH_H - FONT_S) / 2, FONT_S, inst->midi_in_device[0] ? (cur ? C_NOTE : C_VEL) : C_DIM);
  }

  // MIDI-in CHN row
  {
    int y = INST_CONTENT_Y + INST_MIDI_CH_ROW * CH_H;
    bool cur = (ui->inst_row == INST_MIDI_CH_ROW);
    DrawRectangle(0, y, PANEL_W - 1, CH_H, cur ? C_CURSOR : C_BG);
    DrawText("CHN", 2, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);
    const char *ch_str = inst->midi_in_channel == 0
                             ? "ALL"
                             : TextFormat("%d", inst->midi_in_channel);
    DrawText(ch_str, 28, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_VEL);
    if (cur)
      DrawText("holdA+UP/DN", PANEL_W - 68, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
  }

  // SAVE row
  {
    int y = INST_CONTENT_Y + INST_SAVE_ROW * CH_H;
    bool cur = (ui->inst_row == INST_SAVE_ROW);
    DrawRectangle(0, y, PANEL_W - 1, CH_H, cur ? C_CURSOR : C_BG_ALT);
    DrawText("SAVE", 2, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);
    if (cur) DrawText("[A]", PANEL_W - 24, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
  }

  // LOAD row
  {
    int y = INST_CONTENT_Y + INST_LOAD_ROW * CH_H;
    bool cur = (ui->inst_row == INST_LOAD_ROW);
    DrawRectangle(0, y, PANEL_W - 1, CH_H, cur ? C_CURSOR : C_BG);
    DrawText("LOAD", 2, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_HEADER);
    if (cur) DrawText("[A]", PANEL_W - 24, y + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
  }

  DrawLine(PANEL_W, INST_CONTENT_Y, PANEL_W, WIN_H - STATUS_H, C_SEP);

  // Right panel
  const UnitDef *def = slot_def(inst, cur_slot);
  if (!def) {
    DrawText("empty slot", PANEL_W + 4, INST_CONTENT_Y + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
    DrawText("holdA+UP/DN=set unit", PANEL_W + 4, INST_CONTENT_Y + CH_H + (CH_H - FONT_S) / 2, FONT_S - 1, C_DIM);
    goto draw_overlays;
  }

  {
    const char *rlabel = (def->role_label == NULL) ? (def->is_source ? "SOURCE" : "EFFECT") : def->role_label;
    if (rlabel && rlabel[0])
      DrawText(TextFormat("%s  %s", def->name, rlabel),
               PANEL_W + 4, INST_CONTENT_Y + (CH_H - FONT_S) / 2, FONT_S, def->is_source ? C_NOTE : C_FX);
    else
      DrawText(def->name,
               PANEL_W + 4, INST_CONTENT_Y + (CH_H - FONT_S) / 2, FONT_S, def->is_source ? C_NOTE : C_FX);
  }

  {
    ChainSlot *sl = &inst->chain[cur_slot];
    int bar_x = PANEL_W + 94;
    int bar_w = WIN_W - bar_x - 26;  // shrunk to fit CC field at right
    UnitState *cur_state = ui->engine->preview_states[cur_slot];
    if (ui->engine->chan_states[0][cur_slot])
      cur_state = ui->engine->chan_states[0][cur_slot];
    int nparams = (def->dyn_num_params && cur_state) ? def->dyn_num_params(cur_state) : def->num_params;
    bool has_picker_draw = def->picker_count && def->picker_add && cur_state;
    bool has_dev_picker_draw = def->dev_picker_count && def->dev_picker_set && cur_state;
    bool has_add_row_draw = has_picker_draw && nparams < UNIT_MAX_PARAMS;

    int param_offset = 0;
    for (int s = 0; s < cur_slot; s++) {
      const UnitDef *sd = slot_def(inst, s);
      if (!sd)
        continue;
      UnitState *ss = ui->engine->preview_states[s];
      param_offset += (sd->dyn_num_params && ss) ? sd->dyn_num_params(ss) : sd->num_params;
    }

    int max_visible = (WIN_H - STATUS_H - INST_CONTENT_Y - CH_H) / CH_H - 1;
    int param_idx = in_params ? (ui->inst_row - INST_PARAM_BASE) : 0;
    int scroll_off = 0;
    if (param_idx >= max_visible)
      scroll_off = param_idx - max_visible + 1;

    for (int pi = scroll_off; pi < nparams; pi++) {
      int row = pi - scroll_off;
      int y = INST_CONTENT_Y + CH_H + row * CH_H;
      if (y + CH_H > WIN_H - STATUS_H)
        break;
      int param_row = INST_PARAM_BASE + pi;
      bool cur = in_params && (param_row == ui->inst_row);
      DrawRectangle(PANEL_W, y, WIN_W - PANEL_W, CH_H, cur ? C_CURSOR : (pi % 2 == 0 ? C_BG_ALT : C_BG));

      const char *pname = (def->dyn_param_name && cur_state) ? def->dyn_param_name(cur_state, pi) : (pi < UNIT_MAX_PARAMS ? def->param_names[pi] : NULL);
      char name_buf[16];
      snprintf(name_buf, sizeof(name_buf), "%02X %.8s", param_offset + pi, pname ? pname : "");
      DrawText(name_buf, PANEL_W + 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_TEXT);

      bool use_dyn_val = def->get_param_val && cur_state;
      uint8_t pv = use_dyn_val ? def->get_param_val(cur_state, pi)
                               : (pi < UNIT_MAX_PARAMS ? sl->params[pi] : 0);
      bool is_enum = !use_dyn_val && pi < UNIT_MAX_PARAMS && def->param_enum_count[pi] > 0 && def->param_enums[pi];

      const char *fmt = (def->format_param_val && cur_state) ? def->format_param_val(cur_state, pi, pv) : NULL;
      bool is_bool_disp = fmt && (strcmp(fmt, "ON") == 0 || strcmp(fmt, "OFF") == 0);

      if (is_enum) {
        uint8_t idx = pv % def->param_enum_count[pi];
        DrawText(def->param_enums[pi][idx], PANEL_W + 72, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_VEL);
      } else if (fmt) {
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

      // CC map field (right edge)
      {
        uint8_t cc = (pi < UNIT_MAX_PARAMS) ? sl->cc_map[pi] : 0xFF;
        const char *cc_str = (cc <= 127) ? TextFormat("%02X", cc) : "--";
        bool cc_focused = cur && ui->inst_param_cc_col;
        Color cc_col = cc_focused ? C_NOTE : (cc <= 127 ? C_VEL : C_DIM);
        DrawText(cc_str, WIN_W - 20, y + (CH_H - FONT_S) / 2, FONT_S, cc_col);
      }
    }

    // ADD row
    if (has_add_row_draw) {
      int add_row_idx = INST_PARAM_BASE + nparams;
      int add_visible = nparams - scroll_off;
      int y_add = INST_CONTENT_Y + CH_H + add_visible * CH_H;
      if (y_add + CH_H <= WIN_H - STATUS_H) {
        bool cur = in_params && (ui->inst_row == add_row_idx);
        DrawRectangle(PANEL_W, y_add, WIN_W - PANEL_W, CH_H,
                      cur ? C_CURSOR : (nparams % 2 == 0 ? C_BG_ALT : C_BG));
        DrawText("ADD", PANEL_W + 4, y_add + (CH_H - FONT_S) / 2, FONT_S, cur ? C_NOTE : C_DIM);
        DrawText("PARAM", PANEL_W + 32, y_add + (CH_H - FONT_S) / 2, FONT_S, cur ? C_VEL : C_DIM);
      }
    }

    // FILE row
    if (def->data_hint) {
      int data_row = INST_PARAM_BASE + nparams + (has_add_row_draw ? 1 : 0);
      int file_row = nparams - scroll_off + (has_add_row_draw ? 1 : 0);
      int y = INST_CONTENT_Y + CH_H + file_row * CH_H;
      if (y + CH_H <= WIN_H - STATUS_H) {
        bool cur = in_params && (ui->inst_row == data_row);
        bool editing = cur && ui->inst_data_editing;
        Color bg = editing ? C_CURSOR2 : (cur ? C_CURSOR : (nparams % 2 == 0 ? C_BG_ALT : C_BG));
        DrawRectangle(PANEL_W, y, WIN_W - PANEL_W, CH_H, bg);
        const char *dlabel = def->data_label ? def->data_label : "FILE";
        DrawText(dlabel, PANEL_W + 4, y + (CH_H - FONT_S) / 2, FONT_S, cur ? C_TITLE : C_TEXT);

        bool using_hint = !sl->data[0];
        static char path_buf[512];
        const char *path;
        if (using_hint) {
          path = def->data_hint;
        } else {
          strncpy(path_buf, sl->data, sizeof(path_buf) - 1);
          path_buf[sizeof(path_buf) - 1] = '\0';
          char *tab = strchr(path_buf, '\t');
          if (tab)
            *tab = '\0';
          path = path_buf;
        }
        int px = PANEL_W + 4 + MeasureText(dlabel, FONT_S) + 6;
        int pw = WIN_W - px - 4;
        int cw = MeasureText("W", FONT_S);
        int max_chars = pw / (cw > 0 ? cw : 6);
        int plen = (int)strlen(path);
        const char *display = (plen > max_chars) ? path + (plen - max_chars) : path;
        Color tc = editing ? C_STATUS : (using_hint ? C_HEADER : (cur ? C_NOTE : C_VEL));
        DrawText(display, px, y + (CH_H - FONT_S) / 2, FONT_S, tc);

        if (editing && (ui->blink & 16)) {
          int cx = px + MeasureText(display, FONT_S);
          DrawRectangle(cx, y + 2, 2, CH_H - 4, C_STATUS);
        }
      }
    }

    // Picker overlays
    if (ui->clap_picker_active && has_picker_draw) {
      int total = def->picker_count(cur_state);
      int overlay_x = PANEL_W, overlay_y = INST_CONTENT_Y;
      int overlay_w = WIN_W - PANEL_W, overlay_h = WIN_H - STATUS_H - overlay_y;
      DrawRectangle(overlay_x, overlay_y, overlay_w, overlay_h, C_BG);
      const char *ptitle = (def->picker_title && def->picker_title[0]) ? def->picker_title : "ADD PARAM";
      DrawText(TextFormat("%s  [A]=select  [B]=cancel", ptitle),
               overlay_x + 4, overlay_y + (CH_H - FONT_S) / 2, FONT_S - 1, C_HEADER);
      DrawLine(overlay_x, overlay_y + CH_H, WIN_W, overlay_y + CH_H, C_SEP);

      int visible = (overlay_h - CH_H) / CH_H;
      int scroll = 0;
      if (ui->clap_picker_row >= visible)
        scroll = ui->clap_picker_row - visible + 1;

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

    if (ui->dev_picker_active && has_dev_picker_draw) {
      int total = def->dev_picker_count(cur_state);
      int overlay_x = PANEL_W, overlay_y = INST_CONTENT_Y;
      int overlay_w = WIN_W - PANEL_W, overlay_h = WIN_H - STATUS_H - overlay_y;
      DrawRectangle(overlay_x, overlay_y, overlay_w, overlay_h, C_BG);
      const char *dtitle = (def->dev_picker_title && def->dev_picker_title[0])
                               ? def->dev_picker_title
                               : "SELECT DEVICE";
      DrawText(TextFormat("%s  [A]=select  [B]=cancel", dtitle),
               overlay_x + 4, overlay_y + (CH_H - FONT_S) / 2, FONT_S - 1, C_HEADER);
      DrawLine(overlay_x, overlay_y + CH_H, WIN_W, overlay_y + CH_H, C_SEP);

      int visible = (overlay_h - CH_H) / CH_H;
      int scroll = 0;
      if (ui->dev_picker_row >= visible)
        scroll = ui->dev_picker_row - visible + 1;

      for (int i = 0; i < visible && (scroll + i) < total; i++) {
        int pi = scroll + i;
        int py = overlay_y + CH_H + i * CH_H;
        bool cur = (pi == ui->dev_picker_row);
        DrawRectangle(overlay_x, py, overlay_w, CH_H,
                      cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
        const char *dname = def->dev_picker_name(cur_state, pi);
        DrawText(dname ? dname : "(unnamed)",
                 overlay_x + 4, py + (CH_H - FONT_S) / 2, FONT_S,
                 cur ? C_TITLE : C_TEXT);
      }
      if (total == 0)
        DrawText("no devices found", overlay_x + 4, overlay_y + CH_H + (CH_H - FONT_S) / 2, FONT_S, C_DIM);
    }
  }

draw_overlays:
  // MIDI-in device picker overlay (full-panel)
  if (ui->midi_in_picker_active) {
    int total = midi_in_port_count();
    int overlay_x = 0, overlay_y = INST_CONTENT_Y;
    int overlay_w = WIN_W, overlay_h = WIN_H - STATUS_H - overlay_y;
    DrawRectangle(overlay_x, overlay_y, overlay_w, overlay_h, C_BG);
    DrawText("MIDI IN DEVICE  [A]=select  [B]=cancel",
             overlay_x + 4, overlay_y + (CH_H - FONT_S) / 2, FONT_S - 1, C_HEADER);
    DrawLine(overlay_x, overlay_y + CH_H, WIN_W, overlay_y + CH_H, C_SEP);

    int total_draw = midi_in_port_count() + 1;
    int visible = (overlay_h - CH_H) / CH_H;
    int scroll = 0;
    if (ui->midi_in_picker_row >= visible)
      scroll = ui->midi_in_picker_row - visible + 1;

    for (int i = 0; i < visible && (scroll + i) < total_draw; i++) {
      int pi = scroll + i;
      int py = overlay_y + CH_H + i * CH_H;
      bool cur = (pi == ui->midi_in_picker_row);
      DrawRectangle(overlay_x, py, overlay_w, CH_H,
                    cur ? C_CURSOR : (i % 2 == 0 ? C_BG_ALT : C_BG));
      const char *label = (pi == 0) ? "NONE" : midi_in_port_name(pi - 1);
      DrawText(label, overlay_x + 4, py + (CH_H - FONT_S) / 2, FONT_S,
               cur ? C_TITLE : (pi == 0 ? C_DIM : C_TEXT));
    }
  }

}
