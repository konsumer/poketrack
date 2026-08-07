// pthread-synth — a minimal plain-C WCLAP instrument, built with wasi-sdk's
// `-pthread` toolchain, that spawns a real background thread on init() to
// fill a sine wavetable, then plays it back monophonically.
//
// It's a working example of CLAP's C API without any wrapper library (see
// plugins/README.md for AssemblyScript/Rust/PureData alternatives), and
// doubles as the repro for poketrack's web WCLAP host supporting real
// threads: this plugin will not load in a single-threaded host, since it
// genuinely relies on wasi.thread-spawn to run.
//
// IMPORTANT — do not pthread_join() a thread from init()/activate()/process():
// those run on poketrack's main thread, and joining blocks on the wasm
// `memory.atomic.wait` instruction, which browsers refuse to execute on the
// main thread (Workers are fine — Wasmtime, used by poketrack's native host,
// has no such restriction either). Spawn-and-detach, then poll a flag — the
// pattern below — is what actually works everywhere. See the "web" section
// in plugins/README.md for the full explanation.
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"

#define WAVETABLE_SIZE 1024

typedef struct {
  clap_plugin_t plugin;
  const clap_host_t* host;

  float wavetable[WAVETABLE_SIZE];
  atomic_bool wavetable_ready;

  double sample_rate;
  bool note_active;
  int16_t note_key;
  double phase, phase_inc;
  double gain; // 0..1, param id 0
} plugin_data_t;

static void* fill_wavetable(void* arg) {
  plugin_data_t* p = (plugin_data_t*)arg;
  for (int i = 0; i < WAVETABLE_SIZE; i++)
    p->wavetable[i] = (float)sin(2.0 * M_PI * i / WAVETABLE_SIZE);
  atomic_store(&p->wavetable_ready, true);
  return NULL;
}

// ---- clap_plugin ----

static bool p_init(const clap_plugin_t* plugin) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  pthread_t t;
  if (pthread_create(&t, NULL, fill_wavetable, p) != 0)
    return false;
  pthread_detach(t); // fire-and-forget — see the note at the top of this file
  return true;
}

static void p_destroy(const clap_plugin_t* plugin) {
  free(plugin->plugin_data);
}

static bool p_activate(const clap_plugin_t* plugin, double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  p->sample_rate = sample_rate;
  return true;
}

static void p_deactivate(const clap_plugin_t* plugin) {}
static bool p_start_processing(const clap_plugin_t* plugin) { return true; }
static void p_stop_processing(const clap_plugin_t* plugin) {}
static void p_reset(const clap_plugin_t* plugin) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  p->note_active = false;
  p->phase = 0;
}

static void handle_event(plugin_data_t* p, const clap_event_header_t* hdr) {
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  if (hdr->type == CLAP_EVENT_NOTE_ON) {
    const clap_event_note_t* ev = (const clap_event_note_t*)hdr;
    p->note_active = true;
    p->note_key = ev->key;
    p->phase = 0;
    p->phase_inc = 440.0 * pow(2.0, (ev->key - 69) / 12.0) / p->sample_rate;
  } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
    const clap_event_note_t* ev = (const clap_event_note_t*)hdr;
    if (p->note_active && p->note_key == ev->key)
      p->note_active = false;
  } else if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
    const clap_event_param_value_t* ev = (const clap_event_param_value_t*)hdr;
    if (ev->param_id == 0)
      p->gain = ev->value;
  }
}

static clap_process_status p_process(const clap_plugin_t* plugin, const clap_process_t* process) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  uint32_t n = process->frames_count;

  uint32_t ev_count = process->in_events->size(process->in_events);
  uint32_t ev_idx = 0, next_ev_frame = ev_count ? 0 : n;

  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : out_l;

  bool ready = atomic_load(&p->wavetable_ready);

  for (uint32_t i = 0; i < n; i++) {
    while (ev_idx < ev_count) {
      const clap_event_header_t* hdr = process->in_events->get(process->in_events, ev_idx);
      if (hdr->time != i) break;
      handle_event(p, hdr);
      ev_idx++;
    }

    float sample = 0.0f;
    if (ready && p->note_active) {
      double table_pos = p->phase * WAVETABLE_SIZE;
      int i0 = (int)table_pos;
      float frac = (float)(table_pos - i0);
      float s0 = p->wavetable[i0 % WAVETABLE_SIZE];
      float s1 = p->wavetable[(i0 + 1) % WAVETABLE_SIZE];
      sample = (s0 + (s1 - s0) * frac) * (float)p->gain;
      p->phase += p->phase_inc;
      if (p->phase >= 1.0) p->phase -= 1.0;
    }
    out_l[i] = sample;
    out_r[i] = sample;
  }

  return CLAP_PROCESS_CONTINUE;
}

