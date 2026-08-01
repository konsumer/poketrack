# PD 2 WCLAP

Compiles a Pure Data patch straight into a self-contained WCLAP plugin — no
libpd, no Faust, no runtime patch-loading. `dac~`/`adc~` become the plugin's
audio ports, `notein` becomes real CLAP note events, and any `receive`/GUI
object becomes a CLAP param you map with the ADD row, same as every other
unit. Under the hood: [pdast](https://github.com/konsumer/pdast)'s `pd2ast`
parses the patch to a JSON AST, `pdast2wclap` compiles that AST straight to C,
and `wasi-sdk`'s `clang` turns the C into `wasm32-wasi`. The generated C links
against `runtime-shim.c` in this directory, which provides the actual CLAP
plugin surface (`clap_entry`, audio/note/params extensions, a sample-accurate
event-splitting `process()` loop) — the same role an "architecture file"
plays for a Faust patch, just hand-written here since this isn't Faust.

Because the patch is compiled in, not loaded at runtime, "picking a patch" is
a build step: run `build.sh` against your `.pd` file and point poketrack's
DATA field at the resulting `.wasm`, exactly like any other WCLAP plugin.

Some things to consider:

- Any `receive`, without a `send` will get turned into a plugin param (mapped range 0-1) but it's better to use a slider/toggle/etc with a `receive`. They are nice because they let you play with the values in puredata, and also tell poketrack their range (which is mapped to 00-FF.)
- Some ops are not implemented yet, some are missing or work slightly different. If you find something is off in one of your own patches let me know, and I will try to improve [pdast](https://github.com/konsumer/pdast). It's a hack for sure, and there will always be gaps, but I am interested in getting it usably close.
- polyphony is done with standard pattern of `notein -> pack -> poly -> route` and you can make sub-patches (`pd`) to copy logic for voices. see [supersaw](./patches/supersaw.pd) for an example.
- you may need more `OFF` messages in poketrack, dpending on how your patch works. it's standard `note.on/off` messages, which is slightly different than the tracker paradigm. You might need to use envelopes on note-in, or just send more `OFF` messages, since plugins let you take over control of all that.
- use [plugdata](https://plugdata.org/) it's built-in C stuff doesn't work directly for plugins, but you can limit your scope to "compiled mode" which is a pretty good start. The UI is also more modern, and it's primarily what I use, so it'll be easier to stay in sync wth how I do things. SInce people also often use it to target that smaller C subset, you can find a lot of patches for it, that should work well as WCLAP plugins, too.


3 intentionally simple demo patches ship in `patches/`:

- **osc.pd** — `notein` → `mtof` → `osc~`, multiplied by a `volume` param,
  into `dac~`.
- **vcf.pd** — `noise~` through `vcf~` (resonant bandpass)
  slider driving `cutoff` (100–5000 Hz).
- **supersaw.pd** — polyphonous voice-stealing multiple detuned saw-waves

## Params

**osc.pd**

| Param | Range | Notes |
|-------|-------|-------|
| volume | 0–1 | `*~` gain, driven by an `hsl` |

**vcf.pd**

| Param | Range | Notes |
|-------|-------|-------|
| cutoff | 100–5000 Hz | `vcf~` center frequency, driven by an `hsl` |

**supersaw.pd**

| Param | Range | Notes |
|-------|-------|-------|
| detune | 0-255 | Detune amount, with math around native poketrack range (0-255) driven by an `hsl` |

## Install

You need three things on `PATH` (or pointed to via env vars):

```sh
# pd2ast + pdast2wclap, from a pdast checkout
git clone https://github.com/konsumer/pdast
cd pdast
cargo install --path pd2ast
cargo install --path pdast2wclap

# wasi-sdk — download a release for your platform and point WASI_SDK_PATH at it
# https://github.com/WebAssembly/wasi-sdk/releases
export WASI_SDK_PATH=/opt/wasi-sdk
```

`build.sh` also needs the CLAP headers — it reuses poketrack's own
CMake-fetched copy if `cmake -B build` has already been run (or set
`CLAP_INCLUDE` to point at any `clap/include` checkout), otherwise it fetches
its own shallow clone into `vendor/` on first run.

## Building the demo plugins

```sh
./build.sh patches/osc.pd pd-osc
./build.sh patches/vcf.pd pd-vcf
./build.sh patches/supersaw.pd pd-supersaw
```

Each produces `build/<name>.wasm`. `make plugins` (from the repo root) runs
both of these and copies the results into `examples/plugins/`, alongside the
other example plugins.

## Converting your own patch

```sh
./build.sh path/to/your-patch.pd your-plugin-name
```

Then in poketrack, add a `PLUGIN` unit to an instrument chain, point its DATA
field at `build/your-plugin-name.wasm`, and use ADD to map whichever params
your patch registered.

### What gets picked up as a param

Any `[receive NAME]` / `[r NAME]` / `[value NAME]` object with no matching
`[send NAME]` in the same patch, and any GUI object (`hsl`, `vsl`, `nbx`,
`tgl`, `bng`, radio) with its **receive** field set, becomes one CLAP param
named `NAME` — range and default come from the GUI object's own min/max/init
if there is one, otherwise 0–1. A bare `[r NAME]` with no paired GUI object
still works (see `osc.pd`'s `volume`), it just has no range info to borrow, so
it defaults to plain 0–1.

### What's implemented so far

Oscillators (`osc~`, `phasor~`, `noise~`), audio math (`+~ -~ *~ /~`),
one-pole filters (`lop~`/`hip~`), a resonant bandpass (`vcf~`), `sig~`,
`dac~`/`adc~`, `notein`, `mtof`/`ftom`, control math (`+ - * / max min mod
pow`), and `send`/`receive`/`value`. Anything else compiles to a harmless
zero stub with a `warning:` on stderr from `pdast2wclap` — the object
template system (see `pdast2wclap`'s `wclap_gen.rs` in the pdast repo) is
built to grow this list; unsupported objects are a gap to fill, not a wall.

### Control-rate semantics

Unlike `pdast2faust` (which recomputes every control object every audio
sample, and documents real ordering bugs from that shortcut), this backend
recomputes the control graph only at actual event boundaries — a `notein` or
mapped-param change — matching PD's own message-passing behaviour much more
closely. A control value feeding a signal-rate inlet is sample-and-held
between updates, same as real PD.
