#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "midi_in.h"
#include "raylib.h"

// Single lock around all engine state touched by both the audio callback
// thread and the main thread. Emscripten builds are single-threaded (the
// stream callback runs on the same thread as everything else), so it's a
// no-op there.
#ifdef __EMSCRIPTEN__
#define AUDIO_LOCK(eng) ((void)0)
#define AUDIO_UNLOCK(eng) ((void)0)
#elif defined(_WIN32)
#define AUDIO_LOCK(eng) EnterCriticalSection(&(eng)->lock)
#define AUDIO_UNLOCK(eng) LeaveCriticalSection(&(eng)->lock)
#else
#define AUDIO_LOCK(eng) pthread_mutex_lock(&(eng)->lock)
#define AUDIO_UNLOCK(eng) pthread_mutex_unlock(&(eng)->lock)
#endif

// Per-instrument RMS for sidechain ducking (NUM_INSTRUMENTS=16)
float g_sidechain_rms[NUM_INSTRUMENTS] = {0};

static void midi_voice_destroy(AudioEngine* eng, int v);
void audio_midi_kill_all(AudioEngine* eng);

static uint32_t calc_samples_per_tick(uint16_t bpm) {
  if (bpm == 0)
    bpm = 120;
  return (AUDIO_SAMPLE_RATE * 60u) / ((uint32_t)bpm * 4u);
}

// True if this instrument's source unit manages its own polyphony (CLAP) —
// playback should use one shared instance instead of one per track/voice.
static bool inst_is_shared(AudioEngine* eng, uint8_t inst_idx) {
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!inst->chain[s].unit_id[0] || !inst->chain[s].enabled)
      continue;
    const UnitDef* def = unit_find(inst->chain[s].unit_id);
    if (def && def->is_source)
      return def->shared_instance;
  }
  return false;
}

static void destroy_shared_states(AudioEngine* eng, uint8_t inst_idx) {
  if (!eng->shared_active[inst_idx])
    return;
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->shared_states[inst_idx][s]) {
      if (eng->shared_defs[inst_idx][s])
        eng->shared_defs[inst_idx][s]->destroy(eng->shared_states[inst_idx][s]);
      eng->shared_states[inst_idx][s] = NULL;
      eng->shared_defs[inst_idx][s] = NULL;
    }
  }
  eng->shared_active[inst_idx] = false;
}

static void ensure_shared_states(AudioEngine* eng, uint8_t inst_idx) {
  if (eng->shared_active[inst_idx])
    return;
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef* def = unit_find(slot->unit_id);
    if (!def)
      continue;
    eng->shared_states[inst_idx][s] = def->create(AUDIO_SAMPLE_RATE);
    eng->shared_defs[inst_idx][s] = def;
    if (def->set_data && eng->shared_states[inst_idx][s])
      def->set_data(eng->shared_states[inst_idx][s], slot->data, eng->save_dir);
  }
  eng->shared_active[inst_idx] = true;
}

static void kill_shared_states(AudioEngine* eng) {
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    if (!eng->shared_active[i])
      continue;
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (eng->shared_states[i][s] && eng->shared_defs[i][s] && eng->shared_defs[i][s]->kill)
        eng->shared_defs[i][s]->kill(eng->shared_states[i][s]);
    }
  }
}

// Destroy all states for one (lane, track) — uses stored defs, not current unit_id
static void destroy_chan_states(AudioEngine* eng, int ch, int tr) {
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->chan_states[ch][tr][s]) {
      if (eng->chan_defs[ch][tr][s])
        eng->chan_defs[ch][tr][s]->destroy(eng->chan_states[ch][tr][s]);
      eng->chan_states[ch][tr][s] = NULL;
      eng->chan_defs[ch][tr][s] = NULL;
    }
  }
  eng->active_inst[ch][tr] = TRACKER_EMPTY;
}

// Destroy every track's states on a lane
static void destroy_lane_states(AudioEngine* eng, int ch) {
  for (int tr = 0; tr < PATTERN_TRACKS; tr++)
    destroy_chan_states(eng, ch, tr);
}

static void destroy_preview_states(AudioEngine* eng) {
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->preview_states[s]) {
      if (eng->preview_defs[s])
        eng->preview_defs[s]->destroy(eng->preview_states[s]);
      eng->preview_states[s] = NULL;
      eng->preview_defs[s] = NULL;
    }
  }
  eng->preview_inst = TRACKER_EMPTY;
}

// Ensure per (lane,track) states are created for the given instrument
static void ensure_chan_states(AudioEngine* eng, int ch, int tr, uint8_t inst_idx) {
  if (eng->active_inst[ch][tr] == inst_idx)
    return;
  destroy_chan_states(eng, ch, tr);
  if (inst_is_shared(eng, inst_idx)) {
    ensure_shared_states(eng, inst_idx);
    eng->active_inst[ch][tr] = inst_idx;
    return;
  }
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef* def = unit_find(slot->unit_id);
    if (!def)
      continue;
    eng->chan_states[ch][tr][s] = def->create(AUDIO_SAMPLE_RATE);
    eng->chan_defs[ch][tr][s] = def;
    if (def->set_data && eng->chan_states[ch][tr][s])
      def->set_data(eng->chan_states[ch][tr][s], slot->data, eng->save_dir);
  }
  eng->active_inst[ch][tr] = inst_idx;
}

static void ensure_preview_states(AudioEngine* eng, uint8_t inst_idx) {
  if (eng->preview_inst == inst_idx)
    return;
  destroy_preview_states(eng);
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef* def = unit_find(slot->unit_id);
    if (!def)
      continue;
    eng->preview_states[s] = def->create(AUDIO_SAMPLE_RATE);
    eng->preview_defs[s] = def;
    if (def->set_data && eng->preview_states[s])
      def->set_data(eng->preview_states[s], slot->data, eng->save_dir);
  }
  eng->preview_inst = inst_idx;
}

