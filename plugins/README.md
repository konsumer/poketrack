## plugins

You can also use [CLAP](https://github.com/free-audio/clap) plugins for sound-generation and effects — specifically [WCLAP](https://github.com/WebCLAP) (CLAP compiled to wasm32), which runs sandboxed on every target (desktop and web) from a single `.wasm` file, and can be bundled alongside a song using a relative path. Point the unit at a `.wasm` file; if it bundles more than one plugin, a picker lets you choose which one.

These plugins can be written in any language that can compile to wasm ([C](https://github.com/webassembly/wasi-sdk), [rust](https://rust-lang.org/), [assemblyscript](https://www.assemblyscript.org/), [nelua](https://nelua.io/), etc) and work on any client (web, native linux/mac/windows) without recompile.

There's no GUI support (CLAP plugin UIs aren't hosted), so you map plugin params to tracker-controllable slots via the ADD row instead — same workflow as any other unit.

Native `.clap` plugins (the traditional OS-loaded kind) aren't supported — only WCLAP `.wasm` files.

Here are soem examples hoste elsewhere:

- [as-clap](https://github.com/WebCLAP/as-clap) — WCLAP written in AssemblyScript (gain + synth example)
- [clack](https://github.com/prokopyl/clack) — Rust CLAP host/plugin library (gain + polysynth examples)
- [Signalsmith Basics](https://github.com/Signalsmith-Audio/basics) — MIT-licensed effects collection (chorus, crunch, freq-shifter, limiter, reverb, analyser)
- [signalsmith-clap-cpp](https://github.com/geraintluff/signalsmith-clap-cpp) — Signalsmith's C++ CLAP examples, built via WASI-SDK
- [WebCLAP/examples](https://github.com/WebCLAP/examples).

Keep in mind that the standard is not 100% solid, so you may find plugins that won't work. They need to be without threading, and use wasm32 (not wasm64.)

I made some complete examples:

- **[karplus](karplus)** — a Karplus-Strong plucked string, written against [as-clap](https://github.com/WebCLAP/as-clap) (AssemblyScript).
- **[robotalk](robotalk)** — a playable text-to-speech instrument (37 phonemes: vowels, diphthongs, liquids, nasals, fricatives, stops), written against [clack](https://github.com/prokopyl/clack) (Rust).

`make plugins` builds into `examples/plugins/`, alongside the other [example](https://github.com/konsumer/poketrack/tree/main/examples) songs/soundfonts/samples users get.
