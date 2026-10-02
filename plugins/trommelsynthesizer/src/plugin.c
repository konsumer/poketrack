// trommelsynthesizer: CLAP wrapper around the drum-machine engine in trommelsynthesizer.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"
#include "trommelsynthesizer.h"

#define PLUGIN_ID "com.poketrack.plugins.trommelsynthesizer"

typedef struct {
  clap_plugin_t plugin;
  const clap_host_t* host;
  Engine* eng;
  double sample_rate;
} Plugin;

// ---- engine lifetime -------------------------------------------------------

// The engine needs the sample rate up front, but hosts can set params before
// activate(), so a rate change rebuilds it and carries every param across.
static void rebuild(Plugin* p, double sr) {
  Engine* e = eng_create(sr);
  if (p->eng) {
    for (uint32_t id = 0; id < PID_COUNT; id++) eng_set_param(e, id, eng_get_param(p->eng, id));
    eng_destroy(p->eng);
  }
  p->eng = e;
  p->sample_rate = sr;
}

static bool plugin_init(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}

static void plugin_destroy(const clap_plugin_t* plugin) {
  Plugin* p = plugin->plugin_data;
  eng_destroy(p->eng);
  free(p);
}

static bool plugin_activate(const clap_plugin_t* plugin, double sr, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;
  Plugin* p = plugin->plugin_data;
  if (sr != p->sample_rate)
    rebuild(p, sr);
  return true;
}

static void plugin_deactivate(const clap_plugin_t* plugin) { (void)plugin; }
static bool plugin_start_processing(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}
static void plugin_stop_processing(const clap_plugin_t* plugin) { (void)plugin; }

static void plugin_reset(const clap_plugin_t* plugin) {
  Plugin* p = plugin->plugin_data;
  eng_reset(p->eng);
}

// ---- events ----------------------------------------------------------------

static void handle_event(Plugin* p, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
    return;
  switch (h->type) {
    case CLAP_EVENT_NOTE_ON: {
      const clap_event_note_t* ev = (const clap_event_note_t*)h;
      eng_note_on(p->eng, ev->key, (float)ev->velocity);
      break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
      const clap_event_param_value_t* ev = (const clap_event_param_value_t*)h;
      eng_set_param(p->eng, ev->param_id, ev->value);
      break;
    }
    default:
      break;  // drums are one-shots, so note-off does nothing
  }
}

static clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* proc) {
  Plugin* p = plugin->plugin_data;
  uint32_t frames = proc->frames_count;
  uint32_t count = proc->in_events->size(proc->in_events);
  float* l = proc->audio_outputs[0].data32[0];
  float* r = proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : l;

  uint32_t pos = 0, ei = 0;
  while (pos < frames) {
    while (ei < count) {
      const clap_event_header_t* h = proc->in_events->get(proc->in_events, ei);
      if (h->time > pos)
        break;
      handle_event(p, h);
      ei++;
    }
    uint32_t next = frames;
    if (ei < count) {
      uint32_t t = proc->in_events->get(proc->in_events, ei)->time;
      if (t < next)
        next = t;
    }
    eng_render(p->eng, l + pos, r + pos, next - pos);
    pos = next;
  }
  return CLAP_PROCESS_CONTINUE;
}

// ---- ports -----------------------------------------------------------------

static uint32_t note_ports_count(const clap_plugin_t* plugin, bool is_input) {
  (void)plugin;
  return is_input ? 1 : 0;
}

static bool note_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info) {
  (void)plugin;
  if (!is_input || index != 0)
    return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  snprintf(info->name, sizeof info->name, "%s", "note in");
  return true;
}

static const clap_plugin_note_ports_t note_ports_ext = {.count = note_ports_count, .get = note_ports_get};

static uint32_t audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
  (void)plugin;
  return is_input ? 0 : 1;
}

static bool audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
  (void)plugin;
  if (is_input || index != 0)
    return false;
  memset(info, 0, sizeof *info);
  info->id = 0;
  snprintf(info->name, sizeof info->name, "%s", "output");
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

static const clap_plugin_audio_ports_t audio_ports_ext = {.count = audio_ports_count, .get = audio_ports_get};

// ---- params ----------------------------------------------------------------

