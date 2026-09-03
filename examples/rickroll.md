# rickroll

"Never Gonna Give You Up" (Rick Astley), 113 BPM, Ab major.

This started life as a MIDI file run through `scripts/midi2rpt.py`. That script
does the only thing a general converter can do: it dumps every voice it finds
into the next free track, so you end up with four 1024-step patterns — the
whole song, once, on one song row. It plays back fine, but there is nothing to
learn from it, so this example was re-arranged by hand into the shape you'd
actually track a song in.

## What changed

* **One bar per pattern.** Every pattern is 16 steps and the song is 63 rows
  long, so a row on the SONG screen is a bar you can hear.
* **Repeats are repeats.** Bars that are identical share one pattern instead of
  being copied. The whole drum kit for the entire song is 6 patterns.
* **One role per song lane**, so a lane means something both up and down.
* **Fixed track assignments** inside each lane, so the kick is always on track 0
  of whatever drum pattern is playing, voice 1 of a chord is always on the same
  track, and so on.
* **Pattern numbers are banked by lane** — `00-3F` drums, `40-7F` bass,
  `80-BF` chords, `C0-FF` lead — so a pattern number tells you where it goes.
* **Velocities are snapped to a 0x10 grid.** Nobody types `8F` when they mean
  `90`, and the rounding is what lets bars that differed only by MIDI jitter
  collapse onto one pattern.
* **Instruments are named and panned.** Twelve of them, one per part.
* **The drum lane carries no note-offs.** Percussion is one-shot — the `.rptp`
  kits in `beats/` don't use them either, and the engine cuts a track's previous
  note when the next one lands. Sustaining parts keep theirs.
* Four MIDI voices that doubled another instrument note-for-note were dropped:
  a synth-string pad playing the e.piano chords, a second saw in unison with
  the first, a piano doubling the backing vocals, and a synth-drum doubling the
  bell an octave down.

## Lanes and their tracks

| Lane | Role | Patterns | Tracks |
| --- | --- | --- | --- |
| 0 | DRUMS | `00-05` | `0` kick · `1` snare · `2` clap · `3` closed hat · `4` open hat · `5` shaker · `6` cabasa · `7` maracas · `8` conga mute · `9` conga open · `A` side stick |
| 1 | BASS | `40-52` | `0` bass · `1` muted guitar low · `2` muted guitar high |
| 2 | CHORDS | `80-92` | `0-3` e.piano · `4-5` strings riff · `6-9` saw stab · `A-D` clean guitar |
| 3 | LEAD | `C0-EB` | `0-1` lead vocal · `2-4` backing "aahs" · `5-6` backing "oohs" · `7-8` brass · `9` bell |

A chord lives on consecutive tracks, one voice per track, lowest first — the
tracker is monophonic per track, so that's how you get four notes at once.

## Arrangement

