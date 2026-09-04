#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "denormal.h"
#include "midi_in.h"
#include "paths.h"
#include "raylib.h"

// Single lock around all engine state touched by both the audio callback
// thread and the main thread (see lock.h).
#define AUDIO_LOCK(eng) lock_acquire(&(eng)->lock)
#define AUDIO_UNLOCK(eng) lock_release(&(eng)->lock)

// Per-instrument RMS for sidechain ducking (NUM_INSTRUMENTS=16)
float g_sidechain_rms[NUM_INSTRUMENTS] = {0};

// Tempo, published to units that need to be BPM-relative (see unit.h).
uint32_t g_unit_samples_per_line = 0;

// Samples rendered since the last play-start (see unit.h).
uint64_t g_unit_render_samples = 0;

// ROUTE send-bus state — declared up here because render_channel() (below)
// touches it; the machinery itself lives in the send-bus section further down.
//
// How long a bus keeps rendering after its last input, so reverb tails and
// delay repeats ring out instead of being cut off mid-decay.
#define BUS_TAIL_SAMPLES (AUDIO_SAMPLE_RATE * 5u)

static float g_bus_l[NUM_INSTRUMENTS][AUDIO_BLOCK_SIZE];
static float g_bus_r[NUM_INSTRUMENTS][AUDIO_BLOCK_SIZE];
static bool g_bus_fed[NUM_INSTRUMENTS];       // got audio during this sub-block
static uint32_t g_bus_tail[NUM_INSTRUMENTS];  // samples left to render after last feed
// Which instrument's chain is rendering right now, so a ROUTE unit can't feed
// the instrument it lives in (an instant feedback loop). TRACKER_EMPTY when
// nothing routable is rendering.
static uint8_t g_send_owner = TRACKER_EMPTY;
// False while rendering a muted lane — its sends are dropped, matching the
// "muted lanes are excluded from the mix" contract (see audio_toggle_mute).
static bool g_send_open = true;

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
  g_send_owner = ii;  // a ROUTE here must not feed its own instrument's bus
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    const UnitDef* def = eng->chan_defs[ch][tr][s];
    if (!eng->chan_states[ch][tr][s] || !slot->enabled || !def || def->is_source)
      continue;
    def->render(eng->chan_states[ch][tr][s], slot->params,
                eng->tmp_l, eng->tmp_r, eng->tmp_l, eng->tmp_r, frames);
  }
  g_send_owner = TRACKER_EMPTY;

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

// Warm every file-backed resource the song's instruments name, so no create()
// on the audio thread has to read from disk.
//
// preload_chan_states_for_pattern() only reaches the instruments a lane's
// FIRST non-empty row plays, and a track can only hold one chain at a time
// anyway — so an instrument that first appears later in the arrangement used
// to do its cold soundfont load inside fire_step(), on the audio thread. A
// cold load measures ~1.5ms against an 11.6ms block budget on a fast machine;
// on a slow one it blows the deadline outright and clicks. Fonts are cached by
// path, so a song whose instruments share one font costs a single load here.
static void preload_instrument_data(AudioEngine* eng) {
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    TrackerInstrument* inst = &eng->song->instruments[i];
    for (int s = 0; s < CHAIN_MAX; s++) {
      ChainSlot* slot = &inst->chain[s];
      if (!slot->unit_id[0] || !slot->enabled)
        continue;
      const UnitDef* def = unit_find(slot->unit_id);
      if (def && def->preload_data)
        def->preload_data(slot->data, eng->save_dir);
    }
  }
}

