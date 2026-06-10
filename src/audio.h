#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef __EMSCRIPTEN__
#include "clap_host.h"
#endif
#include "tracker.h"
#include "units/unit_registry.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BLOCK_SIZE 512

typedef struct {
  uint16_t song_row;
  uint16_t pattern_step;
} ChannelCursor;

typedef struct {
  TrackerSong *song;
  char save_dir[512];  // directory of loaded save file, for relative path resolution

  bool playing;
  bool pattern_loop;           // true = loop one pattern instead of full song
  uint8_t loop_pattern_idx;    // pattern index to loop (when pattern_loop)
  uint32_t loop_channel_mask;  // bitmask: channels that use loop_pattern_idx
  int song_last_row;           // last song row with any non-empty pattern (computed at play)
  ChannelCursor cursors[SONG_CHANNELS];
  uint64_t tick_counter;
  uint32_t samples_per_tick;
  uint32_t sample_acc;
  uint8_t active_note[SONG_CHANNELS];
  uint8_t active_inst[SONG_CHANNELS];  // which instrument is loaded per channel

  // Per-channel per-slot unit states + which def created them
  UnitState *chan_states[SONG_CHANNELS][CHAIN_MAX];
  const UnitDef *chan_defs[SONG_CHANNELS][CHAIN_MAX];

  // Preview channel (UI keyboard preview — monophonic, params readable by instrument screen)
  UnitState *preview_states[CHAIN_MAX];
  const UnitDef *preview_defs[CHAIN_MAX];
  uint8_t preview_inst;

  // MIDI live-input poly voices
  struct MidiVoice {
    UnitState *states[CHAIN_MAX];
    const UnitDef *defs[CHAIN_MAX];
    uint8_t note;
    uint8_t inst_idx;
    uint8_t vstate;    // 0=free 1=playing 2=released
    uint32_t birth;    // voice-clock at note-on (for stealing)
    uint32_t rel_age;  // voice-clock at note-off (for stealing released)
  } midi_voices[8];
  uint32_t midi_voice_clock;

  // Temp buffers for per-channel mixing
  float tmp_l[AUDIO_BLOCK_SIZE];
  float tmp_r[AUDIO_BLOCK_SIZE];
} AudioEngine;

void audio_init(AudioEngine *eng, TrackerSong *song);
void audio_shutdown(AudioEngine *eng);

// Set base directory for relative path resolution (call after load/save)
void audio_set_save_dir(AudioEngine *eng, const char *save_file_path);
void audio_play(AudioEngine *eng);
void audio_play_pattern(AudioEngine *eng, uint8_t pattern_idx);
void audio_stop(AudioEngine *eng);
bool audio_is_playing(const AudioEngine *eng);

// Preview a note through an instrument (for live editing feedback)
// Destroy live states for inst_idx so they rebuild cleanly after slot changes
void audio_rebuild_instrument(AudioEngine *eng, uint8_t inst_idx);

// Ensure preview states exist for inst_idx (so instrument screen can read param state)
void audio_ensure_preview(AudioEngine *eng, uint8_t inst_idx);
void audio_preview_note(AudioEngine *eng, uint8_t inst_idx, uint8_t note);
void audio_preview_kill(AudioEngine *eng);

// Polyphonic MIDI live input (separate from UI preview)
void audio_midi_note_on(AudioEngine *eng, uint8_t inst_idx, uint8_t note);
void audio_midi_note_off(AudioEngine *eng, uint8_t inst_idx, uint8_t note);
void audio_midi_kill_all(AudioEngine *eng);

void audio_fill_buffer(AudioEngine *eng, float *out, uint32_t frames);

// Call from the main thread each frame to deliver deferred plugin work (CLAP on_main_thread)
void audio_do_main_thread_work(AudioEngine *eng);

// Propagate a dynamic param change to every active UnitState instance for
// (inst_idx, slot_idx) — preview, all channel states, all MIDI voices.
// Call this instead of set_param_val directly from the UI.
void audio_set_dyn_param(AudioEngine *eng, uint8_t inst_idx, int slot_idx, int param, uint8_t val);

// Per-instrument RMS level for sidechain ducking (indexed by instrument index 0-255)
extern float g_sidechain_rms[NUM_INSTRUMENTS];
