// demo image.bin out.wav [rate]
// Plays a short pattern on each of the 11 machines through the synth layer
// (params, voices, parking, resampling: everything but the CLAP glue) and
// writes a 16-bit mono WAV.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "synth.h"

static void put16(FILE* f, int v) {
  fputc(v & 0xff, f);
  fputc((v >> 8) & 0xff, f);
}

static void put32(FILE* f, uint32_t v) {
  put16(f, v & 0xffff);
  put16(f, v >> 16);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: demo image.bin out.wav [rate]\n");
    return 1;
  }
  double rate = argc > 3 ? atof(argv[3]) : 44100;
  FILE* f = fopen(argv[1], "rb");
  if (!f) {
    fprintf(stderr, "can't read %s\n", argv[1]);
    return 1;
  }
  static uint8_t img[ST_IMAGE_SIZE];
  size_t n = fread(img, 1, sizeof(img), f);
  fclose(f);
  synth_t* s = synth_new(n == ST_IMAGE_SIZE ? img : NULL, rate);
  if (!s || !synth_has_engine(s)) {
    fprintf(stderr, "engine init failed\n");
    return 1;
  }

  // 16th notes at 120 bpm, 2 bars per machine
  static const int pattern[16] = {48, 0, 60, 0, 55, 0, 48, 60, 0, 51, 0, 58, 48, 0, 63, 60};
  uint32_t step = (uint32_t)(rate * 60 / 120 / 4);
  uint32_t total = step * 32 * MACHINE_COUNT;
  float* out = calloc(total, sizeof(float));
  uint32_t pos = 0;
  for (int m = 0; m < MACHINE_COUNT; m++) {
    synth_set_param(s, PID_MACHINE, m);
    synth_set_param(s, PID_RELEASE, 0.15);  // 300 ms fade after each note
    for (int i = 0; i < 32; i++) {
      int key = pattern[i % 16];
      if (key)
        synth_note_on(s, key, (i % 4) ? 0.6 : 1.0);
      synth_render(s, out + pos, out + pos, step / 2);
      if (key)
        synth_note_off(s, key);
      synth_render(s, out + pos + step / 2, out + pos + step / 2, step - step / 2);
      pos += step;
    }
    printf("%-10s\n", machines[m].name);
  }

  float peak = 0;
  for (uint32_t i = 0; i < total; i++) peak = fmaxf(peak, fabsf(out[i]));
  FILE* w = fopen(argv[2], "wb");
  fwrite("RIFF", 1, 4, w);
  put32(w, 36 + total * 2);
  fwrite("WAVEfmt ", 1, 8, w);
  put32(w, 16);
  put16(w, 1);
  put16(w, 1);
  put32(w, (uint32_t)rate);
  put32(w, (uint32_t)rate * 2);
  put16(w, 2);
  put16(w, 16);
  fwrite("data", 1, 4, w);
  put32(w, total * 2);
  for (uint32_t i = 0; i < total; i++) put16(w, (int)lrintf(out[i] / (peak > 0 ? peak : 1) * 0.89f * 32767));
  fclose(w);
  printf("wrote %s: %.1fs at %g Hz, peak %.3f\n", argv[2], total / rate, rate, peak);
  synth_free(s);
  free(out);
  return 0;
}
