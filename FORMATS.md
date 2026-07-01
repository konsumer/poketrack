# File Formats

raypoketrack reads and writes three binary file types. All multi-byte integers
are little-endian. Strings are fixed-width, NUL-padded, not necessarily
NUL-terminated when they fill their field (readers always cap them).

Reference implementation: `src/tracker.c` (`tracker_save*` / `tracker_load*`).

---

## Primitive types

| Name      | Bytes | Notes                                                  |
|-----------|-------|--------------------------------------------------------|
| `u8`      | 1     | unsigned byte                                          |
| `u16`     | 2     | unsigned, little-endian                                |
| `u32`     | 4     | unsigned, little-endian                                |
| `str[n]`  | n     | raw bytes, NUL-padded; reader truncates to `n-1` + `\0` |

### Note values (`u8`)

| Value    | Name  | Meaning                                         |
|----------|-------|-------------------------------------------------|
| `0x00`   | `---` | empty / no note (`NOTE_EMPTY`)                  |
| `0x01`–`0xFD` | note | MIDI pitch; name = `NAMES[v%12]`, oct = `v/12 - 1` |
| `0x3C`   | `C-4` | middle C (MIDI 60)                              |
| `0xFE`   | `OFF` | note-off (`NOTE_OFF`)                           |

Pitch names in order: `C- C# D- D# E- F- F# G- G# A- A# B-`.

### Per-step param overrides (`fx` / `fxv`)

`fx[i]` is a **global param index** into the instrument's unit chain, not a
named effect type. The chain slots are numbered in order; their params are
concatenated into a flat index space:

- Slot 0 owns param indices `0 .. nparams0-1`
- Slot 1 owns `nparams0 .. nparams0+nparams1-1`
- etc.

`fx[i] = 0xFF` (`TRACKER_EMPTY`) means "no override for this slot".
Otherwise the audio engine walks the chain, counts params, finds the owning
slot, and sets that param to `fxv[i]`.

`fxv[i]` is the raw `u8` value written to the targeted param. Both `fx` and
`fxv` are displayed as two-digit hex in the UI.

---

## Functional overview

**Pattern** (`.rptp`): a grid of steps. 16 parallel tracks (labeled `0`–`F` in
hex), each with the same variable step count (1–1024). Each step holds one
note, velocity, instrument index, and two effect columns. The 16 tracks are
independent and share only the step count.

**Song** (`.rpt`): a full project. Contains a META header, a SONG arrangement
grid, a PATN block (non-trivial patterns only), and an INST block
(non-default instruments only).

**Song arrangement** (SONG chunk): a grid of `song_len` rows × 4 lanes.
Each cell holds one pattern index (which multi-track pattern plays in that
lane at that row), or `0xFF` for empty. Both `song_len` (1–1024 rows) and
each pattern's step count (1–1024 steps) are independently variable.

---

## Pattern — `.rptp` (`RPTP`)

A single pattern: 16 tracks (indices `0x0`–`0xF`), all sharing one step
count. Written by `tracker_save_pattern`.

```
Offset  Size  Field         Notes
──────  ────  ─────         ─────
0       4     magic         "RPTP"
4       2     version       u16; currently 2 (v1 = single-track)
6       2     len           u16; step count, 1..1024, shared by all tracks
8       1     ntracks       u8; currently 16 (PATTERN_TRACKS)
9       …     tracks        ntracks × len × 7 bytes total (track-major)

Per track (all len steps of track t before track t+1):
  Per step (7 bytes):
    note        u8    MIDI note (see Note values)
    velocity    u8    0..127
    instrument  u8    index into song instrument table (0..255)
    fx[0]       u8    global param index into instrument chain (0xFF = no override)
    fxv[0]      u8    value to write to that param
    fx[1]       u8    global param index into instrument chain (0xFF = no override)
    fxv[1]      u8    value to write to that param
```

**Total size:** `9 + ntracks × len × 7` bytes.

A reader that finds `ntracks > 16` consumes but discards the extra tracks.

---

## Instrument — `.rpti` (`RPTI`)

A single instrument: a name, optional MIDI-input binding, and a chain of up
to 8 unit slots (sources first, then effects). Written by
`tracker_save_instrument`.

```
Offset  Size   Field              Notes
──────  ────   ─────              ─────
0       4      magic              "RPTI"
4       2      version            u16; currently 1
6       16     name               str[16]
22      128    midi_in_device     str[128]; "" = no MIDI input
150     1      midi_in_channel    u8; 0 = all channels, 1..16 = specific

Per chain slot (8 slots, fixed; each slot = variable size):
  unit_id       str[8]    "" = empty slot; e.g. "drum", "sampler", "sf2"
  enabled       u8        0 / 1
  params[8]     u8 × 8    unit param values (UNIT_MAX_PARAMS = 8)
  cc_map[8]     u8 × 8    MIDI CC per param; 0xFF = unmapped
  data_len      u16       byte length of data string that follows
  data          bytes     data_len bytes; extra data (e.g. SF2/sample path), no NUL
```

Absolute `data` paths are rewritten relative to the file's directory on save
and resolved back on load.

**Fixed header size before chain:** 151 bytes. Each chain slot occupies
`8 + 1 + 8 + 8 + 2 + data_len` bytes.

