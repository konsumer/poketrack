## plugins

You can also use [CLAP](https://github.com/free-audio/clap) plugins for sound-generation and effects — specifically [WCLAP](https://github.com/WebCLAP) (CLAP compiled to wasm32), which runs sandboxed on every target (desktop and web) from a single `.wasm` file, and can be bundled alongside a song using a relative path. Point the unit at a `.wasm` file; if it bundles more than one plugin, a picker lets you choose which one.

These plugins can be written in any language that can compile to wasm ([C](https://github.com/webassembly/wasi-sdk), [rust](https://rust-lang.org/), [assemblyscript](https://www.assemblyscript.org/), [nelua](https://nelua.io/), etc) and work on any client (web, native linux/mac/windows) without recompile.

There's no GUI support (CLAP plugin UIs aren't hosted), so you map plugin params to tracker-controllable slots via the ADD row instead — same workflow as any other unit.

Native `.clap` plugins (the traditional OS-loaded kind) aren't supported — only WCLAP `.wasm` files.

Here are some examples hosted elsewhere:

- [as-clap](https://github.com/WebCLAP/as-clap) — WCLAP written in AssemblyScript (gain + synth example)
- [clack](https://github.com/prokopyl/clack) — Rust CLAP host/plugin library (gain + polysynth examples)
- [Signalsmith Basics](https://github.com/Signalsmith-Audio/basics) — MIT-licensed effects collection (chorus, crunch, freq-shifter, limiter, reverb, analyser)
- [signalsmith-clap-cpp](https://github.com/geraintluff/signalsmith-clap-cpp) — Signalsmith's C++ CLAP examples, built via WASI-SDK
- [WebCLAP/examples](https://github.com/WebCLAP/examples).

Keep in mind that the standard is not 100% solid, so you may find plugins that won't work. They need to use wasm32 (not wasm64.)

Plugins built with a `-pthread` wasi-sdk toolchain (declaring shared/thread-capable memory, e.g. [signalsmith-clap-cpp](https://github.com/geraintluff/signalsmith-clap-cpp)) load fine on native; on web they're backed by real Web Workers (one per spawned thread), which requires the page to be [cross-origin isolated](https://developer.mozilla.org/en-US/docs/Web/API/crossOriginIsolated) (`SharedArrayBuffer` needs it). poketrack's own web build works around GitHub Pages not letting you set the `Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy` headers this needs, via a service worker (`webroot/coi-serviceworker.js`) that injects them itself — the first page load registers it and reloads once. If you're self-hosting the web build behind a server that already sets those headers, the service worker just no-ops. Without cross-origin isolation (e.g. it's blocked, or third-party cookies/service workers are disabled), a plugin that actually spawns threads fails to load with a console error instead of hanging; plugins that only *link against* a `-pthread` toolchain without ever spawning a thread work regardless.

I made some complete examples:

- **[karplus](karplus)** — a Karplus-Strong plucked string, written against [as-clap](https://github.com/WebCLAP/as-clap) (AssemblyScript).
- **[subsynth](subsynth)** — an analog-style subtractive synth (oscillator → resonant filter with its own envelope → amp envelope), written against [as-clap](https://github.com/WebCLAP/as-clap) (AssemblyScript).
- **[robotalk](robotalk)** — a playable text-to-speech instrument (37 phonemes: vowels, diphthongs, liquids, nasals, fricatives, stops), written against [clack](https://github.com/prokopyl/clack) (Rust).
- **[pd2wclap](pd2wclap)** — Uses [pdast](https://github.com/konsumer/pdast) to convert puredata patches into full WCLAP plugins

`make plugins` builds into `examples/plugins/`, alongside the other [example](https://github.com/konsumer/poketrack/tree/main/examples) songs/soundfonts/samples users get.
