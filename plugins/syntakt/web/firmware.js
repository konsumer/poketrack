// Syntakt OS (.syx) -> audio-engine image (section 7), entirely in memory.
// Works in browsers and Node (needs crypto.subtle). Ported from Modded-Cycles
// tools/emu/syntakt.py + tools/mtlib (MIT), see README.md#credits.
//
// Layers: SysEx packets (7-bit, two streams) -> ELE3 container -> sections
// (aPLib-variant compressed, or raw). Section 7 is the program of the
// Syntakt's second ColdFire (the digital voice engines), loaded at 0x40000400.

export const PRODUCT = 0x16
export const IMAGE_BASE = 0x40000400

// section 7 is byte-identical in OS 1.41 and 1.42
export const DSP_SHA256 = 'daf6451cf9587c0b628e901b7bb6b25f4e2633d448c534c0c181dd35ec783bc2'
export const OFFICIAL = {
  1.42: 'ded3c59fdb909eb9e2c3307b30a486452c8fa6947b12c14e0ce35a187b7f37c1',
  1.41: '8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
}

const MSG_LEN = 128
const PAYLOAD_OFF = 10
const PAYLOAD_END = 126
const CS_OFF = 126
const V = 0x35
const C0 = 0x2c

export async function sha256(bytes) {
  const h = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))
  return Array.from(h, (b) => b.toString(16).padStart(2, '0')).join('')
}

const be32 = (b, o) => ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0

function splitSysex(raw) {
  const msgs = []
  let i = 0
  while (true) {
    const a = raw.indexOf(0xf0, i)
    if (a < 0) break
    const b = raw.indexOf(0xf7, a)
    if (b < 0) break
    msgs.push(raw.subarray(a, b + 1))
    i = b + 1
  }
  return msgs
}

function checksum(m, c0) {
  let s = 0
  for (let i = 8; i < 126; i++) s += m[i] ^ ((V - i) & 0x3f)
  return (c0 - s) & 0x7f
}

// 8-in-7, MSB first: each group is a byte of high bits then 7 data bytes
function unpack7(p, out, pos) {
  for (let i = 0; i < p.length; i += 8) {
    const hi = p[i]
    for (let k = 0; k < 7 && i + 1 + k < p.length; k++) {
      out[pos++] = ((hi << (k + 1)) & 0x80) | p[i + 1 + k]
    }
  }
  return pos
}

export function unwrap(raw) {
  const msgs = splitSysex(raw)
  if (msgs.length < 3) throw new Error('not an Elektron SysEx file')
  const head = msgs[0]
  if (head[1] !== 0x00 || head[2] !== 0x20 || head[3] !== 0x3c) throw new Error('not an Elektron SysEx file')
  if (head[4] !== PRODUCT) throw new Error(`not a Syntakt OS file (product 0x${head[4].toString(16)})`)
  const data = msgs.slice(1, -1)
  // two packet streams (byte 7 = 0, 1), the second with C0 - 1; concatenated in stream order
  const streams = new Map()
  data.forEach((m, n) => {
    if (m.length !== MSG_LEN || m[CS_OFF] !== checksum(m, C0 - m[7])) throw new Error(`packet ${n}: bad checksum`)
    if (!streams.has(m[7])) streams.set(m[7], [])
    streams.get(m[7]).push(m)
  })
  const out = new Uint8Array(data.length * 101)
  let pos = 0
  for (const k of [...streams.keys()].sort((a, b) => a - b)) {
    for (const m of streams.get(k)) pos = unpack7(m.subarray(PAYLOAD_OFF, PAYLOAD_END), out, pos)
  }
  return out.subarray(0, pos)
}

export function parseContainer(stream) {
  const declared = be32(stream, 0)
  const blob = stream.subarray(8, 8 + declared)
  if (String.fromCharCode(...blob.subarray(0, 4)) !== 'ELE3') throw new Error('no ELE3 container')
  const n = be32(blob, 0x1c)
  if (n < 1 || n > 16) throw new Error(`implausible section count ${n}`)
  const sections = []
  for (let s = 0; s < n; s++) {
    const e = 0x20 + s * 16
    sections.push({ id: be32(blob, e), off: be32(blob, e + 4), size: be32(blob, e + 8), attr: be32(blob, e + 12) })
  }
  return { blob, sections }
}

// aPLib variant: interlaced Elias-gamma, offset bias 767 (also the end marker),
// gamma 2 reuses the last offset, offsets past 3328 add 1 to the length.
// stream includes the 8-byte [u32 len][u32 bytesum] header.
export function depack(stream) {
  let ip = 8
  let tag = 0
  const bit = () => {
    tag <<= 1
    if ((tag & 0xff) === 0) {
      const by = stream[ip++]
      tag = (by << 1) | 1
      return (by >> 7) & 1
    }
    return (tag >> 8) & 1
  }
  const gamma = () => {
    let v = 1
    while (true) {
      v = v * 2 + bit()
      if (bit()) return v
      if (v > 0x02000000) throw new Error('corrupt stream: gamma overflow')
    }
  }
  let out = new Uint8Array(Math.max(1 << 16, stream.length * 4))
  let len = 0
  const grow = (need) => {
    if (len + need <= out.length) return
    const n = new Uint8Array(Math.max(out.length * 2, len + need))
    n.set(out.subarray(0, len))
    out = n
  }
  let lastOff = 1
  while (true) {
    if (ip > stream.length) throw new Error('corrupt stream: ran past the end')
    if (bit()) {
      grow(1)
      out[len++] = stream[ip++]
      continue
    }
    const g = gamma()
    let off
    if (g === 2) {
      off = lastOff
    } else {
      off = (g * 256 + stream[ip++]) >>> 0
      if (off === 767) break
      off -= 767
      lastOff = off
    }
    const sl = 2 * bit() + bit()
    let L = sl || gamma() + 2
    if (off > 3328) L++
    const n = L + 1
    if (off === 0 || off > len) throw new Error('corrupt stream: offset past the output')
    grow(n)
    for (let k = 0; k < n; k++, len++) out[len] = out[len - off]
  }
  return out.slice(0, len)
}

export function sections(raw) {
  const { blob, sections } = parseContainer(unwrap(raw))
  const out = new Map()
  for (const s of sections) {
    const st = blob.subarray(s.off, s.off + s.size)
    try {
      out.set(s.id, depack(st))
    } catch {
      out.set(s.id, st.slice())
    }
  }
  return out
}

// .syx bytes -> { version, image } ; image is section 7, verified
export async function extractEngine(raw) {
  raw = raw instanceof Uint8Array ? raw : new Uint8Array(raw)
  const hash = await sha256(raw)
  const version = Object.keys(OFFICIAL).find((v) => OFFICIAL[v] === hash) || null
  const image = sections(raw).get(7)
  if (!image) throw new Error('no section 7 (audio engine) in this file')
  if ((await sha256(image)) !== DSP_SHA256) throw new Error('unexpected audio engine image: this needs the official Syntakt OS 1.41 or 1.42')
  return { version, image }
}
