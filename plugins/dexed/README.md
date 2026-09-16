# Dexed

[Dexed](https://github.com/asb2m10/dexed)'s Yamaha DX7 engine, as a WCLAP
instrument — the real `msfa` FM engine and Dexed's own voice/cartridge handling,
compiled to wasm with wasi-sdk, **not** a reimplementation:

```
plugins/dexed/vendor/dexed/   dexed's engine sources, unmodified
plugins/dexed/src/            the CLAP wrapper + a port of dexed's audio path
plugins/dexed/vendor/syx/     the 33 cartridges dexed ships (1056 voices)
```

1056 factory voices are compiled in, each reachable as an ordinary param, so a
poketrack instrument can play anything Dexed can — including everything the
M-VAVE FM-1 can, since that hardware is a MiniDEXED (Dexed on bare metal) and
speaks the same DX7 SysEx voices: pick the same program number, or bring the
same cartridge in as a bank (see **Presets**).

Like every other plugin here it's headless: there's no hosted GUI, so every
voice parameter is a CLAP param you map to ADD-row slots, and cartridges are
selected with params rather than a file browser.

## What's exact, and what isn't

Exact means: the DSP that makes the sound is dexed's own code, compiled from
`vendor/dexed` with no edits, and the class around it is a line-for-line port
of `DexedAudioProcessor`'s audio half. So it's the same six-operator engine,
the same three engine variants, the same 16-voice allocator, the same
envelopes/LFO/pitch-EG, the same int32 saturation and the same voice
parameters — not an approximation of them.

| Piece | Status |
|-------|--------|
| `msfa/*` (engine core: FmCore, Dx7Note, Env, Lfo, PitchEnv, Freqlut, Exp2, Sin, Porta, FmOpKernel) | vendored **verbatim** |
| `EngineMkI`, `EngineOpl` | vendored **verbatim** |
| Engine selection (`Modern (24-bit)` / `Mark I` / `OPL Series`, default Mark I) | ported 1:1 (`setEngineType`) |
| Voice allocation, note stealing, MPE-vs-channel note matching, mono mode | ported 1:1 (`chooseNote`/`keydown`/`keyup`) |
| Render loop: 64-sample quanta, remainder buffering, clip/scale maths | ported 1:1 (`processBlock`) |
| Live param changes (`refreshVoice` → re-init live voices + LFO reset) | ported 1:1 |
| Cartridge parsing + voice unpacking (`normparm` clamping included) | ported 1:1 (`Cartridge::load`/`unpackProgram`) |
| Output stage: DC filter, Output gain, 4-pole Obxd filter | ported 1:1 (`PluginFx`), minus its unused 2-pole variant |
| `msfa/tuning.cc` | **replaced**: standard tuning only (SCL/KBM needs a filesystem + file picker, neither of which a WCLAP plugin has). Its numbers are dexed's own, and standard tuning is the state Dexed runs in with no external tuning applied. |
| MTS-ESP (`libMTSClient.h`) | **replaced** by a shim reporting "no master" — the state Dexed runs in when nothing has registered as an MTS master. Both MTS code paths are gated on `MTS_HasMaster()`, so neither executes. |
| Editor, sysex comm, preferences, MIDI-CC map, `.syx` file loading, clipboard | not ported (no GUI, no filesystem, no MIDI in) |
| Sustain pedal, pitch bend, mod wheel, breath/foot/aftertouch, portamento | not ported: poketrack's CLAP unit sends note on/off and param values, nothing else, so there's no input to reach them. They're absent rather than present-and-dead. |

Two deliberate non-Dexed details, both commented at the call site:

- **CLAP velocity → the engine's 0-127 velocity** is rounded, where dexed's own
  JUCE build truncates. poketrack's note data is a 0-255 byte that its bridge
  divides by the MIDI ceiling of 127, so rounding is what makes a poketrack
  velocity byte come back out unchanged. At most one step of the DX7's own
  0-127 velocity either way.
- **`data[155]`** (the packed operator on/off byte) is kept equal to the
  operator switches after a program recall. Dexed leaves it stale there;
  nothing reads it, but a stale byte sitting next to live state is a trap.

Output is mono, like a DX7 (and like Dexed) — both channels get the same
signal.

## Params: a full 1:1 mapping

All 156 of Dexed's own parameters, in Dexed's own order, so param N here is
param N there: 24 globals (Cutoff, Resonance, Output, MonoMode, MASTER TUNE
ADJ, ALGORITHM, FEEDBACK, OSC KEY SYNC, the six LFO params, TRANSPOSE,
P MODE SENS., and the two 4-stage pitch EGs) followed by 6 operators × 22
(each operator's 4+4 EG rates/levels, OUTPUT LEVEL, MODE, F COARSE, F FINE,
OSC DETUNE, BREAK POINT, L/R SCALE DEPTH, L/R KEY SCALE, RATE SCALING,
A MOD SENS., KEY VELOCITY, and its on/off SWITCH) — plus three that Dexed only
reaches from its GUI:

| Extra param | Range | Notes |
|-------------|-------|-------|
| `Cartridge` | 0-32 | Which bundled bank, by name |
| `Program` | 0-31 | Voice within the bank, by name — the DX7's own 32-voice cartridge layout |
| `Engine` | 0-2 | `Modern (24-bit)` / `Mark I` / `OPL Series` — Dexed's three engines, same names as its engine menu |

That's 159 params total. poketrack's ADD row caps a *unit* at 16 mapped params;
that's a mapping-UI limit, not a limit on what a CLAP plugin may declare, so
you pick which ≤16 you actually want automatable per instrument and the rest
sit at whatever the selected `Program` put them at. Both preset params are
declared stepped+enum, so one ADD-row bump moves exactly one program (byte N =
program N, bytes past 31 hold there rather than wrapping), and the ADD row
shows the voice's name instead of a slider.

Recalling a program resets every voice parameter to that voice, exactly like
selecting it in Dexed's program list — including the operator switches, which
Dexed unpacks to all-on with every program. Touching a param afterwards
overrides just that one, for as long as it isn't overwritten by the next
recall. An edit made while a note is held is heard immediately (the live-voice
refresh described above), so parameter automation works mid-note.

### Making an FM-1 style patch

Everything the FM-1 exposes is here, because it's the same parameter set:
6 operators × (4-stage EG, output level, ratio/fixed mode, coarse/fine,
detune, key scaling, rate scaling, amp/velocity sensitivity, on/off),
32 algorithms, feedback, the LFO with all six waveforms — and per-voice
pitch EG. Pick an `Engine` to taste: `Mark I` is Dexed's default (and what a
MiniDEXED-based box sounds like), `Modern (24-bit)` is the msfa/Android engine,
`OPL Series` is the OPL-chip-flavoured variant.

