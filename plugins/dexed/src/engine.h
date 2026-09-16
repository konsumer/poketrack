// The synth: dexed's audio engine (Source/PluginProcessor.{h,cpp} in dexed),
// wired to dexed's msfa/e engine sources compiled verbatim from vendor/dexed.
//
// This class is deliberately the audio half of DexedAudioProcessor and
// nothing else — no editor, no sysex comm, no preferences, no MIDI-CC map —
// but the parts it keeps are a line-for-line port, because "the same engine"
// is the point: same 16-voice allocator and note-stealing order, same
// per-64-sample render loop and int32 saturation maths, same engine-selection
// (msfa FmCore / Mark I / OPL), and the same refresh-on-param-change path.
//
// What's left out is what a WCLAP host can't reach: poketrack's CLAP unit
// sends note on/off and param values, nothing else, so there's no pitch-bend,
// modulation-wheel, sustain-pedal or portamento input to port, and those
// paths are absent rather than present-and-dead. Dexed's own note allocation,
// MPE-channel behaviour and param semantics are kept intact.
#pragma once

#include <stdint.h>

#include <memory>

#include "EngineMkI.h"
#include "EngineOpl.h"
#include "cartridge.h"
#include "fx.h"
#include "msfa/controllers.h"
#include "msfa/dx7note.h"
#include "msfa/lfo.h"
#include "presets-data.h"

enum DexedEngineType { DEXED_ENGINE_MODERN = 0,
                       DEXED_ENGINE_MARKI = 1,
                       DEXED_ENGINE_OPL = 2,
                       DEXED_ENGINE_COUNT = 3 };

struct DexedNoteEvent {
  enum Type : uint8_t { NOTE_ON,
                        NOTE_OFF };
  Type type;
  uint8_t channel;  // 1-based MIDI channel, as dexed's voices[] store it
  uint8_t key;
  uint8_t velocity;  // 0-127
  uint32_t time;     // sample offset within the block; events must be sorted
};

class DexedEngine {
 public:
  static const int kMaxVoices = 16;  // dexed's MAX_ACTIVE_NOTES
  static const int kBlock = 64;      // msfa's N — one engine render quantum

  DexedEngine();
  ~DexedEngine();

  // dexed's prepareToPlay(): the msfa lookup tables and LFO/envelope units
  // are rate-dependent globals.
  void setSampleRate(double sampleRate);

  // dexed's panic(): silence and re-phase every voice without touching the
  // envelopes.
  void panic();

  // Param access, by the ids in params.h. Voice params read and write dexed's
  // own `data[]` byte for that param, so recalling a Program and then reading
  // a param always agrees with what the engine plays.
  double getParam(uint32_t id) const;
  void setParam(uint32_t id, double value);

  // The startup voice (cartridge 0, program 0): what dexed reports as each
  // param's default, frozen so it can't drift as the user browses programs.
  const uint8_t* defaultData() const { return defaults_; }

  // Names, for CLAP value_to_text.
  const char* cartridgeName(int idx) const;
  const char* programName(int idx) const;
  const char* engineName(int idx) const;

  // Renders `frames` mono samples (dexed is a mono synth; the host layer
  // copies to stereo). `events` must be sorted by time.
  void render(float* out, uint32_t frames, const DexedNoteEvent* events, int event_count);

 private:
  void loadCartridge(int idx);
  void loadProgram(int idx);
  void setEngineType(int type);
  void setMonoMode(bool mono);

  // dexed's voice, byte for byte: the "unpacked" voice the msfa engine reads,
  // with data[155] holding the packed operator on/off switches.
  uint8_t data[161];
  Controllers controllers;

  struct Voice {
    uint8_t channel = 0;
    int midi_note = -1;
    int velocity = 0;
    bool keydown = false;
    bool live = false;
    int32_t keydown_seq = -1;
    Dx7Note* dx7_note = nullptr;
  };

  int chooseNote(uint8_t pitch);
  void keydown(uint8_t channel, uint8_t pitch, uint8_t velo);
  void keyup(uint8_t channel, uint8_t pitch);
  void setDxValue(int offset, int value);
  void packOpSwitch();
  void unpackOpSwitch(char packOpValue);
  int tuningTranspositionShift() const;
  bool applyNextEvent(uint32_t samplePos);
  double masterTuneHostValue() const;

  Voice voices_[kMaxVoices];
  // dexed's prepareToPlay() leaves the round-robin cursor at 0; chooseNote()
  // uses it as the starting index of its scan, so it must never be -1.
  int current_note_ = 0;
  int32_t next_keydown_seq_ = 0;
  bool mono_mode_ = false;
  bool refresh_voice_ = false;

  Lfo lfo_;
  DexedFx fx_;
  FmCore core_msfa_;
  EngineMkI core_mark1_;
  EngineOpl core_opl_;
  int engine_type_ = DEXED_ENGINE_MARKI;  // dexed's own default (setEngineType in its ctor)

  DexedCartridge cart_;
  int cart_index_ = 0;
  int program_index_ = 0;
  uint8_t defaults_[161];
  std::shared_ptr<TuningState> tuning_;

  // dexed buffers whatever is left of the current 64-sample quantum when the
  // host asks for a block that isn't a multiple of it.
  float extra_buf_[kBlock];
  int extra_buf_size_ = 0;

  const DexedNoteEvent* events_ = nullptr;
  int event_count_ = 0;
  int event_index_ = 0;

  mutable char name_buf_[16];
};
