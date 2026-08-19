#!/usr/bin/env node
// Decodes Roland Juno-1/Juno-2 (Alpha Juno) bulk-dump .SYX banks into a
// juno1 CLAP plugin preset table.
//
// Byte/bit layout ported from mikerodd/june-21's junosyxloader
// (src/plugins/junosyxloader/src/jsl.c, GPL-3.0) — see that file's header
// comment for the original "structure of a SYX block" documentation and
// its getjuparm()/setjuparm() opcodes for the per-param bit-packing this
// mirrors. Each bank file is 16 blocks of `F0 41 37 00 23 20 01 00` + 4
// tones (64 nibble-bytes each, one MIDI byte per 4-bit nibble) + `F7`,
// i.e. 64 tones per file.
//
// Usage: node scripts/syx2presets.mjs <bankA.syx> [bankB.syx ...] > assembly/plugins/presets-data.ts

import { readFileSync } from 'node:fs'

const TONES_PER_BANK = 64

// t_offset from jsl.c — base nibble offset of each param within a tone block.
const T_OFFSET = {
  dcoaftr: 0, vcfkybd: 0,
  vcfaftr: 2, vcaaftr: 2,
  envkybd: 4, dcobnd: 4,
  dcolfo: 6,
  chorus: 8, dcoenvd: 8,
  pwpwm: 10, dcoenv: 10,
  pwmrate: 12,
  vcffreq: 14, vcfenv: 14,
  vcfreso: 16,
  vcfenvd: 18, vcaenv: 18,
  vcflfo: 20,
  vcalevl: 22, sub: 22,
  lforate: 24,
  lfodely: 26,
  envt1: 28, sawtooth: 28,
  envl1: 30,
  envt2: 32,
  envl2: 34, pulse: 34,
  envt3: 36,
  envl3: 38, hpffreq: 38,
  envt4: 40,
  dcorng: 42, presetName: 42,
  sublevl: 46,
  noislvl: 50,
  crsrate: 54
}

const TABLE_CHARS = 'ABCDEFGHIJKLMNOPKRSTUVWXYZabcdefghijklmnopkrstuvwxyz0123456789 -'

function getOffset (tone, paramName) {
  const poff = T_OFFSET[paramName]
  return 64 * tone + (Math.floor(tone / 4) + 1) * 9 + Math.floor(tone / 4) + poff
}

function readByte (buf, count) {
  return (buf[count] & 0xf) | ((buf[count + 1] & 0xf) << 4)
}

// Plain 7-bit value (low 7 bits of one nibble-pair byte).
function get7 (buf, tone, paramName) {
  return readByte(buf, getOffset(tone, paramName)) & 0b01111111
}

// Plain 4-bit value, high or low nibble of a shared byte.
function get4 (buf, tone, paramName, high) {
  const v = readByte(buf, getOffset(tone, paramName))
  return high ? (v & 0xf0) >> 4 : v & 0xf
}

function getChorus (buf, tone) {
  return (readByte(buf, getOffset(tone, 'chorus')) & 0b10000000) >> 7
}

// 2-bit switch assembled from the MSBs of two consecutive-in-name bytes.
function get2bit (buf, tone, baseParam, weight2Shift, nextOffsetDelta, weight1Shift) {
  let off = getOffset(tone, baseParam)
  const v1 = readByte(buf, off)
  off += nextOffsetDelta
  const v2 = readByte(buf, off)
  const b1 = (v1 & 0x80) >> weight2Shift
  const b2 = (v2 & 0x80) >> weight1Shift
  return b1 | b2
}

// 3-bit switch assembled from the MSBs of three consecutive-in-name bytes.
function get3bit (buf, tone, baseParam) {
  let off = getOffset(tone, baseParam)
  const v1 = readByte(buf, off); off += 2
  const v2 = readByte(buf, off); off += 2
  const v3 = readByte(buf, off)
  const b1 = (v1 & 0x80) >> 5
  const b2 = (v2 & 0x80) >> 6
  const b3 = (v3 & 0x80) >> 7
  return b1 | b2 | b3
}

function getName (buf, tone) {
  let off = getOffset(tone, 'presetName')
  let name = ''
  for (let i = 0; i < 10; i++) {
    const v = readByte(buf, off) & 0b00111111
    name += TABLE_CHARS[v]
    off += 2
  }
  return name.trimEnd()
}

// crsrate spreads across the top 2 bits of 4 consecutive tone-name bytes
// (name chars 7-10) — each byte contributes 2 bits, least-significant byte
// first, per jsl.c's c[0..7] assembly.
function getCrsRate (buf, tone) {
  let off = getOffset(tone, 'crsrate')
  let result = 0
  for (let i = 0; i < 4; i++) {
    const v = readByte(buf, off)
    const hi = (v & 0x80) ? 1 : 0
    const lo = (v & 0x40) ? 1 : 0
    result |= (hi << (2 * i + 1)) | (lo << (2 * i))
    off += 2
  }
  return result
}

