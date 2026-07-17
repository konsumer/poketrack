// Web WCLAP host: thin C wrapper around the JS-side bridge in
// wclap_host_web.lib.js, which does the actual WebAssembly.instantiate +
// struct marshaling against the browser's own wasm engine. This file just
// adapts that to the same clap_host.h surface the native backend exposes.
#include <stdlib.h>
#include <string.h>

#include "clap/clap.h"
#include "clap_host.h"

// Implemented in wclap_host_web.lib.js (mergeInto'd into the Emscripten
// build). Handles are indices into a JS-side array; 0 means "no plugin".
extern int wclap_web_load(const char* path, const char* plugin_id, float sample_rate,
                          int block_size, char* out_name, int out_name_sz, int* out_is_instrument);
extern void wclap_web_unload(int handle);
extern int wclap_web_list_plugins(const char* path, char* out_ids, char* out_names, int id_name_sz, int max_count);
extern void wclap_web_note_on(int handle, int note, int velocity, unsigned int offset);
extern void wclap_web_note_off(int handle, int note, unsigned int offset);
extern void wclap_web_queue_param(int handle, unsigned int param_id, double value);
extern void wclap_web_process(int handle, const float* in_l, const float* in_r,
                              float* out_l, float* out_r, unsigned int frames);
extern int wclap_web_param_count(int handle);
extern int wclap_web_param_info(int handle, unsigned int idx, unsigned int* out_id,
                                char* out_name, int out_name_sz,
                                double* out_min, double* out_max, double* out_default);
extern int wclap_web_param_flags(int handle, unsigned int idx, unsigned int* out_flags);
extern void wclap_web_do_main_thread_work(int handle);

struct ClapPlugin {
  int handle;
  bool is_instrument;
  char name[128];
};

ClapPlugin* clap_host_load(const char* path, const char* plugin_id, float sample_rate, uint32_t block_size) {
  char name[128] = {0};
  int is_instrument = 0;
  int handle = wclap_web_load(path, plugin_id, sample_rate, (int)block_size, name, sizeof(name), &is_instrument);
  if (!handle)
    return NULL;
  ClapPlugin* cp = calloc(1, sizeof(ClapPlugin));
  cp->handle = handle;
  cp->is_instrument = is_instrument != 0;
  strncpy(cp->name, name, sizeof(cp->name) - 1);
  return cp;
}

void clap_host_unload(ClapPlugin* p) {
  if (!p)
    return;
  wclap_web_unload(p->handle);
  free(p);
}

int clap_host_list_plugins(const char* path, char* out_ids, char* out_names, size_t id_name_sz, int max_count) {
  return wclap_web_list_plugins(path, out_ids, out_names, (int)id_name_sz, max_count);
}

void clap_host_note_on(ClapPlugin* p, uint8_t note, uint8_t velocity, uint32_t offset) {
  if (!p)
    return;
  wclap_web_note_on(p->handle, note, velocity, offset);
}

void clap_host_note_off(ClapPlugin* p, uint8_t note, uint32_t offset) {
  if (!p)
    return;
  wclap_web_note_off(p->handle, note, offset);
}

void clap_host_process(ClapPlugin* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r,
                       uint32_t frames) {
  if (!p)
    return;
  wclap_web_process(p->handle, in_l, in_r, out_l, out_r, frames);
}

bool clap_host_is_instrument(ClapPlugin* p) { return p && p->is_instrument; }
const char* clap_host_name(ClapPlugin* p) { return p ? p->name : ""; }

uint32_t clap_host_param_count(ClapPlugin* p) {
  return p ? (uint32_t)wclap_web_param_count(p->handle) : 0;
}

bool clap_host_param_info(ClapPlugin* p, uint32_t idx,
                          uint32_t* out_id, char* out_name, size_t name_sz,
                          double* out_min, double* out_max, double* out_default) {
  if (!p)
    return false;
  return wclap_web_param_info(p->handle, idx, out_id, out_name, (int)name_sz, out_min, out_max, out_default) != 0;
}

bool clap_host_param_flags(ClapPlugin* p, uint32_t idx, uint32_t* out_flags) {
  if (!p)
    return false;
  return wclap_web_param_flags(p->handle, idx, out_flags) != 0;
}

bool clap_host_param_is_stepped(ClapPlugin* p, uint32_t idx) {
  uint32_t flags = 0;
  if (!clap_host_param_flags(p, idx, &flags))
    return false;
  return (flags & CLAP_PARAM_IS_STEPPED) != 0;
}

void clap_host_queue_param(ClapPlugin* p, uint32_t param_id, double value) {
  if (!p)
    return;
  wclap_web_queue_param(p->handle, param_id, value);
}

void clap_host_do_main_thread_work(ClapPlugin* p) {
  if (!p)
    return;
  wclap_web_do_main_thread_work(p->handle);
}
