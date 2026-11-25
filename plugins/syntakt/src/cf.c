// ColdFire ISA_C + EMAC interpreter, see cf.h
//
// Semantics follow the ColdFire Family Programmer's Reference Manual, and for
// the EMAC the exact model of Modded-Cycles tools/emu/emac.py (the bench our
// output is checked against): accumulators are unbounded, fractional products
// keep 8 bits below the LSB, saturation (OMC) only when read back.

#include "cf.h"

#include <stdlib.h>
#include <string.h>

cf_cpu* cf_new(void) {
  return calloc(1, sizeof(cf_cpu));
}

void cf_free(cf_cpu* cpu) {
  if (!cpu)
    return;
  for (uint32_t i = 0; i < CF_NPAGES; i++) free(cpu->pages[i]);
  free(cpu->dec);
  free(cpu);
}

void cf_map(cf_cpu* cpu, uint32_t base, uint32_t size) {
  uint32_t first = base >> CF_PAGE_BITS;
  uint32_t last = (uint32_t)(((uint64_t)base + size - 1) >> CF_PAGE_BITS);
  for (uint32_t p = first; p <= last; p++) cpu->mapped[p] = 1;
}

static void fault(cf_cpu* cpu, int err, uint32_t addr) {
  if (cpu->err)
    return;
  cpu->err = err;
  cpu->err_addr = addr;
}

static uint8_t* page(cf_cpu* cpu, uint32_t addr) {
  uint32_t p = addr >> CF_PAGE_BITS;
  uint8_t* pg = cpu->pages[p];
  if (pg)
    return pg;
  if (!cpu->mapped[p]) {
    fault(cpu, CF_ERR_UNMAPPED, addr);
    return NULL;
  }
  pg = cpu->pages[p] = calloc(1, CF_PAGE_SIZE);
  if (!pg)
    fault(cpu, CF_ERR_OOM, addr);
  return pg;
}

static inline uint32_t rd8(cf_cpu* cpu, uint32_t a) {
  uint8_t* p = page(cpu, a);
  return p ? p[a & (CF_PAGE_SIZE - 1)] : 0;
}

static inline void wr8(cf_cpu* cpu, uint32_t a, uint32_t v) {
  uint8_t* p = page(cpu, a);
  if (p)
    p[a & (CF_PAGE_SIZE - 1)] = (uint8_t)v;
}

static inline uint32_t rd16(cf_cpu* cpu, uint32_t a) {
  uint32_t o = a & (CF_PAGE_SIZE - 1);
  if (o <= CF_PAGE_SIZE - 2) {
    uint8_t* p = page(cpu, a);
    if (!p)
      return 0;
    p += o;
    return (uint32_t)p[0] << 8 | p[1];
  }
  return rd8(cpu, a) << 8 | rd8(cpu, a + 1);
}

static inline void wr16(cf_cpu* cpu, uint32_t a, uint32_t v) {
  uint32_t o = a & (CF_PAGE_SIZE - 1);
  if (o <= CF_PAGE_SIZE - 2) {
    uint8_t* p = page(cpu, a);
    if (!p)
      return;
    p += o;
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return;
  }
  wr8(cpu, a, v >> 8);
  wr8(cpu, a + 1, v);
}

static inline uint32_t rd32(cf_cpu* cpu, uint32_t a) {
  uint32_t o = a & (CF_PAGE_SIZE - 1);
  if (o <= CF_PAGE_SIZE - 4) {
    uint8_t* p = page(cpu, a);
    if (!p)
      return 0;
    p += o;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
  }
  return rd16(cpu, a) << 16 | rd16(cpu, a + 2);
}

static inline void wr32(cf_cpu* cpu, uint32_t a, uint32_t v) {
  uint32_t o = a & (CF_PAGE_SIZE - 1);
  if (o <= CF_PAGE_SIZE - 4) {
    uint8_t* p = page(cpu, a);
    if (!p)
      return;
    p += o;
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return;
  }
  wr16(cpu, a, v >> 16);
  wr16(cpu, a + 2, v);
}

uint32_t cf_rd32(cf_cpu* cpu, uint32_t addr) {
  return rd32(cpu, addr);
}

void cf_wr32(cf_cpu* cpu, uint32_t addr, uint32_t v) {
  wr32(cpu, addr, v);
}

void cf_write(cf_cpu* cpu, uint32_t addr, const void* src, size_t len) {
  const uint8_t* s = src;
  for (size_t i = 0; i < len; i++) wr8(cpu, addr + (uint32_t)i, s[i]);
}

void cf_read(cf_cpu* cpu, uint32_t addr, void* dst, size_t len) {
  uint8_t* d = dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)rd8(cpu, addr + (uint32_t)i);
}

// --- helpers ---------------------------------------------------------------

#define D(n) cpu->r[(n)]
#define A(n) cpu->r[8 + (n)]
#define SP cpu->r[15]

static inline uint32_t fetch16(cf_cpu* cpu) {
  uint32_t v = rd16(cpu, cpu->pc);
  cpu->pc += 2;
  return v;
}

static inline uint32_t fetch32(cf_cpu* cpu) {
  uint32_t v = rd32(cpu, cpu->pc);
  cpu->pc += 4;
  return v;
}

static inline uint32_t szmask(int sz) {
  return sz == 4 ? 0xffffffffu : sz == 2 ? 0xffffu
                                         : 0xffu;
}

static inline uint32_t szmsb(int sz) {
  return 1u << (sz * 8 - 1);
}

static inline uint32_t sext(uint32_t v, int sz) {
  return sz == 1 ? (uint32_t)(int32_t)(int8_t)v : sz == 2 ? (uint32_t)(int32_t)(int16_t)v
                                                          : v;
}

// size field 00/01/10 -> bytes
static inline int szfield(uint32_t f) {
  return f == 0 ? 1 : f == 1 ? 2
                  : f == 2   ? 4
                             : 0;
}

static inline void nz(cf_cpu* cpu, uint32_t v, int sz) {
  v &= szmask(sz);
  cpu->n = (v & szmsb(sz)) != 0;
  cpu->z = v == 0;
}

static inline void logic(cf_cpu* cpu, uint32_t v, int sz) {
  nz(cpu, v, sz);
  cpu->v = cpu->c = 0;
}

static inline uint32_t add_flags(cf_cpu* cpu, uint32_t d, uint32_t s, int sz) {
  uint32_t m = szmask(sz), r = (d + s) & m;
  cpu->c = cpu->x = ((uint64_t)(d & m) + (s & m)) > m;
  cpu->v = ((s ^ r) & (d ^ r) & szmsb(sz)) != 0;
  nz(cpu, r, sz);
  return r;
}

static inline uint32_t sub_flags(cf_cpu* cpu, uint32_t d, uint32_t s, int sz, int setx) {
  uint32_t m = szmask(sz), r = (d - s) & m;
  cpu->c = (s & m) > (d & m);
  if (setx)
    cpu->x = cpu->c;
  cpu->v = ((s ^ d) & (r ^ d) & szmsb(sz)) != 0;
  nz(cpu, r, sz);
  return r;
}

static inline void push32(cf_cpu* cpu, uint32_t v) {
  SP -= 4;
  wr32(cpu, SP, v);
}

static inline uint32_t pop32(cf_cpu* cpu) {
  uint32_t v = rd32(cpu, SP);
  SP += 4;
  return v;
}

