#!/usr/bin/env python3
"""Reference render of a scenario with Modded-Cycles' Unicorn bench (tools/emu/stengine.py).

    MODDED_CYCLES=path/to/Modded-Cycles python3 test/ref_bench.py build/section7.bin test/mix1.txt build/mix1.ref

Needs unicorn and numpy, and m68k-linux-gnu-objdump on PATH (a symlink to m68k-elf-objdump works).
Output: per block, 8 voices x 32 frames of int32 little-endian (same as test/bench.c).
"""
import os
import pathlib
import struct
import sys
import time

sys.path.insert(0, str(pathlib.Path(os.environ['MODDED_CYCLES']) / 'tools' / 'emu'))
import stengine  # noqa: E402

SLOTS = ('tune', 'p1', 'p2', 'p3', 'p4', 'punch', 'gate', 'decay', 'over')


def load_scenario(path):
    events, end = {}, 0
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.split('#')[0].split()
        if not line:
            continue
        if line[0] == 'end':
            end = int(line[1])
            continue
        events.setdefault(int(line[0]), []).append(line[1:])
    return events, end


def main(image_path, scenario_path, out_path):
    img = pathlib.Path(image_path).read_bytes()
    events, end = load_scenario(scenario_path)
    eng = stengine.Engine(img)
    out = bytearray()
    t0 = time.time()
    for b in range(end):
        trig = rel = 0
        for cmd, *args in events.get(b, []):
            t = int(args[0])
            if cmd == 'machine':
                eng.machine(t, int(args[1]))
            elif cmd == 'set':
                eng.set(t, **{args[1]: float(args[2])})
            elif cmd == 'note':
                eng.note(t, float(args[1]))
            elif cmd == 'trig':
                trig |= 1 << t
            elif cmd == 'release':
                rel |= 1 << t
            elif cmd == 'park':
                v = stengine.VOICE0 + stengine.VSTRIDE * t
                eng.uc.mem_write(v, struct.pack('>II', 12, 12))
        blk = eng.block(trig, rel)
        out += struct.pack('<256i', *[int(v) for v in blk.reshape(-1)])
    pathlib.Path(out_path).write_bytes(out)
    print(f'{end} blocks in {time.time() - t0:.1f}s, unmapped accesses: {len(eng.unmapped)}')


if __name__ == '__main__':
    main(*sys.argv[1:4])
