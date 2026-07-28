// pd_wclap.h — fixed ABI between pdast2wclap-generated code and a CLAP
// "runtime shim" that hosts it (see poketrack/plugins/pd2wclap/runtime-shim.c
// for a working example). Generated code implements every symbol declared
// here; the shim only ever calls through this header, never assumes
// anything about how the DSP graph is represented internally.
//
// Copy or symlink this file alongside the shim when compiling — it has no
// dependencies beyond <stdint.h>.
#pragma once
#include <stdint.h>

typedef struct PdState PdState;

typedef struct {
  const char* name;
  double min;
  double max;
  double default_value;
} PdParamInfo;

// ── Metadata (generated as data, not functions, so the shim can read them
//    before creating any PdState) ───────────────────────────────────────────
extern const int PD_NUM_PARAMS;
extern const PdParamInfo PD_PARAMS[];
extern const int PD_HAS_AUDIO_IN;  // patch uses adc~
extern const int PD_HAS_NOTE_IN;   // patch uses notein

// ── Lifecycle ────────────────────────────────────────────────────────────
PdState* pd_create(double sample_rate);
void pd_destroy(PdState* st);

// ── Audio ────────────────────────────────────────────────────────────────
// Renders `nframes` samples of audio-rate signal into out_l/out_r. in_l/in_r
// may be NULL (no audio input wired to this instance, e.g. instrument
// contexts) — treated as silence. Does NOT recompute the control graph;
// call pd_note_on/pd_note_off/pd_set_param between pd_process() calls to
// split a block at each event's sample-accurate time offset.
void pd_process(PdState* st, const float* in_l, const float* in_r, float* out_l, float* out_r, uint32_t nframes);

// ── Events (control-rate, applied instantaneously in "logical time" —
//    each call recomputes the whole control graph, matching PD's own
//    message-passing semantics far more closely than resampling controls
//    every audio sample would) ───────────────────────────────────────────
// velocity01 is CLAP-spec 0..1 (poketrack's host already normalizes it —
// do not assume 0-127 here).
void pd_note_on(PdState* st, int16_t key, double velocity01);
void pd_note_off(PdState* st, int16_t key, double velocity01);

// `index` is the position of the param in PD_PARAMS[]; `value` is already
// scaled into that param's [min, max] range (not a normalized 0..1).
void pd_set_param(PdState* st, int32_t index, double value);
double pd_get_param(PdState* st, int32_t index);
