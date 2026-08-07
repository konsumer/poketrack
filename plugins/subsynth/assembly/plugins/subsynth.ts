import * as Clap from "as-clap"
import { CNumPtr } from "as-clap"

// Analog-style subtractive synth: one multi-shape oscillator through a
// resonant state-variable filter with its own decay envelope, then a
// separate amp envelope. This is the one classic synth-voice shape
// poketrack doesn't already have natively — the built-in OSC unit has no
// filter at all, and the built-in FILTER effect has no per-note envelope
// of its own (only a static cutoff, optionally swept by LFO). Self-
// resonating filter + envelope is the whole point (acid basslines,
// squelchy leads, dark pads), so those two params (RESO, ENVAMT) are kept
// even though it meant leaving out extras like a sub-oscillator or pulse
// width, to fit poketrack's convention of 8 params per unit.

const NUM_VOICES = 8

const PARAM_WAVE: Clap.clap_id = 0x2001
const PARAM_CUTOFF: Clap.clap_id = 0x2002
const PARAM_RESO: Clap.clap_id = 0x2003
const PARAM_ENVAMT: Clap.clap_id = 0x2004
const PARAM_FDECAY: Clap.clap_id = 0x2005
const PARAM_ADECAY: Clap.clap_id = 0x2006
const PARAM_SUSTAIN: Clap.clap_id = 0x2007
const PARAM_VOL: Clap.clap_id = 0x2008

const WAVE_SAW = 0
const WAVE_SQR = 1
const WAVE_TRI = 2
const WAVE_SINE = 3

class Voice {
  active: bool = false
  releasing: bool = false
  key: i32 = -1
  noteId: i32 = -1
  velocity: f32 = 1
  age: i32 = 0

  phase: f32 = 0
  phaseInc: f32 = 0

  // State-variable filter state (Chamberlin topology).
  low: f32 = 0
  band: f32 = 0

  filterEnv: f32 = 0
  filterDecayCoef: f32 = 0.999

  ampEnv: f32 = 0
  ampDecayCoef: f32 = 0.999
  releaseCoef: f32 = 0.99
  sustainLevel: f32 = 0

  matches(key: i32, noteId: i32): bool {
    if (this.noteId != -1 && noteId != -1) return this.noteId == noteId
    return this.key == key
  }

  start(key: i32, noteId: i32, velocity: f32, sampleRate: f32,
        fdecayParam: f32, adecayParam: f32, sustainParam: f32): void {
    this.active = true
    this.releasing = false
    this.key = key
    this.noteId = noteId
    this.velocity = velocity
    this.age = 0

    let freq: f32 = 440.0 * Mathf.pow(2.0, f32(key - 69) / 12.0)
    this.phase = 0
    this.phaseInc = freq / sampleRate

    this.low = 0
    this.band = 0

    // FDECAY: filter envelope always decays fully to the base cutoff, same
    // shape regardless of note-off — a classic acid/303-style sweep, not
    // tied to the amp envelope's sustain/release.
    let fSeconds: f32 = 0.005 + fdecayParam * fdecayParam * 3.0
    this.filterEnv = 1
    this.filterDecayCoef = Mathf.pow(f32(0.001), f32(1.0) / (fSeconds * sampleRate))

    // ADECAY: time for the amp envelope to settle near its target (either
    // SUSTAIN on note-on, or 0 on note-off/release).
    let aSeconds: f32 = 0.01 + adecayParam * adecayParam * 4.0
    this.ampEnv = 1
    this.sustainLevel = sustainParam
    this.ampDecayCoef = Mathf.pow(f32(0.001), f32(1.0) / (aSeconds * sampleRate))
    // Note-off just speeds up whatever's left into a quick release, rather
    // than needing a separate REL param.
    this.releaseCoef = this.ampDecayCoef * this.ampDecayCoef * this.ampDecayCoef * this.ampDecayCoef
  }

  release(): void {
    this.releasing = true
  }

  oscSample(wave: i32): f32 {
    let p = this.phase
    let s: f32 = 0
    if (wave == WAVE_SAW) {
      s = 2.0 * p - 1.0
    } else if (wave == WAVE_SQR) {
      s = p < 0.5 ? 1.0 : -1.0
    } else if (wave == WAVE_TRI) {
      s = p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p)
    } else {
      s = Mathf.sin(2.0 * Mathf.PI * p)
    }
    this.phase += this.phaseInc
    if (this.phase >= 1.0) this.phase -= 1.0
    return s
  }

  step(wave: i32, sampleRate: f32, baseCutoffHz: f32, envAmount: f32, resoQ: f32): f32 {
    let osc = this.oscSample(wave) * this.velocity

    // Filter envelope sweeps cutoff upward from the base, then decays away.
    let cutoffHz = baseCutoffHz * (1.0 + envAmount * 8.0 * this.filterEnv)
    let nyquistLimit = sampleRate * 0.45
    if (cutoffHz > nyquistLimit) cutoffHz = nyquistLimit
    this.filterEnv *= this.filterDecayCoef

    let f: f32 = 2.0 * Mathf.sin(Mathf.PI * cutoffHz / sampleRate)
    if (f > 1.9) f = 1.9
    this.low += f * this.band
    let high = osc - this.low - resoQ * this.band
    this.band += f * high

    // Amp envelope: converges toward SUSTAIN while held, toward 0 once
    // released.
    let target: f32 = this.releasing ? 0 : this.sustainLevel
    let coef: f32 = this.releasing ? this.releaseCoef : this.ampDecayCoef
    this.ampEnv = target + (this.ampEnv - target) * coef

    this.age += 1
    if (this.ampEnv < 1e-4 && target < 1e-4) this.active = false

    return this.low * this.ampEnv
  }
}

