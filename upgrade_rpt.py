#!/usr/bin/env python3
"""Upgrade raypoketrack .rpt songs from v1 (single-track patterns) to v2
(multi-track patterns).

In v1 the SONG grid had 16 lanes, each cell referencing a single-track pattern,
and all lanes played simultaneously. In v2 a pattern holds up to PATTERN_TRACKS
(16) tracks. This tool merges each v1 song row's 16 lane-patterns into the 16
tracks of one v2 pattern, placed on lane 0 of the corresponding v2 song row.

META and INST chunks are copied verbatim (their layout is unchanged). Only the
file version, SONG and PATN chunks are rewritten.

Usage:
  upgrade_rpt.py FILE.rpt [FILE.rpt ...]      # rewrites in place
"""

import struct
import sys

V1_SONG_LANES = 16
PATTERN_TRACKS = 16
SONG_CHANNELS = 4
NOTE_EMPTY = 0x00
FX_EMPTY = 0xFF
EMPTY_STEP = struct.pack("<BBBBBBB", NOTE_EMPTY, 0, 0, FX_EMPTY, FX_EMPTY, 0, 0)


def read_chunks(data):
    if data[:4] != b"RPT2":
        raise ValueError("not an RPT2 file")
    ver, ns = struct.unpack("<HH", data[4:8])
    off = 8
    chunks = []
    for _ in range(ns):
        tag = data[off:off + 4]
        sz = struct.unpack("<I", data[off + 4:off + 8])[0]
        pay = data[off + 8:off + 8 + sz]
        off += 8 + sz
        chunks.append((tag, pay))
    return ver, chunks


def parse_song_v1(pay):
    song_len = struct.unpack("<H", pay[:2])[0]
    cells = pay[2:2 + song_len * V1_SONG_LANES]
    rows = [list(cells[r * V1_SONG_LANES:(r + 1) * V1_SONG_LANES]) for r in range(song_len)]
    return song_len, rows


def parse_patn_v1(pay):
    """Return {index: (len, [step_bytes...])} where each step is 7 raw bytes."""
    cnt = struct.unpack("<H", pay[:2])[0]
    i = 2
    pats = {}
    for _ in range(cnt):
        idx = pay[i]; plen = struct.unpack("<H", pay[i + 1:i + 3])[0]; i += 3
        steps = []
        for _s in range(plen):
            steps.append(pay[i:i + 7]); i += 7
        pats[idx] = (plen, steps)
    return pats


def upgrade(data):
    ver, chunks = read_chunks(data)
    if ver >= 2:
        return None  # already v2
    by_tag = {t: p for t, p in chunks}
    song_len, rows = parse_song_v1(by_tag[b"SONG"])
    v1pats = parse_patn_v1(by_tag.get(b"PATN", struct.pack("<H", 0)))

    # Build one v2 pattern per song row (index = row), tracks = that row's lanes.
    new_song_rows = []      # v2 song rows: lane0 = pattern index, others empty
    new_patterns = []       # (index, len, [16 tracks of step-bytes])
    for r, lanes in enumerate(rows):
        if r > 254:
            break
        refs = [v1pats.get(pi) for pi in lanes]  # None or (len, steps)
        plen = max([p[0] for p in refs if p] or [16])
        tracks = []
        for t in range(PATTERN_TRACKS):
            src = refs[t] if t < len(refs) else None
            step_bytes = bytearray()
            for si in range(plen):
                if src and si < src[0]:
                    step_bytes += src[1][si]
                else:
                    step_bytes += EMPTY_STEP
            tracks.append(bytes(step_bytes))
        new_patterns.append((r, plen, tracks))
        row = [0xFF] * SONG_CHANNELS
        row[0] = r
        new_song_rows.append(row)

    # SONG chunk (v2)
    song = struct.pack("<H", len(new_song_rows))
    for row in new_song_rows:
        song += bytes(row)

    # PATN chunk (v2)
    patn = struct.pack("<H", len(new_patterns))
    for idx, plen, tracks in new_patterns:
        patn += struct.pack("<BHB", idx, plen, PATTERN_TRACKS)
        for t in tracks:
            patn += t

    # Reassemble: keep META and INST verbatim, replace SONG and PATN, bump version.
    out_chunks = []
    for tag, pay in chunks:
        if tag == b"SONG":
            out_chunks.append((b"SONG", song))
        elif tag == b"PATN":
            out_chunks.append((b"PATN", patn))
        else:
            out_chunks.append((tag, pay))
    if b"PATN" not in by_tag:
        out_chunks.append((b"PATN", patn))

    out = bytearray(b"RPT2")
    out += struct.pack("<HH", 2, len(out_chunks))
    for tag, pay in out_chunks:
        out += tag + struct.pack("<I", len(pay)) + pay
    return bytes(out)


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 1
    n = 0
    for path in argv:
        data = open(path, "rb").read()
        new = upgrade(data)
        if new is None:
            print("skip (already v2): %s" % path)
            continue
        open(path, "wb").write(new)
        n += 1
    print("upgraded %d file(s)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