static inline int cond(cf_cpu* cpu, int cc) {
  switch (cc) {
    case 0:
      return 1;
    case 1:
      return 0;
    case 2:
      return !cpu->c && !cpu->z;
    case 3:
      return cpu->c || cpu->z;
    case 4:
      return !cpu->c;
    case 5:
      return cpu->c;
    case 6:
      return !cpu->z;
    case 7:
      return cpu->z;
    case 8:
      return !cpu->v;
    case 9:
      return cpu->v;
    case 10:
      return !cpu->n;
    case 11:
      return cpu->n;
    case 12:
      return cpu->n == cpu->v;
    case 13:
      return cpu->n != cpu->v;
    case 14:
      return !cpu->z && cpu->n == cpu->v;
    default:
      return cpu->z || cpu->n != cpu->v;
  }
}

// --- effective addresses ---------------------------------------------------

enum { EA_D,
       EA_A,
       EA_MEM,
       EA_IMM,
       EA_BAD };

typedef struct {
  int kind;
  uint32_t a;  // register index, address or immediate
} ea_t;

static void illegal(cf_cpu* cpu) {
  fault(cpu, CF_ERR_ILLEGAL, cpu->err_pc);
}

static uint32_t indexed(cf_cpu* cpu, uint32_t base) {
  uint32_t ext = fetch16(cpu);
  if (ext & 0x100) {  // full extension format: not on ColdFire
    illegal(cpu);
    return 0;
  }
  uint32_t x = cpu->r[ext >> 12];
  if (!(ext & 0x800))
    x = sext(x, 2);
  return base + (uint32_t)(int32_t)(int8_t)ext + (x << ((ext >> 9) & 3));
}

static ea_t ea_get(cf_cpu* cpu, int mode, int reg, int sz) {
  ea_t e = {EA_MEM, 0};
  switch (mode) {
    case 0:
      e.kind = EA_D;
      e.a = reg;
      break;
    case 1:
      e.kind = EA_A;
      e.a = 8 + reg;
      break;
    case 2:
      e.a = A(reg);
      break;
    case 3:
      e.a = A(reg);
      A(reg) += sz;
      break;
    case 4:
      A(reg) -= sz;
      e.a = A(reg);
      break;
    case 5:
      e.a = A(reg) + (uint32_t)(int32_t)(int16_t)fetch16(cpu);
      break;
    case 6:
      e.a = indexed(cpu, A(reg));
      break;
    default:
      switch (reg) {
        case 0:
          e.a = (uint32_t)(int32_t)(int16_t)fetch16(cpu);
          break;
        case 1:
          e.a = fetch32(cpu);
          break;
        case 2: {
          uint32_t b = cpu->pc;
          e.a = b + (uint32_t)(int32_t)(int16_t)fetch16(cpu);
          break;
        }
        case 3:
          e.a = indexed(cpu, cpu->pc);
          break;
        case 4:
          e.kind = EA_IMM;
          e.a = sz == 4 ? fetch32(cpu) : sz == 2 ? fetch16(cpu)
                                                 : (fetch16(cpu) & 0xff);
          break;
        default:
          e.kind = EA_BAD;
          illegal(cpu);
      }
  }
  return e;
}

static uint32_t ea_read(cf_cpu* cpu, ea_t e, int sz) {
  switch (e.kind) {
    case EA_D:
    case EA_A:
      return cpu->r[e.a] & szmask(sz);
    case EA_MEM:
      return sz == 4 ? rd32(cpu, e.a) : sz == 2 ? rd16(cpu, e.a)
                                                : rd8(cpu, e.a);
    case EA_IMM:
      return e.a;
    default:
      return 0;
  }
}

static void ea_write(cf_cpu* cpu, ea_t e, int sz, uint32_t v) {
  switch (e.kind) {
    case EA_D: {
      uint32_t m = szmask(sz);
      cpu->r[e.a] = (cpu->r[e.a] & ~m) | (v & m);
      break;
    }
    case EA_A:
      cpu->r[e.a] = v;
      break;
    case EA_MEM:
      if (sz == 4)
        wr32(cpu, e.a, v);
      else if (sz == 2)
        wr16(cpu, e.a, v);
      else
        wr8(cpu, e.a, v);
      break;
    default:
      illegal(cpu);
  }
}

// address of a control-mode EA (lea, pea, jsr, jmp, movem)
static uint32_t ea_addr(cf_cpu* cpu, int mode, int reg) {
  ea_t e = ea_get(cpu, mode, reg, 4);
  if (e.kind != EA_MEM)
    illegal(cpu);
  return e.a;
}

// --- EMAC --------------------------------------------------------------------

#define MACSR_OMC 0x80
#define MACSR_SU 0x40
#define MACSR_FI 0x20
#define MACSR_R 0x10

static uint32_t acc_to_reg(cf_cpu* cpu, int i) {
  __int128 a = cpu->acc[i];
  __int128 v = a;
  if (cpu->macsr & MACSR_FI)
    v = (cpu->macsr & MACSR_R) ? (a + ((__int128)1 << 30)) >> 31 : a >> 31;
  if (cpu->macsr & MACSR_OMC) {
    if (v > INT32_MAX)
      v = INT32_MAX;
    if (v < INT32_MIN)
      v = INT32_MIN;
  }
  return (uint32_t)(uint64_t)v;
}

static int64_t mac_operand(cf_cpu* cpu, int reg, int word, int upper) {
  uint32_t v = cpu->r[reg];
  if (word) {
    v = (upper ? v >> 16 : v) & 0xffff;
    return (cpu->macsr & MACSR_SU) ? (int64_t)v : (int64_t)(int16_t)v;
  }
  return (cpu->macsr & MACSR_SU) ? (int64_t)v : (int64_t)(int32_t)v;
}

static void mac(cf_cpu* cpu, uint32_t ext, int rx, int ry, int acc) {
  int word = !(ext & 0x800);
  int64_t a = mac_operand(cpu, ry, word, ext & 0x40);
  int64_t b = mac_operand(cpu, rx, word, ext & 0x80);
  __int128 p = (__int128)a * b;
  if (word && (cpu->macsr & MACSR_FI))
    p <<= 32;  // Q15*Q15 -> same 2^-62 scale
  switch ((ext >> 9) & 3) {
    case 1:
      p <<= 1;
      break;
    case 3:
      p >>= 1;
      break;
  }
  if (cpu->macsr & MACSR_FI)
    p = (p >> 23) << 23;  // 8 guard bits below the LSB
  if (ext & 0x100)
    cpu->acc[acc] -= p;
  else
    cpu->acc[acc] += p;
}

