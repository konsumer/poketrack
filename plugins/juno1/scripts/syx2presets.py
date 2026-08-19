#!/usr/bin/env python3
"""Decodes Roland Juno-1/Juno-2/MKS-50 (Alpha Juno) bulk-dump .SYX banks into
a juno1 CLAP plugin preset table.

Byte/bit layout ported from mikerodd/june-21's junosyxloader
(src/plugins/junosyxloader/src/jsl.c, GPL-3.0) -- see that file's header
comment for the original "structure of a SYX block" documentation and its
getjuparm()/setjuparm() opcodes for the per-param bit-packing this mirrors.

Unlike scripts/syx2presets.mjs (which assumes a file is always exactly the
factory-bank shape: 16 blocks of 4 tones back-to-back, 64 tones total), this
scans the file for bulk-dump block headers directly. That makes it robust to
real-world banks off the internet, which aren't always a full 64-tone dump --
some have fewer blocks, some concatenate several dumps, some have stray bytes
around the SysEx data. Each block found is decoded independently; malformed
ones are skipped with a warning instead of aborting the whole run.

A "bulk dump" block:
    F0 41 37 00 23 20 01 00   8-byte header (BLD command)
    <patch-start-number>      1 byte (0, 4, 8, ... -- which patch number this
                               block starts at; not needed for decoding, just
                               framing)
    <4 tones, 64 bytes each>  each tone is 63 real data nibbles + 1 dummy,
                               nibble-per-byte, LSN first (so 32 real bytes
                               show up as 64 file bytes)
    F7                        end of exclusive message
i.e. 266 bytes per block, 4 tones per block.

Usage:
    python3 scripts/syx2presets.py bankA.syx [bankB.syx ...] > assembly/plugins/presets-data.ts
    python3 scripts/syx2presets.py bankA.syx -o assembly/plugins/presets-data.ts
"""

import argparse
import json
import sys

BLOCK_HEADER = bytes([0xF0, 0x41, 0x37, 0x00, 0x23, 0x20, 0x01, 0x00])
PREAMBLE_LEN = len(BLOCK_HEADER) + 1  # + the 1-byte "patch start number"
TONE_LEN = 64  # nibble-bytes per tone (63 data + 1 dummy)
TONES_PER_BLOCK = 4
BLOCK_LEN = PREAMBLE_LEN + TONE_LEN * TONES_PER_BLOCK + 1  # + trailing F7 = 266

# t_offset from jsl.c -- nibble offset of each param within a single tone's
# 64-nibble window (i.e. relative to that tone's own first byte).
T_OFFSET = {
    'dcoaftr': 0, 'vcfkybd': 0,
    'vcfaftr': 2, 'vcaaftr': 2,
    'envkybd': 4, 'dcobnd': 4,
    'dcolfo': 6,
    'chorus': 8, 'dcoenvd': 8,
    'pwpwm': 10, 'dcoenv': 10,
    'pwmrate': 12,
    'vcffreq': 14, 'vcfenv': 14,
    'vcfreso': 16,
    'vcfenvd': 18, 'vcaenv': 18,
    'vcflfo': 20,
    'vcalevl': 22, 'sub': 22,
    'lforate': 24,
    'lfodely': 26,
    'envt1': 28, 'sawtooth': 28,
    'envl1': 30,
    'envt2': 32,
    'envl2': 34, 'pulse': 34,
    'envt3': 36,
    'envl3': 38, 'hpffreq': 38,
    'envt4': 40,
    'dcorng': 42, 'presetname': 42,
    'sublevl': 46,
    'noislvl': 50,
    'crsrate': 54,
}

TABLE_CHARS = 'ABCDEFGHIJKLMNOPKRSTUVWXYZabcdefghijklmnopkrstuvwxyz0123456789 -'

FIELDS = [
    'dcoAftr', 'vcfKybd', 'vcfAftr', 'vcaAftr', 'envKybd', 'dcoBnd', 'dcoLfo',
    'chorus', 'dcoEnvd', 'pwPwm', 'dcoEnv', 'pwmRate', 'vcfFreq', 'vcfEnv',
    'vcfReso', 'vcfEnvd', 'vcaEnv', 'vcfLfo', 'vcaLevl', 'sub', 'lfoRate',
    'lfoDely', 'envT1', 'sawtooth', 'envL1', 'envT2', 'envL2', 'pulse',
    'envT3', 'envL3', 'hpfFreq', 'envT4', 'dcoRng', 'subLevl', 'noisLvl',
    'crsRate',
]


class MalformedBlock(Exception):
    pass


def read_byte(buf, offset):
    """Combine two nibble-bytes (buf[offset], buf[offset+1]) into one value."""
    if offset + 1 >= len(buf):
        raise MalformedBlock(f'truncated data at offset {offset}')
    return (buf[offset] & 0xF) | ((buf[offset + 1] & 0xF) << 4)


