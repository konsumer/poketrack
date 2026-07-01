#!/usr/bin/env python3
"""Convert a Standard MIDI File into a raypoketrack song (.rpt).

See FORMATS.md for the binary layout. No third-party dependencies.

Rules:
  * Each MIDI program/instrument becomes a tracker instrument. MIDI channel 10
    (the GM drum channel) becomes a single drum instrument that selects the
    soundfont's percussion bank (bank 128).
  * Each track is monophonic (one note per step), so any polyphony is split
    across tracks: each drum lane (distinct pitch) gets its own track, and
    melodic chords are split into one track per simultaneous voice. Up to
    PATTERN_TRACKS (16) voices are packed into one multi-track pattern; further
    voices spill into additional patterns placed on additional song lanes.
  * A soundfont is optional. When given, every instrument is wired to an `sf2`
    unit whose data path is stored relative to the output file.

Usage:
  midi2rpt.py [--soundfont FONT.sf2] [--out SONG.rpt] [--steps-per-beat N] INPUT.mid
"""

import argparse
import os
import struct
import sys

# --- tracker constants (see FORMATS.md / src/tracker.c) ---
NOTE_EMPTY = 0x00
NOTE_OFF = 0xFE
FX_EMPTY = 0xFF
SONG_CHANNELS = 4       # arrangement lanes
PATTERN_TRACKS = 16     # tracks per pattern
CHAIN_MAX = 8
UNIT_ID_LEN = 8
UNIT_MAX_PARAMS = 8
MAX_PATTERN_STEPS = 1024
DRUM_CHANNEL = 9  # 0-based; MIDI channel 10
DEFAULT_TEMPO = 500000  # us per quarter note -> 120 BPM


# ---------------------------------------------------------------------------
# Minimal Standard MIDI File reader
# ---------------------------------------------------------------------------

class Note:
    __slots__ = ("track", "channel", "program", "pitch", "velocity", "start", "dur")

    def __init__(self, track, channel, program, pitch, velocity, start, dur):
        self.track = track
        self.channel = channel
        self.program = program
        self.pitch = pitch
        self.velocity = velocity
        self.start = start
        self.dur = dur


def _read_vlq(data, i):
    val = 0
    while True:
        b = data[i]
        i += 1
        val = (val << 7) | (b & 0x7F)
        if not b & 0x80:
            return val, i


