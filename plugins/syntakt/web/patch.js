// Writes an engine image into the template plugin's slot (see src/slot.h).
// Works in browsers and Node.

import { extractEngine } from './firmware.js'

export const SLOT_MAGIC = 'SYNTAKT-ENGINE-SLOT-v1----------' // src/slot.h
const MAGIC_SIZE = 32

function fnv1a(bytes) {
  let h = 0x811c9dc5
  for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0
  return h
}

function findAll(hay, needle) {
  const out = []
  outer: for (let i = hay.indexOf(needle[0]); i >= 0 && i <= hay.length - needle.length; i = hay.indexOf(needle[0], i + 1)) {
    for (let k = 1; k < needle.length; k++) if (hay[i + k] !== needle[k]) continue outer
    out.push(i)
  }
  return out
}

// template .wasm bytes + engine image (section 7) -> patched .wasm bytes
export function patchPlugin(template, image) {
  template = template instanceof Uint8Array ? template : new Uint8Array(template)
  const magic = new TextEncoder().encode(SLOT_MAGIC)
  const hits = findAll(template, magic)
  if (hits.length !== 1) throw new Error(`template: engine slot found ${hits.length} times (expected 1) — is this syntakt-template.wasm?`)
  const at = hits[0]
  const head = at + MAGIC_SIZE
  const data = head + 8
  if (data + image.length > template.length) throw new Error('template: engine slot is cut short')
  // an untouched template slot: both sentinels, then the fill, in one piece
  const view = new DataView(template.buffer, template.byteOffset, template.byteLength)
  const empty = view.getUint32(head, true) === 0xffffffff && view.getUint32(head + 4, true) === 0xffffffff
  if (!empty || template.subarray(data, data + image.length).some((b) => b !== 0xa5)) throw new Error('template: engine slot is not an empty one in one piece — use a fresh syntakt-template.wasm')
  const out = template.slice()
  const outView = new DataView(out.buffer)
  outView.setUint32(head, image.length, true)
  outView.setUint32(head + 4, fnv1a(image), true)
  out.set(image, data)
  return out
}

// .syx bytes + template bytes -> { version, plugin }
export async function makePlugin(syx, template) {
  const { version, image } = await extractEngine(syx)
  return { version, plugin: patchPlugin(template, image) }
}
