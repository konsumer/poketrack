# trommelsynthesizer

Drum machines as a WCLAP instrument, played with **General MIDI drum keys**.
Pick a machine with one param and the same pattern plays on any of them:

**Synthesized** (modelled from how the hardware works):

| Machine | Character |
|---------|-----------|
| TR-808 | bridged-T resonators, the six-square metallic cluster, burst-envelope clap |
| TR-909 | swept-sine kick, two-oscillator snare, metallic hats and cymbals |
| SDS-V | Simmons' swept "pew" toms, noise hats and clap |
| CR-78 | Roland's 1978 analog rhythm box |

**Sampled** (recordings of each machine's own samples, embedded in the plugin):

| Machine | Era / character |
|---------|-----------------|
| TR-707, TR-505 | Roland's 80s digital rhythm composers |
| LinnDrum, LM-1 | the 8-bit sound of 80s pop; the LM-1 is the gritty original (1980) |
| DMX | Oberheim: the hip-hop and electro staple |
| Drumulator | E-mu, 12-bit and crunchy |
| Drumtraks | Sequential Circuits, 1984 |
| MPC60 | Akai's 12-bit hip-hop sampler |
| RX5 | Yamaha, clean DX-era digital |
| RZ-1 | Casio, the 80s hip-hop and house box |
| SK-1 | Casio's toy keyboard samples, extremely lo-fi |
| KPR-77 | Korg's small analog box |
| DR-55, DR-110 | Boss's analog drum boxes; the DR-55 is sparse and lo-fi (kick, snare, rim, hat) |
| TR-606 | Roland's analog drum companion to the TB-303 |
| Rhythm Ace | Ace Tone, the 1960s original |
| Space Drum | Visco: synthetic space toms |
| HR-16 | Alesis, 90s with a percussion section |
| SDS5 | the real Simmons recordings (the SDS-V above is modelled) |

23 machines in one `.wasm`, no files to carry alongside a song.

## Using it

Point a PLUGIN unit at `trommelsynthesizer.wclap.wasm` (`make plugins` puts it in
`examples/plugins/`). Notes are General MIDI percussion keys, so `36` is a
kick, `38` a snare, `42` a closed hat, and so on. Velocity sets the hit's level.
Note-off does nothing; drums are one-shots.

```
36 kick   37 rim     38 snare   39 clap    40 snare 2   41 low tom   42 closed hat
43 tom    44 pedal   45 tom     46 open    47 tom       48 tom       49 crash
50 tom    51 ride    52 china   53 bell    54 tambourine  55 splash  56 cowbell
57 crash  59 ride 2  60-66 bongo/conga/timbale   67-68 agogo   69-70 cabasa/maracas
75 claves 76-77 wood blocks
```

A machine plays the keys it has a voice for and the closest neighbour for the
rest: GM's six toms share a machine's three (or one) real ones, retuned; crash
and ride keys fall back to the open hat on machines with no cymbals. Keys with no
sensible voice are silent, so a machine with no percussion plays no congas. Of the 47
GM keys, the 808 and LM-1 play 38, the HR-16 36, the LinnDrum 35, down to the
DR-55 (kick, snare, rim, hat) with 8. Keys 58 and 71-81 (vibraslap, whistles,
guiro, cuica, triangle) are silent everywhere.

Closed hat (and pedal hat) chokes the open hat, as on the real machines.

### Params

Poketrack's ADD row maps up to 16 params per instrument, so the plugin exposes
many more than that and lets you pick. They are all in the ADD list:

| Param | Range | Notes |
|-------|-------|-------|
| Machine | TR-808 … SDS5 | stepped: byte N is machine N, in the order of the tables above |
| Master | 0-100% | |
| `<voice> Level` | 0-100% | default 80% |
| `<voice> Tune` | -12 … +12 semitones | on the sample machines this resamples |
| `<voice> Decay` | 0-100% | 50% is the machine's own decay; on samples it only shortens |
| `<voice> Tone` | 0-100% | 50% is neutral, see below |
| `BD Attack`, `SD Snappy`, `SD2 Snappy` | 0-100% | the originals' extra knobs |

Voices: BD, BD2, SD, SD2, RS (rim), CP (clap), LT, MT, HT (toms), CH, OH, CY,
RD (cymbals), CB, CB2 (cowbells), TB (tambourine), LC, MC, HC (congas), TI
(timbale), MA (maracas), CL (claves). Each has only the knobs that make sense
for it (a clap has Level, Decay, Tone; claves have Level, Tune, Tone).

`Tone` means what the voice's own tone control does:

| Voice | Tone |
|-------|------|
| 808 BD | brightness of the click |
| 909 BD | drive (saturation) |
| SD | noise brightness and body/noise balance |
| toms, congas | click brightness |
| hats, cymbals | high-pass cutoff and long/short tail balance |
| clap, maracas | band centre |
| rim, cowbell, claves | band centre / body balance |
| sampled machines | below 50%: low-pass; above: high shelf |

Only the params you mapped are saved with the song, so unmapped ones sit at
these defaults.

A mapped param's byte `0x80` is its midpoint (50% / 0 semitones, give or take a
byte), which is why a song can map a knob and leave it where it was.

## Accuracy

Honest account, since "as accurate as possible" depends on what's knowable:

