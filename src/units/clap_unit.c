// CLAP plugin wrapper unit
// data format: "path\tplugin_id\t{IIIIIIIIVV}..." where each 10-char block is
//   8-char hex param ID + 2-char hex value, one per mapped param (up to UNIT_MAX_PARAMS)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../clap_host.h"
#include "unit.h"

typedef struct {
  uint32_t id;
  double min_val, max_val, default_val;
  bool is_bool, is_stepped;
  char name[24];
} ClapParamInfo;

typedef struct {
  uint32_t id;
  uint8_t val;
  uint8_t last_sent;
  double min_val, max_val;
  bool is_bool, is_stepped;
  char name[24];
} ClapMapping;

struct UnitState {
  ClapPlugin *plugin;
  float sample_rate;
  bool is_source;
  char resolved_path[512];
  char plugin_id[128];
  int active_note;

  // User-mapped params (up to UNIT_MAX_PARAMS)
  int num_mappings;
  ClapMapping mappings[UNIT_MAX_PARAMS];

  // Full param list for the picker (all plugin params)
  int total_params;
  ClapParamInfo *param_cache;  // malloc'd
};

static UnitState *clap_unit_create(float sr) {
  UnitState *s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  s->active_note = -1;
  return s;
}

static void clap_unit_destroy(UnitState *s) {
  if (s->plugin)
    clap_host_unload(s->plugin);
  free(s->param_cache);
  free(s);
}

// Look up full param info by ID into a ClapMapping slot
static void fill_mapping_from_cache(UnitState *s, uint32_t id, ClapMapping *m) {
  m->id = id;
  m->val = 0x80;
  m->min_val = 0;
  m->max_val = 1;
  m->is_bool = false;
  m->is_stepped = false;
  strncpy(m->name, "?", sizeof(m->name));
  for (int i = 0; i < s->total_params; i++) {
    ClapParamInfo *c = &s->param_cache[i];
    if (c->id == id) {
      m->min_val = c->min_val;
      m->max_val = c->max_val;
      m->is_bool = c->is_bool;
      m->is_stepped = c->is_stepped;
      strncpy(m->name, c->name, sizeof(m->name) - 1);
      double range = c->max_val - c->min_val;
      if (range > 0)
        m->val = (uint8_t)((c->default_val - c->min_val) / range * 255.0 + 0.5);
      break;
    }
  }
  // Don't mark dirty on creation — let plugin keep its current state.
  // Only send when user explicitly changes value or FX sequences it.
  m->last_sent = m->val;
}

static void clap_unit_set_data(UnitState *s, const char *data, const char *base_dir) {
  if (!data || !data[0])
    return;

  char buf[640];
  strncpy(buf, data, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tab1 = strchr(buf, '\t');
  char *id_str = "";
  const char *hex_mappings = NULL;
  if (tab1) {
    *tab1 = '\0';
    id_str = tab1 + 1;
    char *tab2 = strchr(id_str, '\t');
    if (tab2) {
      *tab2 = '\0';
      hex_mappings = tab2 + 1;
    }
  }

  unit_resolve_path(base_dir, buf, s->resolved_path, sizeof(s->resolved_path));
  strncpy(s->plugin_id, id_str, sizeof(s->plugin_id) - 1);

  if (s->plugin) {
    clap_host_unload(s->plugin);
    s->plugin = NULL;
  }
  free(s->param_cache);
  s->param_cache = NULL;
  s->total_params = 0;
  s->num_mappings = 0;

  s->plugin = clap_host_load(s->resolved_path,
                             s->plugin_id[0] ? s->plugin_id : NULL,
                             s->sample_rate, 512);
  if (!s->plugin)
    return;

  s->is_source = clap_host_is_instrument(s->plugin);

  // Build full param cache for picker
  uint32_t total = clap_host_param_count(s->plugin);
  s->total_params = (int)total;
  if (total > 0) {
    s->param_cache = calloc(total, sizeof(ClapParamInfo));
    for (uint32_t i = 0; i < total; i++) {
      ClapParamInfo *c = &s->param_cache[i];
      clap_host_param_info(s->plugin, i, &c->id, c->name, sizeof(c->name),
                           &c->min_val, &c->max_val, &c->default_val);
      bool stepped = clap_host_param_is_stepped(s->plugin, i);
      c->is_bool = stepped && (c->max_val - c->min_val) == 1.0;
      c->is_stepped = stepped && !c->is_bool;
    }
  }

  // Restore saved mappings from hex
  if (hex_mappings) {
    const char *h = hex_mappings;
    while (h[0] && s->num_mappings < UNIT_MAX_PARAMS) {
      if (strlen(h) < 10)
        break;
      char id_hex[9] = {h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], '\0'};
      char val_hex[3] = {h[8], h[9], '\0'};
      uint32_t pid = (uint32_t)strtoul(id_hex, NULL, 16);
      uint8_t pv = (uint8_t)strtoul(val_hex, NULL, 16);
      ClapMapping *m = &s->mappings[s->num_mappings++];
      fill_mapping_from_cache(s, pid, m);
      m->val = pv;
      m->last_sent = 0xFF ^ pv;
      h += 10;
    }
  }
}

