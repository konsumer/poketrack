// See engine.h. Ported from dexed's Source/PluginProcessor.cpp; the ported
// code paths keep dexed's structure, comments and arithmetic, so a diff
// against upstream stays readable.
#include "engine.h"

#include <math.h>
#include <string.h>

#include "msfa/env.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/sin.h"
#include "params.h"

namespace {
// The three engine names dexed's ParamDialog shows for its engine selector.
const char* const kEngineNames[DEXED_ENGINE_COUNT] = {"Modern (24-bit)", "Mark I", "OPL Series"};
}  // namespace

DexedEngine::DexedEngine() {
  // dexed's DexedAudioProcessor constructor: the shared lookup tables first.
  Exp2::init();
  Tanh::init();
  Sin::init();

  tuning_ = createStandardTuning();

  // dexed's prepareToPlay() controller defaults: pitch bend neutral, a 3
  // semitone bend range, and MPE on (it switches itself off on the second
  // keydown on one channel — see keydown()).
  controllers.values_[kControllerPitch] = 0x2000;
  controllers.values_[kControllerPitchRangeUp] = 3;
  controllers.values_[kControllerPitchRangeDn] = 3;
  controllers.values_[kControllerPitchStep] = 0;
  controllers.masterTune = 0;
  controllers.mpePitchBendRange = 24;
  controllers.mpeEnabled = true;
  controllers.portamento_enable_cc = false;
  controllers.portamento_cc = 0;
  controllers.portamento_gliss_cc = false;
  controllers.modwheel_cc = 0;
  controllers.foot_cc = 0;
  controllers.breath_cc = 0;
  controllers.aftertouch_cc = 0;
  // dexed calls controllers.refresh() in prepareToPlay(); it's what turns on
  // eg_mod (127) when no controller is routed to envelope depth, which the
  // envelopes read — without it every voice would render differently.
  controllers.refresh();

  setEngineType(DEXED_ENGINE_MARKI);

  for (int i = 0; i < kMaxVoices; i++)
    voices_[i].dx7_note = new Dx7Note(tuning_, nullptr);

  memset(extra_buf_, 0, sizeof(extra_buf_));

  setSampleRate(44100.0);

  // dexed's initCtrl() → setupStartupCart() → setCurrentProgram(0), then
  // setMonoMode/vuSignal defaults: the state every param default is reported
  // from.
  memset(data, 0, sizeof(data));
  cart_.load(dexed_cart_data[0], DEXED_SYSEX_SIZE);
  loadProgram(0);
  memcpy(defaults_, data, sizeof(defaults_));
}

DexedEngine::~DexedEngine() {
  for (int i = 0; i < kMaxVoices; i++)
    delete voices_[i].dx7_note;
}

void DexedEngine::setSampleRate(double sampleRate) {
  // dexed's prepareToPlay(), minus fx/voice allocation (this engine keeps its
  // voices for its whole life).
  Freqlut::init(sampleRate);
  Lfo::init(sampleRate);
  PitchEnv::init(sampleRate);
  Env::init_sr(sampleRate);
  Porta::init_sr(sampleRate);
  fx_.init(sampleRate);
}

void DexedEngine::panic() {
  for (int i = 0; i < kMaxVoices; i++) {
    voices_[i].midi_note = -1;
    voices_[i].keydown = false;
    voices_[i].live = false;
    voices_[i].dx7_note->oscSync();
  }
  // Note: current_note_ is deliberately NOT reset here. dexed's panic()
  // leaves it alone, and chooseNote() reads voices_[currentNote] as its
  // starting point — a -1 cursor there walks off the voices array.
}

void DexedEngine::loadCartridge(int idx) {
  if (idx < 0 || idx >= DEXED_CART_COUNT)
    return;
  // dexed's loadCartridge(Cartridge&): keep the current program number and
  // re-read it out of the new bank.
  cart_.load(dexed_cart_data[idx], DEXED_SYSEX_SIZE);
  cart_index_ = idx;
  loadProgram(program_index_);
}

void DexedEngine::loadProgram(int idx) {
  if (idx < 0)
    idx = 0;
  if (idx >= DEXED_VOICES_PER_CART)
    idx = DEXED_VOICES_PER_CART - 1;

  // dexed's setCurrentProgram()
  panic();
  cart_.unpackProgram(data, idx);
  unpackOpSwitch(0x3F);
  // dexed leaves data[155] (the packed operator on/off byte) at whatever it
  // was until a switch param is touched — nothing reads it, but a stale byte
  // sitting next to live state is a trap. Keep it equal to opSwitch.
  packOpSwitch();
  lfo_.reset(data + 137);
  program_index_ = idx;
}