static uint32_t p_note_ports_count(const clap_plugin_t* plugin, bool is_input) { return is_input ? 1 : 0; }
static bool p_note_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info) {
  if (!is_input || index != 0) return false;
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  strncpy(info->name, "note in", CLAP_NAME_SIZE);
  return true;
}
static const clap_plugin_note_ports_t note_ports_ext = {
  .count = p_note_ports_count,
  .get = p_note_ports_get,
};

static uint32_t p_params_count(const clap_plugin_t* plugin) { return 1; }
static bool p_params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
  if (index != 0) return false;
  memset(info, 0, sizeof(*info));
  info->id = 0;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  strncpy(info->name, "gain", CLAP_NAME_SIZE);
  info->min_value = 0.0;
  info->max_value = 1.0;
  info->default_value = 0.5;
  return true;
}
static bool p_params_get_value(const clap_plugin_t* plugin, clap_id id, double* out_value) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  if (id != 0) return false;
  *out_value = p->gain;
  return true;
}
static bool p_params_value_to_text(const clap_plugin_t* plugin, clap_id id, double value, char* out, uint32_t out_size) {
  snprintf(out, out_size, "%.2f", value);
  return true;
}
static bool p_params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* out_value) {
  *out_value = atof(text);
  return true;
}
static void p_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
  plugin_data_t* p = (plugin_data_t*)plugin->plugin_data;
  uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; i++) handle_event(p, in->get(in, i));
}
static const clap_plugin_params_t params_ext = {
  .count = p_params_count,
  .get_info = p_params_get_info,
  .get_value = p_params_get_value,
  .value_to_text = p_params_value_to_text,
  .text_to_value = p_params_text_to_value,
  .flush = p_params_flush,
};

static const void* p_get_extension(const clap_plugin_t* plugin, const char* id) {
  if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &note_ports_ext;
  if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &params_ext;
  return NULL;
}
static void p_on_main_thread(const clap_plugin_t* plugin) {}

// ---- factory ----

static const clap_plugin_descriptor_t descriptor = {
  .clap_version = CLAP_VERSION_INIT,
  .id = "com.poketrack.plugins.pthread-synth",
  .name = "pthread-synth",
  .vendor = "poketrack",
  .url = "",
  .manual_url = "",
  .support_url = "",
  .version = "0.1.0",
  .description = "monophonic sine synth that builds its wavetable on a real background thread",
  .features = (const char*[]){ CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL },
};

static const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id) {
  if (strcmp(plugin_id, descriptor.id) != 0) return NULL;
  plugin_data_t* p = calloc(1, sizeof(plugin_data_t));
  p->host = host;
  p->gain = 0.5;
  p->plugin.desc = &descriptor;
  p->plugin.plugin_data = p;
  p->plugin.init = p_init;
  p->plugin.destroy = p_destroy;
  p->plugin.activate = p_activate;
  p->plugin.deactivate = p_deactivate;
  p->plugin.start_processing = p_start_processing;
  p->plugin.stop_processing = p_stop_processing;
  p->plugin.reset = p_reset;
  p->plugin.process = p_process;
  p->plugin.get_extension = p_get_extension;
  p->plugin.on_main_thread = p_on_main_thread;
  return &p->plugin;
}

static uint32_t get_plugin_count(const clap_plugin_factory_t* factory) { return 1; }
static const clap_plugin_descriptor_t* get_plugin_descriptor(const clap_plugin_factory_t* factory, uint32_t index) {
  return index == 0 ? &descriptor : NULL;
}
static const clap_plugin_factory_t factory = {
  .get_plugin_count = get_plugin_count,
  .get_plugin_descriptor = get_plugin_descriptor,
  .create_plugin = create_plugin,
};

static bool entry_init(const char* plugin_path) { return true; }
static void entry_deinit(void) {}
static const void* entry_get_factory(const char* factory_id) {
  return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
  .clap_version = CLAP_VERSION_INIT,
  .init = entry_init,
  .deinit = entry_deinit,
  .get_factory = entry_get_factory,
};