static uint32_t params_count(const clap_plugin_t* plugin) {
  (void)plugin;
  return param_count();
}

static bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
  (void)plugin;
  ParamInfo pi;
  if (!param_info(index, &pi))
    return false;
  memset(info, 0, sizeof *info);
  info->id = pi.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (pi.stepped)
    info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
  snprintf(info->name, sizeof info->name, "%s", pi.name);
  snprintf(info->module, sizeof info->module, "%s", pi.module);
  info->min_value = pi.min;
  info->max_value = pi.max;
  info->default_value = pi.def;
  return true;
}

static bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
  Plugin* p = plugin->plugin_data;
  ParamInfo pi;
  if (!param_info_by_id(id, &pi))
    return false;
  *out = eng_get_param(p->eng, id);
  return true;
}

static bool params_value_to_text(const clap_plugin_t* plugin, clap_id id, double value, char* out, uint32_t size) {
  (void)plugin;
  ParamInfo pi;
  if (!param_info_by_id(id, &pi))
    return false;
  param_text(id, value, out, size);
  return true;
}

static bool params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* out) {
  (void)plugin;
  if (id == PID_MACHINE) {
    for (int m = 0; m < M_COUNT; m++) {
      if (strcmp(text, machine_name(m)) == 0) {
        *out = m;
        return true;
      }
    }
  }
  *out = atof(text);
  if (id != PID_MACHINE && (id < 2 || (id - 2) % P_COUNT != P_TUNE))
    *out /= 100.0;
  return true;
}

static void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
  (void)out;
  Plugin* p = plugin->plugin_data;
  uint32_t n = in->size(in);
  for (uint32_t i = 0; i < n; i++) handle_event(p, in->get(in, i));
}

static const clap_plugin_params_t params_ext = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

static const void* plugin_get_extension(const clap_plugin_t* plugin, const char* id) {
  (void)plugin;
  if (strcmp(id, CLAP_EXT_PARAMS) == 0)
    return &params_ext;
  if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
    return &note_ports_ext;
  if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
    return &audio_ports_ext;
  return NULL;
}

static void plugin_on_main_thread(const clap_plugin_t* plugin) { (void)plugin; }

// ---- factory ---------------------------------------------------------------

static const clap_plugin_descriptor_t descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = PLUGIN_ID,
    .name = "Trommelsynthesizer",
    .vendor = "poketrack",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "Drum machines (TR-808/909 synthesized, TR-707/505 sampled, SDS-V, CR-78) on General MIDI keys",
    .features = (const char*[]){CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE, CLAP_PLUGIN_FEATURE_STEREO, NULL},
};

static const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* id) {
  (void)factory;
  if (strcmp(id, PLUGIN_ID) != 0)
    return NULL;
  Plugin* p = calloc(1, sizeof *p);
  p->host = host;
  rebuild(p, 48000.0);
  p->plugin.desc = &descriptor;
  p->plugin.plugin_data = p;
  p->plugin.init = plugin_init;
  p->plugin.destroy = plugin_destroy;
  p->plugin.activate = plugin_activate;
  p->plugin.deactivate = plugin_deactivate;
  p->plugin.start_processing = plugin_start_processing;
  p->plugin.stop_processing = plugin_stop_processing;
  p->plugin.reset = plugin_reset;
  p->plugin.process = plugin_process;
  p->plugin.get_extension = plugin_get_extension;
  p->plugin.on_main_thread = plugin_on_main_thread;
  return &p->plugin;
}

static uint32_t get_plugin_count(const clap_plugin_factory_t* factory) {
  (void)factory;
  return 1;
}

static const clap_plugin_descriptor_t* get_plugin_descriptor(const clap_plugin_factory_t* factory, uint32_t index) {
  (void)factory;
  return index == 0 ? &descriptor : NULL;
}

static const clap_plugin_factory_t factory = {
    .get_plugin_count = get_plugin_count,
    .get_plugin_descriptor = get_plugin_descriptor,
    .create_plugin = create_plugin,
};

static bool entry_init(const char* path) {
  (void)path;
  return true;
}
static void entry_deinit(void) {}
static const void* entry_get_factory(const char* id) {
  return strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory,
};