static void line_a(cf_cpu* cpu, uint32_t op) {
  if (!(op & 0x100)) {
    uint32_t ext = fetch16(cpu);
    if (!(op & 0x30)) {  // mac/msac Ry,Rx,ACCx
      int rx = ((op >> 9) & 7) | ((op >> 3) & 8);
      int ry = op & 15;
      mac(cpu, ext, rx, ry, ((ext >> 3) & 2) | ((op >> 7) & 1));
      return;
    }
    // mac/msac with load: Ry,Rx,<ea>,Rw,ACCx (ACC lsb is stored inverted here)
    int rw = ((op >> 9) & 7) | ((op >> 3) & 8);
    int mode = (op >> 3) & 7, reg = op & 7;
    int acc = ((ext >> 3) & 2) | (((op >> 7) & 1) ^ 1);
    uint32_t base = A(reg), addr = base, post = base;
    if (mode == 3)
      post = base + 4;
    else if (mode == 4)
      addr = post = base - 4;
    else if (mode == 5)
      addr = base + (uint32_t)(int32_t)(int16_t)fetch16(cpu);
    else if (mode != 2) {
      illegal(cpu);
      return;
    }
    if (ext & 0x20)
      addr &= cpu->mask;
    uint32_t val = rd32(cpu, addr);
    mac(cpu, ext, ext >> 12, ext & 15, acc);
    cpu->r[rw] = val;
    if (mode == 3 || mode == 4)
      A(reg) = post;
    return;
  }
  int sub = (op >> 6) & 3;
  int hi = op & 0x800;
  int which = (op >> 9) & 3;
  if (sub == 1) {  // mov3q #imm,<ea>
    uint32_t v = (op >> 9) & 7;
    if (!v)
      v = 0xffffffffu;
    ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, 4);
    ea_write(cpu, e, 4, v);
    logic(cpu, v, 4);
    return;
  }
  if (sub == 0 && !hi && ((op >> 4) & 3) == 1) {  // move.l ACCy,ACCx
    cpu->acc[which] = cpu->acc[op & 3];
    return;
  }
  if (sub == 0) {  // move.l <ea>,ACC/MACSR/MASK
    ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, 4);
    uint32_t v = ea_read(cpu, e, 4);
    if (!hi)
      cpu->acc[which] = (cpu->macsr & MACSR_FI) ? (__int128)(int32_t)v << 31 : (__int128)(int32_t)v;
    else if (which == 0)
      cpu->macsr = v & 0xff;
    else if (which == 2)
      cpu->mask = v | 0xffff0000u;
    else
      illegal(cpu);  // ACCEXT: not used by the engines
    return;
  }
  if (sub == 2) {  // move.l ACC/MACSR/MASK,Rx
    if (!hi)
      cpu->r[op & 15] = acc_to_reg(cpu, which);
    else if (which == 0)
      cpu->r[op & 15] = cpu->macsr;
    else if (which == 2)
      cpu->r[op & 15] = cpu->mask;
    else
      illegal(cpu);
    return;
  }
  if (!hi) {  // movclr.l ACCy,Rx
    cpu->r[op & 15] = acc_to_reg(cpu, which);
    cpu->acc[which] = 0;
    return;
  }
  illegal(cpu);  // move MACSR,CCR
}

// --- instruction groups ----------------------------------------------------

static void bitop(cf_cpu* cpu, int type, uint32_t bit, int mode, int reg) {
  int sz = mode == 0 ? 4 : 1;
  ea_t e = ea_get(cpu, mode, reg, sz);
  uint32_t v = ea_read(cpu, e, sz);
  uint32_t m = 1u << (bit & (sz == 4 ? 31 : 7));
  cpu->z = !(v & m);
  switch (type) {
    case 0:
      return;  // btst
    case 1:
      v ^= m;
      break;
    case 2:
      v &= ~m;
      break;
    case 3:
      v |= m;
      break;
  }
  ea_write(cpu, e, sz, v);
}

static void line_0(cf_cpu* cpu, uint32_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if ((op & 0xfff8) == 0x00c0) {  // bitrev
    uint32_t v = D(reg), r = 0;
    for (int i = 0; i < 32; i++) r |= ((v >> i) & 1) << (31 - i);
    D(reg) = r;
    return;
  }
  if ((op & 0xfff8) == 0x02c0) {  // byterev
    uint32_t v = D(reg);
    D(reg) = v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24;
    return;
  }
  if ((op & 0xfff8) == 0x04c0) {  // ff1
    uint32_t v = D(reg);
    logic(cpu, v, 4);
    D(reg) = v ? (uint32_t)__builtin_clz(v) : 32;
    return;
  }
  if (op & 0x100) {
    if (mode == 1) {
      illegal(cpu);
      return;
    }
    bitop(cpu, (op >> 6) & 3, D((op >> 9) & 7), mode, reg);
    return;
  }
  if ((op & 0xff00) == 0x0800) {
    uint32_t bit = fetch16(cpu) & 0xff;
    bitop(cpu, (op >> 6) & 3, bit, mode, reg);
    return;
  }
  int sz = szfield((op >> 6) & 3);
  if (!sz) {
    illegal(cpu);
    return;
  }
  uint32_t imm = sz == 4 ? fetch32(cpu) : fetch16(cpu) & szmask(sz);
  ea_t e = ea_get(cpu, mode, reg, sz);
  uint32_t d = ea_read(cpu, e, sz), r;
  switch ((op >> 9) & 7) {
    case 0:
      r = d | imm;
      logic(cpu, r, sz);
      break;
    case 1:
      r = d & imm;
      logic(cpu, r, sz);
      break;
    case 2:
      r = sub_flags(cpu, d, imm, sz, 1);
      break;
    case 3:
      r = add_flags(cpu, d, imm, sz);
      break;
    case 5:
      r = d ^ imm;
      logic(cpu, r, sz);
      break;
    case 6:
      sub_flags(cpu, d, imm, sz, 0);
      return;
    default:
      illegal(cpu);
      return;
  }
  ea_write(cpu, e, sz, r);
}

static void move(cf_cpu* cpu, uint32_t op) {
  int sz = (op >> 12) == 1 ? 1 : (op >> 12) == 3 ? 2
                                                 : 4;
  ea_t s = ea_get(cpu, (op >> 3) & 7, op & 7, sz);
  uint32_t v = ea_read(cpu, s, sz);
  int dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
  if (dmode == 1) {  // movea
    A(dreg) = sext(v, sz);
    return;
  }
  ea_t d = ea_get(cpu, dmode, dreg, sz);
  ea_write(cpu, d, sz, v);
  logic(cpu, v, sz);
}

static void movem(cf_cpu* cpu, uint32_t op, int load) {
  uint32_t mask = fetch16(cpu);
  uint32_t a = ea_addr(cpu, (op >> 3) & 7, op & 7);
  for (int i = 0; i < 16; i++) {
    if (!(mask & (1u << i)))
      continue;
    if (load)
      cpu->r[i] = rd32(cpu, a);
    else
      wr32(cpu, a, cpu->r[i]);
    a += 4;
  }
}