// Pre-load chan_states for all channels in the song arrangement (call before playing).
static void preload_all_chan_states(AudioEngine* eng) {
  preload_instrument_data(eng);
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

// ---- send buses (ROUTE unit) ------------------------------------------------
//
// One stereo bus per instrument. A ROUTE unit inside any chain adds its audio
// into the destination instrument's bus via audio_send_bus(); flush_buses()
// then runs each fed bus through that instrument's effect chain and mixes the
// result into the master. Both happen inside one render sub-block, so a send
// is heard in the same block it was made — no latency.
//
// Buffers live here rather than in AudioEngine because audio_send_bus() is
// called from unit render callbacks, which have no engine handle (same
// reasoning as audio_mod_set_param above).

void audio_send_bus(uint8_t dest_inst, const float* in_l, const float* in_r,
                    uint32_t frames, float gain) {
  if (!g_send_open || !in_l || !in_r || gain <= 0.0f)
    return;
  if (dest_inst == g_send_owner)  // self-send: would feed back instantly
    return;
  if (frames > AUDIO_BLOCK_SIZE)
    frames = AUDIO_BLOCK_SIZE;

  if (!g_bus_fed[dest_inst]) {
    memset(g_bus_l[dest_inst], 0, frames * sizeof(float));
    memset(g_bus_r[dest_inst], 0, frames * sizeof(float));
    g_bus_fed[dest_inst] = true;
  }
  for (uint32_t f = 0; f < frames; f++) {
    g_bus_l[dest_inst][f] += in_l[f] * gain;
    g_bus_r[dest_inst][f] += in_r[f] * gain;
  }
}

static void destroy_bus_states(AudioEngine* eng, uint8_t inst_idx) {
  if (!eng->bus_built[inst_idx])
    return;
  for (int s = 0; s < CHAIN_MAX; s++) {
    if (eng->bus_states[inst_idx][s]) {
      if (eng->bus_defs[inst_idx][s])
        eng->bus_defs[inst_idx][s]->destroy(eng->bus_states[inst_idx][s]);
      eng->bus_states[inst_idx][s] = NULL;
      eng->bus_defs[inst_idx][s] = NULL;
    }
  }
  eng->bus_built[inst_idx] = false;
  g_bus_tail[inst_idx] = 0;
  g_bus_fed[inst_idx] = false;
}

// Build the bus instance of an instrument's chain: effects only. A bus is fed
// audio from elsewhere, so its own sources would never be rendered — skipping
// them also avoids pointlessly loading their sample/soundfont data.
static void ensure_bus_states(AudioEngine* eng, uint8_t inst_idx) {
  if (eng->bus_built[inst_idx])
    return;
  TrackerInstrument* inst = &eng->song->instruments[inst_idx];
  for (int s = 0; s < CHAIN_MAX; s++) {
    ChainSlot* slot = &inst->chain[s];
    if (!slot->unit_id[0] || !slot->enabled)
      continue;
    const UnitDef* def = unit_find(slot->unit_id);
    if (!def || def->is_source)
      continue;
    eng->bus_states[inst_idx][s] = def->create(AUDIO_SAMPLE_RATE);
    eng->bus_defs[inst_idx][s] = def;
    if (def->set_data && eng->bus_states[inst_idx][s])
      def->set_data(eng->bus_states[inst_idx][s], slot->data, eng->save_dir);
  }
  eng->bus_built[inst_idx] = true;
}

static void kill_bus_states(AudioEngine* eng) {
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    g_bus_fed[i] = false;
    g_bus_tail[i] = 0;
    if (!eng->bus_built[i])
      continue;
    for (int s = 0; s < CHAIN_MAX; s++)
      if (eng->bus_states[i][s] && eng->bus_defs[i][s] && eng->bus_defs[i][s]->kill)
        eng->bus_defs[i][s]->kill(eng->bus_states[i][s]);
  }
}

