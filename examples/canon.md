# canon.rpt

An **ARPEGGIATOR** demo: Pachelbel's *Canon in D* chord progression, held as
triads and run through `CHORD → ARP → OSC`, over a bass line and drums.

## Why CHORD is there

A tracker track is monophonic — playing a note releases the previous one — so
an arpeggiator behind a single track would only ever see one note at a time and
would just retrigger it. [`CHORD`](../src/units/README.md#chord) turns the
written root into a triad first, and the arpeggiator then has something to walk:

```
ARP SYNTH   CHORD -> ARP -> OSC -> DELAY
BASS        OSC
DRUMS       SF2 (bank 128, the GM percussion kit in microgm.sf2)
```

## The arpeggio

`ARP` is `RATE = 1/16`, `MODE = UP`, `GATE = 50%`, `OCT = 2`, so a D-major
triad (D4 F#4 A4) comes out as a two-octave run, six notes per bar:

```
step 1 2 3 4 5 6
      D F# A D' F#' A'   then back to D
```

That is the whole effect: hold a chord, get an arpeggio locked to the tempo.

## The progression

Eight bars, one chord each, on lane 0. The chord *quality* is a chain param on
`CHORD` (global param index 1), so it is set per bar from the pattern's FX
column instead of needing eight instruments:

| bar | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|-----|---|---|---|---|---|---|---|---|
| chord | D | A | Bm | F#m | G | D | G | A |
| root (arp) | D4 | A3 | B3 | F#3 | G3 | D4 | G3 | A3 |
| bass | D3 | A2 | B2 | F#2 | G2 | D3 | G2 | A2 |
| FX `TYPE` | MAJ | MAJ | MIN | MIN | MAJ | MAJ | MAJ | MAJ |

Each bar fires the root note and sets the chord type in the same step; the
arpeggiator holds that chord for the bar.

## Layout

Three lanes play at once:

```
row 0   lane 0: arp pattern    lane 1: bass pattern    lane 2: drums
```

One 128-step pattern per part (8 bars × 16 steps), and the song row loops, so
the progression repeats. Bass holds a whole note per bar; drums are a plain
kick/backbeat with 8th hats and an open hat on the last 16th of each bar.

## Tweaking

- **Down / up-down / random:** `ARP` `MODE`.
- **Faster or slower arpeggio:** `ARP` `RATE` (`1/8` … `1/32`).
- **Longer run:** raise `ARP` `OCT` (1–4) to add more octaves.
- **Different chord shape:** `CHORD` `TYPE`, or use `INV` for inversions —
  with `INV = 1` the D triad starts on F# and the line smooths out.
- **Stabs instead of arpeggios:** switch `ARP` off; `CHORD → OSC` then plays
  the triad as one hit.

## Verify it

Rendered, the first six notes of bar 1 come out at D4, F#4, A4, D5, F#5, A5
(293.7 / 370.0 / 440.0 / 587.3 / 740.0 / 880.0 Hz), and bar 3 walks Bm
(246.9 / 293.7 / 370.0 / 493.9 / 587.3 / 740.0 Hz).

```
./build/poketrack examples/canon.rpt          # play
./build/poketrack --wav /tmp/canon.wav examples/canon.rpt
```