static void clap_unit_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *p) {
  (void)p;
  if (!s->plugin || !s->is_source)
    return;
  if (s->active_note >= 0)
    clap_host_note_off(s->plugin, (uint8_t)s->active_note, 0);
  clap_host_note_on(s->plugin, note, vel, 0);
  s->active_note = note;
}

static void clap_unit_note_off(UnitState *s, uint8_t note) {
  if (!s->plugin || !s->is_source)
    return;
  clap_host_note_off(s->plugin, note, 0);
  if (s->active_note == note)
    s->active_note = -1;
}

static void clap_unit_kill(UnitState *s) {
  if (!s->plugin || s->active_note < 0)
    return;
  clap_host_note_off(s->plugin, (uint8_t)s->active_note, 0);
  s->active_note = -1;
}

static int clap_dyn_num_params(UnitState *s) { return s->num_mappings; }

static const char *clap_dyn_param_name(UnitState *s, int idx) {
  if (idx < 0 || idx >= s->num_mappings)
    return "";
  return s->mappings[idx].name;
}

static uint8_t clap_get_param_val(UnitState *s, int idx) {
  if (idx < 0 || idx >= s->num_mappings)
    return 0;
  return s->mappings[idx].val;
}

static void clap_set_param_val(UnitState *s, int idx, uint8_t val) {
  if (idx < 0 || idx >= s->num_mappings)
    return;
  s->mappings[idx].val = val;
  s->mappings[idx].last_sent = 0xFF ^ val;
}

static uint8_t clap_get_param_default(UnitState *s, int idx) {
  if (idx < 0 || idx >= s->num_mappings)
    return 0x80;
  ClapMapping *m = &s->mappings[idx];
  double range = m->max_val - m->min_val;
  if (range == 0)
    return 0;
  // Re-derive default from param_cache
  for (int i = 0; i < s->total_params; i++) {
    if (s->param_cache[i].id == m->id) {
      return (uint8_t)((s->param_cache[i].default_val - m->min_val) / range * 255.0 + 0.5);
    }
  }
  return 0x80;
}

static char s_fmt_buf[32];
static const char *clap_format_param_val(UnitState *s, int idx, uint8_t val) {
  if (idx < 0 || idx >= s->num_mappings)
    return NULL;
  ClapMapping *m = &s->mappings[idx];
  if (m->is_bool)
    return (val >= 128) ? "ON" : "OFF";
  if (m->is_stepped) {
    int actual = (int)(m->min_val + (val / 255.0) * (m->max_val - m->min_val) + 0.5);
    snprintf(s_fmt_buf, sizeof(s_fmt_buf), "%d", actual);
    return s_fmt_buf;
  }
  return NULL;
}

