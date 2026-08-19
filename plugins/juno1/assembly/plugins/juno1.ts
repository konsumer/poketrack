import * as Clap from "as-clap"
import { CNumPtr } from "as-clap"
import * as T from "./tables"
import { Preset, PRESET_COUNT, makePreset } from "./presets-data"

// Roland Juno-1 / Alpha Juno DCO synth, ported from mikerodd/june-21's
// `June21` CSound opcode (src/cabbage-module/june-21.csd, GPL-3.0-or-later)
// — see that file for the reference DSP this mirrors. Not a wrapper: this
// is a from-scratch AssemblyScript reimplementation of the same DCO -> HPF
// -> VCF -> VCA -> chorus signal path, using standard public DSP building
// blocks (PolyBLEP oscillators, a resonance-free 4-pole ladder lowpass +
// resonant SVF bandpass in series, RBJ biquads for the fixed HPF voicings,
// LFO-modulated delay lines for the chorus) in place of Csound's `vco2`,
// `moogvcf`/`reson`, and `StChorus` opcodes, which have no wasm equivalent.
//
// Every one of the real hardware's 36 patch parameters (decoded from real
// Juno-1/Alpha Juno SysEx dumps by scripts/syx2presets.mjs into
// presets-data.ts) is its own CLAP param here — a full 1:1 mapping, plus
// `Patch` to recall a full factory preset. poketrack's ADD-row automation
// caps a *unit* at 16 mappable params, but that's a mapping-UI limit, not a
// limit on what a CLAP plugin may declare — the user picks which ≤16 of
// these 37 they actually want live-automatable per instrument; the rest
// just sit at whatever the selected Patch (or a manually-set default) put
// them at. Selecting a Patch resets every param back to that patch's own
// value; touching a param afterward overrides just that one (same
// "touch a knob to take control of it" behaviour as patch recall on real
// hardware).

const NUM_VOICES = 6 // matches real hardware's gkmaxvoices = 6

// ---------------------------------------------------------------------------
// Param IDs — Patch, then one per real hardware param (FIELD_* order below).
// ---------------------------------------------------------------------------

const P_PATCH: Clap.clap_id = 0x3000
const NUM_FIELDS = 36
const FIELD_ID_BASE: Clap.clap_id = 0x3001 // field i -> FIELD_ID_BASE + i
const NUM_PARAMS = 1 + NUM_FIELDS

const FIELD_NAMES: StaticArray<string> = [
  "DcoAftr", "VcfKybd", "VcfAftr", "VcaAftr", "EnvKybd", "DcoBnd", "DcoLfo",
  "Chorus", "DcoEnvd", "PwPwm", "DcoEnv", "PwmRate", "VcfFreq", "VcfEnv",
  "VcfReso", "VcfEnvd", "VcaEnv", "VcfLfo", "VcaLevl", "Sub", "LfoRate",
  "LfoDely", "EnvT1", "Sawtooth", "EnvL1", "EnvT2", "EnvL2", "Pulse",
  "EnvT3", "EnvL3", "HpfFreq", "EnvT4", "DcoRng", "SubLevl", "NoisLvl",
  "CrsRate"
]

const FIELD_MAX: StaticArray<i32> = [
  15, 15, 15, 15, 15, 15, 127,
  1, 127, 127, 3, 127, 127, 3,
  127, 127, 3, 127, 127, 5, 127,
  127, 127, 5, 127, 127, 127, 3,
  127, 127, 3, 127, 3, 3, 3,
  127
]

function getPresetField(p: Preset, idx: i32): u8 {
  if (idx == 0) return p.dcoAftr
  if (idx == 1) return p.vcfKybd
  if (idx == 2) return p.vcfAftr
  if (idx == 3) return p.vcaAftr
  if (idx == 4) return p.envKybd
  if (idx == 5) return p.dcoBnd
  if (idx == 6) return p.dcoLfo
  if (idx == 7) return p.chorus
  if (idx == 8) return p.dcoEnvd
  if (idx == 9) return p.pwPwm
  if (idx == 10) return p.dcoEnv
  if (idx == 11) return p.pwmRate
  if (idx == 12) return p.vcfFreq
  if (idx == 13) return p.vcfEnv
  if (idx == 14) return p.vcfReso
  if (idx == 15) return p.vcfEnvd
  if (idx == 16) return p.vcaEnv
  if (idx == 17) return p.vcfLfo
  if (idx == 18) return p.vcaLevl
  if (idx == 19) return p.sub
  if (idx == 20) return p.lfoRate
  if (idx == 21) return p.lfoDely
  if (idx == 22) return p.envT1
  if (idx == 23) return p.sawtooth
  if (idx == 24) return p.envL1
  if (idx == 25) return p.envT2
  if (idx == 26) return p.envL2
  if (idx == 27) return p.pulse
  if (idx == 28) return p.envT3
  if (idx == 29) return p.envL3
  if (idx == 30) return p.hpfFreq
  if (idx == 31) return p.envT4
  if (idx == 32) return p.dcoRng
  if (idx == 33) return p.subLevl
  if (idx == 34) return p.noisLvl
  if (idx == 35) return p.crsRate
  return 0
}

