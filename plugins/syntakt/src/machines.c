// Names, ranges and defaults as the Syntakt shows them (its UI parameter
// descriptors, OS 1.41/1.42; the same as Modded-Cycles' engine catalog).

#include "machines.h"

#define TUNE {ST_TUNE, 40, 88, 64, "TUNE", "Tune"}
#define PNCH {ST_PUNCH, 0, 1, 0, "PNCH", "Punch"}
#define OVER {ST_OVER, 0, 127, 0, "OVER", "Overdrive"}
#define DEC(d) {ST_DECAY, 0, 127, d, "DEC", "Decay"}

const machine_t machines[MACHINE_COUNT] = {
    {ST_BD_MODERN, "BD MODERN", {TUNE, {ST_P1, 0, 127, 10, "SMOD", "Square Modulation"}, {ST_P2, 0, 127, 16, "TMOD", "Triangle Mod+Fdbk"}, {ST_P3, 0, 127, 16, "SWEP", "Pitch Sweep"}, {ST_P4, 0, 127, 24, "MENV", "Modulation Env"}, PNCH, DEC(64), OVER}},
    {ST_SD_BASIC, "SD BASIC", {TUNE, {ST_P1, 0, 127, 0, "INHM", "Inharmonicity"}, {ST_P2, 0, 127, 127, "MDFB", "Mod+Fdbk"}, {ST_P3, 0, 127, 8, "SWEP", "Pitch Sweep"}, {ST_P4, 0, 127, 0, "MENV", "Modulation Env"}, PNCH, DEC(64), OVER}},
    {ST_CY_ALLOY, "CY ALLOY", {TUNE, {ST_P1, 0, 127, 127, "RADIO", "Radio"}, {ST_P2, 0, 127, 46, "MOD", "Modulation"}, {ST_P3, 0, 127, 28, "SHIM", "Shimmer"}, {ST_P4, 0, 127, 0, "MENV", "Modulation Env"}, PNCH, DEC(18), OVER}},
    {ST_PC_CARBON, "PC CARBON", {{ST_TUNE, 40, 88, 52, "TUNE", "Tune"}, {ST_P1, 0, 127, 0, "INHM", "Inharmonicity"}, {ST_P2, 0, 127, 26, "MOD", "Modulation"}, {ST_P3, 0, 127, 35, "SWEP", "Pitch Sweep"}, {ST_P4, 0, 127, 80, "MENV", "Modulation Env"}, PNCH, DEC(32), OVER}},
    {ST_SY_TONE, "SY TONE", {{ST_TUNE, 40, 88, 52, "TUNE", "Tune"}, {ST_P1, 0, 127, 4, "RATIO", "Mod Ratio"}, {ST_P2, 0, 127, 75, "MOD", "Modulation"}, {ST_P3, 0, 127, 41, "FDBK", "Feedback"}, {ST_P4, 0, 127, 76, "MENV", "Modulation Env"}, PNCH, DEC(127), OVER}},
    {ST_SY_CHORD, "SY CHORD", {TUNE, {ST_P1, 0, 127, 96, "BAL", "Chord/Osc Balance"}, {ST_P2, 0, 37, 3, "TYPE", "Mode/Chord"}, {ST_P3, 0, 127, 10, "WAVE", "Waveform"}, {ST_P4, 0, 127, 64, "FENV", "Filter Envelope"}, PNCH, DEC(127), OVER}},
    {ST_SD_VINTAGE, "SD VINTAGE", {TUNE, {ST_P1, 0, 127, 0, "INHM", "Inharmonicity"}, {ST_P2, 0, 127, 110, "FCMP", "Freq. Complexity"}, {ST_P3, 0, 127, 74, "SWEP", "Pitch Sweep"}, {ST_P4, 0, 127, 80, "MENV", "Modulation Env"}, PNCH, DEC(33), OVER}},
    {ST_CP_VINTAGE, "CP VINTAGE", {TUNE, {ST_P1, 0, 127, 24, "BODY", "Body Character"}, {ST_P2, 0, 127, 25, "BAL", "Balance"}, {ST_P3, 0, 127, 46, "SPCR", "Spacing/Crunch"}, {ST_P4, 0, 127, 37, "BENV", "Body Envelope"}, PNCH, DEC(32), OVER}},
    {ST_SY_TOY, "SY TOY", {TUNE, {ST_P1, 0, 127, 24, "FORM", "Form"}, {ST_P2, 0, 127, 60, "IMP", "Impact"}, {ST_P3, 0, 127, 110, "BRIG", "Brightness"}, {ST_P4, 0, 127, 64, "PART", "Partial Decay"}, PNCH, DEC(60), OVER}},
    {ST_SY_BITS, "SY BITS", {TUNE, {ST_P1, 40, 88, 52, "DET", "Detune"}, {ST_P2, 0, 127, 102, "BAL", "Balance"}, {ST_P3, 0, 127, 0, "SRR", "Sample Rate Redux"}, {ST_P4, 0, 127, 21, "WAVE", "Waveform"}, {ST_PUNCH, 0, 127, 0, "BR", "Bit Redux"}, DEC(60), OVER}},
    {ST_SY_SWARM, "SY SWARM", {TUNE, {ST_P1, 0, 127, 20, "NMOD", "Noise Modulation"}, {ST_P2, 0, 127, 15, "ANIM", "Detune Animation"}, {ST_P3, 0, 127, 70, "DET", "Detune"}, {ST_P4, 0, 127, 127, "MIX", "Oscillator Mix"}, {ST_PUNCH, 0, 2, 2, "SUB", "Fundamental Sub"}, DEC(75), OVER}},
};

#undef TUNE
#undef PNCH
#undef OVER
#undef DEC
