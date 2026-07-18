## plugins

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