static void line_4(cf_cpu* cpu, uint32_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  switch (op) {
    case 0x4e71:
      return;  // nop
    case 0x4e75:
      cpu->pc = pop32(cpu);
      return;  // rts
  }
  if ((op & 0xf1c0) == 0x41c0 && mode != 0) {  // lea (mode 0 is extb.l)
    uint32_t a = ea_addr(cpu, mode, reg);
    A((op >> 9) & 7) = a;
    return;
  }
  switch (op & 0xfff8) {
    case 0x4e50: {  // link
      int32_t d = (int16_t)fetch16(cpu);
      push32(cpu, A(reg));
      A(reg) = SP;
      SP += (uint32_t)d;
      return;
    }
    case 0x4e58:  // unlk
      SP = A(reg);
      A(reg) = pop32(cpu);
      return;
    case 0x4840: {  // swap
      uint32_t v = D(reg);
      D(reg) = v >> 16 | v << 16;
      logic(cpu, D(reg), 4);
      return;
    }
    case 0x4880:
      D(reg) = (D(reg) & 0xffff0000u) | (sext(D(reg), 1) & 0xffff);
      logic(cpu, D(reg), 2);
      return;  // ext.w
    case 0x48c0:
      D(reg) = sext(D(reg), 2);
      logic(cpu, D(reg), 4);
      return;  // ext.l
    case 0x49c0:
      D(reg) = sext(D(reg), 1);
      logic(cpu, D(reg), 4);
      return;     // extb.l
    case 0x4c80:  // sats.l
      if (cpu->v)
        D(reg) = (D(reg) & 0x80000000u) ? 0x7fffffffu : 0x80000000u;
      logic(cpu, D(reg), 4);
      return;
  }
  switch (op & 0xffc0) {
    case 0x4e80: {  // jsr
      uint32_t a = ea_addr(cpu, mode, reg);
      push32(cpu, cpu->pc);
      cpu->pc = a;
      return;
    }
    case 0x4ec0:
      cpu->pc = ea_addr(cpu, mode, reg);
      return;  // jmp
    case 0x4840:
      push32(cpu, ea_addr(cpu, mode, reg));
      return;  // pea
    case 0x48c0:
      movem(cpu, op, 0);
      return;
    case 0x4cc0:
      movem(cpu, op, 1);
      return;
    case 0x42c0:  // move ccr,Dn
      D(reg) = (D(reg) & 0xffff0000u) | (cpu->x << 4 | cpu->n << 3 | cpu->z << 2 | cpu->v << 1 | cpu->c);
      return;
    case 0x44c0: {  // move <ea>,ccr
      ea_t e = ea_get(cpu, mode, reg, 2);
      uint32_t v = ea_read(cpu, e, 2);
      cpu->x = v >> 4 & 1, cpu->n = v >> 3 & 1, cpu->z = v >> 2 & 1, cpu->v = v >> 1 & 1, cpu->c = v & 1;
      return;
    }
    case 0x4c00: {  // muls.l / mulu.l
      uint32_t ext = fetch16(cpu);
      ea_t e = ea_get(cpu, mode, reg, 4);
      uint32_t s = ea_read(cpu, e, 4);
      if (ext & 0x400) {
        illegal(cpu);
        return;
      }
      uint32_t* dl = &D((ext >> 12) & 7);
      *dl = (ext & 0x800) ? (uint32_t)((int64_t)(int32_t)*dl * (int32_t)s) : *dl * s;
      logic(cpu, *dl, 4);
      return;
    }
    case 0x4c40: {  // divs.l / divu.l / rems.l / remu.l
      uint32_t ext = fetch16(cpu);
      ea_t e = ea_get(cpu, mode, reg, 4);
      uint32_t s = ea_read(cpu, e, 4);
      int dx = (ext >> 12) & 7, dw = ext & 7;
      if (!s) {
        fault(cpu, CF_ERR_DIV0, cpu->err_pc);
        return;
      }
      uint32_t q, r;
      if (ext & 0x800) {
        int32_t n = (int32_t)D(dx), d = (int32_t)s;
        if (n == INT32_MIN && d == -1) {
          cpu->v = 1;
          return;
        }
        q = (uint32_t)(n / d), r = (uint32_t)(n % d);
      } else {
        q = D(dx) / s, r = D(dx) % s;
      }
      logic(cpu, q, 4);
      if (dx == dw)
        D(dx) = q;
      else
        D(dw) = r;
      return;
    }
  }
  int sz = szfield((op >> 6) & 3);
  if (sz) {
    switch (op & 0xff00) {
      case 0x4200: {  // clr
        ea_t e = ea_get(cpu, mode, reg, sz);
        ea_write(cpu, e, sz, 0);
        logic(cpu, 0, sz);
        return;
      }
      case 0x4400: {  // neg
        ea_t e = ea_get(cpu, mode, reg, sz);
        uint32_t v = sub_flags(cpu, 0, ea_read(cpu, e, sz), sz, 1);
        ea_write(cpu, e, sz, v);
        return;
      }
      case 0x4000: {  // negx
        ea_t e = ea_get(cpu, mode, reg, sz);
        uint32_t d = ea_read(cpu, e, sz), m = szmask(sz);
        uint32_t r = (0 - d - cpu->x) & m;
        int z = cpu->z;
        cpu->c = cpu->x = ((uint64_t)(d & m) + cpu->x) > 0;
        cpu->v = (d & r & szmsb(sz)) != 0;
        nz(cpu, r, sz);
        cpu->z = r ? 0 : z;
        ea_write(cpu, e, sz, r);
        return;
      }
      case 0x4600: {  // not
        ea_t e = ea_get(cpu, mode, reg, sz);
        uint32_t v = ~ea_read(cpu, e, sz);
        ea_write(cpu, e, sz, v);
        logic(cpu, v, sz);
        return;
      }
      case 0x4a00: {  // tst
        ea_t e = ea_get(cpu, mode, reg, sz);
        logic(cpu, ea_read(cpu, e, sz), sz);
        return;
      }
    }
  }
  if ((op & 0xffc0) == 0x4ac0 && op != 0x4ac8 && op != 0x4afc) {  // tas
    ea_t e = ea_get(cpu, mode, reg, 1);
    uint32_t v = ea_read(cpu, e, 1);
    logic(cpu, v, 1);
    ea_write(cpu, e, 1, v | 0x80);
    return;
  }
  illegal(cpu);
}

static void line_5(cf_cpu* cpu, uint32_t op) {
  int mode = (op >> 3) & 7, reg = op & 7;
  if ((op & 0xc0) == 0xc0) {
    if ((op & 0xfff8) == 0x51f8 && reg >= 2 && reg <= 4) {  // tpf
      cpu->pc += reg == 2 ? 2 : reg == 3 ? 4
                                         : 0;
      return;
    }
    if (mode != 0) {
      illegal(cpu);
      return;
    }
    D(reg) = (D(reg) & 0xffffff00u) | (cond(cpu, (op >> 8) & 15) ? 0xff : 0);  // scc
    return;
  }
  int sz = szfield((op >> 6) & 3);
  uint32_t q = (op >> 9) & 7;
  if (!q)
    q = 8;
  ea_t e = ea_get(cpu, mode, reg, sz);
  if (e.kind == EA_A) {
    cpu->r[e.a] += (op & 0x100) ? (uint32_t)-q : q;
    return;
  }
  uint32_t d = ea_read(cpu, e, sz);
  uint32_t r = (op & 0x100) ? sub_flags(cpu, d, q, sz, 1) : add_flags(cpu, d, q, sz);
  ea_write(cpu, e, sz, r);
}

static void line_6(cf_cpu* cpu, uint32_t op) {
  uint32_t base = cpu->pc;
  int32_t d = (int8_t)op;
  if (d == 0)
    d = (int16_t)fetch16(cpu);
  else if (d == -1)
    d = (int32_t)fetch32(cpu);
  int cc = (op >> 8) & 15;
  if (cc == 1) {  // bsr
    push32(cpu, cpu->pc);
    cpu->pc = base + (uint32_t)d;
    return;
  }
  if (cond(cpu, cc))
    cpu->pc = base + (uint32_t)d;
}

