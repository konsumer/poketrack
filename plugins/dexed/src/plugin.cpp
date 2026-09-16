// Dexed as a WCLAP instrument — the CLAP C API directly, following
// plugins/pthread-synth, with the synth itself in engine.h behind it.
//
// The plugin is a straight adapter: params map 1:1 onto engine params (all
// 156 of dexed's own, plus Cartridge/Program/Engine for the things dexed only
// reaches from its GUI), and note events are handed to the engine with the
// same 1-based MIDI channel dexed's own JUCE build gives them, so its note
// allocation and MPE-channel behaviour come out identical.
//
// Everything a host can see is headless: no GUI state, no files. Dexed's
// 1056 factory voices are compiled in (cartridge 0 = Dexed's own startup bank,
// then the 32 SynprezFM banks, 32 voices each), and picking one recalls that
// voice exactly the way dexed's program list does.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"
#include "engine.h"
#include "params.h"

#define DEXED_PLUGIN_ID "com.poketrack.plugins.dexed"

// CLAP gives one event list per block; poketrack sends at most one note event
// per note plus up to UNIT_MAX_PARAMS param events per block.
#define MAX_PENDING_EVENTS 64

typedef struct {
  clap_plugin_t plugin;
  const clap_host_t* host;
  DexedEngine* engine;

  // Note events, sorted by time (CLAP requires input events to be sorted).
  DexedNoteEvent pending_notes[MAX_PENDING_EVENTS];
  int pending_note_count;
} dexed_plugin_t;

// --- params ----------------------------------------------------------------

static uint32_t params_count(const clap_plugin_t* plugin) {
  (void)plugin;
  return (uint32_t)dexed_num_params;
}

static bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  if ((int)index >= dexed_num_params)
    return false;
  const DexedParamDesc& d = dexed_params[index];

  memset(info, 0, sizeof(*info));
  info->id = d.id;
  info->flags = d.flags;
  snprintf(info->name, sizeof(info->name), "%s", d.name);
  info->min_value = d.min;
  info->max_value = d.max;
  info->default_value = dexed_param_default_value(*p->engine, d);
  return true;
}

static bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out_value) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  if (!dexed_param_by_id((uint32_t)id))
    return false;
  *out_value = p->engine->getParam((uint32_t)id);
  return true;
}

static bool params_value_to_text(const clap_plugin_t* plugin, clap_id id, double value, char* out, uint32_t out_size) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  const char* text = dexed_param_value_to_text(*p->engine, (uint32_t)id, value);
  if (!text)
    return false;
  snprintf(out, out_size, "%s", text);
  return true;
}

static bool params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* out_value) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  const DexedParamDesc* d = dexed_param_by_id((uint32_t)id);
  if (!d)
    return false;
  return dexed_param_text_to_value(*p->engine, *d, text, out_value);
}

// Param values are applied as they arrive rather than queued: they carry no
// sample offset from this host (time is always 0), the host's event list puts
// them ahead of the block's notes, and flush() delivers them outside process()
// entirely — a queue would let flush's values be dropped by the next process
// call, and would only ever be a reordering of a same-instant change.
static void apply_param(dexed_plugin_t* p, clap_id id, double value) {
  const DexedParamDesc* d = dexed_param_by_id((uint32_t)id);
  if (!d)
    return;
  if (value < d->min)
    value = d->min;
  if (value > d->max)
    value = d->max;
  p->engine->setParam((uint32_t)id, value);
}

static void handle_event(dexed_plugin_t* p, const clap_event_header_t* hdr) {
  if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID)
    return;

  switch (hdr->type) {
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF: {
      if (p->pending_note_count >= MAX_PENDING_EVENTS)
        return;
      const clap_event_note_t* ev = (const clap_event_note_t*)hdr;
      DexedNoteEvent* out = &p->pending_notes[p->pending_note_count++];
      out->type = hdr->type == CLAP_EVENT_NOTE_ON ? DexedNoteEvent::NOTE_ON : DexedNoteEvent::NOTE_OFF;
      // CLAP channels are 0-15; dexed's voices store 1-based MIDI channels
      // (its JUCE build gets them from juce::MidiMessage::getChannel()).
      out->channel = (uint8_t)(ev->channel + 1);
      out->key = ev->key;
      // CLAP velocity is 0.0-1.0; dexed's keydown() wants a MIDI 0-127 value.
      // poketrack's own note data is a 0-255 byte that its bridge divides by
      // the MIDI ceiling of 127, so >1.0 is a real input here, not just an
      // edge case — clamp before scaling. Rounded rather than truncated (which
      // is what JUCE's MidiMessage::noteOn does in dexed's own build) so a
      // poketrack velocity byte round-trips to itself instead of landing one
      // LSB low: at most a single step of the DX7's own 0-127 velocity.
      double vel = ev->velocity;
      if (!(vel > 0))
        vel = 0;
      else if (vel > 1)
        vel = 1;
      out->velocity = (uint8_t)lround(vel * 127.0);
      out->time = hdr->time;
      return;
    }
    case CLAP_EVENT_PARAM_VALUE: {
      const clap_event_param_value_t* ev = (const clap_event_param_value_t*)hdr;
      apply_param(p, ev->param_id, ev->value);
      return;
    }
    default:
      return;
  }
}