// Run every fed (or still-ringing) bus through its instrument's effect chain
// and mix into out. Called once per render sub-block, after every source of
// audio has had its chance to feed a bus.
//
// Ascending instrument order means a bus that itself holds a ROUTE can feed a
// HIGHER-numbered bus in the same sub-block. A send to an equal or lower index
// is dropped instead: that is exactly what makes routing loops impossible.
static void flush_buses(AudioEngine* eng, float* out_l, float* out_r,
                        uint32_t frames, double* sc_energy) {
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    if (g_bus_fed[i]) {
      g_bus_tail[i] = BUS_TAIL_SAMPLES;
    } else if (g_bus_tail[i] > 0) {
      // Still ringing out — run the chain on silence so tails decay.
      g_bus_tail[i] = (g_bus_tail[i] > frames) ? g_bus_tail[i] - frames : 0;
      memset(g_bus_l[i], 0, frames * sizeof(float));
      memset(g_bus_r[i], 0, frames * sizeof(float));
    } else {
      continue;
    }

    ensure_bus_states(eng, (uint8_t)i);
    TrackerInstrument* inst = &eng->song->instruments[i];
    g_send_owner = (uint8_t)i;
    for (int s = 0; s < CHAIN_MAX; s++) {
      const UnitDef* def = eng->bus_defs[i][s];
      if (!eng->bus_states[i][s] || !inst->chain[s].enabled || !def)
        continue;
      def->render(eng->bus_states[i][s], inst->chain[s].params,
                  g_bus_l[i], g_bus_r[i], g_bus_l[i], g_bus_r[i], frames);
    }
    g_send_owner = TRACKER_EMPTY;

    for (uint32_t f = 0; f < frames; f++) {
      float v = (g_bus_l[i][f] + g_bus_r[i][f]) * 0.5f;
      sc_energy[i] += (double)v * v;
      out_l[f] += g_bus_l[i][f];
      out_r[f] += g_bus_r[i][f];
    }
  }
  memset(g_bus_fed, 0, sizeof(g_bus_fed));
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
  lock_init(&eng->lock);
  unit_dsp_init();
  eng->song = song;
  g_mod_engine = eng;
  eng->samples_per_tick = calc_samples_per_tick(song->bpm);
  memset(eng->active_inst, TRACKER_EMPTY, sizeof(eng->active_inst));
  eng->preview_inst = TRACKER_EMPTY;
  eng->cue_row = -1;
}

// Destroy all live states for an instrument — call before mutating its chain slots
void audio_rebuild_instrument(AudioEngine* eng, uint8_t inst_idx) {
  AUDIO_LOCK(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      if (eng->active_inst[ch][tr] == inst_idx)
        destroy_chan_states(eng, ch, tr);
  // Unconditional, like destroy_shared_states below: ensure_preview_states()
  // treats `preview_inst == inst_idx` as "already built, nothing changed" —
  // but preview_inst can still equal inst_idx here from a stale, pre-edit
  // build (e.g. this instrument was last previewed before this chain edit).
  // Without unconditionally invalidating it, that stale cache survives the
  // edit and param changes on newly-added units silently go nowhere until
  // some other edit happens to flip preview_inst away and back.
  destroy_preview_states(eng);
  destroy_shared_states(eng, inst_idx);
  destroy_bus_states(eng, inst_idx);
  AUDIO_UNLOCK(eng);
}

void audio_shutdown(AudioEngine* eng) {
  AUDIO_LOCK(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) destroy_lane_states(eng, ch);
  destroy_preview_states(eng);
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    destroy_shared_states(eng, i);
    destroy_bus_states(eng, i);
  }
  for (int v = 0; v < 8; v++) midi_voice_destroy(eng, v);
  AUDIO_UNLOCK(eng);
  lock_destroy(&eng->lock);
  memset(eng, 0, sizeof(AudioEngine));
}

// Rewind every lane to start_row and clear the transport, ready for playback.
// Shared by all three entry points below and by the offline WAV render; the
// caller holds the lock and sets eng->playing once it has finished setting up.
static void playback_reset(AudioEngine* eng, uint16_t start_row) {
  g_unit_render_samples = 0;  // snaps tempo-synced units back onto the bar grid
  audio_preview_kill(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    eng->cursors[ch].song_row = start_row;
    eng->cursors[ch].pattern_step = 0;
  }
  memset(eng->active_note, 0, sizeof(eng->active_note));
  eng->pattern_loop = false;
  eng->cue_row = -1;
  eng->tick_counter = 0;
  eng->row_tick = 0;
  eng->sample_acc = eng->samples_per_tick;
}

void audio_play_from(AudioEngine* eng, uint16_t start_row) {
  AUDIO_LOCK(eng);
  if (!eng->playing) {
    playback_reset(eng, start_row);
    preload_all_chan_states(eng);
    eng->playing = true;
  }
  AUDIO_UNLOCK(eng);
}

void audio_play(AudioEngine* eng) {
  audio_play_from(eng, 0);
}

