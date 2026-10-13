// Small ColdFire (ISA_C + EMAC) user-mode interpreter, enough to run the
// Syntakt's digital voice engines. Big-endian memory in lazily allocated
// 64 KB pages; only windows given to cf_map() are accessible.

#ifndef CF_H
#define CF_H

#include <stddef.h>
#include <stdint.h>

#define CF_PAGE_BITS 16
#define CF_PAGE_SIZE (1u << CF_PAGE_BITS)
#define CF_NPAGES (1u << (32 - CF_PAGE_BITS))

enum { CF_OK = 0,
       CF_ERR_ILLEGAL,
       CF_ERR_UNMAPPED,
       CF_ERR_BUDGET,
       CF_ERR_DIV0,
       CF_ERR_OOM };

struct cf_cpu;
struct cf_dec;
typedef void (*cf_handler)(struct cf_cpu* cpu, const struct cf_dec* d);

// one predecoded instruction: operands resolved, extension words consumed
typedef struct cf_dec {
  cf_handler fn;
  uint32_t pc, next;
  uint32_t simm, dimm;           // immediate, displacement or absolute address
  uint8_t smode, sreg, sx, sxf;  // source operand (sxf: index scale | long << 2)
  uint8_t dmode, dreg, dx, dxf;  // destination operand
  uint8_t kind, a, b, c;
} cf_dec;

typedef struct cf_cpu {
  uint32_t r[16];  // d0-d7, a0-a7 (a7 = sp)
  uint32_t pc;
  uint8_t x, n, z, v, c;
  __int128 acc[4];  // EMAC accumulators, kept exact (fractional unit 2^-62, as the reference bench)
  uint32_t macsr, mask;
  uint8_t* pages[CF_NPAGES];
  uint8_t mapped[CF_NPAGES];
  int err;
  uint32_t err_pc, err_addr;
  uint16_t err_op;
  uint64_t icount;
  cf_dec* dec;  // predecode cache for [code_lo, code_hi)
  uint32_t code_lo, code_hi;
} cf_cpu;

cf_cpu* cf_new(void);
void cf_free(cf_cpu* cpu);
void cf_map(cf_cpu* cpu, uint32_t base, uint32_t size);
void cf_write(cf_cpu* cpu, uint32_t addr, const void* src, size_t len);
void cf_read(cf_cpu* cpu, uint32_t addr, void* dst, size_t len);
uint32_t cf_rd32(cf_cpu* cpu, uint32_t addr);
void cf_wr32(cf_cpu* cpu, uint32_t addr, uint32_t v);

// predecode instructions in [lo, hi): the code must not be modified afterwards
int cf_set_code(cf_cpu* cpu, uint32_t lo, uint32_t hi);

// run from pc until it reaches stop (or an error, or max instructions); returns err
int cf_run(cf_cpu* cpu, uint32_t stop, uint64_t max);

#endif
