#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Per-instrument RMS for sidechain ducking (NUM_INSTRUMENTS=16)
float g_sidechain_rms[NUM_INSTRUMENTS] = {0};

static void midi_voice_destroy(AudioEngine *eng, int v);
void audio_midi_kill_all(AudioEngine *eng);

static uint32_t calc_samples_per_tick(uint8_t bpm) {
  return (AUDIO_SAMPLE_RATE * 60u) / ((uint32_t)bpm * 4u);
}

// Destroy all states for one channel — uses stored defs, not current unit_id
static void destroy_chan_states(AudioEngine *eng, int ch) {
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->chan_states[ch][s]) {
      if (eng->chan_defs[ch][s])
        eng->chan_defs[ch][s]->destroy(eng->chan_states[ch][s]);
      eng->chan_states[ch][s] = NULL;
      eng->chan_defs[ch][s] = NULL;
    }
  }
  eng->active_inst[ch] = TRACKER_EMPTY;
}

static void destroy_preview_states(AudioEngine *eng) {
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

// Ensure per-channel states are created for the given instrument
static void ensure_chan_states(AudioEngine *eng, int ch, uint8_t inst_idx) {
  if (eng->active_inst[ch] == inst_idx)
    return;
  destroy_chan_states(eng, ch);
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot *slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef *def = unit_find(slot->unit_id);
    if (!def)
      continue;
    eng->chan_states[ch][s] = def->create(AUDIO_SAMPLE_RATE);
    eng->chan_defs[ch][s] = def;
    if (def->set_data && eng->chan_states[ch][s])
      def->set_data(eng->chan_states[ch][s], slot->data, eng->save_dir);
  }
  eng->active_inst[ch] = inst_idx;
}

static void ensure_preview_states(AudioEngine *eng, uint8_t inst_idx) {
  if (eng->preview_inst == inst_idx)
    return;
  destroy_preview_states(eng);
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot *slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef *def = unit_find(slot->unit_id);
    if (!def)
      continue;
    eng->preview_states[s] = def->create(AUDIO_SAMPLE_RATE);
    eng->preview_defs[s] = def;
    if (def->set_data && eng->preview_states[s])
      def->set_data(eng->preview_states[s], slot->data, eng->save_dir);
  }
  eng->preview_inst = inst_idx;
}

// Fire note on/off through a channel's unit states
static void chan_note_on(AudioEngine *eng, int ch, uint8_t note, uint8_t vel) {
  uint8_t ii = eng->active_inst[ch];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument *inst = &eng->song->instruments[ii];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->chan_states[ch][s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    const UnitDef *def = unit_find(slot->unit_id);
    if (def && def->is_source)
      def->note_on(eng->chan_states[ch][s], note, vel, slot->params);
  }
}

static void chan_note_off(AudioEngine *eng, int ch, uint8_t note) {
  uint8_t ii = eng->active_inst[ch];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument *inst = &eng->song->instruments[ii];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->chan_states[ch][s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    const UnitDef *def = unit_find(slot->unit_id);
    if (def && def->is_source)
      def->note_off(eng->chan_states[ch][s], note);
  }
}

static void chan_kill(AudioEngine *eng, int ch) {
  uint8_t ii = eng->active_inst[ch];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument *inst = &eng->song->instruments[ii];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->chan_states[ch][s])
      continue;
    const UnitDef *def = unit_find(inst->chain[s].unit_id);
    if (def)
      def->kill(eng->chan_states[ch][s]);
  }
}

// Render one channel's unit chain into tmp_l/tmp_r, then mix into out
static void render_channel(AudioEngine *eng, int ch, float *out_l, float *out_r, uint32_t frames) {
  uint8_t ii = eng->active_inst[ch];
  if (ii == TRACKER_EMPTY)
    return;
  TrackerInstrument *inst = &eng->song->instruments[ii];

  memset(eng->tmp_l, 0, frames * sizeof(float));
  memset(eng->tmp_r, 0, frames * sizeof(float));

  bool has_source = false;
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->chan_states[ch][s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    if (!slot->enabled)
      continue;
    const UnitDef *def = unit_find(slot->unit_id);
    if (!def)
      continue;
    if (def->is_source) {
      def->render(eng->chan_states[ch][s], slot->params,
                  NULL, NULL, eng->tmp_l, eng->tmp_r, frames);
      has_source = true;
    }
  }
  if (!has_source)
    return;

  // Effects: process in-place
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->chan_states[ch][s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    if (!slot->enabled)
      continue;
    const UnitDef *def = unit_find(slot->unit_id);
    if (!def || def->is_source)
      continue;
    def->render(eng->chan_states[ch][s], slot->params,
                eng->tmp_l, eng->tmp_r, eng->tmp_l, eng->tmp_r, frames);
  }

  // Raw block RMS for sidechain ducking — ducker's envelope handles timing.
  {
    float rms_sum = 0.0f;
    for (uint32_t f = 0; f < frames; f++) {
      float v = (eng->tmp_l[f] + eng->tmp_r[f]) * 0.5f;
      rms_sum += v * v;
    }
    g_sidechain_rms[ii] = (frames > 0) ? sqrtf(rms_sum / (float)frames) : 0.0f;
  }

  // Mix into master
  for (uint32_t f = 0; f < frames; f++) {
    out_l[f] += eng->tmp_l[f];
    out_r[f] += eng->tmp_r[f];
  }
}

