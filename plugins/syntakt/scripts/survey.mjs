#!/usr/bin/env node
// survey.mjs build/section7.bin
// Walks the code reachable from the voice loop (and the voice init/reset and
// the 12 update/render engines) with m68k-elf-objdump, and reports which
// ColdFire instructions the interpreter has to implement.
//
// needs m68k-elf-binutils (brew install m68k-elf-binutils)

import { execFileSync } from 'node:child_process'
import { readFileSync } from 'node:fs'

const BASE = 0x40000400
const CODE_END = 0x4004f6e0
const VOICE_LOOP = 0x40004324
const VOICE_RESET = 0x40003ee0
const VOICE_PREP = 0x40002544
const RENDER_TAB = 0x400148f0
const UPDATE_TAB = 0x40014920
const OBJDUMP = process.env.OBJDUMP || 'm68k-elf-objdump'

const file = process.argv[2] || 'build/section7.bin'
const img = readFileSync(file)
const u32 = (a) => img.readUInt32BE(a - BASE)
const inCode = (a) => a >= BASE && a < CODE_END

// addr -> { size, mn, ops }
const insns = new Map()
function disasm(start, stop) {
  const out = execFileSync(OBJDUMP, ['-D', '-b', 'binary', '-m', 'm68k:isa-c:emac', `--adjust-vma=0x${BASE.toString(16)}`, `--start-address=0x${start.toString(16)}`, `--stop-address=0x${stop.toString(16)}`, file], { maxBuffer: 1 << 28 }).toString()
  for (const line of out.split('\n')) {
    const m = line.match(/^([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*)$/)
    if (!m) continue
    const addr = parseInt(m[1], 16)
    if (insns.has(addr)) continue
    insns.set(addr, { size: m[2].replace(/ /g, '').length / 2, hex: m[2].trim(), mn: m[3], ops: m[4].trim() })
  }
}

// objdump's linear sweep can desync after inline data, so re-sync on demand
function at(a) {
  if (!insns.has(a)) disasm(a, Math.min(a + 0x400, CODE_END))
  return insns.get(a)
}

// one linear sweep over the code (it ends where the engine tables start) keeps everything in sync
disasm(BASE, RENDER_TAB)

const roots = new Set([VOICE_LOOP, VOICE_RESET, VOICE_PREP])
for (let e = 0; e < 12; e++) {
  roots.add(u32(RENDER_TAB + 4 * e))
  roots.add(u32(UPDATE_TAB + 4 * e))
}

const reached = new Map() // addr -> insn
const indirect = []
const codePtrs = new Map() // ptr -> referencing insn
// function pointers loaded with lea/movel and called through jsr (An), found from the constants this script lists
const FN_PTRS = [0x40002770, 0x400032b4, 0x40005728, 0x400058e8, 0x40006074, 0x400061a2, 0x40006346, 0x40006434, 0x40006646, 0x40006704, 0x40006764]
const extraRoots = [
  ...FN_PTRS,
  ...(process.env.ROOTS || '')
    .split(',')
    .filter(Boolean)
    .map((x) => parseInt(x, 16))
]
for (const r of extraRoots) roots.add(r)
const work = [...roots]
const isTerminal = (mn) => /^(rts|rte|bra[sbwl]?|jmp|halt|illegal)$/.test(mn)
const isBranch = (mn) => /^(b(ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)[sbwl]?|jsr|jmp)$/.test(mn)

while (work.length) {
  let a = work.pop()
  while (inCode(a) && !reached.has(a)) {
    const i = at(a)
    if (!i) throw new Error(`no instruction at 0x${a.toString(16)}`)
    reached.set(a, i)
    // absolute/immediate values that point into code are function pointers (e.g. lea 0x40006646,%fp)
    for (const v of i.ops.matchAll(/#?(-?0x[0-9a-f]+|-?\d+)/g)) {
      const n = Number(v[1]) >>> 0
      if (inCode(n) && !isBranch(i.mn) && !codePtrs.has(n)) codePtrs.set(n, `0x${a.toString(16)}: ${i.mn} ${i.ops}`)
    }
    if (isBranch(i.mn)) {
      const t = i.ops.match(/^(?:%pc@\()?0x([0-9a-f]+)\)?$/)
      if (t) work.push(parseInt(t[1], 16))
      else indirect.push(`0x${a.toString(16)}: ${i.mn} ${i.ops}`)
    }
    if (isTerminal(i.mn)) break
    a += i.size
  }
}

const ptrRoots = [...codePtrs.keys()].filter((p) => !reached.has(p))

// operand shape: registers, displacements and immediates collapsed so we see addressing modes
const shape = (ops) =>
  ops
    .replace(/%acc[0-3]/g, 'ACC')
    .replace(/%[da][0-7]|%fp|%sp/g, 'R')
    .replace(/#-?(0x[0-9a-f]+|\d+)/g, '#i')
    .replace(/-?0x[0-9a-f]+|-?\b\d+\b/g, 'n')

const mnCount = new Map()
const formCount = new Map()
let bytes = 0
for (const i of reached.values()) {
  bytes += i.size
  mnCount.set(i.mn, (mnCount.get(i.mn) || 0) + 1)
  const f = `${i.mn} ${shape(i.ops)}`
  formCount.set(f, (formCount.get(f) || 0) + 1)
}

const sorted = (m) => [...m].sort((a, b) => b[1] - a[1])
// DUMP=1: just the reachable listing (address, bytes, instruction), for writing and checking the interpreter
if (process.env.DUMP) {
  for (const [a, i] of [...reached].sort((x, y) => x[0] - y[0])) console.log(`${a.toString(16)}\t${i.hex}\t${i.mn} ${i.ops}`)
  process.exit(0)
}

console.log(`reachable: ${reached.size} instructions, ${bytes} bytes, from ${roots.size} roots + ${ptrRoots.length} code pointers`)
console.log(`code-range constants not reached (data tables, or function pointers to pass in ROOTS=):`)
console.log(
  ptrRoots
    .sort((a, b) => a - b)
    .map((p) => `  0x${p.toString(16)}  <- ${codePtrs.get(p)}`)
    .join('\n')
)
console.log(`\nmnemonics (${mnCount.size}):`)
console.log(
  sorted(mnCount)
    .map(([k, v]) => `  ${k.padEnd(10)} ${v}`)
    .join('\n')
)
console.log(`\noperand forms (${formCount.size}):`)
console.log(
  sorted(formCount)
    .map(([k, v]) => `  ${String(v).padStart(5)}  ${k}`)
    .join('\n')
)
console.log(
  `\nundecoded: ${[...reached]
    .filter(([, i]) => i.mn === '.short')
    .map(([a, i]) => '0x' + a.toString(16) + ' ' + i.ops)
    .join(', ')}`
)
console.log(`\nindirect branches (${indirect.length}):`)
console.log(indirect.map((s) => '  ' + s).join('\n'))