static void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; i++) {
    const clap_event_header_t* hdr = in->get(in, i);
    // Only param values are ours to consume; anything else (including
    // CLAP_EVENT_PARAM_MOD, which this plugin has no modulation target for)
    // is passed through untouched.
    if (hdr->type == CLAP_EVENT_PARAM_VALUE)
      handle_event(p, hdr);
    else
      out->try_push(out, hdr);
  }
}

static const clap_plugin_params_t params_ext = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

// --- plugin ----------------------------------------------------------------

static bool plugin_init(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}

static void plugin_destroy(const clap_plugin_t* plugin) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  delete p->engine;
  free(p);
}

static bool plugin_activate(const clap_plugin_t* plugin, double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  p->engine->setSampleRate(sample_rate);
  return true;
}

static void plugin_deactivate(const clap_plugin_t* plugin) { (void)plugin; }
static bool plugin_start_processing(const clap_plugin_t* plugin) {
  (void)plugin;
  return true;
}
static void plugin_stop_processing(const clap_plugin_t* plugin) { (void)plugin; }

static void plugin_reset(const clap_plugin_t* plugin) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;
  p->pending_note_count = 0;
  p->engine->panic();
}

static clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
  dexed_plugin_t* p = (dexed_plugin_t*)plugin->plugin_data;

  p->pending_note_count = 0;

  uint32_t ev_count = process->in_events->size(process->in_events);
  for (uint32_t i = 0; i < ev_count; i++)
    handle_event(p, process->in_events->get(process->in_events, i));

  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : out_l;

  // The engine renders mono (the DX7 is a mono synth) — render into the left
  // channel, then copy across, the same as dexed's JUCE build does.
  p->engine->render(out_l, process->frames_count, p->pending_notes, p->pending_note_count);
  if (out_r != out_l)
    memcpy(out_r, out_l, process->frames_count * sizeof(float));

  return CLAP_PROCESS_CONTINUE;
}

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
  snprintf(info->name, sizeof(info->name), "%s", "note in");
  return true;
}

static const clap_plugin_note_ports_t note_ports_ext = {
    .count = note_ports_count,
    .get = note_ports_get,
};

static uint32_t audio_ports_count(const clap_plugin_t* plugin, bool is_input) {
  (void)plugin;
  return is_input ? 0 : 1;
}

static bool audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
  (void)plugin;
  if (is_input || index != 0)
    return false;
  memset(info, 0, sizeof(*info));
  info->id = 0;
  snprintf(info->name, sizeof(info->name), "%s", "output");
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

static const clap_plugin_audio_ports_t audio_ports_ext = {
    .count = audio_ports_count,
    .get = audio_ports_get,
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

// --- factory ---------------------------------------------------------------

static const clap_plugin_descriptor_t descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = DEXED_PLUGIN_ID,
    .name = "Dexed",
    .vendor = "poketrack",
    .url = "https://github.com/asb2m10/dexed",
    .manual_url = "",
    .support_url = "",
    .version = "1.0.0",
    .description = "Yamaha DX7 6-operator FM synth — dexed's engine, 1056 built-in voices",
    .features = (const char*[]){CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL},
};

static const clap_plugin_t* factory_create_plugin(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id) {
  (void)factory;
  if (strcmp(plugin_id, descriptor.id) != 0)
    return NULL;

  dexed_plugin_t* p = (dexed_plugin_t*)calloc(1, sizeof(dexed_plugin_t));
  if (!p)
    return NULL;
  p->engine = new DexedEngine();
  p->host = host;
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

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t* factory) {
  (void)factory;
  return 1;
}

static const clap_plugin_descriptor_t* factory_get_plugin_descriptor(const clap_plugin_factory_t* factory, uint32_t index) {
  (void)factory;
  return index == 0 ? &descriptor : NULL;
}

static const clap_plugin_factory_t factory = {
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
  return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory,
};