static void line_7(cf_cpu* cpu, uint32_t op) {
  int dn = (op >> 9) & 7;
  if (!(op & 0x100)) {  // moveq
    D(dn) = (uint32_t)(int32_t)(int8_t)op;
    logic(cpu, D(dn), 4);
    return;
  }
  int sz = (op & 0x40) ? 2 : 1;
  ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, sz);
  uint32_t v = ea_read(cpu, e, sz);
  D(dn) = (op & 0x80) ? v : sext(v, sz);  // mvz : mvs
  logic(cpu, D(dn), 4);
}

// or / and / sub / add / cmp / eor with a data register
static void alu(cf_cpu* cpu, uint32_t op, int kind) {
  int mode = (op >> 3) & 7, reg = op & 7, dn = (op >> 9) & 7;
  int sz = szfield((op >> 6) & 3);
  ea_t e = ea_get(cpu, mode, reg, sz);
  uint32_t s = ea_read(cpu, e, sz), d = D(dn), r;
  int to_ea = (op & 0x100) && kind != 'c';
  if (to_ea) {
    uint32_t t = s;
    s = d;
    d = t;
  }
  switch (kind) {
    case '|':
      r = d | s;
      logic(cpu, r, sz);
      break;
    case '&':
      r = d & s;
      logic(cpu, r, sz);
      break;
    case '^':
      r = d ^ s;
      logic(cpu, r, sz);
      break;
    case '-':
      r = sub_flags(cpu, d, s, sz, 1);
      break;
    case '+':
      r = add_flags(cpu, d, s, sz);
      break;
    default:
      sub_flags(cpu, d, s, sz, 0);
      return;  // cmp
  }
  if (to_ea)
    ea_write(cpu, e, sz, r);
  else
    ea_write(cpu, (ea_t){EA_D, (uint32_t)dn}, sz, r);
}

static void addsub_a(cf_cpu* cpu, uint32_t op, int sub) {
  int sz = (op & 0x100) ? 4 : 2;
  ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, sz);
  uint32_t s = sext(ea_read(cpu, e, sz), sz);
  uint32_t* a = &A((op >> 9) & 7);
  *a = sub ? *a - s : *a + s;
}

static void addsubx(cf_cpu* cpu, uint32_t op, int sub) {
  if (op & 8) {  // -(Ay),-(Ax): not on ColdFire
    illegal(cpu);
    return;
  }
  uint32_t s = D(op & 7), *dp = &D((op >> 9) & 7), d = *dp, x = cpu->x, r;
  int z = cpu->z;
  if (sub) {
    r = d - s - x;
    cpu->c = (uint64_t)s + x > d;
    cpu->v = ((s ^ d) & (r ^ d) & 0x80000000u) != 0;
  } else {
    r = d + s + x;
    cpu->c = (uint64_t)d + s + x > 0xffffffffu;
    cpu->v = ((s ^ r) & (d ^ r) & 0x80000000u) != 0;
  }
  cpu->x = cpu->c;
  nz(cpu, r, 4);
  cpu->z = r ? 0 : z;
  *dp = r;
}

static void mul_div_w(cf_cpu* cpu, uint32_t op, int div) {
  int sgn = op & 0x100;
  ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, 2);
  uint32_t s = ea_read(cpu, e, 2), *dp = &D((op >> 9) & 7);
  if (!div) {
    *dp = sgn ? (uint32_t)((int32_t)(int16_t)*dp * (int16_t)s) : (*dp & 0xffff) * s;
    logic(cpu, *dp, 4);
    return;
  }
  if (!s) {
    fault(cpu, CF_ERR_DIV0, cpu->err_pc);
    return;
  }
  uint32_t q, r;
  if (sgn) {
    int32_t n = (int32_t)*dp, d = (int16_t)s;
    if (n == INT32_MIN && d == -1) {
      cpu->v = 1;
      return;
    }
    int32_t qq = n / d;
    if (qq > 32767 || qq < -32768) {
      cpu->v = 1;
      return;
    }
    q = (uint32_t)qq, r = (uint32_t)(n % d);
  } else {
    q = *dp / s, r = *dp % s;
    if (q > 0xffff) {
      cpu->v = 1;
      return;
    }
  }
  logic(cpu, q, 2);
  *dp = (r & 0xffff) << 16 | (q & 0xffff);
}

// .l register shift; arith: asl/asr, else lsl/lsr
static inline void shift(cf_cpu* cpu, int left, int arith, uint32_t cnt, int reg) {
  uint32_t d = D(reg), r;
  if (!cnt) {
    r = d;
    cpu->c = 0;
  } else if (left) {
    cpu->c = cnt <= 32 ? (d >> (32 - cnt)) & 1 : 0;
    r = cnt < 32 ? d << cnt : 0;
  } else if (!arith || !(d & 0x80000000u)) {
    cpu->c = cnt <= 32 ? (d >> (cnt - 1)) & 1 : 0;
    r = cnt < 32 ? d >> cnt : 0;
  } else {  // asr of a negative value
    cpu->c = cnt <= 32 ? (d >> (cnt - 1)) & 1 : 1;
    r = cnt < 32 ? (uint32_t)((int32_t)d >> cnt) : 0xffffffffu;
  }
  if (cnt)
    cpu->x = cpu->c;
  D(reg) = r;
  nz(cpu, r, 4);
  cpu->v = 0;
}

static void line_e(cf_cpu* cpu, uint32_t op) {
  int type = (op >> 3) & 3;
  if ((op & 0xc0) != 0x80 || type > 1) {  // only .l register as/ls on ColdFire
    illegal(cpu);
    return;
  }
  uint32_t cnt = (op & 0x20) ? D((op >> 9) & 7) & 63 : (((op >> 9) & 7) ?: 8);
  shift(cpu, op & 0x100, type == 0, cnt, op & 7);
}

static void step(cf_cpu* cpu) {
  cpu->err_pc = cpu->pc;
  uint32_t op = fetch16(cpu);
  cpu->err_op = (uint16_t)op;
  switch (op >> 12) {
    case 0x0:
      line_0(cpu, op);
      break;
    case 0x1:
    case 0x2:
    case 0x3:
      move(cpu, op);
      break;
    case 0x4:
      line_4(cpu, op);
      break;
    case 0x5:
      line_5(cpu, op);
      break;
    case 0x6:
      line_6(cpu, op);
      break;
    case 0x7:
      line_7(cpu, op);
      break;
    case 0x8:
      if ((op & 0xc0) == 0xc0)
        mul_div_w(cpu, op, 1);
      else
        alu(cpu, op, '|');
      break;
    case 0x9:
    case 0xd: {
      int sub = (op >> 12) == 0x9;
      if ((op & 0xc0) == 0xc0)
        addsub_a(cpu, op, sub);
      else if ((op & 0x130) == 0x100)
        addsubx(cpu, op, sub);
      else
        alu(cpu, op, sub ? '-' : '+');
      break;
    }
    case 0xa:
      line_a(cpu, op);
      break;
    case 0xb:
      if ((op & 0xc0) == 0xc0) {  // cmpa
        int sz = (op & 0x100) ? 4 : 2;
        ea_t e = ea_get(cpu, (op >> 3) & 7, op & 7, sz);
        sub_flags(cpu, A((op >> 9) & 7), sext(ea_read(cpu, e, sz), sz), 4, 0);
      } else if (op & 0x100) {
        if (((op >> 3) & 7) == 1)
          illegal(cpu);  // cmpm
        else
          alu(cpu, op, '^');
      } else {
        alu(cpu, op, 'c');
      }
      break;
    case 0xc:
      if ((op & 0xc0) == 0xc0)
        mul_div_w(cpu, op, 0);
      else if ((op & 0x130) == 0x100)
        illegal(cpu);  // abcd/exg
      else
        alu(cpu, op, '&');
      break;
    case 0xe:
      line_e(cpu, op);
      break;
    default:
      illegal(cpu);
  }
}

