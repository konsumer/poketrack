# pthread-synth

A minimal monophonic sine synth, written directly against CLAP's plain C API
(`clap/clap.h`, no wrapper library — see [`plugin.c`](plugin.c)) — a working
example of building a WCLAP without AssemblyScript/Rust/PureData, alongside
[karplus](../karplus)/[subsynth](../subsynth) (AssemblyScript),
[robotalk](../robotalk) (Rust), and [pd2wclap](../pd2wclap) (PureData).

On `init()`, it spawns a real background thread (`pthread_create`) to fill a
1024-sample sine wavetable, then detaches it (`pthread_detach`) rather than
waiting on it. `process()` polls an atomic "ready" flag and plays silence
until the table is filled — in practice this takes far less than one audio
block. It's built with wasi-sdk's `-pthread` toolchain
(`wasm32-wasip1-threads`), so it also serves as a real-world repro for
poketrack's web WCLAP host actually supporting threads (see the "web" section
below) — a single-threaded host would hang forever trying to load it.

## Why detach, not join

Don't change this to `pthread_join()` the thread before returning from
`init()`. That deadlocks in a browser: `init()`/`activate()`/`process()` all
run on poketrack's main thread, and joining a thread blocks on the wasm
`memory.atomic.wait` instruction — which browsers refuse to run on the main
thread by spec (it exists specifically to keep the UI responsive). Wasmtime,
which backs poketrack's native host, has no such restriction, so a plugin
that blocks on `pthread_join()` from `init()` would work fine natively while
still hanging (well — now failing loudly instead of hanging, since
poketrack's web host catches the resulting error) on web. Spawn-and-detach,
then poll a flag from `process()`/`on_main_thread()`, is the pattern that
works everywhere.

## Building

```sh
WASI_SDK_PATH=/path/to/wasi-sdk ./build.sh
```

Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (with `-pthread`
support — any release from wasi-sdk-20 onward) at `$WASI_SDK_PATH`, or
`/opt/wasi-sdk` if unset. Reuses poketrack's own CMake-fetched CLAP headers
if `../../build/_deps` exists (run `make build` or `make build-web` once
first), otherwise fetches its own copy into `vendor/` on first build.

## Web: cross-origin isolation

Real threads need `SharedArrayBuffer`, which browsers only allow on a
[cross-origin isolated](https://developer.mozilla.org/en-US/docs/Web/API/crossOriginIsolated)
page (COOP/COEP headers). poketrack's own web build gets this via
`webroot/coi-serviceworker.js` (a service-worker shim, since GitHub Pages
can't set custom headers) — see `plugins/README.md` for details. If you're
testing this plugin against some other web host that isn't cross-origin
isolated, it will fail to load with a clear console error rather than hang.