// Fire note on/off through a (lane,track)'s unit states. Shared-instance
// instruments (CLAP) route into the one instance for the whole instrument
// instead of this track's own copy — the plugin tracks its own voices.
static void chan_note_on(AudioEngine* eng, int ch, int tr, uint8_t note, uint8_t vel) {
  uint8_t ii = eng->active_inst[ch][tr];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument* inst = &eng->song->instruments[ii];
  bool shared = inst_is_shared(eng, ii);
  UnitState** states = shared ? eng->shared_states[ii] : eng->chan_states[ch][tr];
  const UnitDef* const* defs = shared ? eng->shared_defs[ii] : eng->chan_defs[ch][tr];
  for (int s = 0; s < CHAIN_MAX; s++) {
    const UnitDef* def = defs[s];
    if (states[s] && def && def->is_source && def->note_on)
      def->note_on(states[s], note, vel, inst->chain[s].params);
  }
}

static void chan_note_off(AudioEngine* eng, int ch, int tr, uint8_t note) {
  uint8_t ii = eng->active_inst[ch][tr];
  if (ii == TRACKER_EMPTY)
    return;
  bool shared = inst_is_shared(eng, ii);
  UnitState** states = shared ? eng->shared_states[ii] : eng->chan_states[ch][tr];
  const UnitDef* const* defs = shared ? eng->shared_defs[ii] : eng->chan_defs[ch][tr];
  for (int s = 0; s < CHAIN_MAX; s++) {
    const UnitDef* def = defs[s];
    if (states[s] && def && def->is_source && def->note_off)
      def->note_off(states[s], note);
  }
}

static void chan_kill(AudioEngine* eng, int ch, int tr) {
  uint8_t ii = eng->active_inst[ch][tr];
  if (ii == TRACKER_EMPTY)
    return;
  // Shared instances are killed once (across all tracks) via kill_shared_states.
  if (inst_is_shared(eng, ii))
    return;
  for (int s = 0; s < CHAIN_MAX; s++) {
    const UnitDef* def = eng->chan_defs[ch][tr][s];
    if (eng->chan_states[ch][tr][s] && def && def->kill)
      def->kill(eng->chan_states[ch][tr][s]);
  }
}

// Render one (lane,track)'s unit chain into tmp_l/tmp_r, then mix into out.
// Accumulates per-instrument sum-of-squares energy into sc_energy for sidechain ducking.
static void render_channel(AudioEngine* eng, int ch, int tr, float* out_l, float* out_r,
                           uint32_t frames, double* sc_energy) {
  uint8_t ii = eng->active_inst[ch][tr];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument* inst = &eng->song->instruments[ii];

  memset(eng->tmp_l, 0, frames * sizeof(float));
  memset(eng->tmp_r, 0, frames * sizeof(float));

  bool has_source = false;
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    const UnitDef* def = eng->chan_defs[ch][tr][s];
    if (!eng->chan_states[ch][tr][s] || !slot->enabled || !def)
      continue;
    if (def->is_source) {
      def->render(eng->chan_states[ch][tr][s], slot->params,
                  NULL, NULL, eng->tmp_l, eng->tmp_r, frames);
      has_source = true;
    }
  }
  if (!has_source)
    return;

  // Effects: process in-place
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    const UnitDef* def = eng->chan_defs[ch][tr][s];
    if (!eng->chan_states[ch][tr][s] || !slot->enabled || !def || def->is_source)
      continue;
    def->render(eng->chan_states[ch][tr][s], slot->params,
                eng->tmp_l, eng->tmp_r, eng->tmp_l, eng->tmp_r, frames);
  }

  // Accumulate energy per instrument for sidechain ducking (RMS computed at end of fill).
  for (uint32_t f = 0; f < frames; f++) {
    float v = (eng->tmp_l[f] + eng->tmp_r[f]) * 0.5f;
    sc_energy[ii] += (double)v * v;
  }

  // Mix into master
  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] += eng->tmp_l[f];
    out_r[f] += eng->tmp_r[f];
  }
}

// Pre-load chan_states for a specific pattern on the given lane (main-thread safe).
// For each track, scans for the first note-carrying step and ensures states exist.
static void preload_chan_states_for_pattern(AudioEngine* eng, int ch, uint8_t pi) {
  if (pi == TRACKER_EMPTY)
    return;
  Pattern* pat = tracker_pattern_peek(eng->song, pi);
  for (int tr = 0; tr < PATTERN_TRACKS; tr++) {
    for (int step = 0; step < pat->len; step++) {
      PatternStep* s = &pat->steps[tr][step];
      if (s->note != NOTE_EMPTY && s->note != NOTE_OFF) {
        ensure_chan_states(eng, ch, tr, s->instrument);
        break;
      }
    }
  }
}

// Pre-load chan_states for all channels in the song arrangement (call before playing).
static void preload_all_chan_states(AudioEngine* eng) {
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    for (int row = 0; row < eng->song->song_len; row++) {
      uint8_t pi = eng->song->patterns[ch][row];
      if (pi != TRACKER_EMPTY) {
        preload_chan_states_for_pattern(eng, ch, pi);
        break;
      }
    }
  }
}

// Return the pattern at the lane's current cursor position — NULL if empty (silent).
// Clamps the shared pattern_step into range. Does NOT skip ahead.
static Pattern* get_current_pattern(AudioEngine* eng, int ch) {
  TrackerSong* s = eng->song;
  ChannelCursor* cur = &eng->cursors[ch];
  uint8_t pi;
  if (eng->pattern_loop) {
    if (!(eng->loop_channel_mask & (1u << ch)))
      return NULL;
    pi = eng->loop_pattern_idx;
  } else {
    if (cur->song_row >= s->song_len)
      cur->song_row = s->song_len > 0 ? s->song_len - 1 : 0;
    pi = s->patterns[ch][cur->song_row];
  }
  if (pi == TRACKER_EMPTY)
    return NULL;
  Pattern* pat = tracker_pattern_peek(s, pi);
  if (cur->pattern_step >= pat->len)
    cur->pattern_step = 0;
  return pat;
}

