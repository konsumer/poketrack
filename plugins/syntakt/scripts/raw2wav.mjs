#!/usr/bin/env node
// raw2wav.mjs in.raw out.wav [voice]
// Bench/reference renders (per block 8 voices x 32 int32 LE) -> 48 kHz mono
// 16-bit WAV: one voice, or all 8 summed. Normalized to -1 dBFS.

import { readFileSync, writeFileSync } from 'node:fs'

const [inPath, outPath, voiceArg] = process.argv.slice(2)
if (!inPath || !outPath) {
  console.error('usage: raw2wav.mjs in.raw out.wav [voice]')
  process.exit(1)
}

const raw = readFileSync(inPath)
const src = new Int32Array(raw.buffer, raw.byteOffset, raw.length / 4)
const blocks = src.length / 256
const voice = voiceArg === undefined ? -1 : Number(voiceArg)
const mix = new Float64Array(blocks * 32)
for (let b = 0; b < blocks; b++) {
  for (let v = 0; v < 8; v++) {
    if (voice >= 0 && v !== voice) continue
    for (let i = 0; i < 32; i++) mix[b * 32 + i] += src[b * 256 + v * 32 + i]
  }
}
const peak = mix.reduce((m, x) => Math.max(m, Math.abs(x)), 0) || 1
const gain = (0.89 * 32767) / peak

const wav = Buffer.alloc(44 + mix.length * 2)
wav.write('RIFF', 0)
wav.writeUInt32LE(36 + mix.length * 2, 4)
wav.write('WAVEfmt ', 8)
wav.writeUInt32LE(16, 16)
wav.writeUInt16LE(1, 20)
wav.writeUInt16LE(1, 22)
wav.writeUInt32LE(48000, 24)
wav.writeUInt32LE(96000, 28)
wav.writeUInt16LE(2, 32)
wav.writeUInt16LE(16, 34)
wav.write('data', 36)
wav.writeUInt32LE(mix.length * 2, 40)
mix.forEach((x, i) => wav.writeInt16LE(Math.round(x * gain), 44 + i * 2))
writeFileSync(outPath, wav)
console.log(`${outPath}: ${(mix.length / 48000).toFixed(3)}s, peak ${peak}`)
