# File Formats

raypoketrack reads and writes three binary file types. All are little-endian.
Strings are fixed-width, NUL-padded, and not necessarily NUL-terminated when
they fill their field (readers always cap them).

Reference implementation: `src/tracker.c` (`tracker_save*` / `tracker_load*`).

## Primitive types

| Name      | Bytes | Notes                                              |
|-----------|-------|----------------------------------------------------|
| `u8`      | 1     | unsigned byte                                      |
| `u16`     | 2     | little-endian                                      |
| `u32`     | 4     | little-endian                                      |
| `str[n]`  | n     | raw bytes, NUL-padded; reader truncates to `n-1`+`\0` |

### Note values (`u8`)

Notes are standard MIDI note numbers. The octave shown in the UI is
`note/12 - 1`, so:

| Value | Name  | Meaning                          |
|-------|-------|----------------------------------|
| `0x00`| `---` | empty / no note (`NOTE_EMPTY`)   |
| `60`  | `C-4` | middle-ish C (`MIDI 60 = 0x3C`)  |
| `0xFE`| `OFF` | note-off (`NOTE_OFF`)            |
| 1..253| note  | MIDI pitch, `name = NAMES[v%12]`, `oct = v/12 - 1` |

Pitch names: `C- C# D- D# E- F- F# G- G# A- A# B-`.

### Effect ids (`u8`, `fx[]` columns)

`0xFF` (`TRACKER_EMPTY`) means "no effect" — note that the *empty* value is
`0xFF`, not `0`. Otherwise:

| Id | Mnemonic | Id | Mnemonic |
|----|----------|----|----------|
| 0  | NONE     | 6  | RET      |
| 1  | ARP      | 7  | TEM      |
| 2  | CHA      | 8  | TSP      |
| 3  | DEL      | 9  | VIB      |
| 4  | HOP      | 10 | VOL      |
| 5  | KIL      |    |          |

`fxv[]` is the parameter byte for the matching `fx[]` entry.

---

## Pattern — `.rptp` (`RPTP`)

A single pattern: `PATTERN_TRACKS = 16` parallel tracks, each a list of steps
holding one note + velocity + instrument index + two effect columns. All tracks
share one length. Written by `tracker_save_pattern`.

```
"RPTP"            4 bytes   magic
version           u16       currently 2 (v1 was single-track)
len               u16       step count (1..1024), shared by all tracks
ntracks           u8        track count (currently PATTERN_TRACKS = 16)
tracks[ntracks]:
    steps[len]:
        note      u8        MIDI note (see Note values)
        velocity  u8        0..127
        instrument u8       index into the song instrument table (0..255)
        fx[0]     u8        effect id  (0xFF = none)
        fxv[0]    u8        effect param
        fx[1]     u8        effect id  (0xFF = none)
        fxv[1]    u8        effect param
```

Each step is 7 bytes. There are always `FX_PER_STEP = 2` effect columns. A reader
that finds `ntracks > PATTERN_TRACKS` consumes but discards the extra tracks.

> Note ordering differs from the in-song `PATN` chunk: the standalone `.rptp`
> writes `fx[0], fxv[0], fx[1], fxv[1]`, whereas `PATN` writes
> `fx[0], fx[1], fxv[0], fxv[1]`. Match the layout for the file you are writing.

---

## Instrument — `.rpti` (`RPTI`)

A single instrument: a name, optional MIDI-input binding, and a chain of up to
`CHAIN_MAX = 8` unit slots (sources first, then effects). Written by
`tracker_save_instrument`.

```
"RPTI"                4 bytes    magic
version               u16        currently 1
name                  str[16]
midi_in_device        str[128]   "" = no MIDI input
midi_in_channel       u8         0 = all channels, 1..16 = specific
chain[8]:
    unit_id           str[8]     "" = empty slot (e.g. "drum","sampler","sf2")
    enabled           u8         0 / 1
    params[8]         u8 x 8     unit param values (UNIT_MAX_PARAMS)
    cc_map[8]         u8 x 8     MIDI CC per param; 0xFF = unmapped
    data_len          u16        length of data string that follows
    data[data_len]    bytes      extra data (e.g. SF2/sample path), no NUL
```

Absolute `data` paths are rewritten relative to the file's directory on save
and resolved back on load.

Available `unit_id`s (from `src/units/`): `osc`, `fm`, `gran`, `drum`,
`sampler`, `sf2`, `sfz`, `clap` (sources); `dist`, `bcrush`, `delay`, `comp`,
`flanger`, `filter`, `reverb`, `ducker`, `lfo`, `midi`, `phaser`, `tremolo`,
`chorus` (effects).

---

## Song — `.rpt` (`RPT2`)

A full project. Header followed by a count of chunks; each chunk is a 4-byte
tag, a `u32` byte length, then its payload. Unknown chunks can be skipped using
the length. Written by `tracker_save`.

```
"RPT2"            4 bytes   magic
version           u16       currently 1
num_sections      u16       number of chunks that follow

repeated num_sections times:
    tag           str[4]    "META" | "SONG" | "PATN" | "INST"
    size          u32       payload byte length
    payload       size bytes
```

### META

```
name              str[32]
bpm_lo            u8        low byte of BPM
swing             u8
scale_root        u8        0=C .. 11=B
scale_idx         u8        0=chromatic, see SCALES[]
loop              u8        0 / 1
bpm_hi            u8        high byte of BPM (old readers treat as padding)
pad               u8 x 2
```

`bpm = bpm_lo | (bpm_hi << 8)`; if 0, defaults to 120.

### SONG

The arrangement grid: `song_len` rows of `SONG_CHANNELS = 16` channels, stored
row-major. Each cell is a pattern index, `0xFF` (`TRACKER_EMPTY`) = empty.

```
song_len          u16       1..1024
cells:                      song_len * 16 bytes, row-major
    pattern_idx   u8        for each (row, channel); 0xFF = empty
```

### PATN

Only non-trivial patterns are stored (a pattern is skipped if it has no data
and still has the default length of 16).

```
count             u16
patterns[count]:
    index         u8        pattern slot (0..254)
    len           u16       step count
    steps[len]:
        note      u8
        velocity  u8
        instrument u8
        fx[0]     u8        (note: fx,fx,fxv,fxv order here)
        fx[1]     u8
        fxv[0]    u8
        fxv[1]    u8
```

### INST

Only non-default instruments are stored. Per-instrument layout matches the
`.rpti` body (without the `RPTI`/version header), prefixed by its index:

```
count             u16
instruments[count]:
    index         u8        instrument slot (0..255)
    name          str[16]
    midi_in_device str[128]
    midi_in_channel u8
    chain[8]:
        unit_id   str[8]
        enabled   u8
        params[8] u8 x 8
        cc_map[8] u8 x 8
        data_len  u16
        data[data_len] bytes
```

### Defaults (from `tracker_init`)

- All song cells: `0xFF` (empty)
- All pattern steps: `fx[0]=fx[1]=0xFF`, everything else 0
- Pattern length: 16
- Instrument names: `INST00`, `INST01`, … (hex); `cc_map` all `0xFF`
- BPM 120, swing 0, loop on