static void clap_sync_to_data(UnitState *s, char *data_buf, size_t data_buf_sz) {
  // Ensure format: path\tplugin_id\thex...
  char *tab1 = strchr(data_buf, '\t');
  if (!tab1) {
    size_t cur_len = strlen(data_buf);
    if (cur_len + 2 >= data_buf_sz)
      return;
    data_buf[cur_len] = '\t';
    data_buf[cur_len + 1] = '\t';
    data_buf[cur_len + 2] = '\0';
    tab1 = data_buf + cur_len;
  }
  char *tab2 = strchr(tab1 + 1, '\t');
  if (!tab2) {
    size_t cur_len = strlen(data_buf);
    if (cur_len + 1 >= data_buf_sz)
      return;
    data_buf[cur_len] = '\t';
    data_buf[cur_len + 1] = '\0';
    tab2 = data_buf + cur_len;
  }
  char *hex_start = tab2 + 1;
  for (int i = 0; i < s->num_mappings; i++)
    sprintf(hex_start + i * 10, "%08X%02X", s->mappings[i].id, s->mappings[i].val);
  hex_start[s->num_mappings * 10] = '\0';
}

// Picker callbacks
static int clap_picker_count(UnitState *s) { return s->total_params; }

static const char *clap_picker_name(UnitState *s, int idx) {
  if (idx < 0 || idx >= s->total_params)
    return "";
  return s->param_cache[idx].name;
}

static void clap_picker_add(UnitState *s, int picker_idx) {
  if (picker_idx < 0 || picker_idx >= s->total_params)
    return;
  if (s->num_mappings >= UNIT_MAX_PARAMS)
    return;
  uint32_t pid = s->param_cache[picker_idx].id;
  for (int i = 0; i < s->num_mappings; i++)
    if (s->mappings[i].id == pid)
      return;
  ClapMapping *m = &s->mappings[s->num_mappings++];
  fill_mapping_from_cache(s, pid, m);
}

static void clap_mapping_remove(UnitState *s, int map_idx) {
  if (map_idx < 0 || map_idx >= s->num_mappings)
    return;
  for (int i = map_idx; i < s->num_mappings - 1; i++)
    s->mappings[i] = s->mappings[i + 1];
  s->num_mappings--;
}

static void clap_unit_render(UnitState *s, const uint8_t *p,
                             const float *in_l, const float *in_r,
                             float *out_l, float *out_r, uint32_t frames) {
  (void)p;
  if (!s->plugin)
    return;
  for (int i = 0; i < s->num_mappings; i++) {
    ClapMapping *m = &s->mappings[i];
    if (m->val != m->last_sent) {
      double val = m->min_val + (m->val / 255.0) * (m->max_val - m->min_val);
      clap_host_queue_param(s->plugin, m->id, val);
      m->last_sent = m->val;
    }
  }
  float tmp_l[512] = {0}, tmp_r[512] = {0};
  if (frames > 512)
    frames = 512;
  clap_host_process(s->plugin,
                    s->is_source ? NULL : in_l,
                    s->is_source ? NULL : in_r,
                    tmp_l, tmp_r, frames);
  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] += tmp_l[f];
    out_r[f] += tmp_r[f];
  }
}

const UnitDef unit_clap = {
    .id = "clap",
    .name = "CLAP",
    .data_hint = "plugin.clap",
    .file_filter = "*.clap",
    .is_source = true,
    .num_params = 0,
    .dyn_num_params = clap_dyn_num_params,
    .dyn_param_name = clap_dyn_param_name,
    .get_param_val = clap_get_param_val,
    .set_param_val = clap_set_param_val,
    .get_param_default = clap_get_param_default,
    .sync_to_data = clap_sync_to_data,
    .format_param_val = clap_format_param_val,
    .picker_count = clap_picker_count,
    .picker_name = clap_picker_name,
    .picker_add = clap_picker_add,
    .mapping_remove = clap_mapping_remove,
    .create = clap_unit_create,
    .destroy = clap_unit_destroy,
    .set_data = clap_unit_set_data,
    .note_on = clap_unit_note_on,
    .note_off = clap_unit_note_off,
    .kill = clap_unit_kill,
    .render = clap_unit_render,
};
