// runtime-shim.c — the CLAP "architecture file" for pdast2wclap-generated
// PD patches. Plugin-agnostic: every symbol that varies per-patch (the DSP
// itself, the param table, whether the patch uses adc~/notein) comes from
// the generated pd_*.c this is compiled and linked together with (see
// pd_wclap.h for that fixed contract). Only the plugin id/name are
// patch-specific here, and those are supplied at compile time via
// -DPD_PLUGIN_ID=... -DPD_PLUGIN_NAME=... (see build.sh).
//
// Written directly against raw CLAP structs (no framework) since this is a
// single, hand-maintained file shared by every generated patch — see
// plugins/karplus (AssemblyScript/as-clap) and plugins/robotalk (Rust/clack)
// for the equivalent boilerplate in those languages. The event-time-splitting
// process() loop mirrors karplus's hand-rolled "render up to next event,
// then apply it" pattern, since plain C has no clack-style batch() helper.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"
#include "pd_wclap.h"

#ifndef PD_PLUGIN_ID
#define PD_PLUGIN_ID "com.poketrack.clap.pd-patch"
#endif
#ifndef PD_PLUGIN_NAME
#define PD_PLUGIN_NAME "PD Patch (C)"
#endif

// ── Plugin instance ─────────────────────────────────────────────────────────

typedef struct {
  clap_plugin_t plugin;
  const clap_host_t* host;
  PdState* pd;
  double sample_rate;
} PluginInstance;

static PluginInstance* self_of(const clap_plugin_t* p) {
  return (PluginInstance*)p->plugin_data;
}

// ── clap.audio-ports ─────────────────────────────────────────────────────────

static uint32_t audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
  (void)plugin;
  if (is_input)
    return PD_HAS_AUDIO_IN ? 1 : 0;
  return 1;  // always one main stereo output
}

static bool audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input,
                            clap_audio_port_info_t* info) {
  (void)plugin;
  if (index != 0)
    return false;
  if (is_input && !PD_HAS_AUDIO_IN)
    return false;
  info->id = is_input ? 1 : 0;
  snprintf(info->name, CLAP_NAME_SIZE, "%s", is_input ? "in" : "main");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

// ── clap.note-ports ──────────────────────────────────────────────────────────

static uint32_t note_ports_count(const clap_plugin_t* plugin, bool is_input) {
  (void)plugin;
  return (is_input && PD_HAS_NOTE_IN) ? 1 : 0;
}

static bool note_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input,
                           clap_note_port_info_t* info) {
  (void)plugin;
  if (!is_input || index != 0 || !PD_HAS_NOTE_IN)
    return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  snprintf(info->name, CLAP_NAME_SIZE, "notes");
  return true;
}

static const clap_plugin_note_ports_t s_note_ports = {
    .count = note_ports_count,
    .get = note_ports_get,
};

// ── clap.params ──────────────────────────────────────────────────────────────

static uint32_t params_count(const clap_plugin_t* plugin) {
  (void)plugin;
  return (uint32_t)PD_NUM_PARAMS;
}

static bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
  (void)plugin;
  if (index >= (uint32_t)PD_NUM_PARAMS)
    return false;
  const PdParamInfo* p = &PD_PARAMS[index];
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  info->cookie = NULL;
  snprintf(info->name, CLAP_NAME_SIZE, "%s", p->name);
  info->module[0] = '\0';
  info->min_value = p->min;
  info->max_value = p->max;
  info->default_value = p->default_value;
  return true;
}

static bool params_get_value(const clap_plugin_t* plugin, clap_id param_id, double* out_value) {
  PluginInstance* self = self_of(plugin);
  if (param_id >= (uint32_t)PD_NUM_PARAMS)
    return false;
  *out_value = pd_get_param(self->pd, (int32_t)param_id);
  return true;
}

static bool params_value_to_text(const clap_plugin_t* plugin, clap_id param_id, double value,
                                 char* out_buffer, uint32_t out_buffer_capacity) {
  (void)plugin;
  if (param_id >= (uint32_t)PD_NUM_PARAMS)
    return false;
  snprintf(out_buffer, out_buffer_capacity, "%.3f", value);
  return true;
}

static bool params_text_to_value(const clap_plugin_t* plugin, clap_id param_id,
                                 const char* param_value_text, double* out_value) {
  (void)plugin;
  if (param_id >= (uint32_t)PD_NUM_PARAMS)
    return false;
  *out_value = atof(param_value_text);
  return true;
}

// Apply every CLAP_EVENT_PARAM_VALUE / NOTE_ON / NOTE_OFF in `in` to `self`,
// ignoring timing (used by both flush() and, per-sub-block, by process()).
static void apply_event(PluginInstance* self, const clap_event_header_t* hdr) {
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID)
    return;
  if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
    const clap_event_param_value_t* ev = (const clap_event_param_value_t*)hdr;
    if (ev->param_id < (uint32_t)PD_NUM_PARAMS)
      pd_set_param(self->pd, (int32_t)ev->param_id, ev->value);
  } else if (hdr->type == CLAP_EVENT_NOTE_ON) {
    const clap_event_note_t* ev = (const clap_event_note_t*)hdr;
    pd_note_on(self->pd, ev->key, ev->velocity);
  } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
    const clap_event_note_t* ev = (const clap_event_note_t*)hdr;
    pd_note_off(self->pd, ev->key, ev->velocity);
  }
}

