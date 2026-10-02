// trommelsynthesizer: drum machine voices with General MIDI note mapping.
//
// Pure DSP, no CLAP dependency, so test/render.c can drive it natively.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Machines. Add one by appending here, then to machine_name() and the CFG
// table in trommelsynthesizer.c.
enum {
  // synthesized
  M_808,
  M_909,
  M_SDSV,
  M_CR78,
  // recordings of the machines' ROM samples (silent without src/samples.c)
  M_707,
  M_505,
  M_LINNDRUM,
  M_LM1,
  M_DMX,
  M_DRUMULATOR,
  M_DRUMTRAKS,
  M_MPC60,
  M_RX5,
  M_RZ1,
  M_SK1,
  M_KPR77,
  M_DR55,
  M_DR110,
  M_TR606,
  M_RHYTHMACE,
  M_SPACEDRUM,
  M_HR16,
  M_SDS5,
  M_COUNT
};

// Abstract voices. Every machine uses the subset it really had; a GM key that
// maps to a voice the machine lacks falls back to its closest neighbour.
enum {
  V_BD,
  V_BD2,
  V_SD,
  V_SD2,
  V_RS,
  V_CP,
  V_LT,
  V_MT,
  V_HT,
  V_CH,
  V_OH,
  V_CY,
  V_RD,
  V_CB,
  V_CB2,
  V_TB,
  V_LC,
  V_MC,
  V_HC,
  V_TI,
  V_MA,
  V_CL,
  V_COUNT
};

// Per-voice params, in param-id order.
enum { P_LEVEL,
       P_TUNE,
       P_DECAY,
       P_TONE,
       P_EXTRA,
       P_COUNT };

// Param ids: 0 = machine, 1 = master level, then 2 + voice * P_COUNT + param.
#define PID_MACHINE 0
#define PID_MASTER 1
#define PID_VOICE(v, p) (2 + (v) * P_COUNT + (p))
#define PID_COUNT PID_VOICE(V_COUNT, 0)

typedef struct {
  uint32_t id;
  char name[24];
  char module[16];
  double min, max, def;
  bool stepped;
} ParamInfo;

// The exposed param list: only params a voice really has a knob for.
uint32_t param_count(void);
bool param_info(uint32_t index, ParamInfo* out);
bool param_info_by_id(uint32_t id, ParamInfo* out);
void param_text(uint32_t id, double value, char* out, uint32_t size);
const char* machine_name(int machine);

typedef struct Engine Engine;
Engine* eng_create(double sample_rate);
void eng_destroy(Engine* e);
void eng_reset(Engine* e);
void eng_set_param(Engine* e, uint32_t id, double value);
double eng_get_param(const Engine* e, uint32_t id);
// key is a General MIDI drum key (35..81); vel is 0..1.
void eng_note_on(Engine* e, int key, float vel);
// Same, but with an explicit machine/voice, for tests.
void eng_trigger(Engine* e, int machine, int voice, int semis, float vel);
void eng_render(Engine* e, float* l, float* r, uint32_t frames);
// Which voice (and semitone offset) a GM key plays on a machine; false = silent.
bool eng_gm_lookup(int machine, int key, int* voice, int* semis);
bool eng_machine_has(int machine, int voice);