// Last song row with any non-empty pattern. Computed fresh at each row
// boundary (cheap: runs once per row) so arrangement edits made during
// playback move the loop/stop point immediately.
static uint16_t song_last_row(AudioEngine* eng) {
  TrackerSong* s = eng->song;
  for (int row = s->song_len - 1; row > 0; row--)
    for (int ch = 0; ch < SONG_CHANNELS; ch++)
      if (s->patterns[ch][row] != TRACKER_EMPTY)
        return (uint16_t)row;
  return 0;
}

// Max pattern length across all channels in a given song row (minimum 1).
static uint16_t row_max_len(AudioEngine* eng, uint16_t song_row) {
  TrackerSong* s = eng->song;
  uint16_t max = 1;
  for (int c = 0; c < SONG_CHANNELS; c++) {
    uint8_t pi = s->patterns[c][song_row];
    if (pi != TRACKER_EMPTY) {
      uint16_t l = tracker_pattern_peek(s, pi)->len;
      if (l > max)
        max = l;
    }
  }
  return max;
}

// Advance only the per-channel pattern_step; song_row is managed globally.
static void advance_cursor(AudioEngine* eng, int ch, ChannelCursor* cur) {
  TrackerSong* s = eng->song;
  uint8_t pi = eng->pattern_loop ? eng->loop_pattern_idx : s->patterns[ch][cur->song_row];
  if (pi == TRACKER_EMPTY)
    return;
  uint16_t pat_len = tracker_pattern_peek(s, pi)->len;
  cur->pattern_step++;
  if (cur->pattern_step >= pat_len)
    cur->pattern_step = 0;
}

// Find a live UnitState for (inst_idx, slot) usable for dynamic param access:
// the shared instance (CLAP), any playing track's state, or the preview.
static UnitState* find_inst_state(AudioEngine* eng, uint8_t inst_idx, int s) {
  if (eng->shared_active[inst_idx] && eng->shared_states[inst_idx][s])
    return eng->shared_states[inst_idx][s];
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      if (eng->active_inst[ch][tr] == inst_idx && eng->chan_states[ch][tr][s])
        return eng->chan_states[ch][tr][s];
  if (eng->preview_inst == inst_idx && eng->preview_states[s])
    return eng->preview_states[s];
  return NULL;
}

// Write a param addressed by global index across an instrument's chain
// (slot 0 owns indices 0..n0-1, slot 1 owns n0.., ...), resolving dynamic
// params (CLAP mappings, MIDI CC slots) via a live instance when one exists.
// Used by per-step FX and, via audio_mod_set_param, by LFO/DUCKER.
static void set_global_param(AudioEngine* eng, uint8_t inst_idx, uint8_t global_param, uint8_t val) {
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  int remaining = global_param;
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!inst->chain[s].unit_id[0])
      continue;
    const UnitDef* def = unit_find(inst->chain[s].unit_id);
    if (!def)
      continue;
    UnitState* st = (def->dyn_num_params || def->set_param_val)
                        ? find_inst_state(eng, inst_idx, s)
                        : NULL;
    int nparams = (def->dyn_num_params && st) ? def->dyn_num_params(st) : def->num_params;
    if (remaining < nparams) {
      if (def->set_param_val && st) {
        def->set_param_val(st, remaining, val);
        // find_inst_state() prefers the shared/playing instance over the
        // instrument screen's own preview instance, so mirror the write
        // there too — otherwise the screen shows a stale value while the
        // modulation is audibly changing a different (live) instance.
        if (eng->preview_inst == inst_idx && eng->preview_states[s] && eng->preview_states[s] != st)
          def->set_param_val(eng->preview_states[s], remaining, val);
      } else if (remaining < UNIT_MAX_PARAMS)
        inst->chain[s].params[remaining] = val;
      return;
    }
    remaining -= nparams;
  }
}

// Entry point for modulation units (LFO, DUCKER), which run inside render
// with no engine handle. The engine registers itself in audio_init.
static AudioEngine* g_mod_engine = NULL;

void audio_mod_set_param(uint8_t inst_idx, uint8_t global_param, uint8_t val) {
  if (g_mod_engine)
    set_global_param(g_mod_engine, inst_idx, global_param, val);
}

static void fire_step(AudioEngine* eng, int ch, int tr, PatternStep* step) {
  if (!step)
    return;
  if (step->note == NOTE_OFF) {
    chan_note_off(eng, ch, tr, eng->active_note[ch][tr]);
    eng->active_note[ch][tr] = 0;
    return;
  }

  // Determine which instrument to apply FX to.
  // On a note step: use step's instrument and ensure states exist.
  // On an empty step: apply FX to whatever is already playing on this track.
  uint8_t inst_idx;
  if (step->note != NOTE_EMPTY) {
    inst_idx = step->instrument;
    ensure_chan_states(eng, ch, tr, inst_idx);
  } else {
    if (eng->active_inst[ch][tr] == TRACKER_EMPTY)
      return;
    inst_idx = eng->active_inst[ch][tr];
  }

  // Apply per-step param overrides (fx[i] = global param index; see
  // set_global_param for how indices span chain slots and dynamic params).
  for (int fi = 0; fi < FX_PER_STEP; fi++)
    if (step->fx[fi] != TRACKER_EMPTY)
      set_global_param(eng, inst_idx, step->fx[fi], step->fxv[fi]);

  if (step->note == NOTE_EMPTY)
    return;

  if (eng->active_note[ch][tr])
    chan_note_off(eng, ch, tr, eng->active_note[ch][tr]);
  eng->active_note[ch][tr] = step->note;
  chan_note_on(eng, ch, tr, step->note, step->velocity ? step->velocity : 100);
}

