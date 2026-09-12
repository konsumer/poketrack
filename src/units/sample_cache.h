#pragma once
#include <stdint.h>

// One decoded audio file, shared by every unit instance pointing at it.
//
// The sample players (SAMPLER, TURNTABLE) only read the buffer, so one copy
// can back any number of them. Without this, a song with five turntables on
// one file decodes that file five times — and each decode lands on the audio
// thread the first time the unit plays.
//
// Entries are refcounted and kept warm at refs == 0 (tracks alternate between
// instruments across steps, so a momentary zero-ref dip is normal); they're
// reclaimed lazily when the table fills up.
typedef struct {
  char path[512];
  float* samples;  // mono float32
  uint32_t num_samples;
  uint32_t wav_sr;
  int refs;
} SampleCacheEntry;

// The shared entry for `path` (an absolute, resolved path), decoding it on
// first use. Every successful acquire needs exactly one release. NULL if the
// file can't be read or decoded.
SampleCacheEntry* sample_cache_acquire(const char* path);
void sample_cache_release(SampleCacheEntry* e);

// Decode and pin a file so a later acquire() is a cache hit. Called from the
// main thread via UnitDef.preload_data, so the first note doesn't decode on
// the audio thread. Deliberately never released — the pin keeps it resident
// for the rest of the run.
void sample_cache_preload(const char* path);
