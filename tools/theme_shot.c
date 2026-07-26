// Dev tool: renders a filled-in pattern screen under a given theme and
// writes a PNG screenshot. Not part of the shipped app — not built by
// default. Build with:
//   cmake --build build --target theme_shot
//
// One theme per process run, on purpose: theme_load() only overwrites the
// keys a .ptt file actually specifies, so loading several themes back to
// back in one process would leak fields between them. A fresh process per
// theme avoids that by construction — see `make theme-shots` for looping
// over examples/themes/*.ptt.
//
// Usage:
//   ./build/theme_shot THEME.ptt [output_dir]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"  // provides WIN_W/WIN_H (defined in ui.c)

// TrackerSong is tens of MB — keep it static, not on the stack (see tests/test_main.c).
static TrackerSong g_song;
static AudioEngine g_engine;  // zeroed: g_engine.playing stays false, so draw code
                              // never touches the rest of this struct (see screen_pattern.c)
static UIState g_ui;

static void fill_fake_pattern(void) {
  tracker_init(&g_song);
  snprintf(g_song.name, sizeof(g_song.name), "demo");
  g_song.bpm = 120;

  Pattern* pat = tracker_pattern(&g_song, 0);
  static const uint8_t notes[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76};
  for (int t = 0; t < PATTERN_TRACKS; t++) {
    for (int r = 0; r < DEFAULT_PATTERN_STEPS; r++) {
      if ((r + t) % 3 != 0)
        continue;
      PatternStep* s = &pat->steps[t][r];
      s->note = notes[(r + t) % (sizeof(notes) / sizeof(notes[0]))];
      s->velocity = (uint8_t)(60 + 8 * ((r + t) % 8));
      s->instrument = (uint8_t)t;
      if (r % 4 == 0)
        s->fx[0] = FX_VOL;
    }
  }
  pat->len = DEFAULT_PATTERN_STEPS;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s THEME.ptt [output_dir]\n", argv[0]);
    return 1;
  }
  const char* theme_path = argv[1];
  const char* out_dir = argc > 2 ? argv[2] : ".";

  SetConfigFlags(FLAG_WINDOW_HIDDEN);
  InitWindow(WIN_W, WIN_H, "theme_shot");

  if (!theme_load(theme_path)) {
    fprintf(stderr, "couldn't load theme: %s\n", theme_path);
    CloseWindow();
    return 1;
  }

  fill_fake_pattern();
  ui_init(&g_ui, &g_song, &g_engine);
  g_ui.screen = SCREEN_PATTERN;

  // A freshly created (hidden) window's first couple of frames don't reliably
  // land in the framebuffer by the time it's read back below — a hidden
  // window still needs a few real swaps before capture is trustworthy.
  for (int warmup = 0; warmup < 3; warmup++) {
    BeginDrawing();
    ClearBackground(BLACK);
    ui_draw(&g_ui);
    EndDrawing();
  }

  const char* base = strrchr(theme_path, '/');
  base = base ? base + 1 : theme_path;
  const char* dot = strrchr(base, '.');
  int stem_len = dot ? (int)(dot - base) : (int)strlen(base);

  char out_path[1024];
  snprintf(out_path, sizeof(out_path), "%s/%.*s.png", out_dir, stem_len, base);

  Image img = LoadImageFromScreen();
  bool ok = ExportImage(img, out_path);
  UnloadImage(img);
  printf("%s: %s\n", ok ? "wrote" : "FAILED", out_path);

  CloseWindow();
  return ok ? 0 : 1;
}