function setPresetField(p: Preset, idx: i32, v: u8): void {
  if (idx == 0) p.dcoAftr = v
  else if (idx == 1) p.vcfKybd = v
  else if (idx == 2) p.vcfAftr = v
  else if (idx == 3) p.vcaAftr = v
  else if (idx == 4) p.envKybd = v
  else if (idx == 5) p.dcoBnd = v
  else if (idx == 6) p.dcoLfo = v
  else if (idx == 7) p.chorus = v
  else if (idx == 8) p.dcoEnvd = v
  else if (idx == 9) p.pwPwm = v
  else if (idx == 10) p.dcoEnv = v
  else if (idx == 11) p.pwmRate = v
  else if (idx == 12) p.vcfFreq = v
  else if (idx == 13) p.vcfEnv = v
  else if (idx == 14) p.vcfReso = v
  else if (idx == 15) p.vcfEnvd = v
  else if (idx == 16) p.vcaEnv = v
  else if (idx == 17) p.vcfLfo = v
  else if (idx == 18) p.vcaLevl = v
  else if (idx == 19) p.sub = v
  else if (idx == 20) p.lfoRate = v
  else if (idx == 21) p.lfoDely = v
  else if (idx == 22) p.envT1 = v
  else if (idx == 23) p.sawtooth = v
  else if (idx == 24) p.envL1 = v
  else if (idx == 25) p.envT2 = v
  else if (idx == 26) p.envL2 = v
  else if (idx == 27) p.pulse = v
  else if (idx == 28) p.envT3 = v
  else if (idx == 29) p.envL3 = v
  else if (idx == 30) p.hpfFreq = v
  else if (idx == 31) p.envT4 = v
  else if (idx == 32) p.dcoRng = v
  else if (idx == 33) p.subLevl = v
  else if (idx == 34) p.noisLvl = v
  else if (idx == 35) p.crsRate = v
}

// ---------------------------------------------------------------------------
// Small DSP building blocks
// ---------------------------------------------------------------------------

function polyBlep(t: f32, dt: f32): f32 {
  if (t < dt) {
    let x: f32 = t / dt
    return x + x - x * x - 1.0
  } else if (t > 1.0 - dt) {
    let x: f32 = (t - 1.0) / dt
    return x * x + x + x + 1.0
  }
  return 0.0
}

function wrapPhase(p: f32): f32 {
  return p - Mathf.floor(p)
}

function blepSaw(phase: f32, dt: f32): f32 {
  let v: f32 = 2.0 * phase - 1.0
  v -= polyBlep(phase, dt)
  return v
}

function blepPulse(phase: f32, dt: f32, duty: f32): f32 {
  let d: f32 = duty
  if (d < 0.02) d = 0.02
  else if (d > 0.98) d = 0.98
  let v: f32 = phase < d ? 1.0 : -1.0
  v += polyBlep(phase, dt)
  v -= polyBlep(wrapPhase(phase + 1.0 - d), dt)
  return v
}

// Cascaded one-pole lowpass — real Juno's `moogvcf` is always called with
// zero resonance in the source engine (all resonance comes from the reson
// bandpass stage below), so a plain 4-pole RC-style ladder is equivalent.
class LadderLP {
  z1: f32 = 0; z2: f32 = 0; z3: f32 = 0; z4: f32 = 0
  process(x: f32, coef: f32): f32 {
    this.z1 += coef * (x - this.z1)
    this.z2 += coef * (this.z1 - this.z2)
    this.z3 += coef * (this.z2 - this.z3)
    this.z4 += coef * (this.z3 - this.z4)
    return this.z4
  }
}

// Fixed-coefficient biquad (direct form 1) — used for the 4 HPF voicings
// and, with per-sample coefficients, the resonant bandpass stage.
class Biquad {
  b0: f32 = 1; b1: f32 = 0; b2: f32 = 0; a1: f32 = 0; a2: f32 = 0
  x1: f32 = 0; x2: f32 = 0; y1: f32 = 0; y2: f32 = 0

  setBypass(): void {
    this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0
  }

  setHighpass(freq: f32, q: f32, sr: f32): void {
    let w0: f32 = 2.0 * f32(Math.PI) * freq / sr
    let alpha: f32 = Mathf.sin(w0) / (2.0 * q)
    let cosw0: f32 = Mathf.cos(w0)
    let b0: f32 = (1.0 + cosw0) / 2.0
    let b1: f32 = -(1.0 + cosw0)
    let b2: f32 = (1.0 + cosw0) / 2.0
    let a0: f32 = 1.0 + alpha
    let a1: f32 = -2.0 * cosw0
    let a2: f32 = 1.0 - alpha
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0
    this.a1 = a1 / a0; this.a2 = a2 / a0
  }

  setPeaking(freq: f32, gainDb: f32, q: f32, sr: f32): void {
    let a: f32 = Mathf.pow(10.0, gainDb / 40.0)
    let w0: f32 = 2.0 * f32(Math.PI) * freq / sr
    let alpha: f32 = Mathf.sin(w0) / (2.0 * q)
    let cosw0: f32 = Mathf.cos(w0)
    let b0: f32 = 1.0 + alpha * a
    let b1: f32 = -2.0 * cosw0
    let b2: f32 = 1.0 - alpha * a
    let a0: f32 = 1.0 + alpha / a
    let a1: f32 = -2.0 * cosw0
    let a2: f32 = 1.0 - alpha / a
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0
    this.a1 = a1 / a0; this.a2 = a2 / a0
  }