Available `unit_id` values (from `src/units/`):

- Sources: `osc`, `fm`, `gran`, `drum`, `sampler`, `sf2`, `sfz`, `clap`
- Effects: `dist`, `bcrush`, `delay`, `comp`, `flanger`, `filter`, `reverb`,
  `ducker`, `lfo`, `midi`, `phaser`, `tremolo`, `chorus`

---

## Song — `.rpt` (`RPT2`)

A full project. Header followed by a count of chunks; each chunk is a 4-byte
tag, a `u32` byte length, then its payload. Unknown chunks may be skipped
using the length field. Written by `tracker_save`.

```
Offset  Size  Field          Notes
──────  ────  ─────          ─────
0       4     magic          "RPT2"
4       2     version        u16; currently 2 (v1 = single-track patterns)
6       2     num_sections   u16; number of chunks that follow

Repeated num_sections times:
  tag     str[4]   "META" | "SONG" | "PATN" | "INST"
  size    u32      payload byte length
  payload size bytes
```

### META chunk

```
Offset  Size  Field        Notes
──────  ────  ─────        ─────
0       32    name         str[32]
32      1     bpm_lo       u8; low byte of BPM
33      1     swing        u8
34      1     scale_root   u8; 0=C .. 11=B
35      1     scale_idx    u8; 0=chromatic, see SCALES[]
36      1     loop         u8; 0 / 1
37      1     bpm_hi       u8; high byte of BPM (old readers treat as padding)
38      2     pad          u8 × 2
```

`bpm = bpm_lo | (bpm_hi << 8)`; if 0, defaults to 120. **Total: 40 bytes.**

### SONG chunk

The arrangement grid: `song_len` rows × 4 lanes (`SONG_CHANNELS = 4`),
stored row-major (all 4 lane values for row 0, then row 1, etc.). Each cell
is a pattern index (`0x00`–`0xFE`) or `0xFF` (`TRACKER_EMPTY`) for empty.

`song_len` is variable: 1..1024.

```
Offset  Size             Field          Notes
──────  ────             ─────          ─────
0       2                song_len       u16; active row count, 1..1024
2       song_len × 4     cells          u8, row-major
                                        [row0_lane0, row0_lane1, row0_lane2, row0_lane3,
                                         row1_lane0, …]
```

**Total: `2 + song_len × 4` bytes.**

### PATN chunk

Only non-trivial patterns are stored. A pattern is omitted when it has no
note/effect data AND its step count equals the default (16).

Pattern indices run `0x00`–`0xFE` (255 slots); `0xFF` = empty/unused.
Each pattern has 16 tracks (labeled `0`–`F` in hex), all sharing one step
count (variable, 1–1024).

Step layout within PATN differs from `.rptp`: fx and fxv values are
separated (`fx[0], fx[1], fxv[0], fxv[1]`) rather than interleaved.

```
Offset  Size  Field    Notes
──────  ────  ─────    ─────
0       2     count    u16; number of patterns stored

Per pattern (count times):
  index     u8    pattern slot, 0x00..0xFE
  len       u16   step count, 1..1024, shared by all tracks
  ntracks   u8    track count; currently 16 (PATTERN_TRACKS)

  Per track (track-major: all len steps of track 0, then track 1, …):
    Per step (7 bytes):
      note        u8    MIDI note
      velocity    u8    0..127
      instrument  u8    instrument index
      fx[0]       u8    global param index into instrument chain (0xFF = no override)
      fx[1]       u8    global param index into instrument chain (0xFF = no override)
      fxv[0]      u8    value to write to param fx[0]
      fxv[1]      u8    value to write to param fx[1]
```

**Per stored pattern size:** `4 + ntracks × len × 7` bytes.

> **fx byte order differs between file types:**
> - `.rptp`: interleaved — `fx[0], fxv[0], fx[1], fxv[1]` per step
> - `.rpt` PATN: separated — `fx[0], fx[1], fxv[0], fxv[1]` per step

### INST chunk

Only non-default instruments are stored. Per-instrument layout matches the
`.rpti` body (no `RPTI`/version header), prefixed by its slot index.

```
Offset  Size  Field    Notes
──────  ────  ─────    ─────
0       2     count    u16; number of instruments stored

Per instrument (count times):
  index             u8        slot, 0..255
  name              str[16]
  midi_in_device    str[128]
  midi_in_channel   u8
  chain[8]:
    unit_id         str[8]
    enabled         u8
    params[8]       u8 × 8
    cc_map[8]       u8 × 8
    data_len        u16
    data            data_len bytes
```

---

## Defaults (from `tracker_init`)

| Field              | Default                               |
|--------------------|---------------------------------------|
| Song cells         | `0xFF` (empty)                        |
| Song length        | 1 row                                 |
| Pattern step count | 16                                    |
| Pattern steps      | note=0, velocity=0, instrument=0, fx=0xFF, fxv=0 |
| Instrument names   | `INST00`, `INST01`, … (zero-padded hex) |
| cc_map             | all `0xFF` (unmapped)                 |
| BPM                | 120                                   |
| Swing              | 0                                     |
| Loop               | on                                    |
