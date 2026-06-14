#pragma once
#include "unit.h"

extern const UnitDef unit_osc;
extern const UnitDef unit_drum;
extern const UnitDef unit_sf2;
#ifndef __EMSCRIPTEN__
extern const UnitDef unit_clap;
#endif
extern const UnitDef unit_delay;
extern const UnitDef unit_dist;
extern const UnitDef unit_reverb;
extern const UnitDef unit_chorus;
extern const UnitDef unit_flanger;
extern const UnitDef unit_phaser;
extern const UnitDef unit_gran;
extern const UnitDef unit_filter;
extern const UnitDef unit_sampler;
extern const UnitDef unit_bitcrush;
extern const UnitDef unit_tremolo;
extern const UnitDef unit_compressor;
extern const UnitDef unit_ducker;
extern const UnitDef unit_midi;
extern const UnitDef unit_lfo;

const UnitDef *unit_find(const char *id);
void unit_list(const UnitDef **out, int *count);
