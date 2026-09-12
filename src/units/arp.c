// Arpeggiator — note modifier. Place it at the top of an instrument chain,
// ahead of the source; it takes the incoming note(s) and replays them as a
// running arpeggio, one note per RATE, locked to the song tempo.
//
// It collects every note currently held (a MICROTONAL unit or a live MIDI
// chord placed before it can hand it several at once), then steps through
// them on the beat grid, forwarding each one to the source units below via
// unit_note_emit(). Because the step comes from the shared song position
// (g_unit_render_samples, same source as the tempo-synced LFO), every copy of
// the unit in a multi-track song agrees on where the beat is.
//
// P0 ON:   OFF passes notes through untouched; ON arpeggiates
// P1 RATE: step length, a division of a whole note (1/16 = one pattern line)
// P2 MODE: UP / DOWN / UPDN (ping-pong) / RAND
// P3 GATE: note length as a fraction of the step (a shorter gate leaves gaps)
// P4 OCT:  how many octaves the pattern spans
#include <math.h>
#include <stdlib.h>

#include "unit.h"

#define ARP_MAX_HELD 16
#define ARP_NONE -1000.0f

typedef struct {
  float pitch;
  uint8_t vel;
} ArpHeld;

struct UnitState {
  ArpHeld held[ARP_MAX_HELD];
  int nheld;
  float sounding;  // pitch currently sent downstream, ARP_NONE if none
  uint64_t step_last;
  bool step_valid;
  bool gate_closed;
  int cursor;
  int dir;
  uint32_t rng;
};

static UnitState* arp_create(float sr) {
  (void)sr;
  UnitState* s = calloc(1, sizeof(*s));
  s->sounding = ARP_NONE;
  s->dir = 1;
  s->rng = 0x9e3779b9u;
  return s;
}
static void arp_destroy(UnitState* s) { free(s); }
static void arp_kill(UnitState* s) {
  s->nheld = 0;
  s->sounding = ARP_NONE;
  s->step_valid = false;
}

