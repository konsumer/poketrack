// syntakt: CLAP wrapper around the Syntakt's digital voice engines (synth.c).
// The engines themselves aren't in this source: they're patched into the
// built .wasm from your own Syntakt OS file (see README.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"
#include "slot.h"
#include "synth.h"

#define PLUGIN_ID "com.poketrack.plugins.syntakt"

typedef struct {
  clap_plugin_t plugin;
  const clap_host_t* host;
  const clap_host_params_t* host_params;
  synth_t* synth;
  volatile bool rescan;  // the machine changed: ask the host to re-read knob names
} Plugin;

static bool plugin_init(const clap_plugin_t* plugin) {
  Plugin* p = plugin->plugin_data;
  p->host_params = p->host->get_extension(p->host, CLAP_EXT_PARAMS);
  if (!synth_has_engine(p->synth)) {
    const char* msg = "syntakt: this plugin has no engine yet, so it is silent. Make your own from your Syntakt OS file: see plugins/syntakt/README.md";
    const clap_host_log_t* log = p->host->get_extension(p->host, CLAP_EXT_LOG);
    if (log && log->log)
      log->log(p->host, CLAP_LOG_WARNING, msg);
    else
      fprintf(stderr, "%s\n", msg);
  }
  return true;
}

static void plugin_destroy(const clap_plugin_t* plugin) {
  Plugin* p = plugin->plugin_data;
  synth_free(p->synth);
  free(p);
}

static bool plugin_activate(const clap_plugin_t* plugin, double sr, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;
  Plugin* p = plugin->plugin_data;
  synth_set_rate(p->synth, sr);
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
  synth_reset(p->synth);
}

// ---- events ----------------------------------------------------------------

static void handle_event(Plugin* p, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
    return;
  switch (h->type) {
    case CLAP_EVENT_NOTE_ON: {
      const clap_event_note_t* ev = (const clap_event_note_t*)h;
      synth_note_on(p->synth, ev->key, ev->velocity);
      break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
      const clap_event_note_t* ev = (const clap_event_note_t*)h;
      synth_note_off(p->synth, ev->key);
      break;
    }
    case CLAP_EVENT_MIDI: {
      const clap_event_midi_t* ev = (const clap_event_midi_t*)h;
      uint8_t st = ev->data[0] & 0xf0;
      if (st == 0x90 && ev->data[2])
        synth_note_on(p->synth, ev->data[1], ev->data[2] / 127.0);
      else if (st == 0x80 || st == 0x90)
        synth_note_off(p->synth, ev->data[1]);
      break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
      const clap_event_param_value_t* ev = (const clap_event_param_value_t*)h;
      synth_set_param(p->synth, ev->param_id, ev->value);
      // knob names follow the machine: hosts may only rescan from the main thread
      if (synth_take_machine_changed(p->synth) && p->host_params) {
        p->rescan = true;
        p->host->request_callback(p->host);
      }
      break;
    }
    default:
      break;
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
    synth_render(p->synth, l + pos, r + pos, next - pos);
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
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
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
  return PARAM_COUNT;
}

static bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
  Plugin* p = plugin->plugin_data;
  param_info_t pi;
  if (!param_info(p->synth, index, &pi))
    return false;
  memset(info, 0, sizeof *info);
  info->id = pi.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (pi.stepped)
    info->flags |= CLAP_PARAM_IS_STEPPED;
  if (pi.is_enum)
    info->flags |= CLAP_PARAM_IS_ENUM;
  snprintf(info->name, sizeof info->name, "%s", pi.name);
  snprintf(info->module, sizeof info->module, "%s", pi.module);
  info->min_value = pi.min;
  info->max_value = pi.max;
  info->default_value = pi.def;
  return true;
}

static bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
  Plugin* p = plugin->plugin_data;
  param_info_t pi;
  if (!param_info_by_id(p->synth, id, &pi))
    return false;
  *out = synth_get_param(p->synth, id);
  return true;
}

static bool params_value_to_text(const clap_plugin_t* plugin, clap_id id, double value, char* out, uint32_t size) {
  Plugin* p = plugin->plugin_data;
  param_info_t pi;
  if (!param_info_by_id(p->synth, id, &pi))
    return false;
  param_text(p->synth, id, value, out, size);
  return true;
}

static bool params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* out) {
  Plugin* p = plugin->plugin_data;
  param_info_t pi;
  if (!param_info_by_id(p->synth, id, &pi))
    return false;
  if (id == PID_MACHINE) {
    for (int m = 0; m < MACHINE_COUNT; m++) {
      if (strcmp(text, machines[m].name) == 0) {
        *out = m;
        return true;
      }
    }
  }
  if (id == PID_GATE || id == PID_RELEASE) {
    if (strcmp(text, "OFF") == 0) {
      *out = id == PID_RELEASE ? 1.0 : 0.0;
      return true;
    }
    if (strcmp(text, "ON") == 0 && id == PID_GATE) {
      *out = 1.0;
      return true;
    }
  }
  // accept what value_to_text writes: an optional code, then the number
  const char* num = strpbrk(text, "+-0123456789");
  if (!num)
    return false;
  *out = atof(num);
  if (id == PID_LEVEL || id == PID_VELOCITY)
    *out /= 100.0;
  else if (id == PID_RELEASE)
    *out /= RELEASE_MAX * 1000.0;
  else if (param_knob(p->synth, id) && param_knob(p->synth, id)->min == 40 && param_knob(p->synth, id)->max == 88)
    *out += 64;
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

static void plugin_on_main_thread(const clap_plugin_t* plugin) {
  Plugin* p = plugin->plugin_data;
  if (p->rescan && p->host_params) {
    p->rescan = false;
    p->host_params->rescan(p->host, CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_VALUES);
  }
}

// ---- factory ---------------------------------------------------------------

static const clap_plugin_descriptor_t descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = PLUGIN_ID,
    .name = "Syntakt",
    .vendor = "poketrack",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.1.0",
    .description = "The Syntakt's 11 digital machines, run from your own Syntakt OS file",
    .features = (const char*[]){CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, NULL},
};

static const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* id) {
  (void)factory;
  if (strcmp(id, PLUGIN_ID) != 0)
    return NULL;
  Plugin* p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->host = host;
  p->synth = synth_new(slot_image(), 48000.0);
  if (!p->synth) {
    free(p);
    return NULL;
  }
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
