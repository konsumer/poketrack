// Minimal CLAP test instrument: sawtooth oscillator with gain + cutoff params.
// Used to verify that the raypoketrack CLAP host correctly delivers param events.
// If gain/cutoff changes are audible here but not in Surge XT, the issue is Surge-specific.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"

#define PLUGIN_ID "raypoketrack.test.oscillator"
#define PLUGIN_NAME "Test Oscillator"
#define PARAM_GAIN   0
#define PARAM_CUTOFF 1
#define NUM_PARAMS   2

typedef struct {
  const clap_host_t *host;
  double sample_rate;

  // Atomic so audio thread can read while main thread (theoretically) writes.
  // In practice clap_event_param_value arrives in process(), same audio thread.
  double gain;    // 0.0 - 1.0
  double cutoff;  // 0.0 - 1.0 (filter coefficient)

  // Oscillator state
  double phase;
  double freq;
  bool playing;

  // One-pole lowpass state
  double lp_prev;
} TestOsc;

// ---- param helpers ----

static void apply_param(TestOsc *osc, clap_id id, double val) {
  if (id == PARAM_GAIN)
    osc->gain = val < 0.0 ? 0.0 : val > 1.0 ? 1.0 : val;
  else if (id == PARAM_CUTOFF)
    osc->cutoff = val < 0.001 ? 0.001 : val > 1.0 ? 1.0 : val;
}

// ---- plugin callbacks ----

static bool plugin_init(const clap_plugin_t *plugin) {
  TestOsc *osc = (TestOsc *)plugin->plugin_data;
  osc->gain = 0.5;
  osc->cutoff = 1.0;
  osc->phase = 0.0;
  osc->freq = 0.0;
  osc->playing = false;
  osc->lp_prev = 0.0;
  return true;
}

static void plugin_destroy(const clap_plugin_t *plugin) {
  free(plugin->plugin_data);
  free((void *)plugin);
}

static bool plugin_activate(const clap_plugin_t *plugin, double sr,
                            uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames; (void)max_frames;
  TestOsc *osc = (TestOsc *)plugin->plugin_data;
  osc->sample_rate = sr;
  return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) { (void)plugin; }

static bool plugin_start_processing(const clap_plugin_t *plugin) {
  (void)plugin;
  return true;
}
static void plugin_stop_processing(const clap_plugin_t *plugin) { (void)plugin; }
static void plugin_reset(const clap_plugin_t *plugin) { (void)plugin; }

static clap_process_status plugin_process(const clap_plugin_t *plugin,
                                          const clap_process_t *proc) {
  TestOsc *osc = (TestOsc *)plugin->plugin_data;
  uint32_t nframes = proc->frames_count;
  float **out = proc->audio_outputs[0].data32;

  // Process all events first
  uint32_t nevents = proc->in_events->size(proc->in_events);
  for (uint32_t i = 0; i < nevents; i++) {
    const clap_event_header_t *hdr = proc->in_events->get(proc->in_events, i);
    if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
    if (hdr->type == CLAP_EVENT_NOTE_ON) {
      const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
      osc->freq = 440.0 * pow(2.0, (ev->key - 69) / 12.0);
      osc->playing = true;
      osc->phase = 0.0;
      osc->lp_prev = 0.0;
    } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
      osc->playing = false;
    } else if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
      const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
      apply_param(osc, ev->param_id, ev->value);
    }
  }

  if (!osc->playing || osc->sample_rate <= 0.0) {
    memset(out[0], 0, nframes * sizeof(float));
    memset(out[1], 0, nframes * sizeof(float));
    return CLAP_PROCESS_CONTINUE;
  }

  double phase_inc = osc->freq / osc->sample_rate;
  // Simple one-pole LP: y = a*x + (1-a)*y_prev, where a = cutoff (0=silent, 1=bypass)
  double a = osc->cutoff;
  double prev = osc->lp_prev;

  for (uint32_t f = 0; f < nframes; f++) {
    // Sawtooth: phase in [0,1) -> sample in [-1,1)
    float saw = (float)(2.0 * osc->phase - 1.0);
    osc->phase += phase_inc;
    if (osc->phase >= 1.0) osc->phase -= 1.0;

    // One-pole lowpass
    prev = a * (double)saw + (1.0 - a) * prev;
    float s = (float)(prev * osc->gain);
    out[0][f] = s;
    out[1][f] = s;
  }
  osc->lp_prev = prev;
  return CLAP_PROCESS_CONTINUE;
}

static const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
static void plugin_on_main_thread(const clap_plugin_t *plugin) { (void)plugin; }

// ---- params extension ----

static uint32_t params_count(const clap_plugin_t *plugin) {
  (void)plugin;
  return NUM_PARAMS;
}

static bool params_get_info(const clap_plugin_t *plugin, uint32_t idx,
                            clap_param_info_t *info) {
  (void)plugin;
  memset(info, 0, sizeof(*info));
  if (idx == PARAM_GAIN) {
    info->id = PARAM_GAIN;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    strncpy(info->name, "Gain", sizeof(info->name));
    info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.5;
    return true;
  }
  if (idx == PARAM_CUTOFF) {
    info->id = PARAM_CUTOFF;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    strncpy(info->name, "Cutoff", sizeof(info->name));
    info->min_value = 0.001; info->max_value = 1.0; info->default_value = 1.0;
    return true;
  }
  return false;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id id, double *val) {
  TestOsc *osc = (TestOsc *)plugin->plugin_data;
  if (id == PARAM_GAIN) { *val = osc->gain; return true; }
  if (id == PARAM_CUTOFF) { *val = osc->cutoff; return true; }
  return false;
}