void DexedEngine::setEngineType(int type) {
  switch (type) {
    case DEXED_ENGINE_MARKI:
      controllers.core = &core_mark1_;
      break;
    case DEXED_ENGINE_OPL:
      controllers.core = &core_opl_;
      break;
    default:
      controllers.core = &core_msfa_;
      type = DEXED_ENGINE_MODERN;
      break;
  }
  engine_type_ = type;
}

void DexedEngine::setMonoMode(bool mono) {
  if (mono == mono_mode_)
    return;
  panic();
  mono_mode_ = mono;
}

void DexedEngine::packOpSwitch() {
  char value = (controllers.opSwitch[5] == '1') << 5;
  value += (controllers.opSwitch[4] == '1') << 4;
  value += (controllers.opSwitch[3] == '1') << 3;
  value += (controllers.opSwitch[2] == '1') << 2;
  value += (controllers.opSwitch[1] == '1') << 1;
  value += (controllers.opSwitch[0] == '1');
  data[155] = value;
}

void DexedEngine::unpackOpSwitch(char packOpValue) {
  controllers.opSwitch[5] = ((packOpValue >> 5) & 1) + 48;
  controllers.opSwitch[4] = ((packOpValue >> 4) & 1) + 48;
  controllers.opSwitch[3] = ((packOpValue >> 3) & 1) + 48;
  controllers.opSwitch[2] = ((packOpValue >> 2) & 1) + 48;
  controllers.opSwitch[1] = ((packOpValue >> 1) & 1) + 48;
  controllers.opSwitch[0] = (packOpValue & 1) + 48;
}

int DexedEngine::tuningTranspositionShift() const {
  // Standard tuning is the only state this plugin runs in, so dexed's
  // scale-aware branch (which maps the transpose onto scale degrees) can't
  // apply — this is its other branch.
  return data[144] - 24;
}

void DexedEngine::setDxValue(int offset, int value) {
  if (offset < 0)
    return;
  if (data[offset] == (uint8_t)value)
    return;
  data[offset] = (uint8_t)value;
  refresh_voice_ = true;

  // MIDDLE C (transpose) retunes every voice's base pitch
  if (offset == 144)
    panic();
}

double DexedEngine::masterTuneHostValue() const {
  // dexed's CtrlTune::getValueHost()
  int32_t tune = (int32_t)(controllers.masterTune / (1.0 / 12));
  tune = (tune >> 11) + 0x2000;
  return (double)tune / 0x4000;
}

double DexedEngine::getParam(uint32_t id) const {
  switch (id) {
    case 0:
      return fx_.uiCutoff;
    case 1:
      return fx_.uiReso;
    case 2:
      return fx_.uiGain;
    case 3:
      return mono_mode_ ? 1 : 0;
    case 4:
      return masterTuneHostValue();
    case DEXED_PARAM_CARTRIDGE:
      return cart_index_;
    case DEXED_PARAM_PROGRAM:
      return program_index_;
    case DEXED_PARAM_ENGINE:
      return engine_type_;
    default:
      break;
  }

  const DexedParamDesc* d = dexed_param_by_id(id);
  if (!d)
    return 0;
  if (d->op_switch >= 0)
    return controllers.opSwitch[d->op_switch] == '0' ? 0 : 1;
  if (d->dx_offset >= 0)
    return (double)data[d->dx_offset] + d->value_bias;
  return 0;
}

void DexedEngine::setParam(uint32_t id, double value) {
  switch (id) {
    case 0:
      fx_.uiCutoff = (float)value;
      return;
    case 1:
      fx_.uiReso = (float)value;
      return;
    case 2:
      fx_.uiGain = (float)value;
      return;
    case 3:
      setMonoMode(value != 0);
      return;
    case 4: {
      // dexed's CtrlTune::setValueHost()
      int32_t tune = (int32_t)(value * 0x4000) - 0x2000;
      controllers.masterTune = (int32_t)(((float)(tune << 11)) * (1.0 / 12));
      return;
    }
    case DEXED_PARAM_CARTRIDGE:
      loadCartridge((int)lround(value));
      return;
    case DEXED_PARAM_PROGRAM:
      loadProgram((int)lround(value));
      return;
    case DEXED_PARAM_ENGINE:
      setEngineType((int)lround(value));
      return;
    default:
      break;
  }

  const DexedParamDesc* d = dexed_param_by_id(id);
  if (!d)
    return;
  if (d->op_switch >= 0) {
    // dexed's CtrlOpSwitch::setValueHost(): store, then re-pack data[155]
    // through setDxValue's op-switch path, which also flags the voice refresh.
    controllers.opSwitch[d->op_switch] = value != 0 ? '1' : '0';
    packOpSwitch();
    refresh_voice_ = true;
    return;
  }
  if (d->dx_offset >= 0)
    setDxValue(d->dx_offset, (int)lround(value) - d->value_bias);
}