void audio_play_pattern(AudioEngine* eng, uint8_t pattern_idx) {
  AUDIO_LOCK(eng);
  if (eng->playing)
    audio_stop(eng);
  playback_reset(eng, 0);
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
  preload_instrument_data(eng);
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    if (eng->loop_channel_mask & (1u << ch))
      preload_chan_states_for_pattern(eng, ch, pattern_idx);
  }

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
  eng->cue_row = -1;
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    for (int tr = 0; tr < PATTERN_TRACKS; tr++)
      chan_kill(eng, ch, tr);  // immediate stop — prevents TSF release tails replaying on next start
  kill_bus_states(eng);
  memset(eng->active_note, 0, sizeof(eng->active_note));
  audio_midi_kill_all(eng);
  AUDIO_UNLOCK(eng);
}

bool audio_is_playing(const AudioEngine* eng) { return eng->playing; }

void audio_toggle_mute(AudioEngine* eng, int ch) {
  AUDIO_LOCK(eng);
  eng->mute[ch] = !eng->mute[ch];
  AUDIO_UNLOCK(eng);
}

bool audio_is_muted(const AudioEngine* eng, int ch) { return eng->mute[ch]; }

// Deliberately lock-free: this is drawn every UI frame regardless of what
// else is happening, and taking the engine lock that often turned out to
// starve the main thread against a busy audio callback (see the freeze this
// was added to fix). The scope is cosmetic only — same tradeoff already made
// for g_sidechain_rms — so a rare torn read just means one frame's waveform
// looks briefly off, never a crash or a stall.
void audio_copy_scope(AudioEngine* eng, int ch, ChannelScope* out) {
  *out = eng->scope[ch];
}

