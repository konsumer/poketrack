#pragma once
// The one genuine threading shim in the codebase. raylib runs the audio stream
// callback on its own thread on desktop, so engine state shared with the UI
// needs a recursive mutex — and raylib has no mutex API of its own.
//
// Recursive because several main-thread entry points call each other
// (audio_play_pattern -> audio_stop -> audio_midi_kill_all) and all need to
// hold the lock across the whole call chain.
//
// Emscripten builds are single-threaded (the stream callback runs on the same
// thread as everything else), so every operation compiles away to nothing.

#if defined(__EMSCRIPTEN__)

typedef char Lock;  // placeholder member; never read
static inline void lock_init(Lock* l) { (void)l; }
static inline void lock_destroy(Lock* l) { (void)l; }
static inline void lock_acquire(Lock* l) { (void)l; }
static inline void lock_release(Lock* l) { (void)l; }

#elif defined(_WIN32)

// NOGDI/NOUSER avoid the classic raylib-vs-windows.h clash (both declare
// Rectangle/CloseWindow/ShowCursor) in any translation unit that includes
// both this header and raylib.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>

typedef CRITICAL_SECTION Lock;  // recursive by default
static inline void lock_init(Lock* l) { InitializeCriticalSection(l); }
static inline void lock_destroy(Lock* l) { DeleteCriticalSection(l); }
static inline void lock_acquire(Lock* l) { EnterCriticalSection(l); }
static inline void lock_release(Lock* l) { LeaveCriticalSection(l); }

#else

// FIFO ticket lock, not a plain pthread_mutex_t. A plain OS mutex makes no
// ordering promise between waiters — under heavy back-to-back contention
// (the audio callback thread re-locking every buffer while the UI thread is
// also trying to get in) it can let a thread that instantly re-acquires the
// lock win every race indefinitely, starving the other side for seconds even
// though nothing is truly deadlocked. Confirmed on this project: the audio
// thread re-locking every ~11ms (or faster on constrained hardware, where a
// render can take close to its full buffer budget) was enough to stall the
// main thread. A ticket lock hands out FIFO tickets on arrival, so whoever
// asked first is served first — the audio thread cannot cut back in front of
// a thread that's already waiting, bounding the wait to one in-flight
// critical section rather than "however long the OS scheduler feels like."
//
// Recursive (several main-thread entry points call each other, e.g.
// audio_play_pattern -> audio_stop, and need to hold the lock across the
// whole chain): re-entry is detected by comparing the calling thread against
// the current owner. pthread_t is compared via a raw integer cast rather
// than pthread_equal() — not guaranteed portable by the POSIX spec, but true
// in practice on every libc this project ships for (glibc, musl, macOS
// libpthread all define pthread_t as a pointer-sized scalar that pthread_self()
// returns identically on every call from the same thread).
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
  _Atomic uint32_t next_ticket;
  _Atomic uint32_t now_serving;
  _Atomic uintptr_t owner;  // 0 = unheld; else the owning thread's id
  uint32_t depth;           // recursion depth; only ever touched by the owner
} Lock;

static inline void lock_init(Lock* l) {
  atomic_init(&l->next_ticket, 0);
  atomic_init(&l->now_serving, 0);
  atomic_init(&l->owner, (uintptr_t)0);
  l->depth = 0;
}
static inline void lock_destroy(Lock* l) { (void)l; }

static inline void lock_acquire(Lock* l) {
  uintptr_t self = (uintptr_t)pthread_self();
  if (atomic_load_explicit(&l->owner, memory_order_acquire) == self) {
    l->depth++;
    return;
  }
  uint32_t my_ticket = atomic_fetch_add_explicit(&l->next_ticket, 1, memory_order_relaxed);
  // Short spin first (the common case is a very short critical section —
  // a handful of struct writes or one audio block's render), then fall back
  // to yielding the CPU so a longer wait doesn't just burn cycles/power on a
  // low-end/single-core device.
  int spins = 0;
  while (atomic_load_explicit(&l->now_serving, memory_order_acquire) != my_ticket) {
    if (++spins < 200)
      continue;
    sched_yield();
  }
  atomic_store_explicit(&l->owner, self, memory_order_relaxed);
  l->depth = 1;
}

static inline void lock_release(Lock* l) {
  l->depth--;
  if (l->depth == 0) {
    atomic_store_explicit(&l->owner, (uintptr_t)0, memory_order_relaxed);
    atomic_fetch_add_explicit(&l->now_serving, 1, memory_order_release);
  }
}

#endif
