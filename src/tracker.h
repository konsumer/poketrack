#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "units/unit.h"

#define SONG_CHANNELS 16
#define SONG_LENGTH 255
#define NUM_PATTERNS 255
#define PATTERN_STEPS 16
#define NUM_INSTRUMENTS 256
#define FX_PER_STEP 2
#define TRACKER_EMPTY 0xFF

#define NOTE_EMPTY 0x00
#define NOTE_OFF 0xFE

typedef enum {
  FX_NONE = 0,
  FX_ARP,
  FX_CHA,
  FX_DEL,
  FX_HOP,
  FX_KIL,
  FX_RET,
  FX_TEM,
  FX_TSP,
  FX_VIB,
  FX_VOL,
  FX_COUNT,
} FxType;

typedef struct {
  uint8_t note;
  uint8_t velocity;
  uint8_t instrument;
  uint8_t fx[FX_PER_STEP];
  uint8_t fxv[FX_PER_STEP];
} PatternStep;

typedef struct {
  PatternStep steps[PATTERN_STEPS];
} Pattern;

// One slot in an instrument's unit chain
#define CHAIN_MAX 8

typedef struct {
  char unit_id[UNIT_ID_LEN];  // "" = empty/unused
  uint8_t params[UNIT_MAX_PARAMS];
  bool enabled;
  char data[239];  // extra string data (e.g. SF2 file path)
} ChainSlot;

// Instrument = chain of units (sources first, then effects)
typedef struct {
  char name[16];
  ChainSlot chain[CHAIN_MAX];
  char midi_in_device[128];  // "" = disabled; device name to receive MIDI from
  uint8_t midi_in_channel;   // 0 = all channels, 1-16 = specific channel
  uint8_t _pad[3];
} TrackerInstrument;

typedef struct {
  uint8_t patterns[SONG_CHANNELS][SONG_LENGTH];
  Pattern pattern_data[NUM_PATTERNS];
  TrackerInstrument instruments[NUM_INSTRUMENTS];
  uint8_t bpm;
  uint8_t swing;
  char name[32];
  uint8_t scale_root;  // 0=C .. 11=B
  uint8_t scale_idx;   // 0=chromatic, see SCALES[]
  bool loop;           // true = restart from row 0 at song end
} TrackerSong;

#define NUM_SCALES 45

typedef struct {
  const char *name;
  int len;
  uint8_t intervals[12];
} ScaleDef;

extern const ScaleDef SCALES[NUM_SCALES];

// Root note names: SCALE_ROOT_NAMES[0..11]
extern const char *const SCALE_ROOT_NAMES[12];

// Navigate to next/prev note in scale (dir = +1 or -1). Returns clamped note.
uint8_t scale_next_note(uint8_t note, int dir, uint8_t scale_idx, uint8_t root);

void tracker_init(TrackerSong *song);
void tracker_clear(TrackerSong *song);

void tracker_inst_set_slot(TrackerInstrument *inst, int slot, const char *unit_id);

// Returns true on success
bool tracker_save(const TrackerSong *song, const char *path);
bool tracker_load(TrackerSong *song, const char *path);