void audio_set_save_dir(AudioEngine* eng, const char* save_file_path) {
  AUDIO_LOCK(eng);
  path_dir_of(save_file_path, eng->save_dir, sizeof(eng->save_dir));
  // Destroy all states so they rebuild with new dir via set_data
  for (int ch = 0; ch < SONG_CHANNELS; ch++)
    destroy_lane_states(eng, ch);
  destroy_preview_states(eng);
  for (int i = 0; i < NUM_INSTRUMENTS; i++) {
    destroy_shared_states(eng, i);
    destroy_bus_states(eng, i);
  }
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
  g_unit_samples_per_line = eng->samples_per_tick;

  // Interleaved render+tick for accurate timing
  float out_l[AUDIO_BLOCK_SIZE], out_r[AUDIO_BLOCK_SIZE];
  memset(out_l, 0, frames * sizeof(float));
  memset(out_r, 0, frames * sizeof(float));

  // Per-instrument energy accumulated across the whole fill → sidechain RMS at end.
  double sc_energy[NUM_INSTRUMENTS] = {0};

  // Per-lane min/max envelope accumulated across the whole fill → one scope
  // point pushed per lane at the end (stays 0,0 — flat — while stopped or muted).
  float lane_mn[SONG_CHANNELS] = {0};
  float lane_mx[SONG_CHANNELS] = {0};

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
            uint16_t next;
            if (eng->cue_row >= 0) {
              // DJ cue: jump to the marked row, then resume normal advance.
              next = (uint16_t)eng->cue_row;
              eng->cue_row = -1;
            } else if (eng->pat_loop) {
              next = eng->cursors[0].song_row;
            } else {
              next = eng->cursors[0].song_row + 1;
              if (next > song_last_row(eng)) {
                if (eng->song->loop)
                  next = 0;
                else {
                  eng->playing = false;
                  next = eng->cursors[0].song_row;
                }
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

    // While playing, sub-slice this block at the next tick boundary so ticks
    // fire at the right sample. While stopped, there's no tick to align to —
    // consume the rest of the block in one step. This distinction matters:
    // sample_acc == samples_per_tick exactly is a normal, recurring state
    // while playing (it just means a block boundary landed on a tick
    // boundary), but if playback stops right then (sample_acc is untouched
    // by audio_stop()), falling through to `samples_per_tick - sample_acc`
    // unconditionally computes 0 — count stays 0, pos never advances, and
    // this loop spins forever while holding the engine lock, since nothing
    // below runs (or can run, from another thread) to change eng->playing
    // back. See the freeze this was written to fix.
    uint32_t until = eng->playing ? (eng->samples_per_tick - eng->sample_acc) : (frames - pos);
    uint32_t count = frames - pos;
    if (count > until)
      count = until;

    // Render each channel for `count` samples
    float blk_l[AUDIO_BLOCK_SIZE] = {0};
    float blk_r[AUDIO_BLOCK_SIZE] = {0};

    if (eng->playing) {
      for (int ch = 0; ch < SONG_CHANNELS; ch++) {
        float lane_l[AUDIO_BLOCK_SIZE] = {0};
        float lane_r[AUDIO_BLOCK_SIZE] = {0};
        g_send_open = !eng->mute[ch];  // a muted lane feeds no send bus either
        for (int tr = 0; tr < PATTERN_TRACKS; tr++)
          render_channel(eng, ch, tr, lane_l, lane_r, count, sc_energy);
        g_send_open = true;

        for (uint32_t f = 0; f < count; f++) {
          float v = (lane_l[f] + lane_r[f]) * 0.5f;
          if (v < lane_mn[ch])
            lane_mn[ch] = v;
          if (v > lane_mx[ch])
            lane_mx[ch] = v;
        }

        if (!eng->mute[ch]) {
          for (uint32_t f = 0; f < count; f++) {
            blk_l[f] += lane_l[f];
            blk_r[f] += lane_r[f];
          }
        }
      }
    }

    // Preview channel
    if (eng->preview_inst != TRACKER_EMPTY) {
      TrackerInstrument* inst = &eng->song->instruments[eng->preview_inst];
      float pl[AUDIO_BLOCK_SIZE] = {0}, pr[AUDIO_BLOCK_SIZE] = {0};
      g_send_owner = eng->preview_inst;
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
      g_send_owner = TRACKER_EMPTY;
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
      g_send_owner = mv->inst_idx;
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
      g_send_owner = TRACKER_EMPTY;
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

      // Which lanes are currently playing this instrument, and whether any
      // of them is unmuted. A shared instance mixes every lane's voices into
      // one stream (the plugin owns its own polyphony), so per-lane mute
      // can't isolate just the muted lane's notes the way render_channel()
      // does — the closest match to the "muted lane's audio excluded from
      // the mix" contract (see audio_toggle_mute) is to only silence the
      // instrument once EVERY lane using it is muted. An instrument with no
      // lane using it right now (e.g. driven purely by live MIDI) is never
      // gated by mute. Computed before rendering so a ROUTE in this chain
      // can be held back from the send buses on the same terms.
      bool lane_uses_inst[SONG_CHANNELS] = {0};
      bool any_lane_uses_inst = false, any_unmuted_lane_uses_inst = false;
      if (eng->playing) {
        for (int ch = 0; ch < SONG_CHANNELS; ch++) {
          for (int tr = 0; tr < PATTERN_TRACKS; tr++)
            if (eng->active_inst[ch][tr] == i) {
              lane_uses_inst[ch] = true;
              break;
            }
          if (lane_uses_inst[ch]) {
            any_lane_uses_inst = true;
            if (!eng->mute[ch])
              any_unmuted_lane_uses_inst = true;
          }
        }
      }
      bool inst_muted = any_lane_uses_inst && !any_unmuted_lane_uses_inst;

      g_send_owner = (uint8_t)i;
      g_send_open = !inst_muted;
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
      g_send_owner = TRACKER_EMPTY;
      g_send_open = true;

      for (uint32_t f = 0; f < count; f++) {
        float v = (pl[f] + pr[f]) * 0.5f;
        sc_energy[i] += (double)v * v;
      }

      if (!inst_muted) {
        for (uint32_t f = 0; f < count; f++) {
          blk_l[f] += pl[f];
          blk_r[f] += pr[f];
        }
      }

      // render_channel() never touches shared instruments, so their lane's
      // song-view scope would otherwise stay flat even while audibly
      // playing. Feed it here for every lane currently playing this
      // instrument (mirrors the per-lane min/max loop above).
      if (eng->playing) {
        for (int ch = 0; ch < SONG_CHANNELS; ch++) {
          if (!lane_uses_inst[ch])
            continue;
          for (uint32_t f = 0; f < count; f++) {
            float v = (pl[f] + pr[f]) * 0.5f;
            if (v < lane_mn[ch])
              lane_mn[ch] = v;
            if (v > lane_mx[ch])
              lane_mx[ch] = v;
          }
        }
      }
    }

    // Send buses last: every source of audio above has now had its chance
    // to feed one, so a send is heard in the same sub-block it was made.
    flush_buses(eng, blk_l, blk_r, count, sc_energy);

    for (uint32_t f = 0; f < count; f++) {
      out[(pos + f) * 2] = unit_softclip(blk_l[f] * 0.7f);
      out[(pos + f) * 2 + 1] = unit_softclip(blk_r[f] * 0.7f);
    }

    pos += count;
    g_unit_render_samples += count;
    if (eng->playing)
      eng->sample_acc += count;
  }

  // Publish sidechain RMS per instrument (sum-of-squares across all voices this fill).
  if (frames > 0)
    for (int i = 0; i < NUM_INSTRUMENTS; i++)
      g_sidechain_rms[i] = (float)sqrt(sc_energy[i] / (double)frames);

  // Push one scope point per lane (flat 0,0 when muted, so the song screen
  // waveform visibly goes silent instead of showing audio nobody hears).
  for (int ch = 0; ch < SONG_CHANNELS; ch++) {
    float mn = eng->mute[ch] ? 0.0f : lane_mn[ch];
    float mx = eng->mute[ch] ? 0.0f : lane_mx[ch];
    ChannelScope* sc = &eng->scope[ch];
    memmove(sc->mn, sc->mn + 1, (CHANNEL_SCOPE_SAMPLES - 1) * sizeof(float));
    memmove(sc->mx, sc->mx + 1, (CHANNEL_SCOPE_SAMPLES - 1) * sizeof(float));
    sc->mn[CHANNEL_SCOPE_SAMPLES - 1] = mn;
    sc->mx[CHANNEL_SCOPE_SAMPLES - 1] = mx;
  }
}

void audio_lock(AudioEngine* eng) { AUDIO_LOCK(eng); }
void audio_unlock(AudioEngine* eng) { AUDIO_UNLOCK(eng); }

void audio_fill_buffer(AudioEngine* eng, float* out, uint32_t frames) {
  audio_denormals_off();
  // render_block renders through AUDIO_BLOCK_SIZE stack buffers, so a larger
  // request would smash them. raylib currently can't make one — its mixing
  // path reads through a 4096-byte staging buffer, which at 8 bytes/frame caps
  // a callback at exactly 512 frames — but that's an undocumented internal we
  // don't control, so don't let a raylib bump turn into a stack overflow.
  if (frames > AUDIO_BLOCK_SIZE) {
    memset(out + AUDIO_BLOCK_SIZE * 2, 0, (frames - AUDIO_BLOCK_SIZE) * 2 * sizeof(float));
    frames = AUDIO_BLOCK_SIZE;
  }
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

// Write interleaved stereo floats out as a 16-bit PCM WAV. raylib's ExportWave
// handles the container; all that's needed here is the float→int16 conversion.
static bool export_wav16(const char* path, const float* interleaved,
                         uint32_t frames, uint32_t channels) {
  uint32_t n = frames * channels;
  int16_t* pcm = (int16_t*)malloc((size_t)n * sizeof(int16_t));
  if (!pcm)
    return false;
  for (uint32_t i = 0; i < n; i++) {
    float v = interleaved[i];
    v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    pcm[i] = (int16_t)lrintf(v * 32767.0f);
  }
  Wave wave = {
      .frameCount = frames,
      .sampleRate = AUDIO_SAMPLE_RATE,
      .sampleSize = 16,
      .channels = channels,
      .data = pcm,
  };
  bool ok = ExportWave(wave, path);
  free(pcm);
  return ok;
}

bool audio_render_wav(AudioEngine* eng, const char* path) {
  // Set up playback from the top, forcing no-loop so the render terminates.
  AUDIO_LOCK(eng);
  audio_midi_kill_all(eng);
  eng->samples_per_tick = calc_samples_per_tick(eng->song->bpm);
  playback_reset(eng, 0);

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
  audio_denormals_off();  // export runs render_block on this thread, not the callback's
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
  kill_bus_states(eng);
  memset(eng->active_note, 0, sizeof(eng->active_note));
  AUDIO_UNLOCK(eng);

  if (oom || n_frames == 0) {
    free(data);
    return false;
  }

  bool ok = export_wav16(path, data, n_frames, 2);
  free(data);
  return ok;
}