def get7(buf, tone_base, name):
    """Plain 7-bit value (low 7 bits of one nibble-pair byte)."""
    return read_byte(buf, tone_base + T_OFFSET[name]) & 0b0111_1111


def get4(buf, tone_base, name, high):
    """Plain 4-bit value, high or low nibble of a shared byte."""
    v = read_byte(buf, tone_base + T_OFFSET[name])
    return (v & 0xF0) >> 4 if high else v & 0xF


def get_chorus(buf, tone_base):
    return (read_byte(buf, tone_base + T_OFFSET['chorus']) & 0b1000_0000) >> 7


def get_2bit(buf, tone_base, name, weight2_shift, next_delta, weight1_shift):
    """2-bit switch assembled from the MSBs of two consecutive-in-name bytes."""
    off = tone_base + T_OFFSET[name]
    v1 = read_byte(buf, off)
    off += next_delta
    v2 = read_byte(buf, off)
    b1 = (v1 & 0x80) >> weight2_shift
    b2 = (v2 & 0x80) >> weight1_shift
    return b1 | b2


def get_3bit(buf, tone_base, name):
    """3-bit switch assembled from the MSBs of three consecutive-in-name bytes."""
    off = tone_base + T_OFFSET[name]
    v1 = read_byte(buf, off); off += 2
    v2 = read_byte(buf, off); off += 2
    v3 = read_byte(buf, off)
    b1 = (v1 & 0x80) >> 5
    b2 = (v2 & 0x80) >> 6
    b3 = (v3 & 0x80) >> 7
    return b1 | b2 | b3


def get_name(buf, tone_base):
    off = tone_base + T_OFFSET['presetname']
    chars = []
    for _ in range(10):
        v = read_byte(buf, off) & 0b0011_1111
        chars.append(TABLE_CHARS[v])
        off += 2
    return ''.join(chars).rstrip()


def get_crs_rate(buf, tone_base):
    """crsrate spreads across the top 2 bits of 4 consecutive tone-name bytes
    (name chars 7-10) -- each byte contributes 2 bits, least-significant byte
    first, per jsl.c's c[0..7] assembly."""
    off = tone_base + T_OFFSET['crsrate']
    result = 0
    for i in range(4):
        v = read_byte(buf, off)
        hi = 1 if (v & 0x80) else 0
        lo = 1 if (v & 0x40) else 0
        result |= (hi << (2 * i + 1)) | (lo << (2 * i))
        off += 2
    return result


def decode_tone(buf, tone_base):
    return {
        'name': get_name(buf, tone_base),
        'dcoAftr': get4(buf, tone_base, 'dcoaftr', True),
        'vcfKybd': get4(buf, tone_base, 'vcfkybd', False),
        'vcfAftr': get4(buf, tone_base, 'vcfaftr', True),
        'vcaAftr': get4(buf, tone_base, 'vcaaftr', False),
        'envKybd': get4(buf, tone_base, 'envkybd', True),
        'dcoBnd': get4(buf, tone_base, 'dcobnd', False),
        'dcoLfo': get7(buf, tone_base, 'dcolfo'),
        'chorus': get_chorus(buf, tone_base),
        'dcoEnvd': get7(buf, tone_base, 'dcoenvd'),
        'pwPwm': get7(buf, tone_base, 'pwpwm'),
        'dcoEnv': get_2bit(buf, tone_base, 'dcoenv', 6, 2, 7),
        'pwmRate': get7(buf, tone_base, 'pwmrate'),
        'vcfFreq': get7(buf, tone_base, 'vcffreq'),
        'vcfEnv': get_2bit(buf, tone_base, 'vcfenv', 6, 2, 7),
        'vcfReso': get7(buf, tone_base, 'vcfreso'),
        'vcfEnvd': get7(buf, tone_base, 'vcfenvd'),
        'vcaEnv': get_2bit(buf, tone_base, 'vcaenv', 6, 2, 7),
        'vcfLfo': get7(buf, tone_base, 'vcflfo'),
        'vcaLevl': get7(buf, tone_base, 'vcalevl'),
        'sub': get_3bit(buf, tone_base, 'sub'),
        'lfoRate': get7(buf, tone_base, 'lforate'),
        'lfoDely': get7(buf, tone_base, 'lfodely'),
        'envT1': get7(buf, tone_base, 'envt1'),
        'sawtooth': get_3bit(buf, tone_base, 'sawtooth'),
        'envL1': get7(buf, tone_base, 'envl1'),
        'envT2': get7(buf, tone_base, 'envt2'),
        'envL2': get7(buf, tone_base, 'envl2'),
        'pulse': get_2bit(buf, tone_base, 'pulse', 6, 2, 7),
        'envT3': get7(buf, tone_base, 'envt3'),
        'envL3': get7(buf, tone_base, 'envl3'),
        'hpfFreq': get_2bit(buf, tone_base, 'hpffreq', 6, 2, 7),
        'envT4': get7(buf, tone_base, 'envt4'),
        'dcoRng': get_2bit(buf, tone_base, 'dcorng', 6, 2, 7),
        'subLevl': get_2bit(buf, tone_base, 'sublevl', 6, 2, 7),
        'noisLvl': get_2bit(buf, tone_base, 'noislvl', 6, 2, 7),
        'crsRate': get_crs_rate(buf, tone_base),
    }


