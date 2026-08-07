# SubSynth

An analog-style subtractive synth, written in AssemblyScript as a WCLAP
instrument using [as-clap](https://github.com/WebCLAP/as-clap).

One multi-shape oscillator per voice (8-voice polyphonic) runs through a
resonant state-variable filter with its own decay envelope, then a separate
amp envelope with a sustain stage — the classic OSC → VCF → VCA voice
architecture. This fills a real gap in poketrack's built-in units: the
native `OSC` unit has no filter at all, and the native `FILTER` effect has
no per-note envelope of its own (only a static cutoff, optionally swept by
`LFO`). A self-resonating filter driven by its own envelope is what makes
acid basslines, squelchy leads, and dark pads possible, so `RESO`/`ENVAMT`
were kept even though it meant leaving out extras like a sub-oscillator or
pulse width, to stay within poketrack's usual 8-params-per-unit shape.

Like `karplus` and `robotalk`, every note self-terminates on its own via
the amp envelope's `ADECAY`/`SUSTAIN` — an explicit note-off isn't required
to stop a note, it just speeds up whatever's left into a quick release.

## Params

| Param | Range | Notes |
|-------|-------|-------|
| Wave | Saw / Square / Triangle / Sine | Oscillator shape |
| Cutoff | 0–1 | Filter base cutoff, log-mapped 40Hz–8kHz |
| Reso | 0–1 | Filter resonance — high values self-oscillate |
| EnvAmt | 0–1 | How far the filter envelope sweeps cutoff upward from Cutoff |
| FDecay | 0–1 | Filter envelope decay time, 5ms–3s — always fully decays, independent of note-off (classic acid-style sweep) |
| ADecay | 0–1 | Amp envelope decay/release time, 10ms–4s |
| Sustain | 0–1 | Amp envelope sustain level — 0 for plucky/percussive, near 1 to hold like a pad until note-off |
| Volume | 0–1 | Output level |

## Building

```sh
npm install
npm run build
```

`npm install` needs `as-clap` itself (`github:WebCLAP/as-clap`), which isn't
on the npm registry — if git dependencies are disabled in your environment,
clone [WebCLAP/as-clap](https://github.com/WebCLAP/as-clap) and copy it into
`node_modules/as-clap` by hand instead.

`npm run build` compiles `assembly/index.ts` to `build/release.wasm`.

`make plugins` (from the repo root) runs all of this and copies the result
into `examples/plugins/`, alongside the other example plugins.