  // RBJ "constant 0dB peak gain" bandpass — the peak gain stays 1.0
  // regardless of Q, matching Csound's `reson` with iscl=2 (which the real
  // engine uses): only the shape narrows/widens with Q, the passed-through
  // level never drops the way a plain SVF band output's does.
  setBandpassConstantPeak(freq: f32, q: f32, sr: f32): void {
    let w0: f32 = 2.0 * f32(Math.PI) * freq / sr
    let alpha: f32 = Mathf.sin(w0) / (2.0 * q)
    let cosw0: f32 = Mathf.cos(w0)
    let b0: f32 = alpha
    let b1: f32 = 0
    let b2: f32 = -alpha
    let a0: f32 = 1.0 + alpha
    let a1: f32 = -2.0 * cosw0
    let a2: f32 = 1.0 - alpha
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0
    this.a1 = a1 / a0; this.a2 = a2 / a0
  }

  process(x: f32): f32 {
    let y: f32 = this.b0 * x + this.b1 * this.x1 + this.b2 * this.x2 - this.a1 * this.y1 - this.a2 * this.y2
    this.x2 = this.x1; this.x1 = x
    this.y2 = this.y1; this.y1 = y
    return y
  }
}

// Multi-segment envelope generator matching the real engine's `transegr`-
// style behaviour: it keeps progressing through its segments even without
// a note-off (the Juno envelope always eventually decays on its own), and
// note-off jumps straight to the last segment from wherever it currently is.
class EnvGen {
  segTarget: StaticArray<f32> = new StaticArray<f32>(4)
  segDur: StaticArray<f32> = new StaticArray<f32>(4)
  segCurve: StaticArray<f32> = new StaticArray<f32>(4)
  segCount: i32 = 0
  seg: i32 = 0
  segTime: f32 = 0
  segStart: f32 = 0
  value: f32 = 0
  finished: bool = false

  private begin(): void {
    this.seg = 0; this.segTime = 0; this.segStart = 0; this.value = 0; this.finished = false
  }

  // Native 6-breakpoint shape, ported from June21's two `transegr` branches.
  setupNative(envT1: u8, envL1: u8, envT2: u8, envL2: u8, envT3: u8, envL3: u8, envT4: u8): void {
    let l1: f32 = f32(envL1), l2: f32 = f32(envL2), l3: f32 = f32(envL3)
    let dur1: f32, dur2: f32, dur3: f32, dur4: f32, curve3: f32, curve4: f32
    dur2 = T.ENV_T1[envT2]
    if (l1 > l2) {
      dur1 = T.ENV_T1[envT1]
      dur3 = T.ENV_T3[envT3]; curve3 = -2
      dur4 = T.ENV_T4[envT4]; curve4 = -4
    } else {
      let idx: i32 = i32(f32(envT1) * l1 / 127.0)
      if (idx < 0) idx = 0
      if (idx > 127) idx = 127
      dur1 = T.ENV_T1[idx]
      dur3 = T.ENV_T4[envT3]; curve3 = -4
      dur4 = T.ENV_T4[envT4]; curve4 = -4
    }
    this.segTarget[0] = l1; this.segDur[0] = Mathf.max(dur1, 0.0005); this.segCurve[0] = 0
    this.segTarget[1] = l2; this.segDur[1] = Mathf.max(dur2, 0.0005); this.segCurve[1] = 0
    this.segTarget[2] = l3; this.segDur[2] = Mathf.max(dur3, 0.0005); this.segCurve[2] = curve3
    this.segTarget[3] = 0; this.segDur[3] = Mathf.max(dur4, 0.0005); this.segCurve[3] = curve4
    this.segCount = 4
    this.begin()
  }

  // Fixed-shape percussive envelope for the VCA's "Gate" mode.
  setupGate(): void {
    this.segTarget[0] = 1; this.segDur[0] = 0.0001; this.segCurve[0] = -4
    this.segTarget[1] = 0; this.segDur[1] = 0.05; this.segCurve[1] = -4
    this.segCount = 2
    this.begin()
  }

  release(): void {
    let last: i32 = this.segCount - 1
    if (this.seg != last) {
      this.seg = last
      this.segStart = this.value
      this.segTime = 0
    }
  }

  tick(dt: f32): f32 {
    if (this.finished) return this.value
    this.segTime += dt
    let dur: f32 = this.segDur[this.seg]
    let t: f32 = this.segTime / dur
    if (t > 1.0) t = 1.0
    let curve: f32 = this.segCurve[this.seg]
    let start: f32 = this.segStart
    let target: f32 = this.segTarget[this.seg]
    let frac: f32
    if (Mathf.abs(curve) < 0.0001) {
      frac = t
    } else {
      frac = (1.0 - Mathf.exp(-curve * t)) / (1.0 - Mathf.exp(-curve))
    }
    this.value = start + (target - start) * frac
    if (t >= 1.0) {
      if (this.seg < this.segCount - 1) {
        this.seg += 1; this.segStart = this.value; this.segTime = 0
      } else {
        this.finished = true
      }
    }
    return this.value
  }
}

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------

class Voice {
  active: bool = false
  key: i32 = -1
  noteId: i32 = -1
  velocity: f32 = 1
  age: i32 = 0

  // Oscillator phases.
  pulsePhase: f32 = 0
  sawPhase: f32 = 0
  sawPhase2x: f32 = 0
  sawPhase8x: f32 = 0
  subPhaseHalf: f32 = 0
  subPhaseQuarter: f32 = 0
  subPhase2x: f32 = 0
  subPhase4x: f32 = 0
  pwmLfoPhase: f32 = 0
  noiseState: u32 = 0x1234567

  hpf: Biquad = new Biquad()
  vcf: LadderLP = new LadderLP()
  vcfBand: Biquad = new Biquad()

  env: EnvGen = new EnvGen()
  gate: EnvGen = new EnvGen()

  matches(key: i32, noteId: i32): bool {
    if (this.noteId != -1 && noteId != -1) return this.noteId == noteId
    return this.key == key
  }