| Pos | DRUMS | BASS | CHORDS | LEAD | Section |
| --- | --- | --- | --- | --- | --- |
| 00 | 00 | -- | -- | C0 | Pickup fill (drums + bell only) |
| 01 | 01 | 40 | 80 | C1 | Intro riff — bass, e.piano, strings |
| 02 | 02 | 41 | 81 | -- |  |
| 03 | 01 | 42 | 82 | -- |  |
| 04 | 02 | 43 | 83 | C2 | Brass answers |
| 05 | 01 | 40 | 84 | C3 | "Oohs" enter |
| 06 | 02 | 41 | 81 | C4 |  |
| 07 | 01 | 42 | 85 | -- |  |
| 08 | 03 | 43 | 86 | C5 | Fill into the verse |
| 09 | 01 | 44 | 87 | C6 | **Verse 1** — lead vocal enters |
| 0A | 02 | 45 | 88 | C7 |  |
| 0B | 01 | 46 | 89 | C8 |  |
| 0C | 02 | 47 | 8A | C9 | Saw stab accent |
| 0D | 01 | 44 | 87 | CA | Verse repeats — bass and chords reuse the same four patterns |
| 0E | 02 | 45 | 88 | CB |  |
| 0F | 01 | 46 | 89 | CC |  |
| 10 | 02 | 47 | 8A | CD |  |
| 11 | 01 | 44 | 8B | CE | **Pre-chorus** — e.piano drops out, saw stabs carry it |
| 12 | 02 | 45 | 8C | CF |  |
| 13 | 01 | 48 | 8B | D0 |  |
| 14 | 04 | 49 | 8D | D1 | Fill into the chorus |
| 15 | 01 | 40 | 80 | D2 | **Chorus 1** — backing "aahs" in, intro riff returns |
| 16 | 02 | 41 | 81 | D3 |  |
| 17 | 01 | 42 | 82 | D4 |  |
| 18 | 02 | 43 | 83 | D5 |  |
| 19 | 01 | 40 | 84 | D6 |  |
| 1A | 02 | 41 | 81 | D7 |  |
| 1B | 01 | 42 | 8E | D8 |  |
| 1C | 03 | 43 | 8F | D9 | Fill out of the chorus |
| 1D | 01 | 44 | 87 | DA | **Verse 2** — same bass/chord loop as verse 1 |
| 1E | 02 | 45 | 88 | DB |  |
| 1F | 01 | 46 | 89 | DC |  |
| 20 | 02 | 47 | 8A | DD |  |
| 21 | 01 | 44 | 87 | DE |  |
| 22 | 02 | 45 | 88 | CB | Lead bar reused from verse 1 |
| 23 | 01 | 46 | 89 | DF |  |
| 24 | 02 | 47 | 8A | E0 |  |
| 25 | 01 | 4A | 8B | E1 | **Pre-chorus 2** — muted guitar joins the bass |
| 26 | 02 | 4B | 8C | E2 |  |
| 27 | 01 | 4C | 8B | E3 |  |
| 28 | 05 | 4D | 90 | E4 | Big fill |
| 29 | 01 | 4E | 80 | D2 | **Chorus 2** |
| 2A | 02 | 41 | 81 | D3 |  |
| 2B | 01 | 42 | 82 | D4 |  |
| 2C | 02 | 43 | 83 | D5 |  |
| 2D | 01 | 40 | 84 | D6 |  |
| 2E | 02 | 41 | 81 | D7 |  |
| 2F | 01 | 42 | 8E | D8 |  |
| 30 | 03 | 4F | 8F | E5 |  |
| 31 | 01 | 40 | 80 | E6 | **Chorus 3** — straight into it |
| 32 | 02 | 41 | 81 | D3 |  |
| 33 | 01 | 42 | 82 | D4 |  |
| 34 | 02 | 43 | 83 | D5 |  |
| 35 | 01 | 40 | 84 | D6 |  |
| 36 | 02 | 41 | 81 | D7 |  |
| 37 | 01 | 42 | 8E | D8 |  |
| 38 | 03 | 4F | 8F | E7 |  |
| 39 | 01 | 50 | 91 | E8 | **Outro** — clean guitar and "oohs" take over |
| 3A | 02 | 51 | 92 | E9 |  |
| 3B | 01 | 52 | 91 | EA |  |
| 3C | 02 | 51 | 92 | E9 |  |
| 3D | 01 | 52 | 91 | EB |  |
| 3E | 02 | 51 | 92 | E9 | Loops back to 00 |

The drum lane is the clearest illustration of why this is worth doing: `01` and
`02` alternate for most of the song — 56 of the 63 rows between them — `03` and
`05` are fills, `04` is the bar that walks into a chorus, and `00` is the
pickup. Six patterns cover the whole track.

## Things to try

* Mute a lane on the SONG screen and hear what each one is carrying.
* Edit drum pattern `01` — it plays on 31 rows (and `02` on another 25), so one
  change re-grooves most of the song at once. That's the payoff for
  deduplicating.
* Swap the SF2 presets. Every instrument is one `sf2` unit against
  `microgm.sf2`; changing P0 PRESET is the fastest way to re-skin the track.
* The lead lane is the one place patterns barely repeat — a sung melody is
  genuinely different every bar, and that's fine. Not everything deduplicates.
