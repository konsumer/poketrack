#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "controller.h"
#include "input.h"
#include "midi_in.h"
#include "raylib.h"
#include "theme.h"
#include "tracker.h"
#include "ui.h"
#include "units/unit_registry.h"
#ifndef __EMSCRIPTEN__
#include "midi_out.h"
#endif

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

static AudioEngine g_engine;
static UIState g_ui;
static AudioStream g_stream;
static RenderTexture2D g_target;
// Height of the virtual-controller band under the tracker view, or 0 when
// --controller wasn't passed.
static int g_ctrl_h = 0;

static void stream_callback(void* buf, unsigned int frames) {
  audio_fill_buffer(&g_engine, (float*)buf, frames);
}

static void main_loop(void) {
  audio_do_main_thread_work(&g_engine);
  input_update();
  ui_update(&g_ui);

  BeginTextureMode(g_target);
  ui_draw(&g_ui);
  EndTextureMode();

  // The tracker view and the controller band scale as one block, so the pad
  // keeps its proportions when the window is resized.
  int sw = GetScreenWidth(), sh = GetScreenHeight();
  int lh = WIN_H + g_ctrl_h;
  float scale = fminf((float)sw / WIN_W, (float)sh / lh);
  int dw = (int)(WIN_W * scale), dh = (int)(WIN_H * scale);
  float ox = (sw - dw) / 2.0f, oy = (sh - lh * scale) / 2.0f;
  Rectangle src = {0, 0, WIN_W, -WIN_H};  // negative height flips Y (raylib RenderTexture quirk)
  Rectangle dst = {ox, oy, (float)dw, (float)dh};

  BeginDrawing();
  ClearBackground(BLACK);
  DrawTexturePro(g_target.texture, src, dst, (Vector2){0, 0}, 0, WHITE);
  if (g_ctrl_h > 0)  // height derived from dh, not scaled directly, so int truncation leaves no seam
    controller_draw((Rectangle){ox, oy + dh, (float)dw, lh * scale - dh});
  EndDrawing();
}

int main(int argc, char** argv) {
  static TrackerSong song;

#ifndef __EMSCRIPTEN__
  bool start_fullscreen = false;
  bool show_controller = false;
  const char* theme_path = NULL;
  const char* wav_out_path = NULL;
  const char* song_path = "song.rpt";
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf(
        "usage: poketrack [options] [song.rpt]\n"
        "\n"
        "  -f, --fullscreen      Start in fullscreen\n"
        "  --theme <file.ptt>    Load a theme at launch\n"
        "  --no-preview          Disable the note preview that normally fires as the\n"
        "                        cursor moves over pattern cells\n"
        "  --width <px>          Window width (default 480)\n"
        "  --height <px>         Window height (default 320)\n"
        "  --controller          Show a virtual SNES pad below the tracker that lights\n"
        "                        up as you press keys/buttons (for screen recordings)\n"
        "  --wav <out.wav>       Render the song to a WAV file and exit, instead of\n"
        "                        opening the UI\n"
        "  -h, --help            Show this help and exit\n"
        "\n"
        "poketrack loads song.rpt and theme.ptt from the current directory by default;\n"
        "pass a path to load a different song, eg: poketrack --wav out.wav other.rpt\n"
      );
      return 0;
    } else if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
      start_fullscreen = true;
    } else if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
      theme_path = argv[++i];
    } else if (strcmp(argv[i], "--controller") == 0) {
      show_controller = true;
    } else if (strcmp(argv[i], "--no-preview") == 0) {
      g_preview_disabled = true;
    } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
      WIN_W = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
      WIN_H = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
      wav_out_path = argv[++i];
    } else if (argv[i][0] != '-') {
      song_path = argv[i];
    }
  }

  if (wav_out_path) {
    // Headless render: no window, audio device, theme, or MIDI needed.
    tracker_init(&song);
    audio_init(&g_engine, &song);
    if (!tracker_load(&song, song_path)) {
      fprintf(stderr, "poketrack: couldn't load song \"%s\"\n", song_path);
      audio_shutdown(&g_engine);
      return 1;
    }
    bool ok = audio_render_wav(&g_engine, wav_out_path);
    audio_shutdown(&g_engine);
    if (!ok) {
      fprintf(stderr, "poketrack: failed to render \"%s\"\n", wav_out_path);
      return 1;
    }
    printf("wrote %s\n", wav_out_path);
    return 0;
  }
#else
  const char* song_path = "song.rpt";
  (void)argc;
  (void)argv;
#endif

  theme_load_default("theme.ptt");  // like song.rpt, loaded from the start directory if present
#ifndef __EMSCRIPTEN__
  if (theme_path)
    theme_load(theme_path);
#endif

  midi_in_global_init();
#ifndef __EMSCRIPTEN__
  midi_out_global_init();
#endif
  tracker_init(&song);
  audio_init(&g_engine, &song);
  tracker_load(&song, song_path);
  audio_set_save_dir(&g_engine, song_path);

  ui_init(&g_ui, &song, &g_engine);

#ifndef __EMSCRIPTEN__
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  if (show_controller)
    g_ctrl_h = controller_height(WIN_W);
#endif
  InitWindow(WIN_W, WIN_H + g_ctrl_h, "poketrack");
  SetTargetFPS(60);
  if (g_ctrl_h > 0)
    controller_init();
  g_target = LoadRenderTexture(WIN_W, WIN_H);
  SetTextureFilter(g_target.texture, TEXTURE_FILTER_POINT);
#ifndef __EMSCRIPTEN__
  if (start_fullscreen)
    ToggleFullscreen();
#endif
  InitAudioDevice();

  g_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 32, 2);
  SetAudioStreamCallback(g_stream, stream_callback);
  PlayAudioStream(g_stream);

#if defined(PLATFORM_WEB)
  emscripten_set_main_loop(main_loop, 0, 1);
#else
  while (!WindowShouldClose()) main_loop();

  audio_stop(&g_engine);
  StopAudioStream(g_stream);
  UnloadAudioStream(g_stream);
  CloseAudioDevice();
  audio_shutdown(&g_engine);
  midi_in_global_shutdown();
  midi_out_global_shutdown();
  UnloadRenderTexture(g_target);
  controller_unload();
  CloseWindow();
#endif
  return 0;
}
