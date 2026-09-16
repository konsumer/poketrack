// See params.h. The table is dexed's initCtrl() order, index for index.
#include "params.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "clap/clap.h"
#include "engine.h"

#define STEP (CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
#define ENUM (STEP | CLAP_PARAM_IS_ENUM)
#define CONT (CLAP_PARAM_IS_AUTOMATABLE)

// One operator's 22 params, in dexed's order (Source/PluginParam.cpp):
// envelope, then the four page-2 params, then the scaling/level params, then
// the on/off switch. `n` is the 1-based operator number, `id` the first param
// id, `off` the data[] offset of operator `n` (the packed DX7 voice stores
// OP6 first, so OP1 sits at 105).
#define OP_PARAM_ROWS(n, id, off)                                                         \
  {id + 0, "OP" #n " EG RATE 1", 0, 99, STEP, off + 0, 0, 0, -1, FMT_NUMBER},             \
      {id + 1, "OP" #n " EG RATE 2", 0, 99, STEP, off + 1, 0, 0, -1, FMT_NUMBER},         \
      {id + 2, "OP" #n " EG RATE 3", 0, 99, STEP, off + 2, 0, 0, -1, FMT_NUMBER},         \
      {id + 3, "OP" #n " EG RATE 4", 0, 99, STEP, off + 3, 0, 0, -1, FMT_NUMBER},         \
      {id + 4, "OP" #n " EG LEVEL 1", 0, 99, STEP, off + 4, 0, 0, -1, FMT_NUMBER},        \
      {id + 5, "OP" #n " EG LEVEL 2", 0, 99, STEP, off + 5, 0, 0, -1, FMT_NUMBER},        \
      {id + 6, "OP" #n " EG LEVEL 3", 0, 99, STEP, off + 6, 0, 0, -1, FMT_NUMBER},        \
      {id + 7, "OP" #n " EG LEVEL 4", 0, 99, STEP, off + 7, 0, 0, -1, FMT_NUMBER},        \
      {id + 8, "OP" #n " OUTPUT LEVEL", 0, 99, STEP, off + 16, 0, 0, -1, FMT_NUMBER},     \
      {id + 9, "OP" #n " MODE", 0, 1, ENUM, off + 17, 0, 0, -1, FMT_OP_MODE},             \
      {id + 10, "OP" #n " F COARSE", 0, 31, STEP, off + 18, 0, 0, -1, FMT_NUMBER},        \
      {id + 11, "OP" #n " F FINE", 0, 99, STEP, off + 19, 0, 0, -1, FMT_NUMBER},          \
      {id + 12, "OP" #n " OSC DETUNE", 0, 14, STEP, off + 20, 0, -7, -1, FMT_NUMBER},     \
      {id + 13, "OP" #n " BREAK POINT", 0, 99, STEP, off + 8, 0, 0, -1, FMT_BREAK_POINT}, \
      {id + 14, "OP" #n " L SCALE DEPTH", 0, 99, STEP, off + 9, 0, 0, -1, FMT_NUMBER},    \
      {id + 15, "OP" #n " R SCALE DEPTH", 0, 99, STEP, off + 10, 0, 0, -1, FMT_NUMBER},   \
      {id + 16, "OP" #n " L KEY SCALE", 0, 3, ENUM, off + 11, 0, 0, -1, FMT_KEY_SCALE},   \
      {id + 17, "OP" #n " R KEY SCALE", 0, 3, ENUM, off + 12, 0, 0, -1, FMT_KEY_SCALE},   \
      {id + 18, "OP" #n " RATE SCALING", 0, 7, STEP, off + 13, 0, 0, -1, FMT_NUMBER},     \
      {id + 19, "OP" #n " A MOD SENS.", 0, 3, STEP, off + 14, 0, 0, -1, FMT_NUMBER},      \
      {id + 20, "OP" #n " KEY VELOCITY", 0, 7, STEP, off + 15, 0, 0, -1, FMT_NUMBER},     \
  { id + 21, "OP" #n " SWITCH", 0, 1, ENUM, -1, 0, 0, (n) - 1, FMT_ON_OFF }

const DexedParamDesc dexed_params[] = {
    // Output stage and global settings (dexed: fxCutoff, fxReso, output,
    // monoModeCtrl, tune).
    {0, "Cutoff", 0, 1, CONT, -1, 0, 0, -1, FMT_FLOAT},
    {1, "Resonance", 0, 1, CONT, -1, 0, 0, -1, FMT_FLOAT},
    {2, "Output", 0, 1, CONT, -1, 0, 0, -1, FMT_FLOAT},
    {3, "MonoMode", 0, 1, ENUM, -1, 0, 0, -1, FMT_MONO_POLY},
    {4, "MASTER TUNE ADJ", 0, 1, CONT, -1, 0, 0, -1, FMT_MASTER_TUNE},

    // Voice globals: the operator algorithm and feedback, then the LFO, then
    // the pitch envelope.
    {5, "ALGORITHM", 1, 32, STEP, 134, 1, 0, -1, FMT_NUMBER},
    {6, "FEEDBACK", 0, 7, STEP, 135, 0, 0, -1, FMT_NUMBER},
    {7, "OSC KEY SYNC", 0, 1, ENUM, 136, 0, 0, -1, FMT_ON_OFF},
    {8, "LFO SPEED", 0, 99, STEP, 137, 0, 0, -1, FMT_NUMBER},
    {9, "LFO DELAY", 0, 99, STEP, 138, 0, 0, -1, FMT_NUMBER},
    {10, "LFO PM DEPTH", 0, 99, STEP, 139, 0, 0, -1, FMT_NUMBER},
    {11, "LFO AM DEPTH", 0, 99, STEP, 140, 0, 0, -1, FMT_NUMBER},
    {12, "LFO KEY SYNC", 0, 1, ENUM, 141, 0, 0, -1, FMT_ON_OFF},
    {13, "LFO WAVE", 0, 5, ENUM, 142, 0, 0, -1, FMT_LFO_WAVE},
    {14, "TRANSPOSE", 0, 48, STEP, 144, 0, -24, -1, FMT_NUMBER},
    {15, "P MODE SENS.", 0, 7, STEP, 143, 0, 0, -1, FMT_NUMBER},
    {16, "PITCH EG RATE 1", 0, 99, STEP, 126, 0, 0, -1, FMT_NUMBER},
    {17, "PITCH EG RATE 2", 0, 99, STEP, 127, 0, 0, -1, FMT_NUMBER},
    {18, "PITCH EG RATE 3", 0, 99, STEP, 128, 0, 0, -1, FMT_NUMBER},
    {19, "PITCH EG RATE 4", 0, 99, STEP, 129, 0, 0, -1, FMT_NUMBER},
    {20, "PITCH EG LEVEL 1", 0, 99, STEP, 130, 0, 0, -1, FMT_NUMBER},
    {21, "PITCH EG LEVEL 2", 0, 99, STEP, 131, 0, 0, -1, FMT_NUMBER},
    {22, "PITCH EG LEVEL 3", 0, 99, STEP, 132, 0, 0, -1, FMT_NUMBER},
    {23, "PITCH EG LEVEL 4", 0, 99, STEP, 133, 0, 0, -1, FMT_NUMBER},

    // Operators 1-6, in the UI's order (dexed's opTarget = (5 - i) * 21
    // because the packed voice stores OP6 first).
    OP_PARAM_ROWS(1, 24, 105),
    OP_PARAM_ROWS(2, 46, 84),
    OP_PARAM_ROWS(3, 68, 63),
    OP_PARAM_ROWS(4, 90, 42),
    OP_PARAM_ROWS(5, 112, 21),
    OP_PARAM_ROWS(6, 134, 0),

    // Extras: dexed picks these from its cartridge/bank UI rather than
    // exposing them as params; in a host with no plugin GUI they have to be
    // params to be reachable at all.
    {DEXED_PARAM_CARTRIDGE, "Cartridge", 0, DEXED_CART_COUNT - 1, ENUM, -1, 0, 0, -1, FMT_CARTRIDGE},
    {DEXED_PARAM_PROGRAM, "Program", 0, DEXED_VOICES_PER_CART - 1, ENUM, -1, 0, 0, -1, FMT_PROGRAM},
    {DEXED_PARAM_ENGINE, "Engine", 0, DEXED_ENGINE_COUNT - 1, ENUM, -1, 0, 0, -1, FMT_ENGINE},
};

const int dexed_num_params = (int)(sizeof(dexed_params) / sizeof(dexed_params[0]));

const DexedParamDesc* dexed_param_by_id(uint32_t id) {
  for (int i = 0; i < dexed_num_params; i++)
    if (dexed_params[i].id == id)
      return &dexed_params[i];
  return NULL;
}

static char s_text[32];

double dexed_param_default_value(const DexedEngine& engine, const DexedParamDesc& desc) {
  if (desc.op_switch >= 0)
    return 1;  // dexed unpacks 0x3F ("111111") with each program
  if (desc.dx_offset >= 0) {
    double v = (double)engine.defaultData()[desc.dx_offset] + desc.value_bias;
    if (v < desc.min)
      v = desc.min;
    if (v > desc.max)
      v = desc.max;
    return v;
  }

  switch (desc.id) {
    case 0:  // Cutoff
    case 2:  // Output
      return 1;
    case 1:  // Resonance
    case 3:  // MonoMode
      return 0;
    case 4:  // MASTER TUNE ADJ — dexed's CtrlTune reports 0.5 at neutral
      return 0.5;
    case DEXED_PARAM_CARTRIDGE:
    case DEXED_PARAM_PROGRAM:
      return 0;
    case DEXED_PARAM_ENGINE:
      return DEXED_ENGINE_MARKI;
    default:
      return desc.min;
  }
}

const char* dexed_param_value_to_text(const DexedEngine& engine, uint32_t id, double value) {
  const DexedParamDesc* d = dexed_param_by_id(id);
  if (!d)
    return NULL;
  int v = (int)lround(value);

  switch (d->fmt) {
    case FMT_FLOAT:
      snprintf(s_text, sizeof(s_text), "%g", value);
      return s_text;
    case FMT_MASTER_TUNE:
      // dexed's CtrlTune::getValueDisplay(): the host value scaled to -1..1
      snprintf(s_text, sizeof(s_text), "%g", value * 2 - 1);
      return s_text;
    case FMT_MONO_POLY:
      return v ? "MONO" : "POLY";
    case FMT_OP_MODE:
      return v ? "FIXED" : "RATIO";
    case FMT_ON_OFF:
      return v ? "ON" : "OFF";
    case FMT_LFO_WAVE: {
      static const char* const waves[] = {"TRIANGE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S&HOLD"};
      if (v < 0 || v > 5)
        return NULL;
      return waves[v];
    }
    case FMT_KEY_SCALE: {
      static const char* const curves[] = {"-LN", "-EX", "+EX", "+LN"};
      if (v < 0 || v > 3)
        return NULL;
      return curves[v];
    }
    case FMT_BREAK_POINT: {
      static const char* const names[] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
      if (v < 0 || v > 99)
        return NULL;
      snprintf(s_text, sizeof(s_text), "%s%d", names[v % 12], (v + 9) / 12 - 1);
      return s_text;
    }
    case FMT_PROGRAM:
      return engine.programName(v);
    case FMT_CARTRIDGE:
      return engine.cartridgeName(v);
    case FMT_ENGINE:
      return engine.engineName(v);
    case FMT_NUMBER:
    default:
      snprintf(s_text, sizeof(s_text), "%d", v + d->display_bias);
      return s_text;
  }
}

bool dexed_param_text_to_value(const DexedEngine& engine, const DexedParamDesc& d, const char* text, double* out_value) {
  if (!text || !text[0])
    return false;

  // Copied up front: `text` may point at the buffer
  // dexed_param_value_to_text() returned (formatting a value and parsing it
  // back is an entirely reasonable thing for a host to do), and the loop below
  // overwrites that buffer.
  char want[32];
  snprintf(want, sizeof(want), "%s", text);

  if (d.fmt == FMT_NUMBER) {
    char* end = NULL;
    double v = strtod(want, &end);
    if (end == want || (end && *end))
      return false;
    *out_value = v - d.display_bias;
    return true;
  }

  if (d.fmt == FMT_FLOAT || d.fmt == FMT_MASTER_TUNE) {
    char* end = NULL;
    double v = strtod(want, &end);
    if (end == want || (end && *end))
      return false;
    *out_value = d.fmt == FMT_MASTER_TUNE ? (v + 1) * 0.5 : v;
    return true;
  }

  // Named formats: round-trip through value_to_text rather than re-listing the
  // names, so there's exactly one place the spellings live (dexed's, for the
  // ones it has spellings for — "TRIANGE" and all). These ranges are all tiny
  // (2 to 99 values) and this only runs on a host's text entry.
  for (int v = (int)d.min; v <= (int)d.max; v++) {
    const char* name = dexed_param_value_to_text(engine, d.id, v);
    if (name && strcasecmp(name, want) == 0) {
      *out_value = v;
      return true;
    }
  }
  return false;
}