void audio_init(AudioEngine* eng, TrackerSong* song) {
  memset(eng, 0, sizeof(AudioEngine));
#ifndef __EMSCRIPTEN__
  // Recursive: several main-thread entry points call each other
  // (e.g. audio_play_pattern -> audio_stop -> audio_midi_kill_all) and all
  // need to hold the lock across the whole call chain.
#ifdef _WIN32
  InitializeCriticalSection(&eng->lock);  // CRITICAL_SECTION is recursive by default
#else
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&eng->lock, &attr);
  pthread_mutexattr_destroy(&attr);
#endif
#endif
  unit_dsp_init();
  eng->song = song;
  g_mod_engine = eng;
  eng->samples_per_tick = calc_samples_per_tick(song->bpm);
  memset(eng->active_inst, TRACKER_EMPTY, sizeof(eng->active_inst));
  eng->preview_inst = TRACKER_EMPTY;
}

// Destroy all live states for an instrument — call before mutating its chain slots
void audio_rebuild_instrument(AudioEngine* eng, uint8_t inst_idx) {
  AUDIO_LOCK(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      if (eng->active_inst[ch][tr] == inst_idx)
        destroy_chan_states(eng, ch, tr);
  if (eng->preview_inst == inst_idx)
    destroy_preview_states(eng);
  destroy_shared_states(eng, inst_idx);
  AUDIO_UNLOCK(eng);
}

void audio_shutdown(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) destroy_lane_states(eng, ch);
  destroy_preview_states(eng);
  for (int i = 0; i < NUM_INSTRUMENTS; i++) destroy_shared_states(eng, i);
  for (int v = 0; v < 8; v++) midi_voice_destroy(eng, v);
  AUDIO_UNLOCK(eng);
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
  DeleteCriticalSection(&eng->lock);
#else
  pthread_mutex_destroy(&eng->lock);
#endif
#endif
  memset(eng, 0, sizeof(AudioEngine));
}

void audio_play_from(AudioEngine* eng, uint16_t start_row) {
  AUDIO_LOCK(eng);
  if (eng->playing) {
    AUDIO_UNLOCK(eng);
    return;
  }
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = start_row;
    eng->cursors[ch].pattern_step = 0;
  }
  memset(eng->active_note, 0, sizeof(eng->active_note));
  eng->pattern_loop = false;
  eng->tick_counter = 0;
  eng->row_tick = 0;
  eng->sample_acc = eng->samples_per_tick;

  preload_all_chan_states(eng);

  eng->playing = true;
  AUDIO_UNLOCK(eng);
}

void audio_play(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  if (eng->playing) {
    AUDIO_UNLOCK(eng);
    return;
  }
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = 0;
    eng->cursors[ch].pattern_step = 0;
  }
  memset(eng->active_note, 0, sizeof(eng->active_note));
  eng->pattern_loop = false;
  eng->tick_counter = 0;
  eng->row_tick = 0;
  eng->sample_acc = eng->samples_per_tick;

  preload_all_chan_states(eng);

  eng->playing = true;
  AUDIO_UNLOCK(eng);
}

void audio_play_pattern(AudioEngine* eng, uint8_t pattern_idx) {
  AUDIO_LOCK(eng);
  if (eng->playing)
    audio_stop(eng);
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = 0;
    eng->cursors[ch].pattern_step = 0;
  }
  memset(eng->active_note, 0, sizeof(eng->active_note));
  eng->loop_pattern_idx = pattern_idx;
  eng->pattern_loop = true;

  // Only play channels that actually use this pattern in the song arrangement
  eng->loop_channel_mask = 0;
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int row = 0; row < eng->song->song_len; row++)
      if (eng->song->patterns[ch][row] == pattern_idx) {
        eng->loop_channel_mask |= (1u << ch);
        break;
      }
  // Fallback: pattern not placed anywhere yet — play on ch 0 so it's audible
  if (!eng->loop_channel_mask)
    eng->loop_channel_mask = 1u;

  // Pre-load states on this (main) thread so file I/O doesn't hit the audio callback
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    if (eng->loop_channel_mask & (1u << ch))
      preload_chan_states_for_pattern(eng, ch, pattern_idx);
  }

  eng->tick_counter = 0;
  eng->sample_acc = eng->samples_per_tick;
  eng->playing = true;
  AUDIO_UNLOCK(eng);
}

void audio_stop(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  if (!eng->playing) {
    AUDIO_UNLOCK(eng);
    return;
  }
  eng->playing = false;
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      chan_kill(eng, ch, tr);  // immediate stop — prevents TSF release tails replaying on next start
  memset(eng->active_note, 0, sizeof(eng->active_note));
  audio_midi_kill_all(eng);
  AUDIO_UNLOCK(eng);
}

bool audio_is_playing(const AudioEngine* eng) { return eng->playing; }

void audio_set_save_dir(AudioEngine* eng, const char* save_file_path) {
  AUDIO_LOCK(eng);
  strncpy(eng->save_dir, save_file_path, sizeof(eng->save_dir) - 1);
  // Strip filename, keep directory (including trailing slash)
  char* sep = strrchr(eng->save_dir, '/');
  if (!sep)
    sep = strrchr(eng->save_dir, '\\');
  if (sep)
    *(sep + 1) = '\0';
  else {
    eng->save_dir[0] = '.';
    eng->save_dir[1] = '/';
    eng->save_dir[2] = '\0';
  }
  // Destroy all states so they rebuild with new dir via set_data
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    destroy_lane_states(eng, ch);
  destroy_preview_states(eng);
  for (int i = 0; i < NUM_INSTRUMENTS; i++) destroy_shared_states(eng, i);
  AUDIO_UNLOCK(eng);
}

