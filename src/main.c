#include <math.h>
#include <string.h>

#include "audio.h"
#include "input.h"
#include "midi_in.h"
#include "raylib.h"
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

static void stream_callback(void *buf, unsigned int frames) {
  audio_fill_buffer(&g_engine, (float *)buf, frames);
}

static void poll_midi_in(void) {
  MidiInMsg msg;
  while (midi_in_poll(&msg)) {
    uint8_t type     = msg.status & 0xF0;
    uint8_t msg_ch   = (msg.status & 0x0F) + 1;  // 1-16
    bool is_note_on  = (type == 0x90) && msg.data2 > 0;
    bool is_note_off = (type == 0x80) || (type == 0x90 && msg.data2 == 0);
    if (!is_note_on && !is_note_off) continue;
    const char *port_name = midi_in_port_name(msg.port_idx);
    for (int i = 0; i < NUM_INSTRUMENTS; i++) {
      TrackerInstrument *inst = &g_engine.song->instruments[i];
      if (!inst->midi_in_device[0]) continue;
      if (strcmp(inst->midi_in_device, port_name) != 0) continue;
      if (inst->midi_in_channel != 0 && inst->midi_in_channel != msg_ch) continue;
      if (is_note_on)
        audio_preview_note(&g_engine, (uint8_t)i, msg.data1);
      else
        audio_preview_kill(&g_engine);
      break;
    }
  }
}

static void main_loop(void) {
  poll_midi_in();
  input_update();
  ui_update(&g_ui);

  BeginTextureMode(g_target);
  ui_draw(&g_ui);
  EndTextureMode();

  int sw = GetScreenWidth(), sh = GetScreenHeight();
  float scale = fminf((float)sw / WIN_W, (float)sh / WIN_H);
  int dw = (int)(WIN_W * scale), dh = (int)(WIN_H * scale);
  Rectangle src = {0, 0, WIN_W, -WIN_H};  // negative height flips Y (raylib RenderTexture quirk)
  Rectangle dst = {(sw - dw) / 2.0f, (sh - dh) / 2.0f, (float)dw, (float)dh};

  BeginDrawing();
  ClearBackground(BLACK);
  DrawTexturePro(g_target.texture, src, dst, (Vector2){0, 0}, 0, WHITE);
  EndDrawing();
}

int main(int argc, char **argv) {
  static TrackerSong song;

#ifndef __EMSCRIPTEN__
  bool start_fullscreen = false;
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0)
      start_fullscreen = true;
#else
  (void)argc; (void)argv;
#endif

  midi_in_global_init();
#ifndef __EMSCRIPTEN__
  midi_out_global_init();
#endif
  tracker_init(&song);
  audio_init(&g_engine, &song);
  tracker_load(&song, "song.rpt");
  audio_set_save_dir(&g_engine, "song.rpt");

  ui_init(&g_ui, &song, &g_engine);

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(WIN_W, WIN_H, "raypoketrack");
  SetTargetFPS(60);
  g_target = LoadRenderTexture(WIN_W, WIN_H);
  SetTextureFilter(g_target.texture, TEXTURE_FILTER_POINT);
#ifndef __EMSCRIPTEN__
  if (start_fullscreen) ToggleFullscreen();
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
  CloseWindow();
#endif
  return 0;
}
