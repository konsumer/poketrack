#include "clap_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform shared-library loading
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DL_OPEN(p) LoadLibraryA(p)
#define DL_SYM(h, s) GetProcAddress((HMODULE)(h), s)
#define DL_CLOSE(h) FreeLibrary((HMODULE)(h))
typedef HMODULE dl_handle_t;
#else
#include <dlfcn.h>
#define DL_OPEN(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define DL_SYM(h, s) dlsym(h, s)
#define DL_CLOSE(h) dlclose(h)
typedef void *dl_handle_t;
#endif

#include "clap/clap.h"

struct ClapPlugin {
  dl_handle_t lib;
  const clap_plugin_entry_t *entry;
  const clap_plugin_t *plugin;
  const clap_plugin_audio_ports_t *audio_ports;
  const clap_plugin_note_ports_t *note_ports;
  const clap_plugin_params_t *params_ext;

  float sample_rate;
  uint32_t block_size;
  bool is_instrument;
  char name[128];

  // Pending note events for next process call
  clap_event_note_t events[MAX_CLAP_EVENTS];
  int event_count;

  // Pending param value events for next process call
  clap_event_param_value_t param_events[16];
  int param_event_count;

  // Audio buffers
  float *buf_out_l;
  float *buf_out_r;
};

// --- Host callbacks required by CLAP ---

static const char *host_get_name(const clap_host_t *host) {
  (void)host;
  return "raypoketrack";
}

static const void *host_get_extension(const clap_host_t *host, const char *ext_id) {
  (void)host;
  (void)ext_id;
  return NULL;
}

static void host_request_restart(const clap_host_t *host) { (void)host; }
static void host_request_process(const clap_host_t *host) { (void)host; }
static void host_request_callback(const clap_host_t *host) { (void)host; }

static clap_host_t s_host = {
    CLAP_VERSION_INIT,
    NULL,
    "raypoketrack",
    "raypoketrack",
    "",
    "0.1.0",
    host_get_extension,
    host_request_restart,
    host_request_process,
    host_request_callback,
};