def parse_midi(path):
    """Return (notes, ticks_per_beat, tempo_us, beats_per_bar)."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file (missing MThd)")
    _, ntrk, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported")
    tpb = division

    notes = []
    tempo = DEFAULT_TEMPO
    beats_per_bar = 4
    got_tempo = False
    got_timesig = False

    p = 14
    for track_idx in range(ntrk):
        if data[p:p + 4] != b"MTrk":
            raise ValueError("expected MTrk chunk")
        length = struct.unpack(">I", data[p + 4:p + 8])[0]
        body = data[p + 8:p + 8 + length]
        p += 8 + length

        i = 0
        now = 0
        status = 0
        program = [0] * 16            # current program per channel
        active = {}                   # (channel, pitch) -> (start_tick, velocity, program)
        while i < len(body):
            dt, i = _read_vlq(body, i)
            now += dt
            b = body[i]
            if b & 0x80:
                status = b
                i += 1
            # else running status: reuse previous status byte
            evt = status & 0xF0
            chan = status & 0x0F

            if status == 0xFF:                 # meta
                meta = body[i]; i += 1
                ln, i = _read_vlq(body, i)
                payload = body[i:i + ln]; i += ln
                if meta == 0x51 and len(payload) == 3 and not got_tempo:
                    tempo = (payload[0] << 16) | (payload[1] << 8) | payload[2]
                    got_tempo = True
                elif meta == 0x58 and len(payload) >= 2 and not got_timesig:
                    num = payload[0]
                    den = 2 ** payload[1]
                    # beats here are quarter notes (the tracker grid unit)
                    beats_per_bar = max(1, round(num * 4 / den))
                    got_timesig = True
            elif status in (0xF0, 0xF7):       # sysex
                ln, i = _read_vlq(body, i)
                i += ln
            elif evt in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1 = body[i]; d2 = body[i + 1]; i += 2
                if evt == 0x90 and d2 > 0:
                    active[(chan, d1)] = (now, d2, program[chan])
                elif evt == 0x80 or (evt == 0x90 and d2 == 0):
                    key = (chan, d1)
                    if key in active:
                        start, vel, prog = active.pop(key)
                        notes.append(Note(track_idx, chan, prog, d1, vel, start, now - start))
            elif evt in (0xC0, 0xD0):          # program change / channel pressure
                d1 = body[i]; i += 1
                if evt == 0xC0:
                    program[chan] = d1
            else:
                i += 1  # unknown / unhandled, skip a byte defensively

        # close any notes left hanging at end of track
        for (chan, pitch), (start, vel, prog) in active.items():
            notes.append(Note(track_idx, chan, prog, pitch, vel, start, max(1, now - start)))

    return notes, tpb, tempo, beats_per_bar


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

class Instrument:
    def __init__(self, name, bank, preset):
        self.name = name
        self.bank = bank
        self.preset = preset


def split_voices(notes):
    """Split a list of notes (one MIDI track+channel group) into monophonic
    voices. Returns a list of voice-note-lists, ordered by first onset."""
    notes = sorted(notes, key=lambda n: (n.start, n.pitch))
    voices = []          # list of [free_at_tick, notes]
    for n in notes:
        placed = False
        for v in voices:
            if v[0] <= n.start:
                v[0] = n.start + max(1, n.dur)
                v[1].append(n)
                placed = True
                break
        if not placed:
            voices.append([n.start + max(1, n.dur), [n]])
    return [v[1] for v in voices]


# ---------------------------------------------------------------------------
# Binary writer
# ---------------------------------------------------------------------------

def _strn(s, n):
    b = s.encode("latin1", "replace")[:n]
    return b + b"\x00" * (n - len(b))


def write_rpt(path, name, bpm, patterns, instruments, pattern_len, sf_relpath):
    """patterns: list of patterns; each pattern is a list of up to PATTERN_TRACKS
    tracks, where a track is (instrument_index, steps) and steps is a list of
    (note, velocity) or None. Pattern pi is placed on song lane pi in row 0."""
    chunks = []

    # META
    meta = _strn(name, 32)
    meta += struct.pack("<BBBBBBBB", bpm & 0xFF, 0, 0, 0, 1, (bpm >> 8) & 0xFF, 0, 0)
    chunks.append((b"META", meta))

    # SONG (single row, one pattern per lane)
    song = struct.pack("<H", 1)
    row = bytearray([0xFF] * SONG_CHANNELS)
    for ch in range(min(len(patterns), SONG_CHANNELS)):
        row[ch] = ch
    song += bytes(row)
    chunks.append((b"SONG", song))

    # PATN — each pattern has PATTERN_TRACKS tracks (empty ones written as rests)
    patn = bytearray()
    patn += struct.pack("<H", len(patterns))
    for pi, tracks in enumerate(patterns):
        patn += struct.pack("<BHB", pi, pattern_len, PATTERN_TRACKS)
        for t in range(PATTERN_TRACKS):
            iidx, steps = tracks[t] if t < len(tracks) else (0, [])
            for si in range(pattern_len):
                cell = steps[si] if si < len(steps) else None
                if cell is None:
                    patn += struct.pack("<BBBBBBB", NOTE_EMPTY, 0, 0,
                                        FX_EMPTY, FX_EMPTY, 0, 0)
                else:
                    note, vel = cell
                    patn += struct.pack("<BBBBBBB", note, vel, iidx,
                                        FX_EMPTY, FX_EMPTY, 0, 0)
    chunks.append((b"PATN", bytes(patn)))

    # INST
    inst = bytearray()
    inst += struct.pack("<H", len(instruments))
    for ii, instrument in enumerate(instruments):
        inst += struct.pack("<B", ii)
        inst += _strn(instrument.name, 16)
        inst += _strn("", 128)        # midi_in_device
        inst += struct.pack("<B", 0)  # midi_in_channel
        for slot in range(CHAIN_MAX):
            if slot == 0 and sf_relpath is not None:
                inst += _strn("sf2", UNIT_ID_LEN)
                inst += struct.pack("<B", 1)  # enabled
                # PRST, BANK, VOL, PAN, TRAN, TUNE, -, -
                params = [instrument.preset, instrument.bank, 200, 128, 128, 128, 0, 0]
                inst += bytes(params[:UNIT_MAX_PARAMS])
                inst += b"\xFF" * UNIT_MAX_PARAMS  # cc_map: all unmapped
                data = sf_relpath.encode("latin1", "replace")
                inst += struct.pack("<H", len(data)) + data
            else:
                inst += _strn("", UNIT_ID_LEN)
                inst += struct.pack("<B", 0)
                inst += b"\x00" * UNIT_MAX_PARAMS
                inst += b"\xFF" * UNIT_MAX_PARAMS
                inst += struct.pack("<H", 0)
    chunks.append((b"INST", bytes(inst)))

    out = bytearray(b"RPT2")
    out += struct.pack("<HH", 2, len(chunks))  # version 2: multi-track patterns
    for tag, payload in chunks:
        out += tag + struct.pack("<I", len(payload)) + payload

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


# ---------------------------------------------------------------------------

def convert(midi_path, out_path, soundfont=None, steps_per_beat=4, name=None):
    notes, tpb, tempo, beats_per_bar = parse_midi(midi_path)
    if not notes:
        raise ValueError("no notes in %s" % midi_path)

    ticks_per_step = tpb / steps_per_beat
    bar_steps = steps_per_beat * beats_per_bar

    # group/voice/instrument assignment
    groups = {}
    for n in notes:
        groups.setdefault((n.track, n.channel), []).append(n)

    instruments = []
    inst_index = {}

    def get_instrument(key, iname, bank, preset):
        if key not in inst_index:
            inst_index[key] = len(instruments)
            instruments.append(Instrument(iname, bank, preset))
        return inst_index[key]

    voice_specs = []  # (instrument_index, [notes])
    for (track, channel) in sorted(groups):
        group = groups[(track, channel)]
        if channel == DRUM_CHANNEL:
            iidx = get_instrument(("drum",), "Drums", 128, 0)
            by_pitch = {}
            for n in group:
                by_pitch.setdefault(n.pitch, []).append(n)
            order = sorted(by_pitch, key=lambda pit: min(x.start for x in by_pitch[pit]))
            for pit in order:
                voice_specs.append((iidx, by_pitch[pit]))
        else:
            program = group[0].program
            iidx = get_instrument(("mel", channel, program),
                                  "INST%02d" % program, 0, program)
            for voice in split_voices(group):
                voice_specs.append((iidx, voice))

    # find song length in steps
    max_step = 0
    for _, vnotes in voice_specs:
        for n in vnotes:
            max_step = max(max_step, round(n.start / ticks_per_step))
    length = ((max_step // bar_steps) + 1) * bar_steps
    length = max(bar_steps, min(length, MAX_PATTERN_STEPS))

    # render each voice into a monophonic track
    tracks = []
    for iidx, vnotes in voice_specs:
        steps = [None] * length
        for n in sorted(vnotes, key=lambda x: x.start):
            si = round(n.start / ticks_per_step)
            if 0 <= si < length:
                steps[si] = (n.pitch, max(1, min(127, n.velocity)))
        tracks.append((iidx, steps))

    # pack tracks into multi-track patterns (PATTERN_TRACKS each), across song lanes
    max_voices = SONG_CHANNELS * PATTERN_TRACKS
    if len(tracks) > max_voices:
        sys.stderr.write(
            "warning: %d voices exceed %d (=%d lanes x %d tracks); extra voices dropped\n"
            % (len(tracks), max_voices, SONG_CHANNELS, PATTERN_TRACKS))
        tracks = tracks[:max_voices]
    patterns = [tracks[i:i + PATTERN_TRACKS]
                for i in range(0, len(tracks), PATTERN_TRACKS)]
    if not patterns:
        patterns = [[]]

    bpm = max(1, min(65535, round(60_000_000 / tempo)))

    sf_relpath = None
    if soundfont:
        sf_relpath = os.path.relpath(os.path.abspath(soundfont),
                                     os.path.dirname(os.path.abspath(out_path)))

    if name is None:
        name = os.path.splitext(os.path.basename(out_path))[0]

    write_rpt(out_path, name, bpm, patterns, instruments, length, sf_relpath)
    return len(patterns), len(instruments), length, bpm


def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert a MIDI file to a raypoketrack .rpt song")
    ap.add_argument("input", help="input .mid file")
    ap.add_argument("--soundfont", help="soundfont (.sf2); stored relative to --out")
    ap.add_argument("--out", help="output .rpt (default: input name with .rpt)")
    ap.add_argument("--steps-per-beat", type=int, default=4,
                    help="quantisation grid; 4 = sixteenth notes (default)")
    ap.add_argument("--name", help="song name (default: output filename)")
    args = ap.parse_args(argv)

    out = args.out or os.path.splitext(args.input)[0] + ".rpt"
    npat, ninst, length, bpm = convert(
        args.input, out, soundfont=args.soundfont,
        steps_per_beat=args.steps_per_beat, name=args.name)
    print("wrote %s  (%d patterns, %d instruments, %d steps, %d bpm)"
          % (out, npat, ninst, length, bpm))


if __name__ == "__main__":
    main()