class Plugin extends Clap.Plugin {
  sampleRate: f32 = 44100

  waveParam: f32 = 0
  cutoffParam: f32 = 0.5
  resoParam: f32 = 0.3
  envAmtParam: f32 = 0.5
  fDecayParam: f32 = 0.4
  aDecayParam: f32 = 0.3
  sustainParam: f32 = 0.6
  volParam: f32 = 0.8

  voices: Voice[]

  constructor(host: Clap.clap_host) {
    super(host)
    this.voices = new Array<Voice>(NUM_VOICES)
    for (let i = 0; i < NUM_VOICES; ++i) this.voices[i] = new Voice()
  }

  pluginInit(): bool {
    if (!super.pluginInit()) return false
    return true
  }

  pluginActivate(sampleRate: f64, minFrames: u32, maxFrames: u32): bool {
    this.sampleRate = f32(sampleRate)
    return true
  }

  pluginReset(): void {
    for (let i = 0; i < NUM_VOICES; ++i) this.voices[i].active = false
  }

  notePortsCount(isInput: bool): u32 {
    return isInput ? 1 : 0
  }
  notePortsGet(index: u32, isInput: bool, info: Clap.NotePortInfo): bool {
    if (index >= this.notePortsCount(isInput)) return false
    info.id = 0x22345
    info.supportedDialects = Clap.NOTE_DIALECT_CLAP
    info.preferredDialect = Clap.NOTE_DIALECT_CLAP
    info.name = "notes"
    return true
  }

  audioPortsCount(isInput: bool): u32 {
    return isInput ? 0 : 1
  }
  audioPortsGet(index: u32, isInput: bool, info: Clap.AudioPortInfo): bool {
    if (index >= this.audioPortsCount(isInput)) return false
    info.id = 0x22345
    info.name = "main"
    info.channelCount = 2
    info.portType = "stereo"
    return true
  }

  paramsCount(): u32 {
    return 8
  }
  paramsGetInfo(index: u32, info: Clap.ParamInfo): bool {
    info.flags = Clap.PARAM_IS_AUTOMATABLE
    info.minValue = 0
    info.maxValue = 1
    if (index == 0) {
      info.id = PARAM_WAVE
      info.name = "Wave"
      info.maxValue = 3
      info.flags = Clap.PARAM_IS_AUTOMATABLE | Clap.PARAM_IS_STEPPED | Clap.PARAM_IS_ENUM
      info.defaultValue = this.waveParam
    } else if (index == 1) {
      info.id = PARAM_CUTOFF
      info.name = "Cutoff"
      info.defaultValue = this.cutoffParam
    } else if (index == 2) {
      info.id = PARAM_RESO
      info.name = "Reso"
      info.defaultValue = this.resoParam
    } else if (index == 3) {
      info.id = PARAM_ENVAMT
      info.name = "EnvAmt"
      info.defaultValue = this.envAmtParam
    } else if (index == 4) {
      info.id = PARAM_FDECAY
      info.name = "FDecay"
      info.defaultValue = this.fDecayParam
    } else if (index == 5) {
      info.id = PARAM_ADECAY
      info.name = "ADecay"
      info.defaultValue = this.aDecayParam
    } else if (index == 6) {
      info.id = PARAM_SUSTAIN
      info.name = "Sustain"
      info.defaultValue = this.sustainParam
    } else if (index == 7) {
      info.id = PARAM_VOL
      info.name = "Volume"
      info.defaultValue = this.volParam
    } else {
      return false
    }
    return true
  }
  paramsGetValue(id: Clap.clap_id, value: CNumPtr<f64>): bool {
    if (id == PARAM_WAVE) value[0] = this.waveParam
    else if (id == PARAM_CUTOFF) value[0] = this.cutoffParam
    else if (id == PARAM_RESO) value[0] = this.resoParam
    else if (id == PARAM_ENVAMT) value[0] = this.envAmtParam
    else if (id == PARAM_FDECAY) value[0] = this.fDecayParam
    else if (id == PARAM_ADECAY) value[0] = this.aDecayParam
    else if (id == PARAM_SUSTAIN) value[0] = this.sustainParam
    else if (id == PARAM_VOL) value[0] = this.volParam
    else return false
    return true
  }
  paramsValueToText(id: Clap.clap_id, value: f64): string | null {
    if (id == PARAM_WAVE) {
      let w = i32(Math.round(value))
      if (w == WAVE_SAW) return "Saw"
      if (w == WAVE_SQR) return "Square"
      if (w == WAVE_TRI) return "Triangle"
      return "Sine"
    }
    if (id == PARAM_CUTOFF || id == PARAM_RESO || id == PARAM_ENVAMT ||
        id == PARAM_FDECAY || id == PARAM_ADECAY || id == PARAM_SUSTAIN ||
        id == PARAM_VOL) {
      return `${Math.round(value * 100)}%`
    }
    return null
  }
  paramsFlush(inputEvents: Clap.InputEvents, outputEvents: Clap.OutputEvents): void {
    let count = inputEvents.size()
    for (let i: u32 = 0; i < count; ++i) {
      let event = inputEvents.get(i)
      if (!this.handleEvent(event)) outputEvents.tryPush(event)
    }
  }