## Presets

The 33 banks Dexed ships (`assets/builtin_pgm.zip` upstream) are extracted into
`vendor/syx/` and compiled straight into the plugin as raw 4104-byte DX7 bulk
voice dumps — `Dexed_01.syx` (Dexed's own startup bank) first, then
`SynprezFM_01` … `SynprezFM_32`, which is the classic DX7 ROM cartridge set.
33 × 32 = 1056 voices. A WCLAP plugin has no filesystem to read a `.syx` from
at runtime, so baking them into the wasm's data section is what makes presets
work at all.

`src/presets-data.{h,cpp}` is generated (and regenerated by `build.sh` whenever
a `vendor/syx` bank is newer):

```sh
node scripts/syx2presets.mjs --out src/presets-data vendor/syx/*.syx
```

It validates each file as a real DX7 32-voice bulk dump (exact size, header,
`0xF7`, and the SysEx checksum) and fails loudly rather than normalizing
something off-shape — Dexed's own loader tolerates a malformed stream by
copying its first 4096 bytes, which is right for a live MIDI stream but wrong
for a build-time table. Point it at any DX7 cartridge — your own patches, a
different ROM set, the FM-1's bundled banks — and the plugin will offer them as
a `Cartridge`.

## Building

```sh
plugins/dexed/build.sh            # -> plugins/dexed/build/dexed.wasm
make plugins                      # from the repo root: -> examples/plugins/dexed.wclap.wasm
```

Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (set `WASI_SDK_PATH`,
default `/opt/wasi-sdk`) and node (to regenerate the preset table). CLAP
headers come from poketrack's own CMake fetch at `build/_deps/clap-src`,
falling back to a cached `vendor/clap` clone.

The build exports its own memory rather than importing a shared one, like the
AssemblyScript plugins do — importing memory would force it to be *shared*,
which in a browser needs a cross-origin-isolated page (see
[plugins/README.md](../README.md)). The module's only WASI imports are
`fd_write`/`fd_close`/`fd_seek`, all of which poketrack's web host shims, so
the same `.wasm` loads on desktop and web. It spawns no threads.

## Vendoring dexed

`vendor/dexed/` holds dexed at commit `2e182b3db85c09083ab13c8b9b00565ce7d9ff85`
(`Source/msfa/*`, `Source/Dexed.h`, `Source/EngineMkI.*`, `Source/EngineOpl.*`,
plus dexed's GPL-3.0 `LICENSE`), copied as-is. To refresh it:

```sh
git clone https://github.com/asb2m10/dexed /tmp/dexed
cp /tmp/dexed/Source/msfa/*.{h,cc} plugins/dexed/vendor/dexed/msfa/
cp /tmp/dexed/Source/msfa/porta.cpp plugins/dexed/vendor/dexed/msfa/
rm plugins/dexed/vendor/dexed/msfa/tuning.cc   # replaced by compat/tuning.cc
cp /tmp/dexed/Source/{Dexed.h,EngineMkI.*,EngineOpl.*,LICENSE} plugins/dexed/vendor/dexed/
```

`vendor/dexed/compat/` is ours, not dexed's: `tuning.cc` (standard tuning),
`Tunings.h` (the tuning-library type `msfa/tuning.h` names) and
`libMTSClient.h` (the MTS-ESP shim). Nothing else in `vendor/dexed/` is
modified, which is the point — the DSP stays checkable against upstream.

## Licensing

The rest of poketrack is zlib-licensed (see the repo root `LICENSE`). This
plugin is not:

- `vendor/dexed/msfa/*` is Apache-2.0 (Google's Music Synthesizer for Android,
  as shipped in dexed). Those files carry their own headers.
- The rest of `vendor/dexed/` (`EngineMkI`, `EngineOpl`, `Dexed.h`) and the
  bundled cartridges are **GPL-3.0-or-later** — dexed's `LICENSE` is included
  in `vendor/dexed/`, and `vendor/syx/` is Dexed's own `assets/builtin_pgm.zip`
  (the SynprezFM banks are the DX7 ROM cartridges as Dexed redistributes them).

`src/`, `scripts/`, `build.sh` and this README are original work, not
GPL-encumbered on their own — but because the GPL-3.0 engine and preset data
above are compiled into it, **`dexed.wclap.wasm` should be treated as
GPL-3.0-or-later for distribution**, the same as the juno1 plugin.