function decodeTone (buf, tone) {
  return {
    name: getName(buf, tone),
    dcoAftr: get4(buf, tone, 'dcoaftr', true),
    vcfKybd: get4(buf, tone, 'vcfkybd', false),
    vcfAftr: get4(buf, tone, 'vcfaftr', true),
    vcaAftr: get4(buf, tone, 'vcaaftr', false),
    envKybd: get4(buf, tone, 'envkybd', true),
    dcoBnd: get4(buf, tone, 'dcobnd', false),
    dcoLfo: get7(buf, tone, 'dcolfo'),
    chorus: getChorus(buf, tone),
    dcoEnvd: get7(buf, tone, 'dcoenvd'),
    pwPwm: get7(buf, tone, 'pwpwm'),
    dcoEnv: get2bit(buf, tone, 'dcoenv', 6, 2, 7),
    pwmRate: get7(buf, tone, 'pwmrate'),
    vcfFreq: get7(buf, tone, 'vcffreq'),
    vcfEnv: get2bit(buf, tone, 'vcfenv', 6, 2, 7),
    vcfReso: get7(buf, tone, 'vcfreso'),
    vcfEnvd: get7(buf, tone, 'vcfenvd'),
    vcaEnv: get2bit(buf, tone, 'vcaenv', 6, 2, 7),
    vcfLfo: get7(buf, tone, 'vcflfo'),
    vcaLevl: get7(buf, tone, 'vcalevl'),
    sub: get3bit(buf, tone, 'sub'),
    lfoRate: get7(buf, tone, 'lforate'),
    lfoDely: get7(buf, tone, 'lfodely'),
    envT1: get7(buf, tone, 'envt1'),
    sawtooth: get3bit(buf, tone, 'sawtooth'),
    envL1: get7(buf, tone, 'envl1'),
    envT2: get7(buf, tone, 'envt2'),
    envL2: get7(buf, tone, 'envl2'),
    pulse: get2bit(buf, tone, 'pulse', 6, 2, 7),
    envT3: get7(buf, tone, 'envt3'),
    envL3: get7(buf, tone, 'envl3'),
    hpfFreq: get2bit(buf, tone, 'hpffreq', 6, 2, 7),
    envT4: get7(buf, tone, 'envt4'),
    dcoRng: get2bit(buf, tone, 'dcorng', 6, 2, 7),
    subLevl: get2bit(buf, tone, 'sublevl', 6, 2, 7),
    noisLvl: get2bit(buf, tone, 'noislvl', 6, 2, 7),
    crsRate: getCrsRate(buf, tone)
  }
}

function decodeBank (path) {
  const buf = readFileSync(path)
  const tones = []
  for (let t = 0; t < TONES_PER_BANK; t++) tones.push(decodeTone(buf, t))
  return tones
}

const files = process.argv.slice(2)
if (files.length === 0) {
  console.error('usage: syx2presets.mjs <bankA.syx> [bankB.syx ...]')
  process.exit(1)
}

const patches = files.flatMap(decodeBank)

const FIELDS = [
  'dcoAftr', 'vcfKybd', 'vcfAftr', 'vcaAftr', 'envKybd', 'dcoBnd', 'dcoLfo',
  'chorus', 'dcoEnvd', 'pwPwm', 'dcoEnv', 'pwmRate', 'vcfFreq', 'vcfEnv',
  'vcfReso', 'vcfEnvd', 'vcaEnv', 'vcfLfo', 'vcaLevl', 'sub', 'lfoRate',
  'lfoDely', 'envT1', 'sawtooth', 'envL1', 'envT2', 'envL2', 'pulse',
  'envT3', 'envL3', 'hpfFreq', 'envT4', 'dcoRng', 'subLevl', 'noisLvl',
  'crsRate'
]

const lines = []
lines.push('// GENERATED by scripts/syx2presets.mjs — do not edit by hand.')
lines.push('//')
lines.push('// Factory patch data decoded from Roland Juno-1/Alpha Juno bulk-dump')
lines.push('// SysEx banks (FACTORYA.SYX / FACTORYB.SYX) published in')
lines.push('// mikerodd/june-21 (https://github.com/mikerodd/june-21), licensed')
lines.push('// GPL-3.0-or-later. This file, and any compiled plugin binary that')
lines.push('// embeds it, is therefore GPL-3.0-or-later — NOT the zlib license')
lines.push('// that covers the rest of poketrack. See plugins/juno1/README.md.')
lines.push('')
lines.push('export class Preset {')
lines.push('  name: string = ""')
for (const f of FIELDS) lines.push(`  ${f}: u8 = 0`)
lines.push('}')
lines.push('')
lines.push(`export const PRESET_COUNT: i32 = ${patches.length}`)
lines.push('')
lines.push('export function makePreset(i: i32): Preset {')
lines.push('  let p = new Preset()')
lines.push('  switch (i) {')
for (let i = 0; i < patches.length; i++) {
  const patch = patches[i]
  lines.push(`    case ${i}: {`)
  lines.push(`      p.name = ${JSON.stringify(patch.name)}`)
  for (const f of FIELDS) lines.push(`      p.${f} = ${patch[f]}`)
  lines.push('      break')
  lines.push('    }')
}
lines.push('    default: break')
lines.push('  }')
lines.push('  return p')
lines.push('}')
lines.push('')

process.stdout.write(lines.join('\n') + '\n')