// Pre-load chan_states for a specific pattern on the given channel (main-thread safe).
// Scans pattern for the first note-carrying step and ensures states exist for that instrument.
static void preload_chan_states_for_pattern(AudioEngine *eng, int ch, uint8_t pi) {
  if (pi == TRACKER_EMPTY)
    return;
  Pattern *pat = &eng->song->pattern_data[pi];
  for (int step = 0; step < PATTERN_STEPS; step++) {
    PatternStep *s = &pat->steps[step];
    if (s->note != NOTE_EMPTY && s->note != NOTE_OFF) {
      ensure_chan_states(eng, ch, s->instrument);
      return;
    }
  }
}

// Pre-load chan_states for all channels in the song arrangement (call before playing).
static void preload_all_chan_states(AudioEngine *eng) {
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    for (int row = 0; row < SONG_LENGTH; row++) {
      uint8_t pi = eng->song->patterns[ch][row];
      if (pi != TRACKER_EMPTY) {
        preload_chan_states_for_pattern(eng, ch, pi);
        break;  // only need first pattern per channel to prime the state
      }
    }
  }
}

// Return step at current cursor position — NULL if row is empty (silent).
// Does NOT skip ahead; empty rows are waited through like filled ones.
static PatternStep *get_current_step(AudioEngine *eng, int ch) {
  TrackerSong *s = eng->song;
  ChannelCursor *cur = &eng->cursors[ch];
  uint8_t pi;
  if (eng->pattern_loop) {
    if (!(eng->loop_channel_mask & (1u << ch)))
      return NULL;
    pi = eng->loop_pattern_idx;
  } else {
    if (cur->song_row >= SONG_LENGTH)
      cur->song_row = SONG_LENGTH - 1;
    pi = s->patterns[ch][cur->song_row];
  }
  if (pi == TRACKER_EMPTY)
    return NULL;
  if (cur->pattern_step >= PATTERN_STEPS)
    cur->pattern_step = 0;
  return &s->pattern_data[pi].steps[cur->pattern_step];
}

static void advance_cursor(AudioEngine *eng, ChannelCursor *cur) {
  cur->pattern_step++;
  if (cur->pattern_step >= PATTERN_STEPS) {
    cur->pattern_step = 0;
    if (!eng->pattern_loop) {
      cur->song_row++;
      if (cur->song_row > eng->song_last_row) {
        if (eng->song->loop)
          cur->song_row = 0;
        else
          eng->playing = false;
      }
    }
  }
}

static void fire_step(AudioEngine *eng, int ch, PatternStep *step) {
  if (!step)
    return;
  if (step->note == NOTE_OFF) {
    chan_note_off(eng, ch, eng->active_note[ch]);
    eng->active_note[ch] = 0;
    return;
  }

  // Determine which instrument to apply FX to.
  // On a note step: use step's instrument and ensure states exist.
  // On an empty step: apply FX to whatever is already playing on this channel.
  uint8_t inst_idx;
  if (step->note != NOTE_EMPTY) {
    inst_idx = step->instrument;
    ensure_chan_states(eng, ch, inst_idx);
  } else {
    if (eng->active_inst[ch] == TRACKER_EMPTY)
      return;
    inst_idx = eng->active_inst[ch];
  }

  // Apply per-step param overrides.
  // fx[i] is a global param index spanning all chain slots in order:
  // slot 0 params 0..(n0-1), slot 1 params n0..(n0+n1-1), etc.
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int fi = 0; fi < FX_PER_STEP; fi++) {
    if (step->fx[fi] == TRACKER_EMPTY)
      continue;
    int remaining = (int)step->fx[fi];
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (!inst->chain[s].unit_id[0])
        continue;
      const UnitDef *def = unit_find(inst->chain[s].unit_id);
      if (!def)
        continue;
      UnitState *st = eng->chan_states[ch][s];
      int nparams = (def->dyn_num_params && st) ? def->dyn_num_params(st) : def->num_params;
      if (remaining < nparams) {
        if (def->set_param_val && st)
          def->set_param_val(st, remaining, step->fxv[fi]);
        else if (remaining < UNIT_MAX_PARAMS)
          inst->chain[s].params[remaining] = step->fxv[fi];
        break;
      }
      remaining -= nparams;
    }
  }

  if (step->note == NOTE_EMPTY)
    return;

  if (eng->active_note[ch])
    chan_note_off(eng, ch, eng->active_note[ch]);
  eng->active_note[ch] = step->note;
  chan_note_on(eng, ch, step->note, step->velocity ? step->velocity : 100);
}

