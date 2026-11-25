# syntakt

A WCLAP instrument that runs the real digital voice engines of the Elektron Syntakt: **BD MODERN, SD BASIC, CY ALLOY, PC CARBON, SY TONE, SY CHORD, SD VINTAGE, CP VINTAGE, SY TOY, SY BITS and SY SWARM**. It isn't a recreation. It's the Syntakt's own engine code, run on a small ColdFire interpreter, and it's bit-exact with the reference emulator (see [Testing](#testing)).

The Syntakt's other machines are analog circuits, so there's no code to run, and SP TWINSHOT needs factory samples that aren't in the OS file.

## Making your plugin

No Elektron code is in this repo or in the template plugin. You make your own `syntakt.wclap.wasm` from the official Syntakt OS file, which you download from [elektron.se](https://www.elektron.se/support-downloads/syntakt). Use `Syntakt_OS1.42.syx`; 1.41 works too, since its engines are identical. You only do this once.

- **In your browser:** open [the Syntakt WCLAP maker](https://konsumer.js.org/poketrack/syntakt/), pick the `.syx`, and save the plugin. The file is read in the page and nothing is uploaded.
- **From a checkout:**

  ```sh
  plugins/syntakt/build.sh   # the template, needs wasi-sdk
  node plugins/syntakt/scripts/make-plugin.mjs path/to/Syntakt_OS1.42.syx
  # -> plugins/syntakt/build/syntakt.wclap.wasm
  ```

Either way, the result is a normal WCLAP. Point a PLUGIN unit at it like any other.

**It contains Elektron's code, so it's for your own use.** If you share a song that uses it, leave `syntakt.wclap.wasm` out; whoever gets the song makes their own. The unpatched template (`syntakt-template.wasm`) loads but stays silent, and logs where to get a real one.

How it works: the maker unpacks the OS file (SysEx, then the ELE3 container, then aPLib), takes section 7, and checks its hash. Section 7 is the program of the Syntakt's second ColdFire, which computes the digital voices. The maker then writes it into an empty slot in the template's data segment (`src/slot.h`). The template is a CLAP plugin around a ColdFire + EMAC interpreter that calls the engine's own voice loop.

## Params

There are 6 global params and 8 knobs. The knobs are shared by all machines, like the Syntakt's own: their names, defaults and meaning follow the selected machine. Pick SD VINTAGE and knob A is `INHM`; pick SY BITS and it's `DET`. When the machine changes, the plugin asks the host to rescan, so poketrack relabels mapped knobs. Hosts that ignore rescans keep showing the first machine's names, but values still apply.

Each knob is stepped and declared as 0–127 (TUNE as 40–88), so an ADD-row value is the Syntakt value whatever the machine. Each machine clamps it to its own range, and the value text shows it the way the machine plays it (TUNE and SY BITS DET as -24..+24). A knob you haven't set follows the current machine's default.

| Param | |
|---|---|
| Machine | which of the 11 machines plays |
| Level | output level (default 50%) |
| Gate | holds the machine's envelope at full until note-off (the Syntakt's hidden gate flag) |
| Voices | polyphony, 1–8 (default 1, like a Syntakt track: a new note cuts the last). With more, a new note takes a free voice, then the voice already playing that key, then the oldest |
| Velocity | how much note velocity scales the level |
| Release | fade after note-off, up to 2 s. OFF (the default) ignores note-off, like the hardware's trig-only machines; set it to cut long or infinite decays (SY TONE and SY CHORD default to DEC 127) |

The knobs on each machine:

| Machine | TUNE, then knobs A–D | E | DEC, OVER |
|---|---|---|---|
| BD MODERN | SMOD, TMOD, SWEP, MENV | PNCH | DEC, OVER |
| SD BASIC | INHM, MDFB, SWEP, MENV | PNCH | DEC, OVER |
| CY ALLOY | RADIO, MOD, SHIM, MENV | PNCH | DEC, OVER |
| PC CARBON | INHM, MOD, SWEP, MENV | PNCH | DEC, OVER |
| SY TONE | RATIO, MOD, FDBK, MENV | PNCH | DEC, OVER |
| SY CHORD | BAL, TYPE (0–37), WAVE, FENV | PNCH | DEC, OVER |
| SD VINTAGE | INHM, FCMP, SWEP, MENV | PNCH | DEC, OVER |
| CP VINTAGE | BODY, BAL, SPCR, BENV | PNCH | DEC, OVER |
| SY TOY | FORM, IMP, BRIG, PART | PNCH | DEC, OVER |
| SY BITS | DET (-24..+24), BAL, SRR, WAVE | BR (bit redux) | DEC, OVER |
| SY SWARM | NMOD, ANIM, DET, MIX | SUB (0–2) | DEC, OVER |

Notes play at pitch from key 60 (C4), plus TUNE.

The engine runs at 48 kHz in blocks of 32 frames (0.67 ms), and its output is resampled to the host rate. A trig therefore lands on the next block boundary.

## Files

| | |
|---|---|
| `src/cf.c` | ColdFire ISA_C + EMAC interpreter, with a predecode cache |
| `src/engine.c` | the voice loop's calling convention: memory map, voice init, params, one block, parking |
| `src/synth.c`, `src/machines.c` | params, voice allocation, release, parking silent voices, resampling |
| `src/plugin.c`, `src/slot.c` | CLAP glue and the empty engine slot |
| `web/` | the maker page, with `firmware.js` (OS file → engine image) and `patch.js` (image → plugin), shared with the CLI |
| `scripts/` | `make-plugin.mjs`, `extract.mjs`, `survey.mjs` (the instructions the interpreter must cover), `raw2wav.mjs` |
| `test/` | the bit-exact bench and its scenarios, and `demo.c` (renders every machine through the synth to a WAV) |

`make syntakt-web` builds the maker page into `webroot/syntakt/`. `make build-web` does too when wasi-sdk is around. poketrack's own tests load the template and, when you've made one, your plugin (`tests/test_main.c`, `test_clap_plugin_syntakt`).

## Scripts

```sh
node scripts/extract.mjs path/to/Syntakt_OS1.42.syx build/section7.bin
node scripts/survey.mjs build/section7.bin   # needs m68k-elf-objdump (brew install m68k-elf-binutils)
test/run.sh                                  # render test/*.txt, compare with build/*.ref if present
node scripts/raw2wav.mjs build/mix1.raw build/mix1.wav [voice]
cc -O2 -Isrc -o build/demo test/demo.c src/synth.c src/machines.c src/engine.c src/cf.c -lm && build/demo build/section7.bin build/demo.wav
```

## Testing

`test/*.txt` are scenarios: machine changes, parameter changes, notes, trigs, releases and parking, block by block. `test/bench.c` renders them with the interpreter. `test/ref_bench.py` renders them with Modded-Cycles' Unicorn emulator, and `test/run.sh` checks the two match sample for sample:

```sh
# one-time setup for the reference: python venv with unicorn + numpy, and the
# m68k objdump its EMAC model uses under the name m68k-linux-gnu-objdump
python3 -m venv .venv && .venv/bin/pip install unicorn numpy
mkdir -p .venv/bin && ln -sf "$(which m68k-elf-objdump)" .venv/bin/m68k-linux-gnu-objdump
git clone https://github.com/18nelli18/Modded-Cycles ../Modded-Cycles

for s in mix1 mix2 park; do
  PATH=.venv/bin:$PATH MODDED_CYCLES=../Modded-Cycles .venv/bin/python test/ref_bench.py build/section7.bin test/$s.txt build/$s.ref
done
test/run.sh
```

The reference takes about 50 s per scenario. The interpreter takes under 0.1 s.

## Performance

The engine always computes all 8 voices, so the synth **parks** voices that aren't sounding (`st_park`): after about 21 ms below -100 dBFS (note-off or not, unless Gate holds it), or when a Release fade ends. An instance with every voice parked skips the engine entirely. A parked voice costs nothing until its next trig.

Measured on an Apple M-series laptop, as multiples of realtime:

| | native | wasm (wasmtime) |
|---|---|---|
| all 8 voices parked | 44× | 76× |
| one sounding voice | 14–18× | 12–17× |
| 8 voices sounding at once | 2.9× | 2.2× |

That's roughly 6–8% of a core per sounding voice. poketrack's desktop host runs plugins in wasmtime 39, and browsers JIT wasm too.

The template is built with `-fno-jump-tables`, so the dispatch `switch` becomes compares and direct calls. Dispatching through a function pointer (`call_indirect`) or a jump table (`br_table`) runs about 5× slower on wasmtime 39 on arm64. That slowdown is what made early builds garble in poketrack. The interpreter decodes each instruction once into a cached record with a handler specialized for its operand forms (`decode()` in `cf.c`), and falls back to the generic `step()` for anything else.

## What the interpreter has to cover

These figures are from `survey.mjs` on OS 1.42. The code reachable from the voice loop, the voice init/reset and the 12 update/render engines is **6,803 instructions (23.9 KB)** using **66 mnemonics**:

- **Integer core.** Mostly `move.l`/`movea`/`lea`/`pea`, add/sub/cmp/and/neg/not, shifts, `swap`, `ext`, `link`/`unlk`, `movem`, Bcc/`jsr`/`rts`. There are 170 `jsr (An)` calls through function pointers.
- **ColdFire extras.** `mvs.b/w`, `mvz.w`, `sats.l`, `ff1`, `rems.l`, `muls.l`.
- **EMAC.** `mac.l`/`msac.l`, including the parallel-load forms (`mac.l Ry,Rx,(Ay)+,Rw,ACCn`), `movclr.l`, `move.l Rn<->ACCn`, and `move.l #imm,MACSR` (the loop runs at MACSR `0xA0`, signed fractional). No `mac.w`.
- **Not used.** FPU, supervisor-mode instructions, traps, and division other than `rems.l`.

The program also reads data tables from its own image, plus the two SRAM copies made at boot (`0x4004f6e0..0x40057670` → `0x80000000`, `0x40057670..0x4005df10` → `0x80008000`). The interpreter maps the whole 384 KB image.

## Credits

The format details and engine calling convention come from [Modded-Cycles](https://github.com/18nelli18/Modded-Cycles) (MIT; see `notes/02`, `notes/16`, `notes/24` and `tools/emu/`), which builds on [elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool) (MIT).

Elektron and Syntakt are trademarks of Elektron Music Machines MAV AB. This project isn't affiliated with or endorsed by Elektron.