def find_blocks(buf):
    """Yield the file offset of each bulk-dump header found in buf."""
    start = 0
    while True:
        idx = buf.find(BLOCK_HEADER, start)
        if idx == -1:
            return
        yield idx
        start = idx + len(BLOCK_HEADER)


def decode_block(buf, header_offset):
    """Decode the 4 tones in the block whose header starts at header_offset.
    Raises MalformedBlock if the block doesn't have room / a valid F7."""
    end = header_offset + BLOCK_LEN
    if end > len(buf):
        raise MalformedBlock(
            f'block at {header_offset} needs {BLOCK_LEN} bytes, only {len(buf) - header_offset} available')
    terminator = buf[end - 1]
    if terminator != 0xF7:
        raise MalformedBlock(f'block at {header_offset} ends with 0x{terminator:02X}, expected 0xF7 (not a clean 4-tone block)')
    tones = []
    for t in range(TONES_PER_BLOCK):
        tone_base = header_offset + PREAMBLE_LEN + t * TONE_LEN
        tones.append(decode_tone(buf, tone_base))
    return tones


def decode_bank(path, verbose=False):
    with open(path, 'rb') as f:
        buf = f.read()
    patches = []
    skipped = 0
    for header_offset in find_blocks(buf):
        try:
            patches.extend(decode_block(buf, header_offset))
        except MalformedBlock as e:
            skipped += 1
            print(f'warning: {path}: skipping block at byte {header_offset}: {e}', file=sys.stderr)
    if not patches:
        print(f'warning: {path}: no bulk-dump blocks found (not a Juno-1/Juno-2/MKS-50 '
              f'bulk-dump .SYX file?)', file=sys.stderr)
    elif verbose:
        print(f'{path}: {len(patches)} patches decoded from {len(patches) // TONES_PER_BLOCK} '
              f'block(s){f", {skipped} skipped" if skipped else ""}', file=sys.stderr)
    return patches


def render(patches):
    lines = []
    lines.append('// GENERATED by scripts/syx2presets.py -- do not edit by hand.')
    lines.append('//')
    lines.append('// Factory patch data decoded from a Roland Juno-1/Juno-2/MKS-50')
    lines.append('// (Alpha Juno) bulk-dump SysEx bank. If this was generated from')
    lines.append('// mikerodd/june-21\'s FACTORYA.SYX/FACTORYB.SYX (GPL-3.0-or-later),')
    lines.append('// this file -- and any compiled plugin binary that embeds it -- is')
    lines.append('// GPL-3.0-or-later too, NOT the zlib license that covers the rest of')
    lines.append('// poketrack. See plugins/juno1/README.md.')
    lines.append('')
    lines.append('export class Preset {')
    lines.append('  name: string = ""')
    for f in FIELDS:
        lines.append(f'  {f}: u8 = 0')
    lines.append('}')
    lines.append('')
    lines.append(f'export const PRESET_COUNT: i32 = {len(patches)}')
    lines.append('')
    lines.append('export function makePreset(i: i32): Preset {')
    lines.append('  let p = new Preset()')
    lines.append('  switch (i) {')
    for i, patch in enumerate(patches):
        lines.append(f'    case {i}: {{')
        lines.append(f'      p.name = {json.dumps(patch["name"])}')
        for f in FIELDS:
            lines.append(f'      p.{f} = {patch[f]}')
        lines.append('      break')
        lines.append('    }')
    lines.append('    default: break')
    lines.append('  }')
    lines.append('  return p')
    lines.append('}')
    lines.append('')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(
        description='Decode Roland Juno-1/Juno-2/MKS-50 bulk-dump .SYX banks into a juno1 preset table.')
    parser.add_argument('syx_files', nargs='+', metavar='bank.syx', help='one or more bulk-dump .SYX files')
    parser.add_argument('-o', '--output', metavar='PATH',
                         help='write presets-data.ts here instead of stdout')
    parser.add_argument('-v', '--verbose', action='store_true',
                         help='print per-file patch counts to stderr')
    args = parser.parse_args()

    patches = []
    for path in args.syx_files:
        patches.extend(decode_bank(path, verbose=args.verbose))

    if not patches:
        print('error: no patches decoded from any input file', file=sys.stderr)
        sys.exit(1)

    output = render(patches)
    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        if args.verbose:
            print(f'wrote {len(patches)} patches to {args.output}', file=sys.stderr)
    else:
        sys.stdout.write(output)


if __name__ == '__main__':
    main()
