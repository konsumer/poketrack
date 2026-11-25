#!/usr/bin/env node
// make-plugin.mjs Syntakt_OS1.42.syx [out.wasm] [template.wasm]
// Makes your own syntakt.wclap.wasm: the template plugin (build.sh) with the
// engines from your official Syntakt OS file. The result contains Elektron's
// code: keep it for your own use, and don't share it alongside songs.

import { readFile, writeFile } from 'node:fs/promises'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { makePlugin } from '../web/patch.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const [syx, out = join(root, 'build', 'syntakt.wclap.wasm'), template = join(root, 'build', 'syntakt-template.wasm')] = process.argv.slice(2)
if (!syx) {
  console.error('usage: make-plugin.mjs Syntakt_OS1.42.syx [out.wasm] [template.wasm]')
  process.exit(1)
}

try {
  const { version, plugin } = await makePlugin(await readFile(syx), await readFile(template))
  await writeFile(out, plugin)
  console.log(`Made ${out} from Syntakt OS ${version || '(unknown build)'} — for your own use, don't share it`)
} catch (e) {
  console.error(e.code === 'ENOENT' ? `${e.path} not found${e.path === template ? ' (run build.sh first)' : ''}` : e.message)
  process.exit(1)
}