// --- predecode ---------------------------------------------------------------
//
// Each code address is decoded once into a cf_dec with a specialized handler
// for the forms the engines spend their time in (long moves and ALU ops on
// data registers, EMAC, branches). Everything else runs through step().

enum { M_REG = 1,
       M_IMM,
       M_IND,
       M_POST,
       M_PRE,
       M_DISP,
       M_INDEX,
       M_ABS };

enum { K_ADD = 1,
       K_SUB,
       K_AND,
       K_OR,
       K_EOR,
       K_CMP,
       K_TST };

__attribute__((always_inline)) static inline uint32_t addr_of(cf_cpu* cpu, int m, int r, int x, int xf, uint32_t imm) {
  switch (m) {
    case M_IND:
      return cpu->r[r];
    case M_POST: {
      uint32_t a = cpu->r[r];
      cpu->r[r] = a + 4;
      return a;
    }
    case M_PRE:
      return cpu->r[r] -= 4;
    case M_DISP:
      return cpu->r[r] + imm;
    case M_INDEX: {
      uint32_t xv = cpu->r[x];
      if (!(xf & 4))
        xv = sext(xv, 2);
      return cpu->r[r] + imm + (xv << (xf & 3));
    }
    default:
      return imm;  // M_ABS
  }
}

#define SRC_ADDR(d) addr_of(cpu, (d)->smode, (d)->sreg, (d)->sx, (d)->sxf, (d)->simm)
#define DST_ADDR(d) addr_of(cpu, (d)->dmode, (d)->dreg, (d)->dx, (d)->dxf, (d)->dimm)

__attribute__((always_inline)) static inline uint32_t src_l(cf_cpu* cpu, const cf_dec* d) {
  if (d->smode == M_REG)
    return cpu->r[d->sreg];
  if (d->smode == M_IMM)
    return d->simm;
  return rd32(cpu, SRC_ADDR(d));
}

static void h_generic(cf_cpu* cpu, const cf_dec* d) {
  cpu->pc = d->pc;
  step(cpu);
}

static void h_move_l(cf_cpu* cpu, const cf_dec* d) {
  uint32_t v = src_l(cpu, d);
  if (d->dmode == M_REG) {
    cpu->r[d->dreg] = v;
    if (d->dreg < 8)
      logic(cpu, v, 4);  // movea leaves the flags alone
  } else {
    wr32(cpu, DST_ADDR(d), v);
    logic(cpu, v, 4);
  }
  cpu->pc = d->next;
}

static void h_clr_l(cf_cpu* cpu, const cf_dec* d) {
  if (d->dmode == M_REG)
    cpu->r[d->dreg] = 0;
  else
    wr32(cpu, DST_ADDR(d), 0);
  logic(cpu, 0, 4);
  cpu->pc = d->next;
}

// <ea>.l op Dn -> Dn
static void h_alu_dn(cf_cpu* cpu, const cf_dec* d) {
  uint32_t s = src_l(cpu, d), *dp = &cpu->r[d->dreg];
  switch (d->kind) {
    case K_ADD:
      *dp = add_flags(cpu, *dp, s, 4);
      break;
    case K_SUB:
      *dp = sub_flags(cpu, *dp, s, 4, 1);
      break;
    case K_AND:
      *dp &= s;
      logic(cpu, *dp, 4);
      break;
    case K_OR:
      *dp |= s;
      logic(cpu, *dp, 4);
      break;
    case K_EOR:
      *dp ^= s;
      logic(cpu, *dp, 4);
      break;
    case K_CMP:
      sub_flags(cpu, *dp, s, 4, 0);
      break;
    case K_TST:
      logic(cpu, s, 4);
      break;
  }
  cpu->pc = d->next;
}

static void h_adda(cf_cpu* cpu, const cf_dec* d) {
  uint32_t s = src_l(cpu, d);
  cpu->r[d->dreg] += d->kind == K_SUB ? (uint32_t)-s : s;
  cpu->pc = d->next;
}

static void h_cmpa(cf_cpu* cpu, const cf_dec* d) {
  sub_flags(cpu, cpu->r[d->dreg], src_l(cpu, d), 4, 0);
  cpu->pc = d->next;
}

static void h_moveq(cf_cpu* cpu, const cf_dec* d) {
  cpu->r[d->dreg] = d->simm;
  logic(cpu, d->simm, 4);
  cpu->pc = d->next;
}

static void h_lea(cf_cpu* cpu, const cf_dec* d) {
  cpu->r[d->dreg] = SRC_ADDR(d);
  cpu->pc = d->next;
}

static void h_pea(cf_cpu* cpu, const cf_dec* d) {
  push32(cpu, SRC_ADDR(d));
  cpu->pc = d->next;
}

static void h_jsr(cf_cpu* cpu, const cf_dec* d) {
  uint32_t a = SRC_ADDR(d);
  push32(cpu, d->next);
  cpu->pc = a;
}

static void h_jmp(cf_cpu* cpu, const cf_dec* d) {
  cpu->pc = SRC_ADDR(d);
}

static void h_rts(cf_cpu* cpu, const cf_dec* d) {
  (void)d;
  cpu->pc = pop32(cpu);
}

static void h_bra(cf_cpu* cpu, const cf_dec* d) {
  cpu->pc = d->dimm;
}

static void h_bsr(cf_cpu* cpu, const cf_dec* d) {
  push32(cpu, d->next);
  cpu->pc = d->dimm;
}

static void h_bcc(cf_cpu* cpu, const cf_dec* d) {
  cpu->pc = cond(cpu, d->a) ? d->dimm : d->next;
}

static void h_shift(cf_cpu* cpu, const cf_dec* d) {
  uint32_t cnt = d->smode == M_REG ? cpu->r[d->sreg] & 63 : d->simm;
  shift(cpu, d->a, d->b, cnt, d->dreg);
  cpu->pc = d->next;
}

static void h_sats(cf_cpu* cpu, const cf_dec* d) {
  uint32_t* r = &cpu->r[d->dreg];
  if (cpu->v)
    *r = (*r & 0x80000000u) ? 0x7fffffffu : 0x80000000u;
  logic(cpu, *r, 4);
  cpu->pc = d->next;
}

// mac.l product as mac() computes it (.l, no scale factor)
__attribute__((always_inline)) static inline __int128 mac_l(cf_cpu* cpu, uint32_t ry, uint32_t rx) {
  if (!(cpu->macsr & MACSR_SU)) {
    int64_t p = (int64_t)(int32_t)ry * (int32_t)rx;
    if (cpu->macsr & MACSR_FI)
      p &= ~(int64_t)((1 << 23) - 1);
    return p;
  }
  __int128 p = (__int128)(uint64_t)ry * (uint64_t)rx;
  if (cpu->macsr & MACSR_FI)
    p = (p >> 23) << 23;
  return p;
}

// a = Rx, b = Ry, c = ACC, kind = msac
static void h_mac(cf_cpu* cpu, const cf_dec* d) {
  __int128 p = mac_l(cpu, cpu->r[d->b], cpu->r[d->a]);
  if (d->kind)
    cpu->acc[d->c] -= p;
  else
    cpu->acc[d->c] += p;
  cpu->pc = d->next;
}