static bool params_value_to_text(const clap_plugin_t *p, clap_id id, double val,
                                  char *buf, uint32_t sz) {
  (void)p; (void)id;
  snprintf(buf, sz, "%.3f", val);
  return true;
}
static bool params_text_to_value(const clap_plugin_t *p, clap_id id, const char *txt,
                                  double *val) {
  (void)p; (void)id;
  *val = atof(txt);
  return true;
}
static void params_flush(const clap_plugin_t *plugin,
                         const clap_input_events_t *in,
                         const clap_output_events_t *out) {
  (void)out;
  TestOsc *osc = (TestOsc *)plugin->plugin_data;
  uint32_t n = in->size(in);
  for (uint32_t i = 0; i < n; i++) {
    const clap_event_header_t *hdr = in->get(in, i);
    if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID &&
        hdr->type == CLAP_EVENT_PARAM_VALUE) {
      const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
      apply_param(osc, ev->param_id, ev->value);
    }
  }
}

static const clap_plugin_params_t s_params = {
  params_count, params_get_info, params_get_value,
  params_value_to_text, params_text_to_value, params_flush,
};

// ---- audio ports extension ----

static uint32_t audio_ports_count(const clap_plugin_t *p, bool is_input) {
  (void)p;
  return is_input ? 0 : 1;
}
static bool audio_ports_get(const clap_plugin_t *p, uint32_t idx, bool is_input,
                             clap_audio_port_info_t *info) {
  (void)p;
  if (is_input || idx != 0) return false;
  memset(info, 0, sizeof(*info));
  info->id = 0;
  strncpy(info->name, "Main Out", sizeof(info->name));
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
static const clap_plugin_audio_ports_t s_audio_ports = { audio_ports_count, audio_ports_get };

// ---- note ports extension ----

static uint32_t note_ports_count(const clap_plugin_t *p, bool is_input) {
  (void)p;
  return is_input ? 1 : 0;
}
static bool note_ports_get(const clap_plugin_t *p, uint32_t idx, bool is_input,
                            clap_note_port_info_t *info) {
  (void)p;
  if (!is_input || idx != 0) return false;
  memset(info, 0, sizeof(*info));
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  strncpy(info->name, "Notes In", sizeof(info->name));
  return true;
}
static const clap_plugin_note_ports_t s_note_ports = { note_ports_count, note_ports_get };

// ---- get_extension ----

static const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
  (void)plugin;
  if (strcmp(id, CLAP_EXT_PARAMS)      == 0) return &s_params;
  if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
  if (strcmp(id, CLAP_EXT_NOTE_PORTS)  == 0) return &s_note_ports;
  return NULL;
}

// ---- factory ----

static const clap_plugin_descriptor_t s_desc = {
  .clap_version = CLAP_VERSION_INIT,
  .id = PLUGIN_ID,
  .name = PLUGIN_NAME,
  .vendor = "raypoketrack",
  .url = "",
  .manual_url = "",
  .support_url = "",
  .version = "1.0.0",
  .description = "Minimal sawtooth oscillator for host diagnostics",
  .features = (const char *[]){ CLAP_PLUGIN_FEATURE_INSTRUMENT, NULL },
};

static uint32_t factory_count(const clap_plugin_factory_t *f) { (void)f; return 1; }
static const clap_plugin_descriptor_t *factory_get_descriptor(const clap_plugin_factory_t *f,
                                                               uint32_t idx) {
  (void)f;
  return idx == 0 ? &s_desc : NULL;
}
static const clap_plugin_t *factory_create(const clap_plugin_factory_t *f,
                                            const clap_host_t *host, const char *id) {
  (void)f;
  if (strcmp(id, PLUGIN_ID) != 0) return NULL;
  clap_plugin_t *plugin = calloc(1, sizeof(*plugin));
  TestOsc *osc = calloc(1, sizeof(*osc));
  osc->host = host;
  plugin->desc = &s_desc;
  plugin->plugin_data = osc;
  plugin->init = plugin_init;
  plugin->destroy = plugin_destroy;
  plugin->activate = plugin_activate;
  plugin->deactivate = plugin_deactivate;
  plugin->start_processing = plugin_start_processing;
  plugin->stop_processing = plugin_stop_processing;
  plugin->reset = plugin_reset;
  plugin->process = plugin_process;
  plugin->get_extension = plugin_get_extension;
  plugin->on_main_thread = plugin_on_main_thread;
  return plugin;
}

static const clap_plugin_factory_t s_factory = {
  factory_count, factory_get_descriptor, factory_create,
};

// ---- entry point ----

static bool entry_init(const char *path) { (void)path; return true; }
static void entry_deinit(void) {}
static const void *entry_get_factory(const char *id) {
  if (strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) return &s_factory;
  return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
  .clap_version = CLAP_VERSION_INIT,
  .init = entry_init,
  .deinit = entry_deinit,
  .get_factory = entry_get_factory,
};