void audio_ensure_preview(AudioEngine* eng, uint8_t inst_idx) {
  AUDIO_LOCK(eng);
  ensure_preview_states(eng, inst_idx);
  AUDIO_UNLOCK(eng);
}

bool g_preview_disabled = false;

void audio_preview_note(AudioEngine* eng, uint8_t inst_idx, uint8_t note) {
  if (g_preview_disabled)
    return;
  AUDIO_LOCK(eng);
  ensure_preview_states(eng, inst_idx);
  audio_preview_kill(eng);
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->preview_states[s])
      continue;
    ChainSlot* slot = &inst->chain[s];
    const UnitDef* def = unit_find(slot->unit_id);
    if (def && def->is_source && def->note_on)
      def->note_on(eng->preview_states[s], note, 100, slot->params);
  }
  AUDIO_UNLOCK(eng);
}

void audio_preview_kill(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  if (eng->preview_inst == TRACKER_EMPTY) {
    AUDIO_UNLOCK(eng);
    return;
  }
  TrackerInstrument* inst = &eng->song->instruments[eng->preview_inst];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->preview_states[s])
      continue;
    const UnitDef* def = unit_find(inst->chain[s].unit_id);
    if (def && def->kill)
      def->kill(eng->preview_states[s]);
  }
  AUDIO_UNLOCK(eng);
}

// ── MIDI poly voice pool ────────────────────────────────────────────────────

static void midi_voice_destroy(AudioEngine* eng, int v) {
  struct MidiVoice* mv = &eng->midi_voices[v];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (mv->states[s]) {
      if (mv->defs[s])
        mv->defs[s]->destroy(mv->states[s]);
      mv->states[s] = NULL;
      mv->defs[s] = NULL;
    }
  }
  mv->vstate = 0;  // MV_FREE
}

static void midi_voice_init(AudioEngine* eng, int v, uint8_t inst_idx) {
  midi_voice_destroy(eng, v);
  struct MidiVoice* mv = &eng->midi_voices[v];
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef* def = unit_find(slot->unit_id);
    if (!def)
      continue;
    mv->states[s] = def->create(AUDIO_SAMPLE_RATE);
    mv->defs[s] = def;
    if (def->set_data && mv->states[s])
      def->set_data(mv->states[s], slot->data, eng->save_dir);
  }
  mv->inst_idx = inst_idx;
}

// Find best voice to allocate: free > oldest-released > oldest-playing
static int midi_voice_alloc(AudioEngine* eng, uint8_t inst_idx) {
  int best = 0;
  int best_score = -1;
  for (int v = 0; v < 8; v++) {
    struct MidiVoice* mv = &eng->midi_voices[v];
    int score;
    if (mv->vstate == 0)
      score = 3000000 + v;  // free: highest priority
    else if (mv->vstate == 2)
      score = 2000000 - mv->rel_age;  // released: prefer oldest
    else
      score = 1000000 - mv->birth;  // playing: prefer oldest
    if (score > best_score) {
      best_score = score;
      best = v;
    }
  }
  // Reinit states if switching instrument or stealing a live voice
  if (eng->midi_voices[best].inst_idx != inst_idx || eng->midi_voices[best].vstate != 0)
    midi_voice_init(eng, best, inst_idx);
  return best;
}

void audio_midi_note_on(AudioEngine* eng, uint8_t inst_idx, uint8_t note) {
  AUDIO_LOCK(eng);
  if (inst_is_shared(eng, inst_idx)) {
    ensure_shared_states(eng, inst_idx);
    TrackerInstrument* inst = &eng->song->instruments[inst_idx];
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (!eng->shared_states[inst_idx][s])
        continue;
      ChainSlot* slot = &inst->chain[s];
      const UnitDef* def = eng->shared_defs[inst_idx][s];
      if (def && def->is_source && def->note_on)
        def->note_on(eng->shared_states[inst_idx][s], note, 100, slot->params);
    }
    AUDIO_UNLOCK(eng);
    return;
  }
  // Release any voice already playing this note on this instrument
  audio_midi_note_off(eng, inst_idx, note);
  int v = midi_voice_alloc(eng, inst_idx);
  struct MidiVoice* mv = &eng->midi_voices[v];
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!mv->states[s])
      continue;
    ChainSlot* slot = &inst->chain[s];
    const UnitDef* def = mv->defs[s];
    if (def && def->is_source && def->note_on)
      def->note_on(mv->states[s], note, 100, slot->params);
  }
  mv->note = note;
  mv->vstate = 1;  // MV_PLAYING
  mv->birth = eng->midi_voice_clock++;
  AUDIO_UNLOCK(eng);
}

void audio_midi_note_off(AudioEngine* eng, uint8_t inst_idx, uint8_t note) {
  AUDIO_LOCK(eng);
  if (inst_is_shared(eng, inst_idx)) {
    if (eng->shared_active[inst_idx])
      for (int s = 0; s < CHAIN_MAX; s++)
        if (eng->shared_states[inst_idx][s] && eng->shared_defs[inst_idx][s] && eng->shared_defs[inst_idx][s]->note_off)
          eng->shared_defs[inst_idx][s]->note_off(eng->shared_states[inst_idx][s], note);
    AUDIO_UNLOCK(eng);
    return;
  }
  for (int v = 0; v < 8; v++) {
    struct MidiVoice* mv = &eng->midi_voices[v];
    if (mv->vstate != 1 || mv->inst_idx != inst_idx || mv->note != note)
      continue;
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (!mv->states[s] || !mv->defs[s] || !mv->defs[s]->note_off)
        continue;
      mv->defs[s]->note_off(mv->states[s], note);
    }
    mv->vstate = 2;  // MV_RELEASED
    mv->rel_age = eng->midi_voice_clock++;
  }
  AUDIO_UNLOCK(eng);
}

