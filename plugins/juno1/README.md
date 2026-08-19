# Juno-1

A Roland Juno-1 / Alpha Juno DCO synth, written in AssemblyScript as a WCLAP
instrument using [as-clap](https://github.com/WebCLAP/as-clap), following
the same shape as [subsynth](../subsynth): 6-voice polyphonic, one voice per
note, sample-accurate event handling.

Unlike subsynth this isn't an "analog-style" approximation — the whole
DCO → HPF → VCF → VCA → chorus signal path, envelope generator, and every
lookup table (LFO rate/delay curves, envelope segment timings, keyboard
tracking curves, chorus rate, the DCO pitch-envelope table) are ported from
[mikerodd/june-21](https://github.com/mikerodd/june-21)'s actual `June21`
CSound instrument (`src/cabbage-module/june-21.csd`), which itself was
measured/extrapolated from real Juno-1/Juno-2 hardware. Where Csound had an
opcode with no wasm equivalent (`vco2`'s band-limited oscillators,
`moogvcf`/`reson`, `StChorus`), this uses standard public DSP techniques
instead (PolyBLEP oscillators, a resonance-free 4-pole ladder lowpass feeding
a resonant SVF bandpass, RBJ biquads, LFO-modulated delay-line chorus) —
same *architecture*, different building blocks.

256 real patches ship built in (`Patch`) — exactly filling the ADD row's
0-255 byte range — decoded byte-for-byte from real Roland Juno-1/Juno-2
SysEx bulk-dump banks using the format documented in june-21's
`junosyxloader` (`src/plugins/junosyxloader/src/jsl.c`). See **Licensing**
below — this is the one part of the plugin that isn't zlib-licensed like
the rest of poketrack.

## Params: a full 1:1 mapping

Every one of the real hardware's 36 patch parameters is its own CLAP
param here — a straight 1:1 mapping, plus `Patch` to recall a full factory
preset (37 params total). poketrack's ADD-row automation caps a *unit* at
16 mappable params, but that's a limit on the mapping UI, not on what a
CLAP plugin may declare — the CLAP spec has no such cap, and poketrack has
no hosted plugin GUI to need one either. So instead of curating a fixed
subset, this plugin exposes everything and lets you pick which ≤16 you
actually want ADD-mapped per instrument.

Every param, including `Patch`, declares its own natural range — poketrack's
ADD row is always a raw 0-255 byte and scales that into whatever range the
target param declares, same as it already does for any other plugin (e.g.
a pd2wclap patch with a 100-5000 Hz cutoff slider). For a stepped/enum
param like `Patch` (0..127, one per factory preset) that scaling maps each
byte directly onto a step — byte N is patch N — rather than spreading the
full 0-255 range proportionally across the narrower step range, so one
ADD-row bump always moves exactly one patch (bytes past the last patch
just hold there). Recalling a patch (same as picking one on real hardware)
resets every param back to "follow the patch". Touching any other param
afterward overrides just that one — it plays back the patch's value until
you explicitly set it, then holds your value until the next `Patch` change.

| Param | Range | Notes |
|-------|-------|-------|
| Patch | 0-255 | Recalls a full patch — resets every param below back to "follow the patch" |
| Pulse | Off/Square/Pulse75/PWM | |
| Sawtooth | Off + 5 gated-combination variants | |
| Sub | Off + 5 sub-oscillator variants | |
| SubLevl | 0-3 | |
| NoisLvl | 0-3 | |
| PwPwm | 0-127 | PWM amount, shared by Pulse=PWM and Sawtooth's PWM variant |
| PwmRate | 0-127 | PWM LFO rate; 0 = static offset instead of modulation |
| VcfFreq | 0-127 | VCF cutoff |
| VcfReso | 0-127 | VCF resonance |
| VcfEnv | Normal/Inverted/Dyn-Normal/Dyn-Inverted | VCF envelope routing mode |
| VcfEnvd | 0-127 | VCF envelope depth |
| VcfKybd | 0-15 | VCF keyboard tracking |
| VcfLfo | 0-127 | VCF LFO depth |
| DcoLfo | 0-127 | DCO (pitch) LFO depth |
| DcoEnv | Normal/Inverted/Dyn-Normal/Dyn-Inverted | DCO pitch-envelope routing mode |
| DcoEnvd | 0-127 | DCO pitch-envelope depth |
| DcoRng | 0-3 | DCO octave range |
| DcoBnd | 0-15 | Pitch-bend range (unused — no pitch-bend input wired up yet) |
| LfoRate | 0-127 | Shared LFO rate |
| LfoDely | 0-127 | Shared LFO delay (restarts when all voices release) |
| EnvT1/EnvL1/EnvT2/EnvL2/EnvT3/EnvL3/EnvT4 | 0-127 | The native 6-breakpoint envelope (not a standard ADSR — see below) |
| VcaEnv | Normal/Gate/Dyn-Normal/Dyn-Gate | VCA envelope mode |
| VcaLevl | 0-127 | Output level |
| VcaAftr, VcfAftr, DcoAftr | 0-15 | Aftertouch depths (unused — no aftertouch input wired up yet) |
| EnvKybd | 0-15 | Envelope keyboard-follow (unused in this port) |
| HpfFreq | 4 fixed voicings | |
| Chorus | 0/1 | On/off — real hardware has no separate wet-mix knob |
| CrsRate | 0-127 | Chorus LFO rate |

The envelope is an unusual 6-breakpoint shape, not a standard ADSR: it
always progresses through all 4 segments even without a note-off (the
Juno envelope eventually decays on its own if you never release), and
note-off jumps straight to the final segment from wherever it currently
is. `EnvL1`/`EnvL2` above/below each other selects one of two duration-
table lookups for the shape, exactly mirroring the two branches in
June21's own envelope generator.

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

`make plugins` (from the repo root) runs this and copies the result into
`examples/plugins/juno1.wclap.wasm`, alongside the other example plugins.

### Regenerating the preset table

`assembly/plugins/presets-data.ts` is generated from real Juno-1/Juno-2/
MKS-50 (Alpha Juno) SysEx bulk-dump banks by `scripts/syx2presets.mjs`:

```sh
node scripts/syx2presets.mjs bankA.syx [bankB.syx ...] > assembly/plugins/presets-data.ts
```

The checked-in version was generated from all four banks in `vendor/`
(`FACTORYA.SYX`, `FACTORYB.SYX`, `MIKEJUNO.SYX`, `JOHNSEXT.SYX` — 64
patches each, 256 total, exactly filling `Patch`'s 0-255 ADD-row byte
range — see **Licensing** for where each came from):

```sh
node scripts/syx2presets.mjs vendor/FACTORYA.SYX vendor/FACTORYB.SYX vendor/MIKEJUNO.SYX vendor/JOHNSEXT.SYX > assembly/plugins/presets-data.ts
```

You can point it at any real Juno-1/Juno-2/MKS-50 bulk-dump file — your own
patches, a different cartridge, banks found online (e.g. the collection at
[llamamusic.com/mks50](https://llamamusic.com/mks50/mks-50_patches.html),
where `MIKEJUNO.SYX`/`JOHNSEXT.SYX` came from). Multiple files are
concatenated in the order given.

The script assumes each file is exactly the factory-bank shape (16 blocks
of 4 tones, 64 tones total, back-to-back) and reads it via a fixed offset
formula, so it'll misbehave on a file that isn't that exact shape (a
partial bank, several dumps concatenated into one file, stray bytes around
the SysEx data). All four bundled `vendor/` banks are that exact shape.

## Licensing

Almost all of poketrack is zlib-licensed (see the repo root `LICENSE`). This
plugin is the exception: `assembly/plugins/tables.ts` is ported directly
from [mikerodd/june-21](https://github.com/mikerodd/june-21)
(**GPL-3.0-or-later**), and `assembly/plugins/presets-data.ts` is decoded
from four real Juno-1/Juno-2/MKS-50 SysEx banks (`vendor/`) of mixed
provenance:

- `FACTORYA.SYX` / `FACTORYB.SYX` — the official factory ROM patches,
  sourced from mikerodd/june-21, **GPL-3.0-or-later**.
- `MIKEJUNO.SYX` / `JOHNSEXT.SYX` — user-contributed banks from the
  [llamamusic.com MKS-50/Alpha Juno patch collection](https://llamamusic.com/mks50/mks-50_patches.html).
  That archive carries no license or attribution file of its own; patch
  sharing in this community has historically been informal/unrestricted
  (the point of a public archive like that one), but this isn't a
  documented grant. Treated as GPL-3.0-or-later here too, out of caution.

All three files carry a GPL-3.0-or-later notice in their own header, as
poketrack's root `LICENSE` explicitly permits per-file overrides. Because
they're compiled into `juno1.wclap.wasm`, **the compiled plugin binary
should be treated as GPL-3.0-or-later for distribution**, distinct from the
rest of poketrack. `scripts/syx2presets.mjs` and
`assembly/plugins/juno1.ts` (the engine itself) are original work and not
GPL-encumbered on their own — only the bundled lookup/preset *data* is.