static void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                         const clap_output_events_t* out) {
  (void)out;
  PluginInstance* self = self_of(plugin);
  uint32_t n = in->size(in);
  for (uint32_t i = 0; i < n; i++) apply_event(self, in->get(in, i));
}

static const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

// ── clap_plugin_t vtable ─────────────────────────────────────────────────────

static bool plugin_init(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}

static void plugin_destroy(const clap_plugin_t* plugin) {
  PluginInstance* self = self_of(plugin);
  if (self->pd)
    pd_destroy(self->pd);
  free(self);
}

static bool plugin_activate(const clap_plugin_t* plugin, double sample_rate, uint32_t min_frames,
                            uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;
  PluginInstance* self = self_of(plugin);
  self->sample_rate = sample_rate;
  if (self->pd)
    pd_destroy(self->pd);
  self->pd = pd_create(sample_rate);
  return self->pd != NULL;
}

static void plugin_deactivate(const clap_plugin_t* plugin) {
  PluginInstance* self = self_of(plugin);
  if (self->pd)
    pd_destroy(self->pd);
  self->pd = NULL;
}

static bool plugin_start_processing(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}
static void plugin_stop_processing(const clap_plugin_t* plugin) { (void)plugin; }

static void plugin_reset(const clap_plugin_t* plugin) {
  PluginInstance* self = self_of(plugin);
  if (!self->pd)
    return;
  pd_destroy(self->pd);
  self->pd = pd_create(self->sample_rate);
}

static clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
  PluginInstance* self = self_of(plugin);
  if (!self->pd)
    return CLAP_PROCESS_ERROR;

  const float* in_l = NULL;
  const float* in_r = NULL;
  if (PD_HAS_AUDIO_IN && process->audio_inputs_count > 0 && process->audio_inputs[0].data32) {
    in_l = process->audio_inputs[0].data32[0];
    in_r = process->audio_inputs[0].channel_count > 1 ? process->audio_inputs[0].data32[1] : in_l;
  }
  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : out_l;

  const clap_input_events_t* in_events = process->in_events;
  uint32_t n_events = in_events->size(in_events);
  uint32_t frames = process->frames_count;

  // Split the block at each event's sample-accurate time offset: apply every
  // event due at-or-before the current position (never producing a
  // zero-width render, since only strictly-future events can set block_end),
  // then render up to the next one.
  uint32_t sample_pos = 0;
  uint32_t event_idx = 0;
  while (sample_pos < frames) {
    uint32_t block_end = frames;
    while (event_idx < n_events) {
      const clap_event_header_t* hdr = in_events->get(in_events, event_idx);
      if (hdr->time > sample_pos) {
        if (hdr->time < block_end)
          block_end = hdr->time;
        break;
      }
      apply_event(self, hdr);
      event_idx++;
    }
    uint32_t block_frames = block_end - sample_pos;
    pd_process(self->pd, in_l ? in_l + sample_pos : NULL, in_r ? in_r + sample_pos : NULL,
               out_l + sample_pos, out_r + sample_pos, block_frames);
    sample_pos = block_end;
  }

  return CLAP_PROCESS_CONTINUE;
}

static const void* plugin_get_extension(const clap_plugin_t* plugin, const char* id) {
  (void)plugin;
  if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
    return &s_audio_ports;
  if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
    return &s_note_ports;
  if (strcmp(id, CLAP_EXT_PARAMS) == 0)
    return &s_params;
  return NULL;
}

static void plugin_on_main_thread(const clap_plugin_t* plugin) { (void)plugin; }

// ── Descriptor / factory / entry ────────────────────────────────────────────

static const char* const s_features[] = {"instrument", NULL};

static const clap_plugin_descriptor_t s_descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = PD_PLUGIN_ID,
    .name = PD_PLUGIN_NAME,
    .vendor = "poketrack",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "Compiled from a Pure Data patch via pdast2wclap",
    .features = s_features,
};

static const clap_plugin_t* factory_create_plugin(const struct clap_plugin_factory* factory,
                                                  const clap_host_t* host, const char* plugin_id) {
  (void)factory;
  if (strcmp(plugin_id, s_descriptor.id) != 0)
    return NULL;
  PluginInstance* self = (PluginInstance*)calloc(1, sizeof(PluginInstance));
  if (!self)
    return NULL;
  self->host = host;
  self->pd = NULL;
  self->sample_rate = 48000.0;
  self->plugin.desc = &s_descriptor;
  self->plugin.plugin_data = self;
  self->plugin.init = plugin_init;
  self->plugin.destroy = plugin_destroy;
  self->plugin.activate = plugin_activate;
  self->plugin.deactivate = plugin_deactivate;
  self->plugin.start_processing = plugin_start_processing;
  self->plugin.stop_processing = plugin_stop_processing;
  self->plugin.reset = plugin_reset;
  self->plugin.process = plugin_process;
  self->plugin.get_extension = plugin_get_extension;
  self->plugin.on_main_thread = plugin_on_main_thread;
  return &self->plugin;
}

static uint32_t factory_get_plugin_count(const struct clap_plugin_factory* factory) {
  (void)factory;
  return 1;
}

static const clap_plugin_descriptor_t* factory_get_plugin_descriptor(
    const struct clap_plugin_factory* factory, uint32_t index) {
  (void)factory;
  return index == 0 ? &s_descriptor : NULL;
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

static bool entry_init(const char* plugin_path) {
  (void)plugin_path;
  return true;
}
static void entry_deinit(void) {}

static const void* entry_get_factory(const char* factory_id) {
  if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
    return &s_factory;
  return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory,
};
