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

#include <pthread.h>

typedef pthread_mutex_t Lock;
static inline void lock_init(Lock* l) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(l, &attr);
  pthread_mutexattr_destroy(&attr);
}
static inline void lock_destroy(Lock* l) { pthread_mutex_destroy(l); }
static inline void lock_acquire(Lock* l) { pthread_mutex_lock(l); }
static inline void lock_release(Lock* l) { pthread_mutex_unlock(l); }

#endif
