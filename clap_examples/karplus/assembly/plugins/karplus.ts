import * as Clap from "as-clap"
import { CNumPtr } from "as-clap"

// Karplus-Strong plucked string.
//
// Each voice is a circular delay line (length = sampleRate/pitch) fed back
// through a one-pole lowpass filter — this loop recirculates near-losslessly
// and shapes brightness only (DAMP). The audible decay (DECAY) is a separate
// amplitude envelope multiplied onto the loop's output but *not* fed back
// into it, so the two controls stay independent: feeding the decay back into
// the loop let the filter's own dynamics swamp it, making DECAY nearly
// inaudible next to DAMP.

const NUM_VOICES = 8
const MAX_DELAY = 4096

const PARAM_DECAY: Clap.clap_id = 0x1001
const PARAM_DAMP: Clap.clap_id = 0x1002
const PARAM_PLUCK: Clap.clap_id = 0x1003
const PARAM_VOL: Clap.clap_id = 0x1004

class Voice {
  active: bool = false
  key: i32 = -1
  noteId: i32 = -1

  buf: Float32Array = new Float32Array(MAX_DELAY)
  bufLen: i32 = 8
  writePos: i32 = 0
  filtered: f32 = 0
  dampCoef: f32 = 0.5
  // Tiny fixed loss in the resonating loop itself — not the decay control,
  // just a safety net against a literally-undamped resonator.
  loopDecay: f32 = 0.9999995
  ampEnv: f32 = 0
  ampDecay: f32 = 0.999
  age: i32 = 0 // for voice-stealing only, not the decay/kill logic

  rng: u32 = 0x2545f491

  nextNoise(): f32 {
    // xorshift32
    let x = this.rng
    x ^= x << 13
    x ^= x >> 17
    x ^= x << 5
    this.rng = x
    return f32(x) / f32(u32.MAX_VALUE) * 2 - 1
  }

  matches(key: i32, noteId: i32): bool {
    if (this.noteId != -1 && noteId != -1) return this.noteId == noteId
    return this.key == key
  }

  pluck(key: i32, noteId: i32, velocity: f32, sampleRate: f32,
        decayParam: f32, dampParam: f32, pluckParam: f32): void {
    this.active = true
    this.key = key
    this.noteId = noteId
    this.age = 0
    this.ampEnv = 1

    let freq: f32 = 440.0 * Mathf.pow(2.0, f32(key - 69) / 12.0)
    let delayLen: f32 = sampleRate / freq
    if (delayLen < 4.0) delayLen = 4.0
    if (delayLen > f32(MAX_DELAY - 2)) delayLen = f32(MAX_DELAY - 2)
    this.bufLen = i32(Mathf.ceil(delayLen)) + 1
    this.writePos = 0
    this.filtered = 0

    // DAMP: brightness of the loop filter — low = dark/muted string,
    // high = bright/metallic ringing. Independent of DECAY (see below).
    this.dampCoef = 0.1 + dampParam * 0.85

    // DECAY: length of the amplitude envelope, in seconds — a plain
    // exponential ramp applied to the loop's output but not fed back into
    // it, so DAMP can't shorten or lengthen it.
    let targetSeconds: f32 = 0.05 + decayParam * decayParam * 8.0
    this.ampDecay = Mathf.pow(f32(0.0005), f32(1.0) / (targetSeconds * sampleRate))

    // Excitation: noise burst filtered by PLUCK brightness (soft mallet vs
    // hard pick), scaled by velocity.
    let pluckBrightness: f32 = 0.05 + pluckParam * 0.9
    let lp: f32 = 0
    for (let i = 0; i < this.bufLen; ++i) {
      let noise = this.nextNoise()
      lp += (noise - lp) * pluckBrightness
      this.buf[i] = lp * velocity
    }
  }

  release(): void {
    // A real string keeps ringing after you let go of the note — only
    // stop it early if it's already effectively silent.
  }

  step(): f32 {
    let sample = this.buf[this.writePos]

    this.filtered += (sample - this.filtered) * this.dampCoef
    let loopOut = this.filtered * this.loopDecay

    this.buf[this.writePos] = loopOut
    this.writePos += 1
    if (this.writePos >= this.bufLen) this.writePos = 0

    this.age += 1
    this.ampEnv *= this.ampDecay
    if (this.ampEnv < 1e-4) this.active = false

    return loopOut * this.ampEnv
  }
}

class Plugin extends Clap.Plugin {
  sampleRate: f32 = 44100

  decayParam: f32 = 0.5
  dampParam: f32 = 0.6
  pluckParam: f32 = 0.5
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
    info.id = 0x12345
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
    info.id = 0x12345
    info.name = "main"
    info.channelCount = 2
    info.portType = "stereo"
    return true
  }

  paramsCount(): u32 {
    return 4
  }
  paramsGetInfo(index: u32, info: Clap.ParamInfo): bool {
    info.flags = Clap.PARAM_IS_AUTOMATABLE
    info.minValue = 0
    info.maxValue = 1
    if (index == 0) {
      info.id = PARAM_DECAY
      info.name = "Decay"
      info.defaultValue = this.decayParam
    } else if (index == 1) {
      info.id = PARAM_DAMP
      info.name = "Damp"
      info.defaultValue = this.dampParam
    } else if (index == 2) {
      info.id = PARAM_PLUCK
      info.name = "Pluck"
      info.defaultValue = this.pluckParam
    } else if (index == 3) {
      info.id = PARAM_VOL
      info.name = "Volume"
      info.defaultValue = this.volParam
    } else {
      return false
    }
    return true
  }
  paramsGetValue(id: Clap.clap_id, value: CNumPtr<f64>): bool {
    if (id == PARAM_DECAY) value[0] = this.decayParam
    else if (id == PARAM_DAMP) value[0] = this.dampParam
    else if (id == PARAM_PLUCK) value[0] = this.pluckParam
    else if (id == PARAM_VOL) value[0] = this.volParam
    else return false
    return true
  }
  paramsValueToText(id: Clap.clap_id, value: f64): string | null {
    if (id == PARAM_DECAY || id == PARAM_DAMP || id == PARAM_PLUCK) {
      return `${Math.round(value * 100)}%`
    }
    if (id == PARAM_VOL) {
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
      if (valueEvent._param_id == PARAM_DECAY) this.decayParam = v
      else if (valueEvent._param_id == PARAM_DAMP) this.dampParam = v
      else if (valueEvent._param_id == PARAM_PLUCK) this.pluckParam = v
      else if (valueEvent._param_id == PARAM_VOL) this.volParam = v
      else return false
      if (this.hostState) this.hostStateMarkDirty()
      return true
    } else if (event._type == Clap.EVENT_NOTE_ON) {
      let noteEvent = changetype<Clap.clap_event_note>(event)
      // Steal the oldest voice if every voice is already ringing.
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
      victim.pluck(noteEvent._key, noteEvent._note_id, f32(noteEvent._velocity),
        this.sampleRate, this.decayParam, this.dampParam, this.pluckParam)
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
    let vol = this.volParam

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
          if (voice.active) mix += voice.step()
        }
        mix *= vol
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

let pluginSpec = Clap.registerPlugin<Plugin>("Karplus String (AssemblyScript)", "com.poketrack.clap.karplus")
pluginSpec.vendor = "poketrack"
pluginSpec.features = ["instrument"]