void audio_init(AudioEngine *eng, TrackerSong *song) {
  memset(eng, 0, sizeof(AudioEngine));
  eng->song = song;
  eng->samples_per_tick = calc_samples_per_tick(song->bpm);
  memset(eng->active_inst, TRACKER_EMPTY, sizeof(eng->active_inst));
  eng->preview_inst = TRACKER_EMPTY;
}

// Destroy all live states for an instrument — call before mutating its chain slots
void audio_rebuild_instrument(AudioEngine *eng, uint8_t inst_idx) {
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    if (eng->active_inst[ch] == inst_idx)
      destroy_chan_states(eng, ch);
  if (eng->preview_inst == inst_idx)
    destroy_preview_states(eng);
}

void audio_shutdown(AudioEngine *eng) {
  for (int ch = 0; ch < SONG_CHANNELS; ch++) destroy_chan_states(eng, ch);
  destroy_preview_states(eng);
  for (int v = 0; v < 8; v++) midi_voice_destroy(eng, v);
  memset(eng, 0, sizeof(AudioEngine));
}

void audio_play(AudioEngine *eng) {
  if (eng->playing)
    return;
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = 0;
    eng->cursors[ch].pattern_step = 0;
    eng->active_note[ch] = 0;
  }
  eng->pattern_loop = false;
  eng->tick_counter = 0;
  eng->sample_acc = eng->samples_per_tick;

  // Find last song row with any non-empty pattern across all channels
  int last = 0;
  for (int row = 0; row < SONG_LENGTH; row++)
    for (int ch = 0; ch < SONG_CHANNELS; ch++)
      if (eng->song->patterns[ch][row] != TRACKER_EMPTY)
        last = row;
  eng->song_last_row = last;
  preload_all_chan_states(eng);

  eng->playing = true;
}

void audio_play_pattern(AudioEngine *eng, uint8_t pattern_idx) {
  if (eng->playing)
    audio_stop(eng);
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = 0;
    eng->cursors[ch].pattern_step = 0;
    eng->active_note[ch] = 0;
  }
  eng->loop_pattern_idx = pattern_idx;
  eng->pattern_loop = true;

  // Only play channels that actually use this pattern in the song arrangement
  eng->loop_channel_mask = 0;
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int row = 0; row < SONG_LENGTH; row++)
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
}

void audio_stop(AudioEngine *eng) {
  if (!eng->playing)
    return;
  eng->playing = false;
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    chan_kill(eng, ch);  // immediate stop — prevents TSF release tails replaying on next start
    eng->active_note[ch] = 0;
  }
  audio_midi_kill_all(eng);
}

bool audio_is_playing(const AudioEngine *eng) { return eng->playing; }

void audio_set_save_dir(AudioEngine *eng, const char *save_file_path) {
  strncpy(eng->save_dir, save_file_path, sizeof(eng->save_dir) - 1);
  // Strip filename, keep directory (including trailing slash)
  char *sep = strrchr(eng->save_dir, '/');
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
    destroy_chan_states(eng, ch);
  destroy_preview_states(eng);
}

void audio_ensure_preview(AudioEngine *eng, uint8_t inst_idx) {
  ensure_preview_states(eng, inst_idx);
}

void audio_preview_note(AudioEngine *eng, uint8_t inst_idx, uint8_t note) {
  ensure_preview_states(eng, inst_idx);
  audio_preview_kill(eng);
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->preview_states[s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    const UnitDef *def = unit_find(slot->unit_id);
    if (def && def->is_source)
      def->note_on(eng->preview_states[s], note, 100, slot->params);
  }
}

void audio_preview_kill(AudioEngine *eng) {
  if (eng->preview_inst == TRACKER_EMPTY)
    return;
  TrackerInstrument *inst = &eng->song->instruments[eng->preview_inst];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!eng->preview_states[s])
      continue;
    const UnitDef *def = unit_find(inst->chain[s].unit_id);
    if (def)
      def->kill(eng->preview_states[s]);
  }
}

// ── MIDI poly voice pool ────────────────────────────────────────────────────

