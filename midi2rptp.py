#!/usr/bin/env python3
"""Convert a Standard MIDI File into a raypoketrack pattern (.rptp).

Like midi2rpt.py, but writes a single multi-track pattern instead of a whole
song — which is all you need now that patterns hold up to PATTERN_TRACKS (16)
tracks. Each monophonic voice becomes one track: each distinct drum pitch on the
GM drum channel, and each split chord-voice on melodic channels.

Pattern files don't carry instrument definitions (only a per-step instrument
index), so set up the matching instruments in your song — e.g. a drum instrument
at index 0, which is what GM-drum MIDI maps to here.

Usage:
  midi2rptp.py [--out PATTERN.rptp] [--steps-per-beat N] INPUT.mid

Shares the MIDI reader and voice-splitter with midi2rpt.py.
"""

import argparse
import os
import struct
import sys

from midi2rpt import (parse_midi, split_voices, NOTE_EMPTY, FX_EMPTY,
                      DRUM_CHANNEL, PATTERN_TRACKS, MAX_PATTERN_STEPS)


def build_tracks(midi_path, steps_per_beat=4):
    """Return (tracks, length): tracks is a list of (instrument_index, steps),
    steps a list of (note, velocity) or None; length is the shared step count."""
    notes, tpb, tempo, beats_per_bar = parse_midi(midi_path)
    if not notes:
        raise ValueError("no notes in %s" % midi_path)

    ticks_per_step = tpb / steps_per_beat
    bar_steps = steps_per_beat * beats_per_bar

    groups = {}
    for n in notes:
        groups.setdefault((n.track, n.channel), []).append(n)

    # Consistent instrument-index assignment: drums share index 0, each melodic
    # (channel, program) gets the next index (matches midi2rpt.py).
    inst_index = {}

    def get_instrument(key):
        if key not in inst_index:
            inst_index[key] = len(inst_index)
        return inst_index[key]

    voice_specs = []  # (instrument_index, [notes])
    for (track, channel) in sorted(groups):
        group = groups[(track, channel)]
        if channel == DRUM_CHANNEL:
            iidx = get_instrument(("drum",))
            by_pitch = {}
            for n in group:
                by_pitch.setdefault(n.pitch, []).append(n)
            order = sorted(by_pitch, key=lambda pit: min(x.start for x in by_pitch[pit]))
            for pit in order:
                voice_specs.append((iidx, by_pitch[pit]))
        else:
            program = group[0].program
            iidx = get_instrument(("mel", channel, program))
            for voice in split_voices(group):
                voice_specs.append((iidx, voice))

    max_step = 0
    for _, vnotes in voice_specs:
        for n in vnotes:
            max_step = max(max_step, round(n.start / ticks_per_step))
    length = ((max_step // bar_steps) + 1) * bar_steps
    length = max(bar_steps, min(length, MAX_PATTERN_STEPS))

    tracks = []
    for iidx, vnotes in voice_specs:
        steps = [None] * length
        for n in sorted(vnotes, key=lambda x: x.start):
            si = round(n.start / ticks_per_step)
            if 0 <= si < length:
                steps[si] = (n.pitch, max(1, min(127, n.velocity)))
        tracks.append((iidx, steps))

    if len(tracks) > PATTERN_TRACKS:
        sys.stderr.write("warning: %d voices exceed %d tracks; extra voices dropped\n"
                         % (len(tracks), PATTERN_TRACKS))
        tracks = tracks[:PATTERN_TRACKS]
    return tracks, length


def write_rptp(path, tracks, length):
    """Write an RPTP v2 pattern. Step layout: note, vel, inst, fx0, fxv0, fx1, fxv1."""
    out = bytearray(b"RPTP")
    out += struct.pack("<HHB", 2, length, PATTERN_TRACKS)  # version, len, ntracks
    for t in range(PATTERN_TRACKS):
        iidx, steps = tracks[t] if t < len(tracks) else (0, [])
        for si in range(length):
            cell = steps[si] if si < len(steps) else None
            if cell is None:
                out += struct.pack("<BBBBBBB", NOTE_EMPTY, 0, 0, FX_EMPTY, 0, FX_EMPTY, 0)
            else:
                note, vel = cell
                out += struct.pack("<BBBBBBB", note, vel, iidx, FX_EMPTY, 0, FX_EMPTY, 0)

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def convert(midi_path, out_path, steps_per_beat=4):
    tracks, length = build_tracks(midi_path, steps_per_beat)
    write_rptp(out_path, tracks, length)
    return len(tracks), length


def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert a MIDI file to a raypoketrack .rptp pattern")
    ap.add_argument("input", help="input .mid file")
    ap.add_argument("--out", help="output .rptp (default: input name with .rptp)")
    ap.add_argument("--steps-per-beat", type=int, default=4,
                    help="quantisation grid; 4 = sixteenth notes (default)")
    args = ap.parse_args(argv)

    out = args.out or os.path.splitext(args.input)[0] + ".rptp"
    ntracks, length = convert(args.input, out, steps_per_beat=args.steps_per_beat)
    print("wrote %s  (%d tracks, %d steps)" % (out, ntracks, length))


if __name__ == "__main__":
    main()