  start(key: i32, noteId: i32, velocity: f32, patch: Preset): void {
    this.active = true
    this.key = key
    this.noteId = noteId
    this.velocity = velocity
    this.age = 0

    this.pulsePhase = 0; this.sawPhase = 0; this.sawPhase2x = 0; this.sawPhase8x = 0
    this.subPhaseHalf = 0; this.subPhaseQuarter = 0; this.subPhase2x = 0; this.subPhase4x = 0
    this.pwmLfoPhase = 0
    this.hpf.x1 = 0; this.hpf.x2 = 0; this.hpf.y1 = 0; this.hpf.y2 = 0
    this.vcf.z1 = 0; this.vcf.z2 = 0; this.vcf.z3 = 0; this.vcf.z4 = 0
    this.vcfBand.x1 = 0; this.vcfBand.x2 = 0; this.vcfBand.y1 = 0; this.vcfBand.y2 = 0

    this.env.setupNative(patch.envT1, patch.envL1, patch.envT2, patch.envL2, patch.envT3, patch.envL3, patch.envT4)
    this.gate.setupGate()
  }

  release(): void {
    this.env.release()
    this.gate.release()
  }

  nextNoise(): f32 {
    // xorshift32 -> unit white noise.
    let x = this.noiseState
    x ^= x << 13; x ^= x >> 17; x ^= x << 5
    this.noiseState = x
    return (f32(x & 0xffffff) / f32(0xffffff)) * 2.0 - 1.0
  }
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

class Plugin extends Clap.Plugin {
  sampleRate: f32 = 44100

  patch: Preset = makePreset(0)
  patchIndex: i32 = 0 // 0..PRESET_COUNT-1 — also the param's own value; see loadPatch()
  defaultPatch: Preset = makePreset(0) // stable source for reported CLAP defaults

  // Raw CLAP param values for the 36 real fields, and whether each has been
  // explicitly set since the last Patch recall (if not, the field tracks
  // whatever the currently-selected Patch specifies).
  ovValue: StaticArray<u8> = new StaticArray<u8>(NUM_FIELDS)
  ovActive: StaticArray<bool> = new StaticArray<bool>(NUM_FIELDS)

  // Effective (override-aware) patch, recomputed once per process() call —
  // read by both the sample loop and note-on handling within that call.
  eff: Preset = makePreset(0)

  voices: Voice[]

  // Shared LFO (one per instrument, like real hardware — all voices hear
  // the same modulation).
  lfoPhase: f32 = 0
  lfoDelayElapsed: f32 = 0
  activeVoiceCount: i32 = 0
  wasIdle: bool = true

  // Chorus — two short modulated delay lines on the mixed output.
  chorusBufL: StaticArray<f32> = new StaticArray<f32>(4096)
  chorusBufR: StaticArray<f32> = new StaticArray<f32>(4096)
  chorusWrite: i32 = 0
  chorusLfoPhase: f32 = 0

  constructor(host: Clap.clap_host) {
    super(host)
    this.voices = new Array<Voice>(NUM_VOICES)
    for (let i = 0; i < NUM_VOICES; ++i) this.voices[i] = new Voice()
    for (let i = 0; i < NUM_FIELDS; ++i) { this.ovValue[i] = 0; this.ovActive[i] = false }
    this.loadPatch(0)
  }

  // Declares its natural range (0..PRESET_COUNT-1), same as any other param
  // — poketrack's ADD row is always a raw 0-255 byte and scales it into
  // whatever range the param declares (same as pd2wclap plugins), with a
  // direct byte->step mapping for CLAP_PARAM_IS_STEPPED/ENUM params
  // specifically so one ADD-row bump is always exactly one patch change.
  // See clap_unit.c's clap_byte_to_value/clap_value_to_byte on the host side.
  loadPatch(index: i32): void {
    if (index < 0) index = 0
    if (index >= PRESET_COUNT) index = PRESET_COUNT - 1
    this.patchIndex = index
    this.patch = makePreset(index)
    for (let i = 0; i < NUM_FIELDS; ++i) this.ovActive[i] = false
  }

  // Rebuilds `this.eff` from the current patch + any per-field overrides.
  refreshEffective(): void {
    let p = this.eff
    p.name = this.patch.name
    for (let i = 0; i < NUM_FIELDS; ++i) {
      let v: u8 = this.ovActive[i] ? this.ovValue[i] : getPresetField(this.patch, i)
      setPresetField(p, i, v)
    }
  }

  updateHpf(voice: Voice): void {
    let mode = this.eff.hpfFreq
    if (mode == 0) voice.hpf.setPeaking(106, 6, 0.7071068, this.sampleRate)
    else if (mode == 1) voice.hpf.setBypass()
    else if (mode == 2) voice.hpf.setHighpass(124, 0.7071068, this.sampleRate)
    else voice.hpf.setHighpass(220, 0.7071068, this.sampleRate)
  }

  // --- CLAP plugin surface ----------------------------------------------

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
    this.activeVoiceCount = 0
    this.lfoPhase = 0; this.lfoDelayElapsed = 0
    for (let i = 0; i < 4096; ++i) { this.chorusBufL[i] = 0; this.chorusBufR[i] = 0 }
  }

  notePortsCount(isInput: bool): u32 { return isInput ? 1 : 0 }
  notePortsGet(index: u32, isInput: bool, info: Clap.NotePortInfo): bool {
    if (index >= this.notePortsCount(isInput)) return false
    info.id = 0x22346
    info.supportedDialects = Clap.NOTE_DIALECT_CLAP
    info.preferredDialect = Clap.NOTE_DIALECT_CLAP
    info.name = "notes"
    return true
  }

