# sarniezz.rpt

A bass-and-drums microtonal riff, transcribed from *Sarniezz* by Angine de
Poitrine. It is a worked example of the **MICRO** note modifier: the tracker
note grid is treated as a 24-steps-per-octave scale, so the riff can be
notated with quarter tones the western 12-note grid has no symbol for.

## The riff

The source MIDI has six tracks: two "Acoustic Guitar" parts, two "Bass Guitar"
parts, and two drum tracks. The four melodic tracks are the same riff layered
by a loop pedal — one pair low, one pair an octave up, the second part of each
pair offset by a 16th note. All four are played here by the same bass
instrument, which is what makes the stacked, phasing texture.

- **Tempo** 100 BPM, **16th-note grid** (one tracker line = one 16th),
  pattern length 56 steps (7 bars of 2/4).
- MIDI pitches are the reference; the *written* notes are re-notated into the
  microtonal grid (below).

## The instrument chains

```
BASS   MICRO -> OSC -> FILTER
DRUMS  SF2 (bank 128, the GM percussion kit in microgm.sf2)
```

`MICRO` is the first slot, so it owns every note before the oscillator sees it.

## How MICRO is set up

```
pitch = ROOT + (note - ROOT) * 12 / STEPS
```

- `STEPS = 24` — a quarter tone per written note.
- `ROOT = 26` (D1) — the riff's tonic keeps its pitch; everything is measured
  from there.

Because each written step is 50 cents, the notation is **doubled**: a written
note is `26 + (intended_pitch - 26) * 2`. The whole part sits in notes
`26…62` (36 steps) where 12-EDO would have used 18, and every *odd* note is a
quarter tone between two western pitches.

The MIDI is a 12-EDO approximation, so the two chromatic neighbours are
re-notated a quarter tone down, placing them *between* the western notes:

| MIDI pitch | intended | written note | note name |
|---|---|---|---|
| 26 | D1 | 26 | D1 |
| 27 | D1 + 50c | 27 | between D and D# |
| 28 | E1 | 30 | E1 |
| 31 | G1 | 36 | G1 |
| 32 | G1 + 50c | 37 | between G and G# |
| 35 | B1 | 44 | B1 |
| 38 | D2 | 50 | D2 |
| 39 | D2 + 50c | 51 | between D and D# |
| 40 | E2 | 54 | E2 |
| 43 | G2 | 60 | G2 |
| 44 | G2 + 50c | 61 | between G and G# |

So the riff's `D`/`D#` and `G`/`G#` pairs become 50-cent oscillations: the
quarter-tone inflections sit exactly where the western semitones used to.

## Drums

The two MIDI drum tracks are a kick/backbeat plus **continuous 16th-note
closed hi-hats**, kept with accents: the hats hit harder on the beat and on
the 8ths (`118 / 96 / 78 / 60` by position), the open hat marks the end of a
phrase, and the `full` pattern adds a syncopated kick, ghost snares and
off-16th hat accents. The MIDI's `27`/`31` note numbers map to the GM kick
(`36`) and snare (`38`).

## Arrangement

Bass and drums are **separate patterns on separate song lanes**, so the song
view shows them side by side and either part can be muted, edited or
rearranged on its own. Lane 0 is bass, lane 1 is drums; lanes 2–3 are empty.

```
pattern
  0  bass, all four layers     tracks 0–3
  1  bass, low layers only     tracks 0–1
  2  drums, main              tracks 0–2 (kick/snare/hats)
  3  drums, intro             tracks 0–3 (adds crash)
  4  drums, break             hat/kick accents, no crash
  5  drums, full              syncopated kick, ghost snares
```

```
row  lane 0 (bass)   lane 1 (drums)
 0      1 low            3 intro
 1      0 full           2 main
 2      0 full           2 main
 3      1 low            4 break
 4      0 full           2 main
 5      0 full           2 main
 6      0 full           5 full
 7      0 full           2 main
```

Eight rows of 56 steps, both lanes playing at once, looping (≈67 s per pass).

## Tweaking

- **Finer or coarser grid:** `STEPS` cycles `12 / 17 / 19 / 22 / 24 / 31 / 36 /
  48 / 53 / 72 / 96`. `12` is a passthrough. Higher values shrink the part's
  octave range further and put more playable pitches between the semitones.
- **Different anchor:** move `ROOT` to the tonic if you transpose the riff.
- **Hear the microtonality:** set `STEPS = 12` on the bass chain. The written
  notes then play as plain semitones — the odd-numbered, in-between notes snap
  onto the western grid and the quarter-tone character disappears.

## Verify the mapping

With `STEPS = 24`, `ROOT = 26`, a held note renders at:

| written note | pitch | 12-EDO neighbours |
|---|---|---|
| 36 | G1 = 49.00 Hz | |
| 37 | 50.4 Hz | between G1 and G#1 |
| 38 | G#1 = 51.91 Hz | |

```
./build/poketrack examples/sarniezz.rpt          # play
./build/poketrack --wav /tmp/sarniezz.wav examples/sarniezz.rpt
```