// as h_mac, plus load: smode/sreg/simm = (An) (An)+ -(An) d16(An), dreg = Rw, dx = use MASK
static void h_macld(cf_cpu* cpu, const cf_dec* d) {
  uint32_t base = cpu->r[d->sreg], addr = base;
  if (d->smode == M_PRE)
    addr = base - 4;
  else if (d->smode == M_DISP)
    addr = base + d->simm;
  if (d->dx)
    addr &= cpu->mask;
  uint32_t val = rd32(cpu, addr);
  __int128 p = mac_l(cpu, cpu->r[d->b], cpu->r[d->a]);
  if (d->kind)
    cpu->acc[d->c] -= p;
  else
    cpu->acc[d->c] += p;
  cpu->r[d->dreg] = val;
  if (d->smode == M_POST)
    cpu->r[d->sreg] = base + 4;
  else if (d->smode == M_PRE)
    cpu->r[d->sreg] = base - 4;
  cpu->pc = d->next;
}

static void h_toacc(cf_cpu* cpu, const cf_dec* d) {
  int32_t v = (int32_t)src_l(cpu, d);
  cpu->acc[d->c] = (cpu->macsr & MACSR_FI) ? (__int128)v << 31 : (__int128)v;
  cpu->pc = d->next;
}

static void h_fromacc(cf_cpu* cpu, const cf_dec* d) {
  cpu->r[d->dreg] = acc_to_reg(cpu, d->c);
  cpu->pc = d->next;
}

static void h_movclr(cf_cpu* cpu, const cf_dec* d) {
  cpu->r[d->dreg] = acc_to_reg(cpu, d->c);
  cpu->acc[d->c] = 0;
  cpu->pc = d->next;
}

// a .l operand, read without side effects; *pc moves past its extension words
static int dec_ea(cf_cpu* cpu, uint32_t* pc, int mode, int reg, uint8_t* m, uint8_t* r, uint8_t* x, uint8_t* xf, uint32_t* imm) {
  switch (mode) {
    case 0:
      *m = M_REG;
      *r = (uint8_t)reg;
      return 1;
    case 1:
      *m = M_REG;
      *r = (uint8_t)(8 + reg);
      return 1;
    case 2:
      *m = M_IND;
      *r = (uint8_t)(8 + reg);
      return 1;
    case 3:
      *m = M_POST;
      *r = (uint8_t)(8 + reg);
      return 1;
    case 4:
      *m = M_PRE;
      *r = (uint8_t)(8 + reg);
      return 1;
    case 5:
      *m = M_DISP;
      *r = (uint8_t)(8 + reg);
      *imm = sext(rd16(cpu, *pc), 2);
      *pc += 2;
      return 1;
    case 6: {
      uint32_t ext = rd16(cpu, *pc);
      if (ext & 0x100)
        return 0;
      *pc += 2;
      *m = M_INDEX;
      *r = (uint8_t)(8 + reg);
      *x = (uint8_t)(ext >> 12);
      *xf = (uint8_t)(((ext >> 9) & 3) | ((ext & 0x800) ? 4 : 0));
      *imm = sext(ext & 0xff, 1);
      return 1;
    }
  }
  switch (reg) {
    case 0:
      *m = M_ABS;
      *imm = sext(rd16(cpu, *pc), 2);
      *pc += 2;
      return 1;
    case 1:
      *m = M_ABS;
      *imm = rd32(cpu, *pc);
      *pc += 4;
      return 1;
    case 2:
      *m = M_ABS;
      *imm = *pc + sext(rd16(cpu, *pc), 2);
      *pc += 2;
      return 1;
    case 4:
      *m = M_IMM;
      *imm = rd32(cpu, *pc);
      *pc += 4;
      return 1;
  }
  return 0;
}

#define DEC_SRC(mode, reg) dec_ea(cpu, &p, (mode), (reg), &d->smode, &d->sreg, &d->sx, &d->sxf, &d->simm)
#define DEC_DST(mode, reg) dec_ea(cpu, &p, (mode), (reg), &d->dmode, &d->dreg, &d->dx, &d->dxf, &d->dimm)

static int is_mem(int mode) {
  return mode >= M_IND && mode != M_IMM;
}