  handleEvent(event: Clap.clap_event_header): bool {
    if (event._space_id != Clap.CORE_EVENT_SPACE_ID) return false
    if (event._type == Clap.EVENT_PARAM_VALUE) {
      let valueEvent = changetype<Clap.clap_event_param_value>(event)
      let v = f32(valueEvent._value)
      if (valueEvent._param_id == PARAM_WAVE) this.waveParam = v
      else if (valueEvent._param_id == PARAM_CUTOFF) this.cutoffParam = v
      else if (valueEvent._param_id == PARAM_RESO) this.resoParam = v
      else if (valueEvent._param_id == PARAM_ENVAMT) this.envAmtParam = v
      else if (valueEvent._param_id == PARAM_FDECAY) this.fDecayParam = v
      else if (valueEvent._param_id == PARAM_ADECAY) this.aDecayParam = v
      else if (valueEvent._param_id == PARAM_SUSTAIN) this.sustainParam = v
      else if (valueEvent._param_id == PARAM_VOL) this.volParam = v
      else return false
      if (this.hostState) this.hostStateMarkDirty()
      return true
    } else if (event._type == Clap.EVENT_NOTE_ON) {
      let noteEvent = changetype<Clap.clap_event_note>(event)
      let victim: Voice = this.voices[0]
      let oldestAge = -1
      for (let n = 0; n < NUM_VOICES; ++n) {
        let voice = this.voices[n]
        if (!voice.active) {
          victim = voice
          oldestAge = i32.MAX_VALUE
          break
        }
        if (voice.age > oldestAge) {
          oldestAge = voice.age
          victim = voice
        }
      }
      let velocity = f32(noteEvent._velocity)
      if (velocity <= 0) velocity = 1
      victim.start(noteEvent._key, noteEvent._note_id, velocity, this.sampleRate,
        this.fDecayParam, this.aDecayParam, this.sustainParam)
      return true
    } else if (event._type == Clap.EVENT_NOTE_OFF) {
      let noteEvent = changetype<Clap.clap_event_note>(event)
      for (let n = 0; n < NUM_VOICES; ++n) {
        let voice = this.voices[n]
        if (voice.active && voice.matches(noteEvent._key, noteEvent._note_id)) {
          voice.release()
          break
        }
      }
      if (noteEvent._note_id >= 0) {
        event._type = u16(Clap.EVENT_NOTE_END)
        return false
      }
      return true
    }
    return false
  }

  pluginProcess(process: Clap.Process): i32 {
    let length = process.framesCount
    let audioOut = process.audioOutputs[0]
    let left = audioOut.data32[0]
    let right = audioOut.data32[1]

    let eventCount = process.inEvents.size()
    let eventIndex: u32 = 0
    let sampleIndex: u32 = 0

    let wave = i32(Math.round(this.waveParam))
    let vol = this.volParam
    let baseCutoffHz: f32 = 40.0 * Mathf.pow(200.0, this.cutoffParam)
    let envAmount = this.envAmtParam
    let resoQ: f32 = 2.02 - 1.98 * this.resoParam

    while (true) {
      let blockEnd = length
      if (eventIndex < eventCount) {
        let peek = process.inEvents[eventIndex]
        if (peek._time < length) blockEnd = peek._time
      }
      for (let i = sampleIndex; i < blockEnd; ++i) {
        let mix: f32 = 0
        for (let n = 0; n < NUM_VOICES; ++n) {
          let voice = this.voices[n]
          if (voice.active) {
            mix += voice.step(wave, this.sampleRate, baseCutoffHz, envAmount, resoQ)
          }
        }
        mix *= vol * 0.5
        left[i] = mix
        right[i] = mix
      }
      sampleIndex = blockEnd
      if (eventIndex >= eventCount) break
      let event = process.inEvents[eventIndex++]
      if (!this.handleEvent(event)) process.outEvents.tryPush(event)
    }

    return Clap.PROCESS_CONTINUE
  }
}

let pluginSpec = Clap.registerPlugin<Plugin>("SubSynth (AssemblyScript)", "com.poketrack.clap.subsynth")
pluginSpec.vendor = "poketrack"
pluginSpec.features = ["instrument"]