// Resolve .clap bundle path to actual dylib path (macOS bundles)
static void resolve_clap_path(const char *in, char *out, size_t out_size) {
#ifdef __APPLE__
  // Strip trailing slash (tinyfd_selectFolderDialog returns "path/")
  char tmp[512];
  strncpy(tmp, in, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  size_t len = strlen(tmp);
  while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

  // <name>.clap/Contents/MacOS/<name>
  const char *slash = strrchr(tmp, '/');
  const char *base = slash ? slash + 1 : tmp;
  char stem[256];
  strncpy(stem, base, sizeof(stem) - 1);
  stem[sizeof(stem) - 1] = '\0';
  char *dot = strrchr(stem, '.');
  if (dot)
    *dot = '\0';
  snprintf(out, out_size, "%s/Contents/MacOS/%s", tmp, stem);
#else
  strncpy(out, in, out_size);
#endif
}

ClapPlugin *clap_host_load(const char *path, const char *plugin_id, float sample_rate, uint32_t block_size) {
  char real_path[512];
  resolve_clap_path(path, real_path, sizeof(real_path));

  dl_handle_t lib = DL_OPEN(real_path);
  if (!lib) {
    fprintf(stderr, "clap_host: cannot open '%s'\n", real_path);
    return NULL;
  }

  const clap_plugin_entry_t *entry =
      (const clap_plugin_entry_t *)DL_SYM(lib, "clap_entry");
  if (!entry) {
    fprintf(stderr, "clap_host: no clap_entry in '%s'\n", real_path);
    DL_CLOSE(lib);
    return NULL;
  }

  if (!entry->init(real_path)) {
    fprintf(stderr, "clap_host: entry->init failed\n");
    DL_CLOSE(lib);
    return NULL;
  }

  const clap_plugin_factory_t *factory =
      (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  if (!factory) {
    fprintf(stderr, "clap_host: no plugin factory\n");
    entry->deinit();
    DL_CLOSE(lib);
    return NULL;
  }

  // Find plugin by ID (or use first if id is NULL/empty)
  uint32_t count = factory->get_plugin_count(factory);
  const clap_plugin_descriptor_t *desc = NULL;
  for (uint32_t i = 0; i < count; i++) {
    const clap_plugin_descriptor_t *d = factory->get_plugin_descriptor(factory, i);
    if (!plugin_id || plugin_id[0] == '\0' || strcmp(d->id, plugin_id) == 0) {
      desc = d;
      break;
    }
  }

  if (!desc) {
    fprintf(stderr, "clap_host: plugin id '%s' not found\n", plugin_id ? plugin_id : "(any)");
    entry->deinit();
    DL_CLOSE(lib);
    return NULL;
  }

  const clap_plugin_t *plugin = factory->create_plugin(factory, &s_host, desc->id);
  if (!plugin || !plugin->init(plugin)) {
    fprintf(stderr, "clap_host: plugin init failed\n");
    if (plugin)
      plugin->destroy(plugin);
    entry->deinit();
    DL_CLOSE(lib);
    return NULL;
  }

  ClapPlugin *cp = calloc(1, sizeof(ClapPlugin));
  cp->lib = lib;
  cp->entry = entry;
  cp->plugin = plugin;
  cp->sample_rate = sample_rate;
  cp->block_size = block_size;
  strncpy(cp->name, desc->name, sizeof(cp->name) - 1);

  // Check if instrument (has MIDI input)
  cp->note_ports = (const clap_plugin_note_ports_t *)
                       plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
  cp->is_instrument = (cp->note_ports != NULL);

  cp->audio_ports = (const clap_plugin_audio_ports_t *)
                        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
  cp->params_ext = (const clap_plugin_params_t *)
                       plugin->get_extension(plugin, CLAP_EXT_PARAMS);

  plugin->activate(plugin, sample_rate, 1, block_size);
  plugin->start_processing(plugin);

  cp->buf_out_l = calloc(block_size, sizeof(float));
  cp->buf_out_r = calloc(block_size, sizeof(float));

  return cp;
}

void clap_host_unload(ClapPlugin *p) {
  if (!p)
    return;
  p->plugin->stop_processing(p->plugin);
  p->plugin->deactivate(p->plugin);
  p->plugin->destroy(p->plugin);
  if (p->entry) p->entry->deinit();
  DL_CLOSE(p->lib);
  free(p->buf_out_l);
  free(p->buf_out_r);
  free(p);
}

void clap_host_note_on(ClapPlugin *p, uint8_t note, uint8_t velocity, uint32_t offset) {
  if (!p || p->event_count >= MAX_CLAP_EVENTS)
    return;
  clap_event_note_t *ev = &p->events[p->event_count++];
  memset(ev, 0, sizeof(*ev));
  ev->header.size = sizeof(*ev);
  ev->header.time = offset;
  ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev->header.type = CLAP_EVENT_NOTE_ON;
  ev->header.flags = 0;
  ev->note_id = -1;
  ev->port_index = 0;
  ev->channel = 0;
  ev->key = note;
  ev->velocity = velocity / 127.0;
}

void clap_host_note_off(ClapPlugin *p, uint8_t note, uint32_t offset) {
  if (!p || p->event_count >= MAX_CLAP_EVENTS)
    return;
  clap_event_note_t *ev = &p->events[p->event_count++];
  memset(ev, 0, sizeof(*ev));
  ev->header.size = sizeof(*ev);
  ev->header.time = offset;
  ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev->header.type = CLAP_EVENT_NOTE_OFF;
  ev->header.flags = 0;
  ev->note_id = -1;
  ev->port_index = 0;
  ev->channel = 0;
  ev->key = note;
  ev->velocity = 0.0;
}

// Input event list helpers — param events first (time=0), then note events
static const clap_event_header_t *list_get(const clap_input_events_t *list, uint32_t index) {
  ClapPlugin *p = (ClapPlugin *)list->ctx;
  if ((int)index < p->param_event_count)
    return &p->param_events[index].header;
  return &p->events[index - p->param_event_count].header;
}
static uint32_t list_size(const clap_input_events_t *list) {
  ClapPlugin *p = (ClapPlugin *)list->ctx;
  return (uint32_t)(p->param_event_count + p->event_count);
}

// Output event list helpers (we discard output events for now)
static bool out_try_push(const clap_output_events_t *list, const clap_event_header_t *ev) {
  (void)list;
  (void)ev;
  return true;
}

void clap_host_process(ClapPlugin *p,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r,
                       uint32_t frames) {
  if (!p)
    return;

  // Clear output
  memset(p->buf_out_l, 0, frames * sizeof(float));
  memset(p->buf_out_r, 0, frames * sizeof(float));

  // Build audio buffers
  const float *in_bufs[2] = {in_l, in_r};
  float *out_bufs[2] = {p->buf_out_l, p->buf_out_r};

  // clap_audio_buffer_t: data32, data64, channel_count, latency, constant_mask
  clap_audio_buffer_t audio_in = {(float **)in_bufs, NULL, 2, 0, 0};
  clap_audio_buffer_t audio_out = {out_bufs, NULL, 2, 0, 0};

  // clap_input_events_t: ctx, size, get
  clap_input_events_t in_events = {
      p,
      list_size,
      list_get,
  };
  // clap_output_events_t: ctx, try_push
  clap_output_events_t out_events = {p, out_try_push};

  // clap_process_t: steady_time, frames_count, transport,
  //   audio_inputs, audio_outputs, audio_inputs_count, audio_outputs_count,
  //   in_events, out_events
  clap_process_t proc = {
      -1,
      frames,
      NULL,
      in_l ? &audio_in : NULL,
      &audio_out,
      in_l ? 1u : 0u,
      1u,
      &in_events,
      &out_events,
  };

  p->plugin->process(p->plugin, &proc);
  p->event_count = 0;
  p->param_event_count = 0;

  if (out_l)
    memcpy(out_l, p->buf_out_l, frames * sizeof(float));
  if (out_r)
    memcpy(out_r, p->buf_out_r, frames * sizeof(float));
}

bool clap_host_is_instrument(ClapPlugin *p) { return p && p->is_instrument; }
const char *clap_host_name(ClapPlugin *p) { return p ? p->name : ""; }

uint32_t clap_host_param_count(ClapPlugin *p) {
  if (!p || !p->params_ext)
    return 0;
  return p->params_ext->count(p->plugin);
}

bool clap_host_param_flags(ClapPlugin *p, uint32_t idx, uint32_t *out_flags) {
  if (!p || !p->params_ext)
    return false;
  clap_param_info_t info;
  if (!p->params_ext->get_info(p->plugin, idx, &info))
    return false;
  if (out_flags)
    *out_flags = (uint32_t)info.flags;
  return true;
}

bool clap_host_param_is_stepped(ClapPlugin *p, uint32_t idx) {
  if (!p || !p->params_ext)
    return false;
  clap_param_info_t info;
  if (!p->params_ext->get_info(p->plugin, idx, &info))
    return false;
  return (info.flags & CLAP_PARAM_IS_STEPPED) != 0;
}

bool clap_host_param_info(ClapPlugin *p, uint32_t idx,
                          uint32_t *out_id, char *out_name, size_t name_sz,
                          double *out_min, double *out_max, double *out_default) {
  if (!p || !p->params_ext)
    return false;
  clap_param_info_t info;
  if (!p->params_ext->get_info(p->plugin, idx, &info))
    return false;
  if (out_id)
    *out_id = (uint32_t)info.id;
  if (out_name)
    snprintf(out_name, name_sz, "%s", info.name);
  if (out_min)
    *out_min = info.min_value;
  if (out_max)
    *out_max = info.max_value;
  if (out_default)
    *out_default = info.default_value;
  return true;
}

void clap_host_queue_param(ClapPlugin *p, uint32_t param_id, double value) {
  if (!p || p->param_event_count >= 16)
    return;
  clap_event_param_value_t *ev = &p->param_events[p->param_event_count++];
  memset(ev, 0, sizeof(*ev));
  ev->header.size = sizeof(*ev);
  ev->header.time = 0;
  ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev->header.type = CLAP_EVENT_PARAM_VALUE;
  ev->header.flags = 0;
  ev->param_id = (clap_id)param_id;
  ev->value = value;
  ev->note_id = -1;
  ev->port_index = -1;
  ev->channel = -1;
  ev->key = -1;
}