void audio_midi_kill_all(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  kill_shared_states(eng);
  for (int v = 0; v < 8; v++) {
    struct MidiVoice* mv = &eng->midi_voices[v];
    if (mv->vstate == 0)
      continue;
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (mv->states[s] && mv->defs[s] && mv->defs[s]->kill)
        mv->defs[s]->kill(mv->states[s]);
    }
    mv->vstate = 0;  // MV_FREE
  }
  AUDIO_UNLOCK(eng);
}

void audio_set_dyn_param(AudioEngine* eng, uint8_t inst_idx, int slot_idx, int param, uint8_t val) {
  const UnitDef* def = unit_find(eng->song->instruments[inst_idx].chain[slot_idx].unit_id);
  if (!def || !def->set_param_val)
    return;
  AUDIO_LOCK(eng);
  if (eng->preview_inst == inst_idx && eng->preview_states[slot_idx])
    def->set_param_val(eng->preview_states[slot_idx], param, val);
  if (eng->shared_active[inst_idx] && eng->shared_states[inst_idx][slot_idx])
    def->set_param_val(eng->shared_states[inst_idx][slot_idx], param, val);
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      if (eng->active_inst[ch][tr] == inst_idx && eng->chan_states[ch][tr][slot_idx])
        def->set_param_val(eng->chan_states[ch][tr][slot_idx], param, val);
  for (int v = 0; v < 8; v++) {
    struct MidiVoice* mv = &eng->midi_voices[v];
    if (mv->inst_idx == inst_idx && mv->states[slot_idx])
      def->set_param_val(mv->states[slot_idx], param, val);
  }
  AUDIO_UNLOCK(eng);
}

void audio_do_main_thread_work(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->preview_states[s] && eng->preview_defs[s] && eng->preview_defs[s]->main_thread_work)
      eng->preview_defs[s]->main_thread_work(eng->preview_states[s]);
    for (int ch = 0; ch < SONG_CHANNELS; ch++)
      for (int tr = 0; tr < PATTERN_TRACKS; tr++) {
        if (eng->chan_states[ch][tr][s] && eng->chan_defs[ch][tr][s] && eng->chan_defs[ch][tr][s]->main_thread_work)
          eng->chan_defs[ch][tr][s]->main_thread_work(eng->chan_states[ch][tr][s]);
      }
    for (int v = 0; v < 8; v++) {
      struct MidiVoice* mv = &eng->midi_voices[v];
      if (mv->states[s] && mv->defs[s] && mv->defs[s]->main_thread_work)
        mv->defs[s]->main_thread_work(mv->states[s]);
    }
    for (int i = 0; i < NUM_INSTRUMENTS; i++) {
      if (eng->shared_states[i][s] && eng->shared_defs[i][s] && eng->shared_defs[i][s]->main_thread_work)
        eng->shared_defs[i][s]->main_thread_work(eng->shared_states[i][s]);
    }
  }
  AUDIO_UNLOCK(eng);
}

// Drain the lock-free MIDI-in ring and route note-on/off/CC to instruments.
// Runs on the audio thread at the top of every fill, instead of once per UI
// frame — on a slow/laggy main loop the frame interval (tens of ms) was the
// dominant source of input latency, not the audio buffer itself.
static void audio_process_midi_in(AudioEngine* eng) {
  MidiInMsg msg;
  while (midi_in_poll(&msg)) {
    uint8_t type = msg.status & 0xF0;
    uint8_t msg_ch = (msg.status & 0x0F) + 1;  // 1-16
    bool is_note_on = (type == 0x90) && msg.data2 > 0;
    bool is_note_off = (type == 0x80) || (type == 0x90 && msg.data2 == 0);
    bool is_cc = (type == 0xB0);
    if (!is_note_on && !is_note_off && !is_cc)
      continue;
    const char* port_name = midi_in_port_name(msg.port_idx);
    for (int i = 0; i < NUM_INSTRUMENTS; i++) {
      TrackerInstrument* inst = &eng->song->instruments[i];
      if (!inst->midi_in_device[0])
        continue;
      if (strcmp(inst->midi_in_device, port_name) != 0)
        continue;
      if (inst->midi_in_channel != 0 && inst->midi_in_channel != msg_ch)
        continue;
      if (is_note_on) {
        audio_midi_note_on(eng, (uint8_t)i, msg.data1);
      } else if (is_note_off) {
        audio_midi_note_off(eng, (uint8_t)i, msg.data1);
      } else if (is_cc) {
        // Apply CC to any param with a matching cc_map entry
        uint8_t cc_num = msg.data1 & 0x7F;
        uint8_t cc_val = msg.data2 & 0x7F;
        for (int s = 0; s < CHAIN_MAX; s++) {
          ChainSlot* sl = &inst->chain[s];
          for (int p = 0; p < UNIT_MAX_PARAMS; p++) {
            if (sl->cc_map[p] != cc_num)
              continue;
            sl->params[p] = (uint8_t)(cc_val * 2);  // 0-127 → 0-254
          }
        }
      }
      break;
    }
  }
}