  audioPortsCount(isInput: bool): u32 { return isInput ? 0 : 1 }
  audioPortsGet(index: u32, isInput: bool, info: Clap.AudioPortInfo): bool {
    if (index >= this.audioPortsCount(isInput)) return false
    info.id = 0x22346
    info.name = "main"
    info.channelCount = 2
    info.portType = "stereo"
    return true
  }

  paramsCount(): u32 { return NUM_PARAMS }

  paramsGetInfo(index: u32, info: Clap.ParamInfo): bool {
    if (index == 0) {
      info.id = P_PATCH
      info.name = "Patch"
      info.minValue = 0
      info.maxValue = f64(PRESET_COUNT - 1)
      info.defaultValue = 0
      // ENUM: paramsValueToText names every value (the preset name) instead
      // of just formatting a number — lets a host skip drawing a slider for
      // this param and show the name alone, the way it already can for a
      // plain enum. Requires non-blank text for every value in range, which
      // makePreset(idx).name always is.
      info.flags = Clap.PARAM_IS_AUTOMATABLE | Clap.PARAM_IS_STEPPED | Clap.PARAM_IS_ENUM
      return true
    }
    let i = i32(index) - 1
    if (i < 0 || i >= NUM_FIELDS) return false
    info.id = FIELD_ID_BASE + u32(i)
    info.name = FIELD_NAMES[i]
    info.minValue = 0
    info.maxValue = f64(FIELD_MAX[i])
    info.defaultValue = f64(getPresetField(this.defaultPatch, i))
    info.flags = Clap.PARAM_IS_AUTOMATABLE | Clap.PARAM_IS_STEPPED
    // Same ENUM signal as Patch, for the fields whose paramsValueToText
    // names every value (Pulse/Sawtooth/Sub) rather than just formatting a
    // number — see FIELDS/paramsValueToText for which index is which.
    if (i == 19 || i == 23 || i == 27) info.flags |= Clap.PARAM_IS_ENUM
    return true
  }

  paramsGetValue(id: Clap.clap_id, value: CNumPtr<f64>): bool {
    if (id == P_PATCH) { value[0] = f64(this.patchIndex); return true }
    let i = i32(id) - i32(FIELD_ID_BASE)
    if (i < 0 || i >= NUM_FIELDS) return false
    value[0] = f64(this.ovActive[i] ? this.ovValue[i] : getPresetField(this.patch, i))
    return true
  }