// Step length in samples for the current params (0 until tempo is known).
static uint64_t arp_step_samples(const uint8_t* p) {
  static const float mult[] = {1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
  int rate = p[1] < 6 ? p[1] : 5;
  uint32_t spl = g_unit_samples_per_line;
  if (!spl)
    return 0;
  // A whole note is 16 pattern lines.
  uint64_t n = (uint64_t)(mult[rate] * 16.0f * (float)spl + 0.5f);
  return n ? n : 1;
}

static void arp_hold(UnitState* s, float pitch, uint8_t vel) {
  for (int i = 0; i < s->nheld; i++)
    if (fabsf(s->held[i].pitch - pitch) < 0.001f)
      return;
  if (s->nheld >= ARP_MAX_HELD)
    return;
  s->held[s->nheld].pitch = pitch;
  s->held[s->nheld].vel = vel;
  s->nheld++;
  if (s->nheld == 1) {  // first note since silence: start clean on next block
    s->cursor = 0;
    s->dir = 1;
    s->step_valid = false;
  }
}

static void arp_release(UnitState* s, float pitch) {
  for (int i = 0; i < s->nheld; i++) {
    if (fabsf(s->held[i].pitch - pitch) >= 0.001f)
      continue;
    for (int j = i; j < s->nheld - 1; j++)
      s->held[j] = s->held[j + 1];
    s->nheld--;
    return;
  }
}

static void arp_release_sounding(UnitState* s) {
  if (s->sounding != ARP_NONE) {
    unit_note_emit(s->sounding, 0, false);
    s->sounding = ARP_NONE;
  }
}

// Pick the note for this step from the held notes sorted ascending, cycling
// through OCT octaves. Advances the mode's own cursor; returns the pitch and
// writes the source note's velocity.
static float arp_pick(UnitState* s, const uint8_t* p, uint8_t* out_vel) {
  ArpHeld sorted[ARP_MAX_HELD];
  for (int i = 0; i < s->nheld; i++) {
    ArpHeld v = s->held[i];
    int j = i;
    while (j > 0 && sorted[j - 1].pitch > v.pitch) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = v;
  }

  int oct = 1 + (int)(p[4] / 255.0f * 3.0f + 0.5f);  // 1..4
  if (oct < 1)
    oct = 1;
  if (oct > 4)
    oct = 4;
  int total = s->nheld * oct;
  int index;

  switch (p[2]) {
    case 1:  // DOWN
      index = total - 1 - (s->cursor % total);
      s->cursor = (s->cursor + 1) % total;
      break;
    case 2:  // UPDN (ping-pong)
      index = s->cursor;
      if (total > 1) {
        s->cursor += s->dir;
        if (s->cursor >= total - 1 || s->cursor <= 0)
          s->dir = -s->dir;
      }
      break;
    case 3:  // RAND
      s->rng = s->rng * 1664525u + 1013904223u;
      index = (int)(s->rng % (uint32_t)total);
      break;
    default:  // UP
      index = s->cursor % total;
      s->cursor = (s->cursor + 1) % total;
      break;
  }

  ArpHeld* n = &sorted[index % s->nheld];
  *out_vel = n->vel;
  return n->pitch + (float)(index / s->nheld) * 12.0f;
}

static bool arp_note_event(UnitState* s, const uint8_t* p, float pitch, uint8_t vel, bool on) {
  if (!p[0]) {  // OFF — hand notes through untouched
    if (s->nheld) {
      arp_release_sounding(s);
      s->nheld = 0;
    }
    return false;
  }
  if (on) {
    arp_hold(s, pitch, vel);
  } else {
    arp_release(s, pitch);
    if (s->nheld == 0)
      arp_release_sounding(s);
  }
  return true;
}

static void arp_render(UnitState* s, const uint8_t* p,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r, uint32_t frames) {
  (void)in_l;
  (void)in_r;
  (void)out_l;
  (void)out_r;
  (void)frames;

  if (!p[0] || s->nheld == 0) {
    arp_release_sounding(s);
    s->step_valid = false;
    return;
  }

  uint64_t step_len = arp_step_samples(p);
  if (!step_len)
    return;

  uint64_t step = g_unit_render_samples / step_len;
  uint64_t pos = g_unit_render_samples % step_len;

  if (!s->step_valid || step != s->step_last) {
    arp_release_sounding(s);
    uint8_t vel = 100;
    float next = arp_pick(s, p, &vel);
    unit_note_emit(next, vel, true);
    s->sounding = next;
    s->step_last = step;
    s->step_valid = true;
    s->gate_closed = false;
    return;
  }

  if (!s->gate_closed) {
    float gate = p2f(p[3], 0.05f, 1.0f);
    if (gate >= 0.999f)
      return;  // holds the whole step; retrigger at the next one
    if (pos >= (uint64_t)(gate * (float)step_len)) {
      arp_release_sounding(s);
      s->gate_closed = true;
    }
  }
}

static const char* const arp_on_names[] = {"OFF", "ON"};
static const char* const arp_rate_names[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
static const char* const arp_mode_names[] = {"UP", "DOWN", "UPDN", "RAND"};

static char arp_fmt_buf[8];
static const char* arp_format_param(UnitState* s, int idx, uint8_t val) {
  (void)s;
  if (idx == 4) {  // OCT — show the octave count, not a hex byte
    snprintf(arp_fmt_buf, sizeof(arp_fmt_buf), "%d", 1 + (int)(val / 255.0f * 3.0f + 0.5f));
    return arp_fmt_buf;
  }
  return NULL;
}

const UnitDef unit_arp = {
    .id = "arp",
    .name = "ARP",
    .is_source = false,
    .role_label = "NOTE",
    .renders_for_side_effect = true,
    .num_params = 5,
    .param_names = {"ON", "RATE", "MODE", "GATE", "OCT"},
    .param_defaults = {1, 4, 0, 0x80, 0},
    .param_enums = {arp_on_names, arp_rate_names, arp_mode_names, NULL, NULL},
    .param_enum_count = {2, 6, 4, 0, 0},
    .format_param_val = arp_format_param,
    .create = arp_create,
    .destroy = arp_destroy,
    .kill = arp_kill,
    .note_event = arp_note_event,
    .render = arp_render,
};