static void decode(cf_cpu* cpu, cf_dec* d, uint32_t pc) {
  uint32_t op = rd16(cpu, pc), p = pc + 2;
  int mode = (op >> 3) & 7, reg = op & 7, rn = (op >> 9) & 7;
  memset(d, 0, sizeof(*d));
  d->pc = pc;
  d->fn = h_generic;

  switch (op >> 12) {
    case 0x0: {  // immediate .l to Dn
      static const uint8_t kinds[8] = {K_OR, K_AND, K_SUB, K_ADD, 0, K_EOR, K_CMP, 0};
      if ((op & 0xf1f8) == 0x0080 && kinds[rn]) {
        d->kind = kinds[rn];
        d->smode = M_IMM;
        d->simm = rd32(cpu, p);
        p += 4;
        d->dreg = (uint8_t)reg;
        d->fn = h_alu_dn;
      }
      break;
    }
    case 0x2:  // move.l / movea.l
      if (DEC_SRC(mode, reg)) {
        int dmode = (op >> 6) & 7;
        if (dmode == 1) {
          d->dmode = M_REG;
          d->dreg = (uint8_t)(8 + rn);
          d->fn = h_move_l;
        } else if (dmode != 7 && DEC_DST(dmode, rn)) {
          d->fn = h_move_l;
        }
      }
      break;
    case 0x4:
      if (op == 0x4e75) {
        d->fn = h_rts;
      } else if ((op & 0xf1c0) == 0x41c0 && mode != 0) {
        if (DEC_SRC(mode, reg) && is_mem(d->smode) && d->smode != M_POST && d->smode != M_PRE) {
          d->dreg = (uint8_t)(8 + rn);
          d->fn = h_lea;
        }
      } else if ((op & 0xffc0) == 0x4e80 || (op & 0xffc0) == 0x4ec0 || ((op & 0xffc0) == 0x4840 && mode >= 2)) {
        if (DEC_SRC(mode, reg) && is_mem(d->smode) && d->smode != M_POST && d->smode != M_PRE) {
          d->fn = (op & 0xffc0) == 0x4e80 ? h_jsr : (op & 0xffc0) == 0x4ec0 ? h_jmp
                                                                            : h_pea;
        }
      } else if ((op & 0xfff8) == 0x4c80) {
        d->dreg = (uint8_t)reg;
        d->fn = h_sats;
      } else if ((op & 0xffc0) == 0x4280) {
        if (DEC_DST(mode, reg) && d->dmode != M_IMM && d->dmode != M_ABS)
          d->fn = h_clr_l;
      } else if ((op & 0xffc0) == 0x4a80) {
        if (DEC_SRC(mode, reg)) {
          d->kind = K_TST;
          d->dreg = 0;
          d->fn = h_alu_dn;
        }
      }
      break;
    case 0x5:  // addq/subq .l
      if ((op & 0xc0) == 0x80 && mode <= 1) {
        d->smode = M_IMM;
        d->simm = rn ? (uint32_t)rn : 8;
        d->kind = (op & 0x100) ? K_SUB : K_ADD;
        d->dreg = (uint8_t)(mode ? 8 + reg : reg);
        d->fn = mode ? h_adda : h_alu_dn;
      }
      break;
    case 0x6: {
      int32_t disp = (int8_t)op;
      if (disp == 0) {
        disp = (int16_t)rd16(cpu, p);
        p += 2;
      } else if (disp == -1) {
        disp = (int32_t)rd32(cpu, p);
        p += 4;
      }
      d->dimm = pc + 2 + (uint32_t)disp;
      d->a = (uint8_t)((op >> 8) & 15);
      d->fn = d->a == 0 ? h_bra : d->a == 1 ? h_bsr
                                            : h_bcc;
      break;
    }
    case 0x7:
      if (!(op & 0x100)) {
        d->dreg = (uint8_t)rn;
        d->simm = sext(op & 0xff, 1);
        d->fn = h_moveq;
      }
      break;
    case 0x8:
    case 0x9:
    case 0xb:
    case 0xc:
    case 0xd: {
      int opmode = (op >> 6) & 7, line = op >> 12;
      if (opmode == 2 && DEC_SRC(mode, reg)) {  // <ea>.l op Dn
        static const uint8_t kinds[16] = {[0x8] = K_OR, [0x9] = K_SUB, [0xb] = K_CMP, [0xc] = K_AND, [0xd] = K_ADD};
        d->kind = kinds[line];
        d->dreg = (uint8_t)rn;
        d->fn = h_alu_dn;
      } else if (opmode == 7 && (line == 0x9 || line == 0xb || line == 0xd) && DEC_SRC(mode, reg)) {  // suba/cmpa/adda .l
        d->dreg = (uint8_t)(8 + rn);
        d->kind = line == 0x9 ? K_SUB : K_ADD;
        d->fn = line == 0xb ? h_cmpa : h_adda;
      }
      break;
    }
    case 0xa:
      if (!(op & 0x100)) {
        uint32_t ext = rd16(cpu, p);
        p += 2;
        if (!(ext & 0x800) || ((ext >> 9) & 3))
          break;  // .w or scale factor: generic
        d->kind = (ext & 0x100) != 0;
        if (!(op & 0x30)) {
          d->a = (uint8_t)(((op >> 9) & 7) | ((op >> 3) & 8));
          d->b = (uint8_t)(op & 15);
          d->c = (uint8_t)(((ext >> 3) & 2) | ((op >> 7) & 1));
          d->fn = h_mac;
        } else if (mode >= 2 && mode <= 5) {
          d->a = (uint8_t)(ext >> 12);
          d->b = (uint8_t)(ext & 15);
          d->c = (uint8_t)(((ext >> 3) & 2) | (((op >> 7) & 1) ^ 1));
          d->dreg = (uint8_t)(((op >> 9) & 7) | ((op >> 3) & 8));
          d->dx = (ext & 0x20) != 0;
          d->sreg = (uint8_t)(8 + reg);
          d->smode = mode == 2 ? M_IND : mode == 3 ? M_POST
                                     : mode == 4   ? M_PRE
                                                   : M_DISP;
          if (mode == 5) {
            d->simm = sext(rd16(cpu, p), 2);
            p += 2;
          }
          d->fn = h_macld;
        }
      } else if (!(op & 0x800)) {
        int sub = (op >> 6) & 3;
        d->c = (uint8_t)((op >> 9) & 3);
        if (sub == 0 && ((op >> 4) & 3) != 1 && (mode <= 1 || (mode == 7 && reg == 4)) && DEC_SRC(mode, reg)) {
          d->fn = h_toacc;
        } else if (sub == 2) {
          d->dreg = (uint8_t)(op & 15);
          d->fn = h_fromacc;
        } else if (sub == 3) {
          d->dreg = (uint8_t)(op & 15);
          d->fn = h_movclr;
        }
      }
      break;
    case 0xe: {  // .l register shifts
      int type = (op >> 3) & 3;
      if ((op & 0xc0) == 0x80 && type <= 1) {
        d->a = (op & 0x100) != 0;
        d->b = type == 0;
        d->dreg = (uint8_t)reg;
        if (op & 0x20) {
          d->smode = M_REG;
          d->sreg = (uint8_t)rn;
        } else {
          d->smode = M_IMM;
          d->simm = rn ? (uint32_t)rn : 8;
        }
        d->fn = h_shift;
      }
      break;
    }
  }
  d->next = p;
  if (d->fn == h_generic)
    d->next = 0;
}

// Handler numbers: the run loop dispatches with a switch built without jump
// tables (-fno-jump-tables), so each instruction costs a few compares and a
// direct call. An indirect call or a br_table per instruction is several times
// slower on some wasm runtimes (wasmtime 39 on arm64, which poketrack uses).
#define HANDLERS \
  X(h_generic)   \
  X(h_move_l)    \
  X(h_clr_l)     \
  X(h_alu_dn)    \
  X(h_adda)      \
  X(h_cmpa)      \
  X(h_moveq)     \
  X(h_lea)       \
  X(h_pea)       \
  X(h_jsr)       \
  X(h_jmp)       \
  X(h_rts)       \
  X(h_bra)       \
  X(h_bsr)       \
  X(h_bcc)       \
  X(h_shift)     \
  X(h_sats)      \
  X(h_mac)       \
  X(h_macld)     \
  X(h_toacc)     \
  X(h_fromacc)   \
  X(h_movclr)

enum {
  H_DECODE = 0,
#define X(n) H_##n,
  HANDLERS
#undef X
};

static void set_handler(cf_dec* d) {
#define X(n)      \
  if (d->fn == n) \
    d->h = H_##n;
  HANDLERS
#undef X
}

int cf_set_code(cf_cpu* cpu, uint32_t lo, uint32_t hi) {
  free(cpu->dec);
  uint32_t n = (hi - lo) / 2;
  cpu->dec = calloc(n, sizeof(cf_dec));
  if (!cpu->dec)
    return CF_ERR_OOM;
  cpu->code_lo = lo;
  cpu->code_hi = hi;
  return CF_OK;
}

#ifdef CF_PROFILE
uint64_t cf_prof[0x20000];
#define PROFILE(pc)                  \
  if ((pc) - 0x40000000u < 0x40000u) \
    cf_prof[((pc) - 0x40000000u) >> 1]++;
#else
#define PROFILE(pc)
#endif

int cf_run(cf_cpu* cpu, uint32_t stop, uint64_t max) {
  uint64_t left = max;
  uint32_t lo = cpu->code_lo, span = cpu->code_hi - cpu->code_lo;
  cf_dec* dec = cpu->dec;
  while (!cpu->err) {
    uint32_t pc = cpu->pc, off = pc - lo;
    if (!left--) {
      fault(cpu, CF_ERR_BUDGET, pc);
      break;
    }
    PROFILE(pc)
    if (off < span) {  // the stop address is never inside the code
      cf_dec* d = &dec[off >> 1];
      if (!d->h) {
        decode(cpu, d, pc);
        set_handler(d);
      }
      switch (d->h) {
#define X(n)   \
  case H_##n:  \
    n(cpu, d); \
    break;
        HANDLERS
#undef X
      }
      if (cpu->err && cpu->err != CF_ERR_ILLEGAL)
        cpu->err_pc = pc;
    } else {
      if (pc == stop) {
        left++;
        break;
      }
      step(cpu);
    }
  }
  cpu->icount += max - left;
  return cpu->err;
}