// Core render. Assumes the audio lock is already held by the caller.
static void render_block(AudioEngine* eng, float* out, uint32_t frames) {
  audio_process_midi_in(eng);
  memset(out, 0, frames * 2 * sizeof(float));
  eng->samples_per_tick = calc_samples_per_tick(eng->song->bpm);

  // Interleaved render+tick for accurate timing
  float out_l[AUDIO_BLOCK_SIZE], out_r[AUDIO_BLOCK_SIZE];
  memset(out_l, 0, frames * sizeof(float));
  memset(out_r, 0, frames * sizeof(float));

  // Per-instrument energy accumulated across the whole fill → sidechain RMS at end.
  double sc_energy[NUM_INSTRUMENTS] = {0};

  uint32_t pos = 0;
  while (pos < frames) {
    if (eng->playing) {
      while (eng->sample_acc >= eng->samples_per_tick) {
        eng->sample_acc -= eng->samples_per_tick;
        for (int ch = 0; ch < SONG_CHANNELS; ch++) {
          Pattern* pat = get_current_pattern(eng, ch);
          if (pat) {
            uint16_t si = eng->cursors[ch].pattern_step;
            for (int tr = 0; tr < PATTERN_TRACKS; tr++)
              fire_step(eng, ch, tr, &pat->steps[tr][si]);
          }
          advance_cursor(eng, ch, &eng->cursors[ch]);
        }
        // Global song-row advancement: all channels share one song_row.
        // Advance after row_max_len ticks so shorter patterns loop within the row.
        if (!eng->pattern_loop) {
          uint16_t row_len = row_max_len(eng, eng->cursors[0].song_row);
          if (++eng->row_tick >= row_len) {
            eng->row_tick = 0;
            uint16_t next = eng->cursors[0].song_row + 1;
            if (next > song_last_row(eng)) {
              if (eng->song->loop)
                next = 0;
              else {
                eng->playing = false;
                next = eng->cursors[0].song_row;
              }
            }
            for (int c = 0; c < SONG_CHANNELS; c++) {
              eng->cursors[c].song_row = next;
              eng->cursors[c].pattern_step = 0;
            }
          }
        }
        eng->tick_counter++;
      }
    }

    uint32_t until = eng->samples_per_tick - eng->sample_acc;
    uint32_t count = frames - pos;
    if (count > until)
      count = until;

    // Render each channel for `count` samples
    float blk_l[AUDIO_BLOCK_SIZE] = {0};
    float blk_r[AUDIO_BLOCK_SIZE] = {0};

    if (eng->playing) {
      for (int ch = 0; ch < SONG_CHANNELS; ch++)
        for (int tr = 0; tr < PATTERN_TRACKS; tr++)
          render_channel(eng, ch, tr, blk_l, blk_r, count, sc_energy);
    }

    // Preview channel
    if (eng->preview_inst != TRACKER_EMPTY) {
      TrackerInstrument* inst = &eng->song->instruments[eng->preview_inst];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->preview_states[s])
          continue;
        ChainSlot* slot = &inst->chain[s];
        if (!slot->enabled)
          continue;
        const UnitDef* def = unit_find(slot->unit_id);
        if (!def)
          continue;
        if (def->is_source)
          def->render(eng->preview_states[s], slot->params, NULL, NULL, pl, pr, count);
      }
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->preview_states[s])
          continue;
        ChainSlot* slot = &inst->chain[s];
        if (!slot->enabled)
          continue;
        const UnitDef* def = unit_find(slot->unit_id);
        if (!def || def->is_source)
          continue;
        def->render(eng->preview_states[s], slot->params, pl, pr, pl, pr, count);
      }
      for (uint32_t f = 0; f < count; f++) {
        blk_l[f] += pl[f];
        blk_r[f] += pr[f];
      }
    }

    // MIDI poly voices
    for (int v = 0; v < 8; v++) {
      struct MidiVoice* mv = &eng->midi_voices[v];
      if (mv->vstate == 0)
        continue;
      TrackerInstrument* inst = &eng->song->instruments[mv->inst_idx];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!mv->states[s] || !inst->chain[s].enabled)
          continue;
        const UnitDef* def = mv->defs[s];
        if (!def || !def->is_source)
          continue;
        def->render(mv->states[s], inst->chain[s].params, NULL, NULL, pl, pr, count);
      }
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!mv->states[s] || !inst->chain[s].enabled)
          continue;
        const UnitDef* def = mv->defs[s];
        if (!def || def->is_source)
          continue;
        def->render(mv->states[s], inst->chain[s].params, pl, pr, pl, pr, count);
      }
      for (uint32_t f = 0; f < count; f++) {
        blk_l[f] += pl[f];
        blk_r[f] += pr[f];
      }
    }

    // Shared-instance instruments (CLAP etc.) — one render per instrument
    // total, covering both pattern playback and MIDI live input, since the
    // plugin already mixes its own polyphony internally.
    for (int i = 0; i < NUM_INSTRUMENTS; i++) {
      if (!eng->shared_active[i])
        continue;
      TrackerInstrument* inst = &eng->song->instruments[i];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->shared_states[i][s] || !inst->chain[s].enabled)
          continue;
        const UnitDef* def = eng->shared_defs[i][s];
        if (!def || !def->is_source)
          continue;
        def->render(eng->shared_states[i][s], inst->chain[s].params, NULL, NULL, pl, pr, count);
      }
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->shared_states[i][s] || !inst->chain[s].enabled)
          continue;
        const UnitDef* def = eng->shared_defs[i][s];
        if (!def || def->is_source)
          continue;
        def->render(eng->shared_states[i][s], inst->chain[s].params, pl, pr, pl, pr, count);
      }
      for (uint32_t f = 0; f < count; f++) {
        float v = (pl[f] + pr[f]) * 0.5f;
        sc_energy[i] += (double)v * v;
      }
      for (uint32_t f = 0; f < count; f++) {
        blk_l[f] += pl[f];
        blk_r[f] += pr[f];
      }
    }

    for (uint32_t f = 0; f < count; f++) {
      out[(pos + f) * 2] = unit_softclip(blk_l[f] * 0.7f);
      out[(pos + f) * 2 + 1] = unit_softclip(blk_r[f] * 0.7f);
    }

    pos += count;
    if (eng->playing)
      eng->sample_acc += count;
  }

  // Publish sidechain RMS per instrument (sum-of-squares across all voices this fill).
  if (frames > 0)
    for (int i = 0; i < NUM_INSTRUMENTS; i++)
      g_sidechain_rms[i] = (float)sqrt(sc_energy[i] / (double)frames);
}

