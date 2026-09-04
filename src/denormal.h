#pragma once
// Flush-to-zero for the audio thread.
//
// Every recursive filter in units/ decays its state toward zero, and state
// that lands in the denormal range stays there: a low cutoff IS a long time
// constant, so filter.c's SVF sits on denormal values for as long as its input
// is quiet. Measured: >90% of its output samples are denormal once the input
// goes silent, and 57% of its render blocks in a real song with an LFO
// sweeping the cutoff. phaser.c's allpass chain behaves the same way. (delay
// and reverb pass THROUGH the denormal window and reach exact zero, so they
// were never the problem.)
//
// On x86-64 each denormal SSE operation takes a microcode assist costing
// roughly two orders of magnitude more than a normal one — enough to blow a
// 512-frame deadline and stutter. ARM handles denormals in hardware at close
// to full speed, which is why this only ever showed up on Linux/x86 and never
// on an Apple Silicon Mac.
//
// The mode lives in a per-thread register (MXCSR / FPCR) and the audio thread
// is created by miniaudio, so there's no init hook to hang this on: set it at
// the top of every callback instead. It's a handful of cycles once per block.
#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

#include <pmmintrin.h>
#include <xmmintrin.h>
static inline void audio_denormals_off(void) {
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);          // denormal results -> 0
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);  // denormal inputs  -> 0
}

#elif defined(__aarch64__)

static inline void audio_denormals_off(void) {
  uint64_t fpcr;
  __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
  __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1u << 24)));  // FZ
}

#else

// wasm and everything else: no per-thread denormal control to set.
static inline void audio_denormals_off(void) {}

#endif
