#!/usr/bin/env python3
"""Turn a phoneme sequence into a poketrack pattern (.rptp) for the
ROBOTALK CLAP instrument (this directory).

This is the "play phonemes" half of speaking text -- deliberately
separate from text2phonemes.py's "text to phonemes" half, so you can feed
it phonemes from anywhere (this script's own text2phonemes.py, a
hand-written list, some other G2P tool).

Pattern files don't carry instrument definitions (only a per-step
instrument index) -- set up a ROBOTALK instrument at the index you pass
(default 0) in your song first, same convention midi2rptp.py uses for
drums.

Usage:
  phonemes2rptp.py "hello world" --out hello.rptp
  echo "HH AH L OW" | phonemes2rptp.py --phonemes --out hello.rptp
"""

import argparse
import os
import struct
import sys

from text2phonemes import text_to_phonemes

# --- tracker constants (see FORMATS.md / src/tracker.c) ---
NOTE_EMPTY = 0x00
FX_EMPTY = 0xFF
PATTERN_TRACKS = 16
MAX_PATTERN_STEPS = 1024

# Note -> phoneme layout, matching src/phonemes.rs
# exactly (same order, same base note) -- no separate translation table.
BASE_NOTE = 36
PHONEME_ORDER = [
    "IY", "IH", "EH", "AE", "AA", "AH", "ER", "AO", "UH", "UW",
    "EY", "AY", "AW", "OY", "OW",
    "L", "R", "W", "Y",
    "M", "N", "NG",
    "S", "Z", "SH", "ZH", "F", "V", "TH", "DH", "HH",
    "P", "T", "K", "B", "D", "G",
]
NOTE_FOR_PHONEME = {sym: BASE_NOTE + i for i, sym in enumerate(PHONEME_ORDER)}


def phonemes_to_notes(phonemes):
    notes = []
    for p in phonemes:
        note = NOTE_FOR_PHONEME.get(p)
        if note is None:
            sys.stderr.write("warning: unknown phoneme %r, skipped\n" % p)
            continue
        notes.append(note)
    return notes


def build_steps(words_and_phonemes, steps_per_phoneme=4, gap_steps=4, velocity=100):
    """words_and_phonemes: list of (word, [phonemes]). Returns a list of
    (note, velocity) or None (rest), one per pattern step."""
    steps = []
    for _word, phonemes in words_and_phonemes:
        notes = phonemes_to_notes(phonemes)
        if not notes:
            continue
        if steps:
            steps.extend([None] * gap_steps)
        for note in notes:
            steps.append((note, velocity))
            steps.extend([None] * (steps_per_phoneme - 1))
    while steps and steps[-1] is None:
        steps.pop()
    return steps


def write_rptp(path, steps, instrument):
    """Write an RPTP v2 pattern: track 0 carries the phoneme notes, tracks
    1-15 are left empty. Step layout: note, vel, inst, fx0, fxv0, fx1, fxv1."""
    length = max(1, min(len(steps), MAX_PATTERN_STEPS))
    out = bytearray(b"RPTP")
    out += struct.pack("<HHB", 2, length, PATTERN_TRACKS)
    for t in range(PATTERN_TRACKS):
        for si in range(length):
            cell = steps[si] if t == 0 and si < len(steps) else None
            if cell is None:
                out += struct.pack("<BBBBBBB", NOTE_EMPTY, 0, 0, FX_EMPTY, 0, FX_EMPTY, 0)
            else:
                note, vel = cell
                out += struct.pack("<BBBBBBB", note, vel, instrument, FX_EMPTY, 0, FX_EMPTY, 0)

    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def convert(text, out_path, instrument=0, steps_per_phoneme=4, gap_steps=4, from_phonemes=False):
    if from_phonemes:
        words_and_phonemes = [("", text.split())]
    else:
        words_and_phonemes = text_to_phonemes(text)
    steps = build_steps(words_and_phonemes, steps_per_phoneme=steps_per_phoneme, gap_steps=gap_steps)
    if not steps:
        raise ValueError("no phonemes produced for %r" % text)
    write_rptp(out_path, steps, instrument)
    return len(steps)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Convert text (or a phoneme list) to a poketrack .rptp pattern for ROBOTALK")
    ap.add_argument("text", help="text to speak, or a space-separated phoneme list with --phonemes")
    ap.add_argument("--phonemes", action="store_true",
                    help="treat `text` as an already-tokenized phoneme list, not English text")
    ap.add_argument("--out", help="output .rptp (default: text.rptp)")
    ap.add_argument("--instrument", type=int, default=0,
                    help="instrument index the ROBOTALK plugin is set up on (default: 0)")
    ap.add_argument("--steps-per-phoneme", type=int, default=4,
                    help="rows between each phoneme; 4 = one per beat on a 16th-note "
                         "grid at a normal ~120 BPM speaking pace (default: 4)")
    ap.add_argument("--gap-steps", type=int, default=4,
                    help="extra rest steps between words, on top of --steps-per-phoneme (default: 4)")
    args = ap.parse_args(argv)

    out = args.out or "text.rptp"
    n = convert(args.text, out, instrument=args.instrument,
                steps_per_phoneme=args.steps_per_phoneme, gap_steps=args.gap_steps,
                from_phonemes=args.phonemes)
    print("wrote %s  (%d steps)" % (out, n))


if __name__ == "__main__":
    main()
