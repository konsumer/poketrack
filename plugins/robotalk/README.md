# Robotalk

A playable text-to-speech instrument, written in Rust as a WCLAP instrument
using [clack](https://github.com/prokopyl/clack) — 37 phonemes covering
enough of English (vowels, diphthongs, liquids, nasals, fricatives, and
stops) to speak recognizable words, not just vowels.

If you want to generate a poketrack pattern that says text, visit the [phoneme pattern exporter](https://konsumer.js.org/poketrack/tts/)


**Each note selects a phoneme, not a pitch.** Notes 36-72 (C2-C5) step
through 37 ARPABET-style phonemes, in five contiguous blocks, wrapping
every 37 notes so transposing never goes silent:

| Notes | Category | Phonemes |
|-------|----------|----------|
| 36-50 | Vowels + diphthongs | IY IH EH AE AA AH ER AO UH UW EY AY AW OY OW |
| 51-54 | Liquids/glides | L R W Y |
| 55-57 | Nasals | M N NG |
| 58-66 | Fricatives | S Z SH ZH F V TH DH HH |
| 67-72 | Stops | P T K B D G |

This exact table (symbols, order, base note) is shared with the `robotalk`
CLI below via `src/phonemes.rs` directly, so there's no separate
translation table to keep in sync.

## `robotalk` CLI

A companion binary, built from the same `Cargo.toml` as the plugin
(`cargo build --release` produces both), turns English text into ARPABET
phonemes or a full poketrack pattern:

```sh
cargo build --release --bin robotalk

# preview the phonemes for some text, without writing a file
./target/release/robotalk phonemes "hello world"

# write a .rptp pattern that speaks it on a ROBOTALK instrument
./target/release/robotalk pattern "hello world" --out hello.rptp --instrument 0

# or feed it an already-tokenized phoneme list directly
./target/release/robotalk pattern --phonemes "HH AH L OW" --out hello.rptp
```

Text-to-phoneme is via [misaki-rs](https://crates.io/crates/misaki-rs) with
`default-features = false` — its lexicon/tagger data is embedded into the
binary at compile time (no runtime data directory, no espeak/C-toolchain
dependency). Out-of-vocabulary words (including "robotalk" itself) get
spelled out letter by letter rather than phonetically guessed — this is
accepted as-is, not a bug; type a phonetic-ish spelling instead if a
specific word's pronunciation matters (e.g. "poke e track").

## How it makes each sound

One shared engine (a source pushed through 3 parallel resonant filters,
same idea as the earlier `formant` example) handles every category —
vowels/liquids/nasals differ only in which frequencies the filters target:

- **Vowels/diphthongs/liquids**: a voiced/noise excitation blend through 3
  resonators tuned to real Peterson & Barney (1952) formant frequencies.
  Diphthongs (and W/Y as glides) linearly interpolate the formant targets
  over the note's duration instead of staying static.
- **Nasals**: same engine, but the third resonator is *subtracted* instead
  of added — an antiformant notch, approximating the spectral zero a real
  nasal cavity side-branch causes (Fujimura, 1962).
- **Fricatives**: mostly-noise excitation through the same resonator bank,
  tuned to spectral peaks measured by Jongman, Wayland & Wong (2000);
  voiced members (Z, V, DH, ZH) mix in a low buzz.
- **Stops**: bypass the resonant engine entirely — a one-shot
  closure → burst → voicing-onset state machine (burst spectra from
  Stevens & Blumstein 1978, voice-onset timing from Lisker & Abramson
  1964), like a drum hit. Doesn't sustain regardless of how long the note
  is held.

Every note self-terminates on its own (DECAY), the same as poketrack's
native units — a spoken phoneme shouldn't need an explicit note-off to
stop ringing. An actual note-off just speeds up whatever's left into a
quick release.

None of this is ported from an existing TTS engine's code — DECtalk is
proprietary, and SAM (Software Automatic Mouth) traces to an unlicensed
1982 disassembly, so both were ruled out as sources to copy. The acoustic
*data* above (frequencies, timings) comes from published, freely-usable
phonetics papers instead; only the general architecture (source-filter
synthesis, per-phoneme duration tables) was informed by studying how SAM's
still worked.

## Params

| Param | Range | Notes |
|-------|-------|-------|
| Pitch | 0–1 | Voiced source fundamental, 70Hz–220Hz |
| Rate | 0–1 | Speaking speed — scales stop/decay timing (0.5x–2x) |
| Breath | 0–1 | Voiced/noise excitation blend for vowels — 0 = clean tone, 1 = whispery |
| Res | 0–1 | Formant resonance sharpness (Q) |
| Decay | 0–1 | How long sustained phonemes ring before self-terminating, 150ms–3s |
| Volume | 0–1 | Output level |

## Building the plugin

```sh
rustup target add wasm32-wasip1
cargo build --target wasm32-wasip1 --release --lib
```

`--lib` restricts the wasm build to the plugin's `cdylib` target — the
`robotalk` CLI binary (and its `misaki-rs`/`clap` dependencies) is a
native-only tool and never needs to cross-compile for wasm32-wasip1.

Two linker flags in `.cargo/config.toml` are required for the result to
load as a WCLAP plugin at all, beyond what `cargo build` does by default:

- `--export=malloc` — wasi-libc already defines `malloc` internally (Rust's
  allocator uses it), but doesn't export it; the native host
  (wclap-bridge) needs to call it directly to allocate guest memory across
  the host/guest boundary.
- `--export-table` and `--growable-table` — the host installs CLAP host
  callback functions into the guest's indirect function table; by default
  the table has no room to grow.

`make plugins` (from the repo root) runs all of this and copies the result
into `examples/plugins/`, alongside the other example plugins.

## Building the browser tool

The text -> phonemes -> pattern pipeline (g2p.rs + pattern.rs) is also
built as a wasm-bindgen module for `webroot/tts/`, a standalone page that
lets anyone build a `.rptp` pattern in the browser without installing the
CLI:

```sh
rustup target add wasm32-unknown-unknown
cargo install wasm-bindgen-cli --version 0.2.100 --locked
cargo build --target wasm32-unknown-unknown --release --lib
wasm-bindgen target/wasm32-unknown-unknown/release/robotalk.wasm \
  --out-dir ../../webroot/tts --out-name robotalk --target web --no-typescript
```

`make build-web` (from the repo root) runs all of this alongside the
Emscripten build. The `wasm-bindgen` crate version in `Cargo.toml` is
pinned exactly, since the CLI and crate versions must match.

Every consumer of this crate compiles a different subset via `#[cfg]` in
`lib.rs`: `plugin.rs` (+ `params.rs`/`voice.rs`, which only exist to serve
it) is wasm32-wasip1-only, since clack has no reason to link into the
CLI or browser build; `g2p.rs`/`pattern.rs`/`web.rs` are excluded from the
wasi build for the opposite reason -- the plugin has no use for
misaki-rs's ~30MB embedded English lexicon. That lexicon is also why the
generated `robotalk_bg.wasm` is large (~35MB, ~8MB gzipped) -- expected,
not a bug, and the same trade-off the native CLI already makes for a
runtime-data-free binary.