const char* DexedEngine::cartridgeName(int idx) const {
  if (idx < 0 || idx >= DEXED_CART_COUNT)
    return NULL;
  return dexed_cart_names[idx];
}

const char* DexedEngine::programName(int idx) const {
  if (idx < 0 || idx >= DEXED_VOICES_PER_CART)
    return NULL;
  cart_.getProgramName(idx, name_buf_);
  return name_buf_;
}

const char* DexedEngine::engineName(int idx) const {
  if (idx < 0 || idx >= DEXED_ENGINE_COUNT)
    return NULL;
  return kEngineNames[idx];
}

// dexed's DexedAudioProcessor::chooseNote()
int DexedEngine::chooseNote(uint8_t pitch) {
  // order of preference:
  // 1. a note that is not playing
  // 2. a note with its key up, playing the same pitch
  // 3. a note with its key up, playing a different pitch
  // 4. a note with its key down, playing the same pitch
  // 5. a note with its key down, playing a different pitch
  // break ties by preferring note with least recent keydown
  int bestNote = current_note_;
  int bestScore = -1;
  int note = current_note_;
  for (int i = 0; i < kMaxVoices; i++) {
    int score = 0;
    if (!voices_[note].dx7_note->isPlaying())
      score += 4;
    if (!voices_[note].keydown)
      score += 2;
    if (voices_[note].midi_note == pitch)
      score += 1;
    if ((score > bestScore) || (score == bestScore && voices_[note].keydown_seq < voices_[bestNote].keydown_seq)) {
      bestNote = note;
      bestScore = score;
    }
    note = (note + 1) % kMaxVoices;
  }
  return bestNote;
}

// dexed's keydown(), without the sustain-pedal/portamento branches (there's
// no CC input to reach them from a CLAP host that only sends notes).
void DexedEngine::keydown(uint8_t channel, uint8_t pitch, uint8_t velo) {
  if (velo == 0) {
    keyup(channel, pitch);
    return;
  }

  pitch = (uint8_t)(pitch + tuningTranspositionShift());

  // MPE is on by default in dexed; two keydowns on the same channel mean the
  // source isn't MPE, and it switches the whole instance back to
  // per-pitch note matching.
  if (controllers.mpeEnabled) {
    int note = current_note_;
    for (int i = 0; i < kMaxVoices; i++) {
      if (voices_[note].keydown && voices_[note].channel == channel)
        controllers.mpeEnabled = false;
      note = (note + 1) % kMaxVoices;
    }
  }

  bool triggerLfo = true;
  for (int i = 0; i < kMaxVoices; i++) {
    if (voices_[i].keydown) {
      triggerLfo = false;
      break;
    }
  }
  if (triggerLfo)
    lfo_.keydown();

  int note = chooseNote(pitch);

  current_note_ = (note + 1) % kMaxVoices;
  voices_[note].channel = channel;
  voices_[note].midi_note = pitch;
  voices_[note].velocity = velo;
  voices_[note].keydown = true;
  voices_[note].keydown_seq = next_keydown_seq_++;
  // to avoid click, don't sync oscillators when voice stealing
  bool voice_steal = voices_[note].dx7_note->isPlaying();
  voices_[note].dx7_note->init(data, pitch, velo, channel, &controllers);
  if (data[136] && !voice_steal)
    voices_[note].dx7_note->oscSync();

  if (mono_mode_) {
    for (int i = 0; i < kMaxVoices; i++) {
      if (voices_[i].live) {
        // all keys are up, only transfer signal
        if (!voices_[i].keydown) {
          voices_[i].live = false;
          voices_[note].dx7_note->transferSignal(*voices_[i].dx7_note);
          break;
        }
        if (voices_[i].midi_note < pitch) {
          voices_[i].live = false;
          voices_[note].dx7_note->transferState(*voices_[i].dx7_note);
          break;
        }
        return;
      }
    }
  } else if (!data[136]) {
    // if another note at the same pitch is playing, transfer phase to avoid
    // unpredictable destructive interference. this can cause clicking when
    // voice stealing, but we've tried to choose a voice to steal that will
    // minimise the chances of clicking
    for (int i = 0; i < kMaxVoices; i++) {
      if (i != note && voices_[i].dx7_note->isPlaying() && voices_[i].midi_note == pitch) {
        voices_[note].dx7_note->transferPhase(*voices_[i].dx7_note);
        break;
      }
    }
  }

  voices_[note].live = true;
}

