#!/usr/bin/env node
// extract.mjs Syntakt_OS1.42.syx [out.bin]
// Lists the sections of an official Syntakt OS file and writes the audio-engine
// image (section 7, load address 0x40000400) to out.bin.

import { readFile, writeFile } from 'node:fs/promises'
import { sections, extractEngine, sha256 } from '../web/firmware.js'

const [syx, out = 'build/section7.bin'] = process.argv.slice(2)
if (!syx) {
  console.error('usage: extract.mjs Syntakt_OS1.42.syx [out.bin]')
  process.exit(1)
}

const raw = new Uint8Array(await readFile(syx))
for (const [id, d] of sections(raw)) console.log(`section ${id}: ${d.length} bytes, sha256 ${(await sha256(d)).slice(0, 16)}...`)
const { version, image } = await extractEngine(raw)
await writeFile(out, image)
console.log(`OS ${version || '(unknown build)'}: wrote ${image.length} bytes to ${out}`)