void audio_fill_buffer(AudioEngine* eng, float* out, uint32_t frames) {
  AUDIO_LOCK(eng);
  // During an offline WAV render the export thread drives the engine directly;
  // the live callback must stay silent so it doesn't double-advance the song.
  if (eng->exporting) {
    memset(out, 0, frames * 2 * sizeof(float));
    AUDIO_UNLOCK(eng);
    return;
  }
  render_block(eng, out, frames);
  AUDIO_UNLOCK(eng);
}

// Build a canonical 16-bit PCM stereo WAV byte stream from interleaved floats.
static unsigned char* build_wav16(const float* interleaved, uint32_t frames,
                                  uint32_t sample_rate, uint32_t channels, int* out_size) {
  uint32_t data_bytes = frames * channels * 2u;
  uint32_t total = 44u + data_bytes;
  unsigned char* buf = (unsigned char*)malloc(total);
  if (!buf)
    return NULL;

  uint32_t byte_rate = sample_rate * channels * 2u;
  uint16_t block_align = (uint16_t)(channels * 2u);

  // Little-endian writers
  unsigned char* p = buf;
#define W8(v) (*p++ = (unsigned char)(v))
#define W16(v)             \
  do {                     \
    W8((v) & 0xFF);        \
    W8(((v) >> 8) & 0xFF); \
  } while (0)
#define W32(v)              \
  do {                      \
    W8((v) & 0xFF);         \
    W8(((v) >> 8) & 0xFF);  \
    W8(((v) >> 16) & 0xFF); \
    W8(((v) >> 24) & 0xFF); \
  } while (0)
  W8('R');
  W8('I');
  W8('F');
  W8('F');
  W32(36u + data_bytes);
  W8('W');
  W8('A');
  W8('V');
  W8('E');
  W8('f');
  W8('m');
  W8('t');
  W8(' ');
  W32(16u);  // fmt chunk size
  W16(1u);   // PCM
  W16(channels);
  W32(sample_rate);
  W32(byte_rate);
  W16(block_align);
  W16(16u);  // bits per sample
  W8('d');
  W8('a');
  W8('t');
  W8('a');
  W32(data_bytes);
#undef W8
#undef W16
#undef W32

  int16_t* samples = (int16_t*)p;
  uint32_t n = frames * channels;
  for (uint32_t i = 0; i < n; i++) {
    float v = interleaved[i];
    if (v > 1.0f)
      v = 1.0f;
    if (v < -1.0f)
      v = -1.0f;
    samples[i] = (int16_t)lrintf(v * 32767.0f);
  }

  *out_size = (int)total;
  return buf;
}

bool audio_render_wav(AudioEngine* eng, const char* path) {
  // Set up playback from the top, forcing no-loop so the render terminates.
  AUDIO_LOCK(eng);
  audio_preview_kill(eng);
  audio_midi_kill_all(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = 0;
    eng->cursors[ch].pattern_step = 0;
  }
  memset(eng->active_note, 0, sizeof(eng->active_note));
  eng->pattern_loop = false;
  eng->tick_counter = 0;
  eng->row_tick = 0;
  eng->samples_per_tick = calc_samples_per_tick(eng->song->bpm);
  eng->sample_acc = eng->samples_per_tick;

  preload_all_chan_states(eng);

  bool prev_loop = eng->song->loop;
  eng->song->loop = false;
  eng->exporting = true;
  eng->playing = true;
  AUDIO_UNLOCK(eng);

  const uint32_t BLK = AUDIO_BLOCK_SIZE;
  const uint32_t max_frames = AUDIO_SAMPLE_RATE * 600u;  // 10 min safety cap
  uint32_t cap = AUDIO_SAMPLE_RATE * 8u;                 // grow as needed
  float* data = (float*)malloc((size_t)cap * 2u * sizeof(float));
  uint32_t n_frames = 0;
  float blk[AUDIO_BLOCK_SIZE * 2];

  bool oom = (data == NULL);
  while (!oom) {
    AUDIO_LOCK(eng);
    bool play = eng->playing;
    if (play)
      render_block(eng, blk, BLK);
    AUDIO_UNLOCK(eng);
    if (!play)
      break;

    if (n_frames + BLK > cap) {
      cap *= 2;
      float* grown = (float*)realloc(data, (size_t)cap * 2u * sizeof(float));
      if (!grown) {
        oom = true;
        break;
      }
      data = grown;
    }
    memcpy(data + (size_t)n_frames * 2u, blk, BLK * 2u * sizeof(float));
    n_frames += BLK;
    if (n_frames >= max_frames)
      break;
  }

  // Restore engine state
  AUDIO_LOCK(eng);
  eng->exporting = false;
  eng->playing = false;
  eng->song->loop = prev_loop;
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      chan_kill(eng, ch, tr);
  kill_shared_states(eng);
  memset(eng->active_note, 0, sizeof(eng->active_note));
  AUDIO_UNLOCK(eng);

  if (oom || n_frames == 0) {
    free(data);
    return false;
  }

  int wav_size = 0;
  unsigned char* wav = build_wav16(data, n_frames, AUDIO_SAMPLE_RATE, 2, &wav_size);
  free(data);
  if (!wav)
    return false;

  bool ok = SaveFileData(path, wav, wav_size);
  free(wav);
  return ok;
}