  paramsValueToText(id: Clap.clap_id, value: f64): string | null {
    if (id == P_PATCH) {
      let idx = i32(Math.round(value))
      if (idx < 0) idx = 0
      if (idx >= PRESET_COUNT) idx = PRESET_COUNT - 1
      return makePreset(idx).name
    }
    let i = i32(id) - i32(FIELD_ID_BASE)
    if (i < 0 || i >= NUM_FIELDS) return null
    if (i == 27) { // pulse
      let w = i32(Math.round(value))
      if (w == 0) return "Off"
      if (w == 1) return "Square"
      if (w == 2) return "Pulse 75%"
      return "PWM"
    }
    if (i == 23 || i == 19) { // sawtooth, sub
      let w = i32(Math.round(value))
      return w == 0 ? "Off" : `Type ${w}`
    }
    return `${i32(Math.round(value))}`
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
      let id = valueEvent._param_id
      let v = f32(valueEvent._value)
      if (!(v > -1e9) || !(v < 1e9)) v = 0 // NaN/out-of-range guard — casts below trap on NaN
      if (id == P_PATCH) {
        this.loadPatch(i32(Math.round(v)))
      } else {
        let i = i32(id) - i32(FIELD_ID_BASE)
        if (i < 0 || i >= NUM_FIELDS) return false
        let rounded = Mathf.round(v)
        if (rounded < 0) rounded = 0
        if (rounded > f32(FIELD_MAX[i])) rounded = f32(FIELD_MAX[i])
        this.ovValue[i] = u8(rounded)
        this.ovActive[i] = true
      }
      this.refreshEffective()
      if (this.hostState) this.hostStateMarkDirty()
      return true
    } else if (event._type == Clap.EVENT_NOTE_ON) {
      let noteEvent = changetype<Clap.clap_event_note>(event)
      let victim: Voice = this.voices[0]
      let oldestAge = -1
      for (let n = 0; n < NUM_VOICES; ++n) {
        let voice = this.voices[n]
        if (!voice.active) { victim = voice; oldestAge = i32.MAX_VALUE; break }
        if (voice.age > oldestAge) { oldestAge = voice.age; victim = voice }
      }
      // CLAP velocity is nominally 0.0-1.0, but poketrack's own note data is
      // a raw 0-255 byte (fresh pattern notes default to 0xFF) and the host
      // bridge normalizes by /127 rather than /255 — so values noticeably
      // above 1.0 (and NaN, defensively) are real inputs here, not just a
      // theoretical edge case. Clamping is required: velocity later indexes
      // fixed-size tables, and AssemblyScript's bounds checks trap on an
      // out-of-range index.
      let velocity = f32(noteEvent._velocity)
      if (!(velocity > 0)) velocity = 1 // catches <=0 and NaN
      else if (velocity > 1) velocity = 1
      if (!victim.active) this.activeVoiceCount += 1
      victim.start(noteEvent._key, noteEvent._note_id, velocity, this.eff)
      this.updateHpf(victim)
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

  // --- DCO ----------------------------------------------------------------

  computeDco(voice: Voice, noteHz: f32, eff: Preset): f32 {
    let sr = this.sampleRate
    let pulseType = i32(eff.pulse)
    let sawType = i32(eff.sawtooth)
    let subType = i32(eff.sub)
    let subLevlRaw = i32(eff.subLevl)
    let noisLvlRaw = i32(eff.noisLvl)
    let pwPwm = f32(eff.pwPwm)
    let pwmRate = i32(eff.pwmRate)

    // Shared PWM LFO (bipolar around 0 when a rate is set, else a static offset).
    let lfoPw: f32
    if (pwmRate != 0) {
      let pwmHz: f32 = T.PWM_RATE[pwmRate]
      voice.pwmLfoPhase = wrapPhase(voice.pwmLfoPhase + pwmHz / sr)
      lfoPw = Mathf.sin(2.0 * f32(Math.PI) * voice.pwmLfoPhase) * (pwPwm / 2.0)
    } else {
      lfoPw = pwPwm
    }

    let osc1: f32 = 0
    if (pulseType != 0) {
      let dt: f32 = noteHz / sr
      let duty: f32 = 0.5
      if (pulseType == 2) duty = 0.75
      else if (pulseType == 3) duty = 0.5 - lfoPw * 0.49 / 127.0
      let v = blepPulse(voice.pulsePhase, dt, duty)
      voice.pulsePhase = wrapPhase(voice.pulsePhase + dt)
      osc1 = -v
    }

    let osc2: f32 = 0
    if (sawType != 0) {
      let dt: f32 = noteHz / sr
      let saw1 = blepSaw(voice.sawPhase, dt)
      voice.sawPhase = wrapPhase(voice.sawPhase + dt)
      if (sawType == 1) {
        osc2 = -saw1
      } else if (sawType == 2) {
        let dt2: f32 = 2.0 * noteHz / sr
        let sq2 = blepPulse(voice.sawPhase2x, dt2, 0.5)
        voice.sawPhase2x = wrapPhase(voice.sawPhase2x + dt2)
        osc2 = (-saw1 + 1.0) * (-sq2 + 1.0) / 4.0
      } else if (sawType == 3) {
        let dt2: f32 = 2.0 * noteHz / sr
        let duty: f32 = 0.5 - lfoPw * 0.49 / 127.0
        let sqpwm = blepPulse(voice.sawPhase2x, dt2, duty)
        voice.sawPhase2x = wrapPhase(voice.sawPhase2x + dt2)
        osc2 = (-saw1 + 1.0) * (-sqpwm + 1.0) / 2.0
      } else if (sawType == 4) {
        let dt8: f32 = 8.0 * noteHz / sr
        let gate8 = blepPulse(voice.sawPhase8x, dt8, 0.5)
        voice.sawPhase8x = wrapPhase(voice.sawPhase8x + dt8)
        osc2 = (-saw1 + 1.0) * (-gate8 + 1.0)
      } else {
        let dt2: f32 = 2.0 * noteHz / sr
        let dt8: f32 = 8.0 * noteHz / sr
        let sq2 = blepPulse(voice.sawPhase2x, dt2, 0.5)
        let sq8 = blepPulse(voice.sawPhase8x, dt8, 0.5)
        voice.sawPhase2x = wrapPhase(voice.sawPhase2x + dt2)
        voice.sawPhase8x = wrapPhase(voice.sawPhase8x + dt8)
        let gate1 = (-sq2 + 1.0) / 2.0
        let gate2 = (sq8 + 1.0) / 2.0
        osc2 = (-saw1 + 1.0) * gate1 * gate2
      }
    }

    let subLevel: f32 = subLevlRaw == 0 ? 0.0 : Mathf.pow(2.0, f32(subLevlRaw)) / 8.0
    let osc3: f32 = 0
    if (subLevel > 0) {
      if (subType == 0) {
        let dt: f32 = 0.5 * noteHz / sr
        let v = blepPulse(voice.subPhaseHalf, dt, 0.5)
        voice.subPhaseHalf = wrapPhase(voice.subPhaseHalf + dt)
        osc3 = -v * subLevel
      } else if (subType == 1) {
        let dt: f32 = 0.5 * noteHz / sr
        let v = blepPulse(voice.subPhaseHalf, dt, 0.75)
        voice.subPhaseHalf = wrapPhase(voice.subPhaseHalf + dt)
        osc3 = -v * subLevel
      } else if (subType == 2) {
        let dtH: f32 = 0.5 * noteHz / sr
        let dt2: f32 = 2.0 * noteHz / sr
        let vh = blepPulse(voice.subPhaseHalf, dtH, 0.5)
        let v2 = blepPulse(voice.subPhase2x, dt2, 0.5)
        voice.subPhaseHalf = wrapPhase(voice.subPhaseHalf + dtH)
        voice.subPhase2x = wrapPhase(voice.subPhase2x + dt2)
        osc3 = ((-v2 + 1.0) * (-vh + 1.0) / 2.0) * subLevel
      } else if (subType == 3) {
        let dtH: f32 = 0.5 * noteHz / sr
        let dt4: f32 = 4.0 * noteHz / sr
        let vh = blepPulse(voice.subPhaseHalf, dtH, 0.5)
        let v4 = blepPulse(voice.subPhase4x, dt4, 0.5)
        voice.subPhaseHalf = wrapPhase(voice.subPhaseHalf + dtH)
        voice.subPhase4x = wrapPhase(voice.subPhase4x + dt4)
        osc3 = ((-v4 + 1.0) * (-vh + 1.0) / 2.0) * subLevel
      } else if (subType == 4) {
        let dt: f32 = 0.25 * noteHz / sr
        let v = blepPulse(voice.subPhaseQuarter, dt, 0.5)
        voice.subPhaseQuarter = wrapPhase(voice.subPhaseQuarter + dt)
        osc3 = -v * subLevel
      } else {
        let dt: f32 = 0.25 * noteHz / sr
        let v = blepPulse(voice.subPhaseQuarter, dt, 0.75)
        voice.subPhaseQuarter = wrapPhase(voice.subPhaseQuarter + dt)
        osc3 = -v * subLevel
      }
    }

    let noise: f32 = noisLvlRaw == 0 ? 0.0 : voice.nextNoise() * (f32(noisLvlRaw) / 6.0)

    return osc1 * 0.2 + osc2 * 0.2 + osc3 * 0.2 + noise * 0.2
  }

  // --- process --------------------------------------------------------

  pluginProcess(process: Clap.Process): i32 {
    let length = process.framesCount
    let audioOut = process.audioOutputs[0]
    let left = audioOut.data32[0]
    let right = audioOut.data32[1]

    let eventCount = process.inEvents.size()
    let eventIndex: u32 = 0
    let sampleIndex: u32 = 0
    let sr = this.sampleRate
    let dt: f32 = 1.0 / sr

    this.refreshEffective()
    let eff = this.eff

    let dcoLfoDepth = T.LFO_VALS[eff.dcoLfo]
    let vcfLfoDepth = f32(eff.vcfLfo)
    let lfoRateHz = T.LFO_RATE[eff.lfoRate]
    let lfoDelaySec = T.LFO_DELS[eff.lfoDely]
    let vcfKybd = eff.vcfKybd
    let dcoRngMul: f32 = 8.0 / Mathf.pow(2.0, f32(eff.dcoRng) + 2.0)
    let dcoEnvMode = eff.dcoEnv
    let dcoEnvd = eff.dcoEnvd
    let vcfEnvMode = eff.vcfEnv
    let vcfEnvDepth = f32(eff.vcfEnvd)
    let cutoffKnob = f32(eff.vcfFreq)
    let resoKnob = Mathf.max(f32(eff.vcfReso), 1.0)
    let vcaLevl = f32(eff.vcaLevl) / 127.0
    let vcaEnvMode = eff.vcaEnv
    let chorusWet: f32 = eff.chorus != 0 ? 0.5 : 0.0
    let chorusRateHz = T.CRS_RATE[eff.crsRate]

    while (true) {
      let blockEnd = length
      if (eventIndex < eventCount) {
        let peek = process.inEvents[eventIndex]
        if (peek._time < length) blockEnd = peek._time
      }

      for (let i = sampleIndex; i < blockEnd; ++i) {
        // Shared LFO, with delay that restarts when all voices release.
        if (this.activeVoiceCount == 0) {
          if (!this.wasIdle) { this.lfoDelayElapsed = 0; this.wasIdle = true }
        } else {
          this.wasIdle = false
        }
        let lfoAmp: f32 = lfoDelaySec <= 0.0001 ? 1.0 : Mathf.min(this.lfoDelayElapsed / lfoDelaySec, 1.0)
        this.lfoDelayElapsed += dt
        this.lfoPhase = wrapPhase(this.lfoPhase + lfoRateHz * dt)
        let lfo: f32 = Mathf.sin(2.0 * f32(Math.PI) * this.lfoPhase) * lfoAmp

        let mix: f32 = 0
        for (let n = 0; n < NUM_VOICES; ++n) {
          let voice = this.voices[n]
          if (!voice.active) continue

          let envRaw = voice.env.tick(dt)
          let gateRaw = voice.gate.tick(dt)

          // --- DCO pitch ---
          let baseHz: f32 = 440.0 * Mathf.pow(2.0, f32(voice.key - 69) / 12.0)
          let noteHz: f32 = baseHz * dcoRngMul
          noteHz = noteHz + lfo * (noteHz * dcoLfoDepth / 2.0)
          let rowIdx = i32(Math.round(envRaw))
          if (rowIdx < 0) rowIdx = 0
          if (rowIdx > 127) rowIdx = 127
          let dcoEnvVal = T.DCO_ENV[128 * rowIdx + dcoEnvd]
          if (dcoEnvMode == 0) noteHz += (noteHz / 130.9) * dcoEnvVal
          else if (dcoEnvMode == 1) noteHz -= (noteHz / (130.9 * 8.0)) * dcoEnvVal
          else if (dcoEnvMode == 2) noteHz += (noteHz / 130.9) * dcoEnvVal * voice.velocity
          else noteHz -= (noteHz / (130.9 * 8.0)) * dcoEnvVal * voice.velocity
          if (noteHz < 1.0) noteHz = 1.0
          if (noteHz > sr * 0.45) noteHz = sr * 0.45

          let dcoOut = this.computeDco(voice, noteHz, eff)
          let hpfOut = voice.hpf.process(dcoOut)

          // --- VCF --- (kx1 mirrors June21's 4 kVcfEnv modes verbatim)
          let kx1: f32
          if (vcfEnvMode == 0) {
            kx1 = (cutoffKnob + (lfo * vcfLfoDepth / 4.0 + envRaw * vcfEnvDepth / 127.0)) / 12.0 - 3.2
          } else if (vcfEnvMode == 1) {
            kx1 = (cutoffKnob - (lfo * vcfLfoDepth / 4.0 + envRaw * vcfEnvDepth / 127.0)) / 12.0 - 3.2
          } else if (vcfEnvMode == 2) {
            kx1 = (cutoffKnob + (lfo * vcfLfoDepth / 4.0 + voice.velocity * envRaw * vcfEnvDepth / 127.0)) / 12.0 - 3.2
          } else {
            kx1 = (cutoffKnob + (lfo * vcfLfoDepth / 4.0 + (voice.velocity * 127.0 / 220.0) * vcfEnvDepth)) / 12.0 - 3.2
          }
          let noteFreqForKybd = baseHz
          let kx2: f32
          if (noteFreqForKybd > 261.63) {
            kx2 = (T.VCF_ENV_D_UPPER[vcfKybd] * (noteFreqForKybd - 261.63) + 261.63) / 261.63
          } else {
            kx2 = (T.VCF_ENV_D_LOWER[vcfKybd] * (noteFreqForKybd - 261.63) + 261.63) / 261.63
          }
          let cutoffHz: f32 = 100.0 * Mathf.pow(2.0, kx1) * kx2
          if (cutoffHz < 20.0) cutoffHz = 20.0
          if (cutoffHz > 10000.0) cutoffHz = 10000.0
          if (cutoffHz > sr * 0.45) cutoffHz = sr * 0.45

          let coef: f32 = 1.0 - Mathf.exp(-2.0 * f32(Math.PI) * cutoffHz / sr)
          let ladderOut = voice.vcf.process(hpfOut, coef)

          let bandFc: f32 = 1.25 * cutoffHz
          if (bandFc > sr * 0.45) bandFc = sr * 0.45
          if (bandFc < 20.0) bandFc = 20.0
          let bandwidthHz: f32 = (cutoffHz * 8.0) / resoKnob
          let q: f32 = bandFc / Mathf.max(bandwidthHz, 1.0 as f32)
          if (q < 0.05) q = 0.05
          if (q > 20.0) q = 20.0
          // Csound's `reson` is called with iscl=2: constant 0dB peak gain
          // no matter the bandwidth/Q, unlike a plain SVF bandpass (whose
          // peak gain falls off at low Q) — use the matching RBJ formula.
          voice.vcfBand.setBandpassConstantPeak(bandFc, q, sr)
          let vcfOut = voice.vcfBand.process(ladderOut)

          // --- VCA ---
          let vcaGain: f32
          if (vcaEnvMode == 0) {
            vcaGain = envRaw / 127.0
          } else if (vcaEnvMode == 1) {
            vcaGain = gateRaw
          } else if (vcaEnvMode == 2) {
            vcaGain = (envRaw / 127.0) * T.DYN_VCA_RES[i32(voice.velocity * 127.0)]
          } else {
            vcaGain = gateRaw * T.DYN_VCA_RES[i32(voice.velocity * 127.0)]
          }

          mix += vcfOut * vcaGain * vcaLevl

          voice.age += 1
          if (voice.env.finished && voice.env.value < 0.05) {
            voice.active = false
            this.activeVoiceCount -= 1
          }
        }

        // --- Chorus (post-mix, shared) ---
        this.chorusLfoPhase = wrapPhase(this.chorusLfoPhase + chorusRateHz * dt)
        let choMod: f32 = (Mathf.sin(2.0 * f32(Math.PI) * this.chorusLfoPhase) + 1.0) * 0.5
        let choModR: f32 = (Mathf.sin(2.0 * f32(Math.PI) * wrapPhase(this.chorusLfoPhase + 0.25)) + 1.0) * 0.5
        let delayMsL: f32 = 2.0 + choMod * 8.0
        let delayMsR: f32 = 2.0 + choModR * 8.0
        let delaySamplesL: f32 = delayMsL * 0.001 * sr
        let delaySamplesR: f32 = delayMsR * 0.001 * sr

        this.chorusBufL[this.chorusWrite] = mix
        this.chorusBufR[this.chorusWrite] = mix

        let readL: f32 = f32(this.chorusWrite) - delaySamplesL
        while (readL < 0) readL += 4096
        let readR: f32 = f32(this.chorusWrite) - delaySamplesR
        while (readR < 0) readR += 4096
        let i0L = i32(readL) % 4096, i1L = (i0L + 1) % 4096
        let fracL = readL - Mathf.floor(readL)
        let i0R = i32(readR) % 4096, i1R = (i0R + 1) % 4096
        let fracR = readR - Mathf.floor(readR)
        let wetL = this.chorusBufL[i0L] * (1.0 - fracL) + this.chorusBufL[i1L] * fracL
        let wetR = this.chorusBufR[i0R] * (1.0 - fracR) + this.chorusBufR[i1R] * fracR

        this.chorusWrite = (this.chorusWrite + 1) % 4096

        let outL: f32 = mix * (1.0 - chorusWet) + wetL * chorusWet
        let outR: f32 = mix * (1.0 - chorusWet) + wetR * chorusWet

        left[i] = outL
        right[i] = outR
      }

      sampleIndex = blockEnd
      if (eventIndex >= eventCount) break
      let event = process.inEvents[eventIndex++]
      if (!this.handleEvent(event)) process.outEvents.tryPush(event)
    }

    return Clap.PROCESS_CONTINUE
  }
}

let pluginSpec = Clap.registerPlugin<Plugin>("Juno-1 (AssemblyScript)", "com.poketrack.clap.juno1")
pluginSpec.vendor = "poketrack"
pluginSpec.features = ["instrument"]
