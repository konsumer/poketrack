This is a cross-platform joystick-driven music-tracker.

<img align="right" width="150" src="https://github.com/konsumer/poketrack/blob/main/art/square.png?raw=true" />

## installation

Grab [the latest release](https://github.com/konsumer/poketrack/releases/latest) for your platform.

You can find device-specific instructions in [DEVICES](./DEVICES.md).

## usage

You should be able to track quickly with a joystick, or keys:

![keys](./keys.png)

It may seem a bit inscrutable at first, but input is meant to be consistent and fast with a joystick, so once you get the hang of it, it should work well. A is "edit/change value", B is "delete/reset/cancel", X is "fill column", Y is "clear column", SELECT + arrow is "change screen", and START is "play song/pattern".


**Any screen**

| Input | Purpose |
|---|---|
| SELECT + ←/↑/↓/→ | Switch to Song / Pattern / Instrument / Menu |
| START | Play/stop song |

**Song**

| Input | Purpose |
|---|---|
| ↑/↓ | Move cursor row |
| ←/→ | Scroll channels |
| A + ↑/↓ | Set pattern number in cell |
| B | Clear cell |
| SELECT + START | Play from current row |

**Pattern**

| Input | Purpose |
|---|---|
| L / R | Previous / next pattern |
| ↑/↓ | Move cursor row |
| ←/→ | Move cursor column (note → vel → inst → fx…) |
| X / Y | Fill / clear entire column |
| START | Loop current pattern (not full song) |
| B | Clear cell |
| A + ↑/↓ | Set node/param number in cell |
| A + B | OFF or instrument-stop |

**Instrument**

| Input | Purpose |
|---|---|
| L / R | Previous / next instrument |
| ↑/↓ | Navigate slots / params |
| → / ← | Enter / leave param panel |
| A + ↑/↓ | Cycle unit type (slot) · Change value ±1 (param) |
| A + ←/→ | Change value ±16 (param) |
| A + B | Toggle slot enabled/disabled |
| A | Open file picker (FILE row) |
| B | Clear slot / reset param / clear file path |

**Menu**

| Input | Purpose |
|---|---|
| ↑/↓ | Navigate items |
| A + ↑/↓ | Change value ±1 (BPM / KEY / SCALE) |
| A + ←/→ | Change BPM ±10 |
| A | Confirm action |


### units

There are some built-in units (effects/sound-generators) that are documented [here](./src//units/README.md).

### themes

The UI is themable at launch: `poketrack --theme ~/cyber.ptt`. See [THEMES](./THEMES.md) for the file format and how to convert LGPT themes.

### CLI flags

| Flag | Purpose |
|---|---|
| `-f`, `--fullscreen` | Start in fullscreen |
| `--theme <file.ptt>` | Load a theme at launch |
| `--no-preview` | Disable the note preview that normally fires as the cursor moves over pattern cells — useful when editing sequences live (e.g. against an external clock/sequencer) where you don't want every cursor move to also trigger a note |


### plugins

You can also use [CLAP](https://github.com/free-audio/clap) plugins for sound-generation and effects — specifically [WCLAP](https://github.com/WebCLAP) (CLAP compiled to wasm32), which runs sandboxed on every target (desktop and web) from a single `.wasm` file, and can be bundled alongside a song using a relative path. Point the unit at a `.wasm` file; if it bundles more than one plugin, a picker lets you choose which one.

These plugins can be written in any language that can compile to wasm (C, rust, assemblyscript, nelua, etc) and work on any client (web, native linux/mac/windows.)

There's no GUI support (CLAP plugin UIs aren't hosted), so you map plugin params to tracker-controllable slots via the ADD row instead — same workflow as any other unit.

Native `.clap` plugins (the traditional OS-loaded kind) aren't supported — only WCLAP `.wasm` files.

`test_clap_plugin/` has a few real WCLAP plugins used for testing:
- [as-clap](https://github.com/WebCLAP/as-clap) — WCLAP written in AssemblyScript (gain + synth example)
- [clack](https://github.com/prokopyl/clack) — Rust CLAP host/plugin library (gain + polysynth examples)
- [Signalsmith Basics](https://github.com/Signalsmith-Audio/basics) — MIT-licensed effects collection (chorus, crunch, freq-shifter, limiter, reverb, analyser)
- [signalsmith-clap-cpp](https://github.com/geraintluff/signalsmith-clap-cpp) — Signalsmith's C++ CLAP examples, built via WASI-SDK

All fetched/packaged as WCLAP by [WebCLAP/examples](https://github.com/WebCLAP/examples).

`test_clap_plugin/karplus/` and `test_clap_plugin/robotalk/` are from-scratch examples built for this project — full buildable projects (see their own READMEs), templates for writing new WCLAP instruments/effects that go beyond what the built-in units can do:
- **karplus** — a Karplus-Strong plucked string, written against [as-clap](https://github.com/WebCLAP/as-clap) (AssemblyScript).
- **robotalk** — a playable text-to-speech instrument (37 phonemes: vowels, diphthongs, liquids, nasals, fricatives, stops), written against [clack](https://github.com/prokopyl/clack) (Rust). Pairs with [`text2phonemes.py`](./text2phonemes.py) + [`phonemes2rptp.py`](./phonemes2rptp.py) at the repo root — text to phonemes and phonemes to a poketrack pattern are deliberately separate scripts, independent of the instrument itself.

`make plugins` builds both into `examples/plugins/`, alongside the other [example](https://github.com/konsumer/poketrack/tree/main/examples) songs/soundfonts/samples users get.

## samples & key-mapping

Samples & instruments are intentionally simple. The idea is that you can be more "in the moment" and not uncomfortably tweaking samples & key/velocity maps with a small screen & limited controls. You can do your setup offline, and save it as a SFZ (or zip of SFZ.) I also made [this editor](https://konsumer.js.org/sfzmaker/) to facilitate that. It can do cool stuff with breakbeats, lots of samples, different velocity-zones, etc.


### what about my handheld?

Read about [specific devices](DEVICES.md).


### videos

Here are some videos:

[![built-in synths](https://img.youtube.com/vi/3JeYaVriygU/0.jpg)](https://www.youtube.com/playlist?list=PLDE2Ywpu1J__p2yBXrMOoKCgtIYQgfGo7)

## development

I use `make` to record common tasks (and `cmake` to actually build) so you can run `make` to get documentation.

## todo

- "bundle" a save for distribution, that creates a zip with song + all referenced files
- web: some WCLAP plugins (e.g. `signalsmith-clap-cpp.wasm`) declare shared/thread-capable memory and hang on load — needs real Worker-based multi-agent threading in the web CLAP host, not just single-threaded WASI. Native (Wasmtime) already handles this fine.



