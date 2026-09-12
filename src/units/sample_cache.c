#include "sample_cache.h"

#include <raylib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_CACHE_MAX 32
static SampleCacheEntry* cache[SAMPLE_CACHE_MAX];

// Decode to mono float32 — raylib handles WAV/MP3/OGG/FLAC.
static bool cache_load(SampleCacheEntry* e) {
  Wave w = LoadWave(e->path);
  if (w.frameCount == 0 || !w.data)
    return false;

  e->wav_sr = w.sampleRate;
  WaveFormat(&w, w.sampleRate, 32, 1);  // convert in-place: mono, 32-bit float

  float* smp = LoadWaveSamples(w);
  uint32_t n = w.frameCount;
  UnloadWave(w);
  if (!smp)
    return false;

  float* out = malloc(n * sizeof(float));
  if (!out) {
    UnloadWaveSamples(smp);
    return false;
  }
  memcpy(out, smp, n * sizeof(float));
  UnloadWaveSamples(smp);

  e->samples = out;
  e->num_samples = n;
  return true;
}

SampleCacheEntry* sample_cache_acquire(const char* path) {
  if (!path || !path[0])
    return NULL;

  for (int i = 0; i < SAMPLE_CACHE_MAX; i++)
    if (cache[i] && strcmp(cache[i]->path, path) == 0) {
      cache[i]->refs++;
      return cache[i];
    }

  SampleCacheEntry* e = calloc(1, sizeof(*e));
  if (!e)
    return NULL;
  strncpy(e->path, path, sizeof(e->path) - 1);
  if (!cache_load(e)) {
    free(e);
    return NULL;
  }
  e->refs = 1;

  for (int i = 0; i < SAMPLE_CACHE_MAX; i++)
    if (!cache[i]) {
      cache[i] = e;
      return e;
    }

  // Full: evict the least-referenced entry (warm-but-unused ones have 0).
  int evict = 0;
  for (int i = 1; i < SAMPLE_CACHE_MAX; i++)
    if (cache[i]->refs < cache[evict]->refs)
      evict = i;
  free(cache[evict]->samples);
  free(cache[evict]);
  cache[evict] = e;
  return e;
}

void sample_cache_release(SampleCacheEntry* e) {
  if (e && e->refs > 0)
    e->refs--;
}

void sample_cache_preload(const char* path) {
  if (!path || !path[0])
    return;
  // The returned reference is intentionally dropped on the floor: it stays
  // warm in the cache, so nothing needs to hold it open.
  (void)sample_cache_acquire(path);
}