// dexed's keyup(), ditto on the dropped branches.
void DexedEngine::keyup(uint8_t channel, uint8_t pitch) {
  pitch = (uint8_t)(pitch + tuningTranspositionShift());

  int note;
  for (note = 0; note < kMaxVoices; ++note) {
    if (((controllers.mpeEnabled && voices_[note].channel == channel) ||    // MPE mode - find voice by channel
         (!controllers.mpeEnabled && voices_[note].midi_note == pitch)) &&  // regular mode find voice by pitch
        voices_[note].keydown)                                              // but still only grab the one which is keydown
    {
      voices_[note].keydown = false;
      break;
    }
  }

  // note not found?
  if (note >= kMaxVoices)
    return;

  if (mono_mode_) {
    int highNote = -1;
    int target = 0;
    for (int i = 0; i < kMaxVoices; i++) {
      if (voices_[i].keydown && voices_[i].midi_note > highNote) {
        target = i;
        highNote = voices_[i].midi_note;
      }
    }

    if (highNote != -1 && voices_[note].live) {
      voices_[note].live = false;
      voices_[target].live = true;
      voices_[target].dx7_note->transferState(*voices_[note].dx7_note);
    }
  }

  voices_[note].dx7_note->keyup();
}

bool DexedEngine::applyNextEvent(uint32_t samplePos) {
  if (event_index_ >= event_count_)
    return false;
  const DexedNoteEvent& e = events_[event_index_];
  if (e.time > samplePos)
    return false;
  event_index_++;
  if (e.type == DexedNoteEvent::NOTE_ON)
    keydown(e.channel, e.key, e.velocity);
  else
    keyup(e.channel, e.key);
  return true;
}

// dexed's DexedAudioProcessor::processBlock(), with the JUCE MidiBuffer sweep
// replaced by the event list the host layer hands over.
void DexedEngine::render(float* out, uint32_t frames, const DexedNoteEvent* events, int event_count) {
  events_ = events;
  event_count_ = event_count;
  event_index_ = 0;

  if (refresh_voice_) {
    for (int i = 0; i < kMaxVoices; i++) {
      if (voices_[i].live)
        voices_[i].dx7_note->update(data, voices_[i].midi_note, voices_[i].velocity, voices_[i].channel);
    }
    lfo_.reset(data + 137);
    refresh_voice_ = false;
  }

  int numSamples = (int)frames;
  int i;

  // flush first events
  for (i = 0; i < numSamples && i < extra_buf_size_; i++)
    out[i] = extra_buf_[i];

  // remaining buffer is still to be processed
  if (extra_buf_size_ > numSamples) {
    for (int j = 0; j < extra_buf_size_ - numSamples; j++)
      extra_buf_[j] = extra_buf_[j + numSamples];
    extra_buf_size_ -= numSamples;

    // flush the events, they will be processed in the next cycle
    while (applyNextEvent((uint32_t)numSamples)) {
    }
  } else {
    for (; i < numSamples; i += kBlock) {
      int32_t audiobuf[kBlock];
      float sumbuf[kBlock];

      while (applyNextEvent((uint32_t)i)) {
      }

      for (int j = 0; j < kBlock; ++j) {
        audiobuf[j] = 0;
        sumbuf[j] = 0;
      }
      int32_t lfovalue = lfo_.getsample();
      int32_t lfodelay = lfo_.getdelay();

      for (int note = 0; note < kMaxVoices; ++note) {
        if (voices_[note].live) {
          voices_[note].dx7_note->compute(audiobuf, lfovalue, lfodelay, &controllers);

          for (int j = 0; j < kBlock; ++j) {
            int32_t val = audiobuf[j];
            val = val >> 4;
            int clip_val = val < -(1 << 24) ? 0x8000 : val >= (1 << 24) ? 0x7fff
                                                                        : val >> 9;
            float f = ((float)clip_val) / (float)0x8000;
            if (f > 1)
              f = 1;
            if (f < -1)
              f = -1;
            sumbuf[j] += f;
            audiobuf[j] = 0;
          }
        }
      }

      int jmax = numSamples - i;
      for (int j = 0; j < kBlock; ++j) {
        if (j < jmax)
          out[i + j] = sumbuf[j];
        else
          extra_buf_[j - jmax] = sumbuf[j];
      }
    }
    extra_buf_size_ = i - numSamples;
  }

  while (applyNextEvent((uint32_t)numSamples)) {
  }

  fx_.process(out, numSamples);
}
