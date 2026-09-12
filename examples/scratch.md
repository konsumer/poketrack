# scratch.rpt

A [TURNTABLE](../src/units/README.md#turntable) demo: three regions sliced out
of `scratch.ogg` become a pad bank of scratch sounds — baby scratch, chirp,
tear, transform — played over a 92 BPM boom-bap beat.

## The source

`scratch.ogg` is 13.6 s of 8 kHz mono scratch hits. An onset scan (envelope,
5% of peak, gaps merged under 250 ms) found four sample clusters; three short,
loud hits are used here, addressed by the turntable's `LSTR`/`LEND` as
fractions of the file:

| region | time | LSTR / LEND | peak |
|---|---|---|---|
| A | 6.97–7.33 s | 131 / 138 | 0.67 |
| B | 8.26–8.67 s | 155 / 163 | 0.53 |
| C | 12.51–13.36 s | 235 / 251 | 0.55 |

The file's rate (8 kHz against the engine's 48 kHz) is handled by the unit, so
a written `C-4` plays the region at its natural speed.

## Instruments

`DRUMS` is the GM soundfont, `BASS` a saw pluck. The rest are turntables, one
per gesture — the pad bank:

| instrument | region | DPTH | RATE | SHPE | CUT | gesture |
|---|---|---|---|---|---|---|
| AHH | A | 1.6× | 5 Hz | SINE | 0 | baby scratch — platter swings both ways, fader open |
| FRESH | B | 1.6× | 5 Hz | SINE | 0 | same, different sample |
| CHIRP | A | 1.8× | 6 Hz | SINE | 0.88 | fader cuts the push, so the pull-back is what you hear |
| TEAR | B | 2.2× | 6 Hz | SAW | 0 | hard reverse each cycle |
| TRANS | C | 0 | 8 Hz | SINE | 1.0 | record runs steady, fader chops it |

Every one is a plain `TURNTABLE` source with different params — no extra units,
no external modulators. Each scratch is a note held for two steps (a 16th pair,
~0.33 s at 92 BPM, so about 1.5 gesture cycles) with a note-off — or one step
when the second would land past the bar. (A note-off at step 16 of a 16-step
pattern is never played, and the turntable would loop on.)

## Layout

Everything is a **default 16-step pattern** — one bar each — arranged in the
song view, three lanes: drums, bass, scratch.

```
pattern
  0  D_main    kick 0/10, snare 4/12, 8th hats, open hat on 14
  1  D_fill    D_main + ghost-snare roll into the next bar
  2  B_A       root + fifth          (A1)
  3  B_C       root + fifth          (C2)
  4  B_D       root + fifth          (D2)
  5  B_E       root + fifth          (E2)
  6  S_baby    AHH   @6, FRESH @14
  7  S_chirp   AHH   @6, CHIRP @14
  8  S_tear    TEAR  @6, AHH   @14
  9  S_trans   TRANS @4, TRANS @12
```

```
row  lane 0 drums   lane 1 bass   lane 2 scratch
 1     D_main        B_A           S_baby
 2     D_main        B_A           S_chirp
 3     D_main        B_C           S_trans
 4     D_main        B_C           S_tear
 5     D_main        B_D           S_baby
 6     D_main        B_D           S_trans
 7     D_main        B_A           S_chirp
 8     D_fill        B_E           S_tear
```

Eight rows of one bar each, looping (~20.7 s). Bass roots walk A-A-C-C-D-D-A-E
and the scratches alternate baby / chirp / transform / tear, so the grid reads
as a form instead of three long unbroken patterns.

## Tweaking

- **Different sample:** move `LSTR`/`LEND` to another region — they're just
  fractions of the file, so any two points work.
- **Faster hand:** raise `RATE`; **deeper cut:** raise `DPTH` past 1 (below 1×
  the platter never actually reverses — it just slows down).
- **Pitch it up:** `TUNE` or play a note above `C-4`.
- **Longer hold:** keep the note held for more steps; the region loops for as
  long as the note sounds.

## Verify it

Isolating the scratch lane (mute lanes 0/1) shows every bar has scratch audio,
and the windowed mean of the first scratch falls in 94 of 187 windows — the
platter is genuinely travelling backwards, not just being gated.

```
./build/poketrack examples/scratch.rpt          # play
./build/poketrack --wav /tmp/scratch.wav examples/scratch.rpt
```
