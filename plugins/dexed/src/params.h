// The parameter model: dexed's own parameter list (Source/PluginParam.cpp's
// initCtrl()), in dexed's own order, so param index N here is param index N
// in Dexed itself.
//
// Dexed exposes params through JUCE Ctrl objects for a 145-byte "unpacked
// voice" plus a handful of non-voice settings. Each voice param is a plain
// byte at a known offset in that voice — Byte 7 of the DX7's packed format
// is split across two params, so the mapping isn't byte-for-byte, but it is
// index-for-index. We keep that: a table row either names a byte offset in
// `DexedEngine::data`, an operator's on/off flag, or one of the settings that
// live outside the voice (filter, output, mono/poly, engine, cartridge,
// program).
//
// Ranges are the ranges dexed's UI declares (via each Ctrl subclass's
// `steps`), not squeezed into poketrack's 0-255 ADD-row byte: the host scales
// its byte into whatever range a param declares, and for stepped params it
// maps byte directly onto step (see clap_unit.c).
#pragma once

#include <stdint.h>

class DexedEngine;

// Params that aren't part of the voice. Everything else uses its table index
// as its param id.
#define DEXED_PARAM_CARTRIDGE 0x4000u
#define DEXED_PARAM_PROGRAM 0x4001u
#define DEXED_PARAM_ENGINE 0x4002u

enum DexedValueFmt : uint8_t {
  FMT_NUMBER,       // plain integer (plus display_bias)
  FMT_FLOAT,        // continuous setting, shown with its decimal value
  FMT_MASTER_TUNE,  // -1..1
  FMT_MONO_POLY,    // POLY / MONO
  FMT_OP_MODE,      // RATIO / FIXED
  FMT_ON_OFF,       // OFF / ON
  FMT_LFO_WAVE,     // TRIANGE / SAW DOWN / ... (dexed's own spelling)
  FMT_KEY_SCALE,    // -LN / -EX / +EX / +LN
  FMT_BREAK_POINT,  // A-1 ... G9
  FMT_PROGRAM,      // voice name in the selected cartridge
  FMT_CARTRIDGE,    // cartridge name
  FMT_ENGINE,       // Modern / Mark I / OPL
};

struct DexedParamDesc {
  uint32_t id;
  const char* name;
  double min, max;
  uint32_t flags;
  int16_t dx_offset;    // byte in DexedEngine::data, or -1 for non-voice params
  int8_t value_bias;    // param value = data[dx_offset] + value_bias
  int8_t display_bias;  // displayed = value + display_bias
  int8_t op_switch;     // index into Controllers::opSwitch, or -1
  DexedValueFmt fmt;
};

extern const DexedParamDesc dexed_params[];
extern const int dexed_num_params;

// The param table row with this id, or NULL.
const DexedParamDesc* dexed_param_by_id(uint32_t id);

// The param's default value — the startup voice (cartridge 0, program 0) for
// voice params, and dexed's own initial setting for the rest. Frozen, so
// browsing programs can't move what the host sees as a param's default.
double dexed_param_default_value(const DexedEngine& engine, const DexedParamDesc& desc);

// Formats a param value the way dexed's Ctrl subclasses display it. `engine`
// is only read for the params that name a program/cartridge. Returns NULL if
// the param has no text form (let the host fall back to a number).
const char* dexed_param_value_to_text(const DexedEngine& engine, uint32_t id, double value);

// Parses text back to a value, for CLAP_EXT_PARAMS::text_to_value. Accepts
// whatever value_to_text produces, plus a bare number for every param.
bool dexed_param_text_to_value(const DexedEngine& engine, const DexedParamDesc& desc, const char* text, double* out_value);