- **808 and 909 are modelled from how the circuits work**, not recorded. The
  808's metallic hats/cowbell/cymbal use the well-documented square-wave
  cluster (205.3, 304.4, 369.6, 522.7, 540 and 800 Hz) through band- and
  high-pass filters, the clap is the characteristic repeated noise burst with a
  longer tail, kicks and toms are swept, decaying sines. Band-limited
  oscillators avoid aliasing the hardware never had. Frequencies, decays and
  sweep depths are tuned by ear against the machines' known character; they
  haven't been matched against measurements of a real unit.
- **The real 909's hats, crash and ride are ROM samples**, not analog. Here
  they use the same metallic-noise model as the 808 with different constants.
  They're recognisably 909-ish but not the sample.
- **The sampled machines are the real thing**: their sounds are PCM in ROM, and
  these are recordings of them, so they are as faithful as the source material.
  Only one recording is kept for each voice (many machines had several snares
  or kicks), and cymbal tails are cut at 2.5 s.
- **SDS-V and CR-78** are modelled the same way as the 808/909, with fewer
  voices than the originals had.
- Tuning knobs the hardware doesn't have (e.g. BD Tune on an 808) are extras.

## Samples

The 19 sampled machines are 219 mono recordings, 2.8 MB in all, built into the
`.wasm` with C23 `#embed` (the plugin is about 3 MB). They are stored as 8-bit
mu-law (G.711), and recordings with no energy above a quarter of the sample rate
are stored at half rate; see [Load time](#load-time). They aren't in git. `make plugin-samples` fetches
them from [tidal-drum-machines](https://github.com/geikha/tidal-drum-machines)
(only the folders it needs, pinned to a commit) and generates the blobs. To use a
copy you already have, point the prep script at its `machines/` folder:

```
scripts/prep_samples.py WAV_ROOT
```

(`RolandTR707/rolandtr707-bd/Bassdrum-01.wav` and so on). Either way it writes
`src/samples/*.bin` and `src/samples.c`. Which recording stands for which
voice is the `MACHINES` table at the top of the script. Without the samples
`build.sh` uses `src/samples_stub.c`, the 19 sampled machines are silent and
the four synthesized ones work.

These are recordings of the machines' own ROM samples, and that repository doesn't
state a license for them. The release workflow bakes them into the plugin that
ships in `examples.zip`, so check you're comfortable distributing them before
releasing (or before committing the generated files).

## Building

Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) with clang 19 or newer
(for C23 `#embed`; wasi-sdk 25+), at `/opt/wasi-sdk` or `$WASI_SDK_PATH`:

```
make plugin-samples                # fetch + generate the sample blobs (once; needs git and python3)
make plugins                       # builds this and the other plugins into examples/plugins/
plugins/trommelsynthesizer/build.sh   # just this one -> plugins/trommelsynthesizer/build/
```

Plain C, no threads, exported memory, so it loads on desktop and web.

### CI

The release workflow's `examples` job installs wasi-sdk 33, then runs
`make test-plugins`, `make plugin-samples` and `make plugins`, and fails if the
built plugin is under 1 MB (which would mean it was built with the stub sample
table, leaving 19 machines silent). The samples are fetched by
`scripts/fetch_samples.sh`, a sparse checkout of just the machine folders it
needs, pinned to a commit (`SAMPLES_REF`) so builds are reproducible. Change
`SAMPLES_REF` or `SAMPLES_REPO` to use another snapshot.

## Testing

`make test-plugins` builds `test/render.c` natively against the same engine
and runs its `--check` (with the stub sample table, so it covers the synthesized
machines and runs anywhere). To render every voice, sampled machines included:

```
clang -std=c23 -O2 -Isrc -o /tmp/render test/render.c src/trommelsynthesizer.c src/samples.c -lm
/tmp/render --check               # every voice audible, finite, decays; GM map consistent
/tmp/render --wav /tmp/out        # one WAV per machine/voice, with peak and tail length
/tmp/render --calibrate > src/gains.h   # re-normalise voice levels after changing a voice
```

`src/gains.h` holds each voice's output gain, which brings every voice to a
target peak (kicks loud, hats quiet) so the machines sit at the same loudness.

## Load time

Opening a WCLAP module costs about 5 ms per MB of `.wasm`, and poketrack pays it
again whenever an instrument is edited (it tears down every instance, and the
module with them). With the samples stored as 16-bit this plugin took about 32 ms
to load, which is long enough to glitch playback if it lands during a block. Storing
them as 8-bit mu-law (median 37 dB SNR against the originals, no worse than the
8-12 bit machines they come from) and halving the rate of recordings with nothing
above 11 kHz brought the load to about 15 ms. The first load after a new build is
slower (about 55 ms) while wasmtime compiles and caches the code.

Playing it is cheap: a 512-frame block takes about 0.12 ms, around 1% of the time
available, even with many voices triggering at once.

## Song data limit

A song stores a unit's plugin path and its mapped params in one string of at most
230 characters (path + plugin id + 10 characters per mapped param). Keep the
`.wasm` path short, or map fewer params.

## Adding a machine

Append it to the `M_*` enum in `trommelsynthesizer.h` and give it a name in
`machine_name()` in `trommelsynthesizer.c`. A synthesized machine also gets a row in the
`CFG` table (each voice is one of a few kinds with a handful of constants:
swept sine, snare, metallic, two-tone ping, clap, noise burst); a sampled one
gets an entry in the `MACHINES` table in `scripts/prep_samples.py`. Then run
`render --calibrate`.

## Licensing

zlib, like the rest of poketrack, apart from the embedded ROM recordings above.