static void midi_voice_destroy(AudioEngine *eng, int v) {
  struct MidiVoice *mv = &eng->midi_voices[v];
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

static void midi_voice_init(AudioEngine *eng, int v, uint8_t inst_idx) {
  midi_voice_destroy(eng, v);
  struct MidiVoice *mv = &eng->midi_voices[v];
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot *slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef *def = unit_find(slot->unit_id);
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
static int midi_voice_alloc(AudioEngine *eng, uint8_t inst_idx) {
  int best = 0;
  int best_score = -1;
  for (int v = 0; v < 8; v++) {
    struct MidiVoice *mv = &eng->midi_voices[v];
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

void audio_midi_note_on(AudioEngine *eng, uint8_t inst_idx, uint8_t note) {
  // Release any voice already playing this note on this instrument
  audio_midi_note_off(eng, inst_idx, note);
  int v = midi_voice_alloc(eng, inst_idx);
  struct MidiVoice *mv = &eng->midi_voices[v];
  TrackerInstrument *inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (!mv->states[s])
      continue;
    ChainSlot *slot = &inst->chain[s];
    const UnitDef *def = mv->defs[s];
    if (def && def->is_source && def->note_on)
      def->note_on(mv->states[s], note, 100, slot->params);
  }
  mv->note = note;
  mv->vstate = 1;  // MV_PLAYING
  mv->birth = eng->midi_voice_clock++;
}

void audio_midi_note_off(AudioEngine *eng, uint8_t inst_idx, uint8_t note) {
  for (int v = 0; v < 8; v++) {
    struct MidiVoice *mv = &eng->midi_voices[v];
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
}

void audio_midi_kill_all(AudioEngine *eng) {
  for (int v = 0; v < 8; v++) {
    struct MidiVoice *mv = &eng->midi_voices[v];
    if (mv->vstate == 0)
      continue;
    for (int s = 0; s < CHAIN_MAX; s++) {
      if (mv->states[s] && mv->defs[s] && mv->defs[s]->kill)
        mv->defs[s]->kill(mv->states[s]);
    }
    mv->vstate = 0;  // MV_FREE
  }
}

void audio_fill_buffer(AudioEngine *eng, float *out, uint32_t frames) {
  memset(out, 0, frames * 2 * sizeof(float));
  eng->samples_per_tick = calc_samples_per_tick(eng->song->bpm);

  // Interleaved render+tick for accurate timing
  float out_l[AUDIO_BLOCK_SIZE], out_r[AUDIO_BLOCK_SIZE];
  memset(out_l, 0, frames * sizeof(float));
  memset(out_r, 0, frames * sizeof(float));

  uint32_t pos = 0;
  while (pos < frames) {
    if (eng->playing) {
      while (eng->sample_acc >= eng->samples_per_tick) {
        eng->sample_acc -= eng->samples_per_tick;
        for (int ch = 0; ch < SONG_CHANNELS; ch++) {
          PatternStep *step = get_current_step(eng, ch);
          fire_step(eng, ch, step);
          advance_cursor(eng, &eng->cursors[ch]);  // always advance, even on empty rows
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
        render_channel(eng, ch, blk_l, blk_r, count);
    }

    // Preview channel
    if (eng->preview_inst != TRACKER_EMPTY) {
      TrackerInstrument *inst = &eng->song->instruments[eng->preview_inst];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->preview_states[s])
          continue;
        ChainSlot *slot = &inst->chain[s];
        if (!slot->enabled)
          continue;
        const UnitDef *def = unit_find(slot->unit_id);
        if (!def)
          continue;
        if (def->is_source)
          def->render(eng->preview_states[s], slot->params, NULL, NULL, pl, pr, count);
      }
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!eng->preview_states[s])
          continue;
        ChainSlot *slot = &inst->chain[s];
        if (!slot->enabled)
          continue;
        const UnitDef *def = unit_find(slot->unit_id);
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
      struct MidiVoice *mv = &eng->midi_voices[v];
      if (mv->vstate == 0)
        continue;
      TrackerInstrument *inst = &eng->song->instruments[mv->inst_idx];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!mv->states[s] || !inst->chain[s].enabled)
          continue;
        const UnitDef *def = mv->defs[s];
        if (!def || !def->is_source)
          continue;
        def->render(mv->states[s], inst->chain[s].params, NULL, NULL, pl, pr, count);
      }
      for (int s = 0; s < CHAIN_MAX; s++) {
        if (!mv->states[s] || !inst->chain[s].enabled)
          continue;
        const UnitDef *def = mv->defs[s];
        if (!def || def->is_source)
          continue;
        def->render(mv->states[s], inst->chain[s].params, pl, pr, pl, pr, count);
      }
      for (uint32_t f = 0; f < count; f++) {
        blk_l[f] += pl[f];
        blk_r[f] += pr[f];
      }
    }

    for (uint32_t f = 0; f < count; f++) {
      out[(pos + f) * 2] = tanhf(blk_l[f] * 0.7f);
      out[(pos + f) * 2 + 1] = tanhf(blk_r[f] * 0.7f);
    }

    pos += count;
    if (eng->playing)
      eng->sample_acc += count;
  }
}
