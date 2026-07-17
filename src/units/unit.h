#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UNIT_MAX_PARAMS 16
#define UNIT_ID_LEN 8

// Opaque per-instance runtime state
typedef struct UnitState UnitState;

typedef struct {
  const char* id;           // short id: "osc","drum","delay","dist"
  const char* name;         // display name
  const char* data_hint;    // placeholder shown when slot->data is empty (NULL = no data field)
  const char* data_label;   // label shown left of data field (NULL = "FILE")
  const char* role_label;   // label shown after unit name (NULL = "SOURCE"/"EFFECT", "" = hidden)
  const char* file_filter;  // file dialog filter pattern e.g. "*.sf2" (NULL = any)
  bool is_source;           // true=generates audio, false=processes audio
  // If true, the engine uses ONE instance of this unit's whole chain per
  // instrument for real playback (pattern + MIDI), instead of one per
  // active track/voice — for units (like CLAP) whose backing plugin
  // already tracks its own polyphony from a stream of note events.
  bool shared_instance;
  int num_params;
  const char* param_names[UNIT_MAX_PARAMS];
  uint8_t param_defaults[UNIT_MAX_PARAMS];
  // Optional enum labels. NULL = continuous 0-FF slider.
  // When set, param value is a direct index 0..(count-1).
  const char* const* param_enums[UNIT_MAX_PARAMS];
  uint8_t param_enum_count[UNIT_MAX_PARAMS];

  // Optional: dynamic param count/name (for plugins with variable params like CLAP)
  int (*dyn_num_params)(UnitState* s);
  const char* (*dyn_param_name)(UnitState* s, int idx);
  uint8_t (*get_param_val)(UnitState* s, int idx);
  void (*set_param_val)(UnitState* s, int idx, uint8_t val);
  uint8_t (*get_param_default)(UnitState* s, int idx);
  // Encode current param values back into slot->data (call after set_param_val)
  void (*sync_to_data)(UnitState* s, char* data_buf, size_t data_buf_sz);
  // Return display string for a param value; NULL = use default %02X hex display.
  // "ON"/"OFF" → boolean toggle display (no bar). Any other string → stepped int display.
  const char* (*format_param_val)(UnitState* s, int idx, uint8_t val);

  // Param picker (ADD row → opens overlay to add a mapped param slot)
  const char* picker_title;                                  // overlay header (NULL = "ADD PARAM")
  int (*picker_count)(UnitState* s);                         // total params available to pick
  const char* (*picker_name)(UnitState* s, int picker_idx);  // name of picker entry
  void (*picker_add)(UnitState* s, int picker_idx);          // add picker entry as next mapping
  void (*mapping_remove)(UnitState* s, int map_idx);         // remove a mapped param

  // Device picker (DATA row A → opens overlay to select a device/port)
  const char* dev_picker_title;  // overlay header (NULL = "SELECT DEVICE")
  int (*dev_picker_count)(UnitState* s);
  const char* (*dev_picker_name)(UnitState* s, int idx);
  void (*dev_picker_set)(UnitState* s, int idx);  // select device at idx

  // Optional: override defaults after placement (inst_idx = which instrument this is in)
  void (*init_params)(uint8_t* params, int inst_idx);

  UnitState* (*create)(float sample_rate);
  void (*destroy)(UnitState* s);

  // Called after create with slot's data string and save-file base directory.
  // Use for loading external files (SF2, CLAP) with relative-path support.
  // base_dir is the directory containing the save file (e.g. "/home/user/music/").
  // data is the raw string from ChainSlot.data (may be empty).
  void (*set_data)(UnitState* s, const char* data, const char* base_dir);

  // Called from main thread each frame; use for deferred work (e.g. CLAP on_main_thread).
  void (*main_thread_work)(UnitState* s);

  // All three are optional (NULL = no-op). Sources need note_on/note_off;
  // effects with internal state (delay lines, envelopes) should provide kill.
  void (*note_on)(UnitState* s, uint8_t note, uint8_t vel, const uint8_t* params);
  void (*note_off)(UnitState* s, uint8_t note);
  void (*kill)(UnitState* s);

  // Sources: in_l/in_r are NULL; ADD output to out_l/out_r (don't clear first).
  // Effects: read from in_l/in_r, write to out_l/out_r (may be same buffers).
  void (*render)(UnitState* s, const uint8_t* params,
                 const float* in_l, const float* in_r,
                 float* out_l, float* out_r, uint32_t frames);
} UnitDef;

// Resolve a path relative to base_dir; writes result to out.
// If path is already absolute, copies as-is.
static inline void unit_resolve_path(const char* base_dir, const char* path,
                                     char* out, int sz) {
  if (!path || !path[0]) {
    out[0] = '\0';
    return;
  }
#ifdef _WIN32
  int abs = (path[0] == '/' || path[0] == '\\' || (path[1] == ':'));
#else
  int abs = (path[0] == '/');
#endif
  if (abs || !base_dir || !base_dir[0]) {
    snprintf(out, sz, "%s", path);
  } else {
    snprintf(out, sz, "%s%s", base_dir, path);
  }
}

// Map 00-FF param to float range [lo, hi]
static inline float p2f(uint8_t p, float lo, float hi) {
  return lo + (p / 255.0f) * (hi - lo);
}

// Map 00-FF param to a bipolar range with 0x80 landing EXACTLY on the
// midpoint (plain p2f puts 0x80 slightly above it: 128/255 != 1/2).
// Use for pan/detune/transpose-style params whose default is "no change".
static inline float p2f_center(uint8_t p, float lo, float hi) {
  float half = 0.5f * (hi - lo);
  float mid = lo + half;
  return p <= 128 ? mid - (128 - p) * (half / 128.0f)
                  : mid + (p - 128) * (half / 127.0f);
}

// ---- shared DSP helpers (audio-thread hot paths) ----------------------------

// Sine lookup table, filled by unit_dsp_init() (called once from audio_init).
#define UNIT_SIN_N 2048
extern float g_unit_sin_lut[UNIT_SIN_N + 1];
void unit_dsp_init(void);

// Implemented by the audio engine (audio.c): write a param addressed by
// global index across an instrument's chain, resolving dynamic params
// (CLAP mappings, MIDI CC slots) — for modulation units' render callbacks.
void audio_mod_set_param(uint8_t inst_idx, uint8_t global_param, uint8_t val);

// sin(2*pi*phase) for any phase (wraps, handles negatives). LUT + linear
// interpolation, error < -100dB — replaces per-sample sinf in render loops.
static inline float unit_sin(float phase) {
  phase -= floorf(phase);
  float x = phase * UNIT_SIN_N;
  int i = (int)x;
  return g_unit_sin_lut[i] + (g_unit_sin_lut[i + 1] - g_unit_sin_lut[i]) * (x - i);
}

// tanh-shaped soft clip, much cheaper than tanhf. Smoothly saturates,
// reaching exactly +/-1 at |x|=3.
static inline float unit_softclip(float x) {
  if (x > 3.0f)
    return 1.0f;
  if (x < -3.0f)
    return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// LFO waveform: 0=sine 1=square 2=saw 3=tri, phase in cycles
static inline float unit_lfo_wave(int shape, float phase) {
  switch (shape) {
    case 0:
      return unit_sin(phase);
    case 1:
      return (phase < 0.5f) ? 1.0f : -1.0f;
    case 2:
      return 2.0f * phase - 1.0f;
    default:
      return 1.0f - 4.0f * fabsf(phase - 0.5f);
  }
}
