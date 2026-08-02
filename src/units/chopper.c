// Chopper — tempo-synced buffer repeat / beat-chop, modelled on Renoise's
// Repeater. While engaged it grabs the slice of audio that just went past and
// loops it, so the source stutters in time with the song instead of playing
// on. Repeat length is always derived from the song BPM (see unit.h's
// g_unit_samples_per_line), so it follows tempo changes for free.
//
// P0 ON:   0=bypass (audio passes through)  1=chopping
// P1 MODE: EVEN=plain divisions  TRIP=triplets (x2/3)  DOT=dotted (x3/2)
//          FREE=continuous, unquantised divisor
// P2 SIZE: repeat length, 8/1 (eight units) down to 1/128. EVEN/TRIP/DOT
//          snap to the table; FREE sweeps the same range smoothly (so it can
//          be swept from a pattern for a riser/glitch).
// P3 SYNC: what "1/1" means. BEAT=a whole note (four beats), so 4/1 is four
//          bars and 1/4 is a single beat. LINE=one pattern line, so 4/1 is
//          every fourth line and 1/2 is half a line.
//
// LINE sync reaches every SIZE at any sane tempo. BEAT sync doesn't: a slice
// has to be buffered in full, and past 1/1 that outgrows the four seconds
// below at normal tempos (at 120 BPM, 2/1 is already 4s, so 2/1 / 4/1 / 8/1
// all fold to the same length there). Use LINE sync for long chops.
//
// ON is a plain 0/1 param specifically so it can be automated per step from
// the pattern — that's the whole point of the effect, and it's why ON exists
// separately from MODE rather than folding an "off" into the mode list.
// There's deliberately no HOLD: freezing the slice is just "leave ON set and
// don't touch SIZE", and making SIZE changes always re-slice keeps a swept
// SIZE useful instead of silently doing nothing.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

// Four seconds per channel (~1.4 MB per instance, and there's one instance
// per lane/track playing the instrument — so this is the main cost of the
// unit and deliberately not larger). Covers every SIZE in LINE sync at any
// sane tempo, and up to 2/1 on the beat grid at 120 BPM. A request longer
// than this is halved until it fits rather than clamped, so it stays on a
// musical division instead of landing on an arbitrary length.
#define CHOP_MAX (44100 * 4)
#define CHOP_MIN_LEN 64  // guard against a zero/absurd length locking up playback

struct UnitState {
  float buf_l[CHOP_MAX];
  float buf_r[CHOP_MAX];
  int write;       // circular record head
  int active;      // currently looping a slice
  int loop_start;  // where the captured slice begins in buf
  int loop_len;    // its length in samples
  int play_pos;    // 0..loop_len
  // Display only: format_param_val() gets no access to the other params, so
  // the last-rendered MODE is cached to label DIV correctly. Cosmetic — a
  // stale value can only mislabel the readout, never affect audio.
  uint8_t disp_mode;
  float sample_rate;
};

static UnitState* chopper_create(float sr) {
  UnitState* s = calloc(1, sizeof(*s));
  s->sample_rate = sr;
  return s;
}
static void chopper_destroy(UnitState* s) { free(s); }
static void chopper_kill(UnitState* s) {
  memset(s->buf_l, 0, sizeof(s->buf_l));
  memset(s->buf_r, 0, sizeof(s->buf_r));
  s->write = 0;
  s->active = 0;
  s->play_pos = 0;
  s->loop_len = 0;
}

// Repeat length as a multiple of the sync unit (whole note, or pattern line).
// Values above 1 are the "every Nth" end: 4/1 in BEAT sync is four bars.
static const float chop_sizes[] = {8, 4, 2, 1, 0.5f, 0.25f, 0.125f,
                                   0.0625f, 0.03125f, 0.015625f, 0.0078125f};
#define CHOP_NSIZES ((int)(sizeof(chop_sizes) / sizeof(chop_sizes[0])))

static int chop_size_index(uint8_t divp) {
  int i = (divp * CHOP_NSIZES) / 256;
  return i > CHOP_NSIZES - 1 ? CHOP_NSIZES - 1 : i;
}

// Same range as the table, swept continuously: 8 at one end, 1/128 at the other.
static float chop_free_size(uint8_t divp) {
  return 8.0f * powf(1024.0f, -(divp / 255.0f));
}

// "4/1" for multiples, "1/8" for fractions.
static void chop_size_label(char* out, size_t n, float mult, const char* suffix) {
  if (mult >= 1.0f)
    snprintf(out, n, "%g/1%s", (double)mult, suffix);
  else
    snprintf(out, n, "1/%g%s", (double)(1.0f / mult), suffix);
}

// Repeat length in samples for the current params, or 0 if tempo isn't known
// yet (before the first block renders).
static int chop_len_samples(UnitState* s, const uint8_t* p) {
  uint32_t spl = g_unit_samples_per_line;
  if (!spl)
    return 0;

  int mode = p[1];
  // BEAT: the unit is a whole note (16 lines). LINE: it's a single line.
  float base = p[3] ? (float)spl : (float)spl * 16.0f;

  float size = mode == 3 ? chop_free_size(p[2]) : chop_sizes[chop_size_index(p[2])];
  float feel = mode == 1 ? (2.0f / 3.0f) : mode == 2 ? 1.5f
                                                     : 1.0f;

  int n = (int)(base * size * feel + 0.5f);
  // Too long for the buffer: halve until it fits. Keeps it on a musical
  // division (an octave down each time) rather than an arbitrary cut.
  while (n > CHOP_MAX)
    n /= 2;
  if (n < CHOP_MIN_LEN)
    n = CHOP_MIN_LEN;
  return n;
}

static void chopper_render(UnitState* s, const uint8_t* p,
                           const float* in_l, const float* in_r,
                           float* out_l, float* out_r, uint32_t frames) {
  s->disp_mode = p[1];

  int want_len = chop_len_samples(s, p);
  int on = p[0] && want_len > 0;

  if (!on) {
    // Bypass: pass through, but keep recording so a slice is already primed
    // the instant ON flips — otherwise the first chop would replay silence.
    s->active = 0;
    for (uint32_t i = 0; i < frames; i++) {
      float l = in_l ? in_l[i] : 0.0f;
      float r = in_r ? in_r[i] : 0.0f;
      s->buf_l[s->write] = l;
      s->buf_r[s->write] = r;
      if (++s->write >= CHOP_MAX)
        s->write = 0;
      out_l[i] = l;
      out_r[i] = r;
    }
    return;
  }

  // Engaging, or re-slicing after a SIZE change: capture the slice that just
  // went past the record head and start looping it.
  if (!s->active || want_len != s->loop_len) {
    s->loop_len = want_len;
    s->loop_start = s->write - want_len;
    while (s->loop_start < 0)
      s->loop_start += CHOP_MAX;
    s->play_pos = 0;
    s->active = 1;
  }

  // Recording stops while engaged, so the slice can't be overwritten by the
  // audio still arriving underneath it.
  for (uint32_t i = 0; i < frames; i++) {
    int idx = s->loop_start + s->play_pos;
    if (idx >= CHOP_MAX)
      idx -= CHOP_MAX;
    out_l[i] = s->buf_l[idx];
    out_r[i] = s->buf_r[idx];
    if (++s->play_pos >= s->loop_len)
      s->play_pos = 0;
  }
}

static const char* const chop_on_names[] = {"OFF", "ON"};
static const char* const chop_mode_names[] = {"EVEN", "TRIP", "DOT", "FREE"};
static const char* const chop_sync_names[] = {"BEAT", "LINE"};

static char chop_fmt_buf[16];
static const char* chop_format_param(UnitState* s, int idx, uint8_t val) {
  if (idx != 2)
    return NULL;            // only SIZE gets a custom readout
  if (s->disp_mode == 3) {  // FREE — show the actual continuous length
    float m = chop_free_size(val);
    if (m >= 1.0f)
      snprintf(chop_fmt_buf, sizeof(chop_fmt_buf), "%.2f/1", (double)m);
    else
      snprintf(chop_fmt_buf, sizeof(chop_fmt_buf), "1/%.2f", (double)(1.0f / m));
  } else {
    const char* suffix = s->disp_mode == 1 ? "T" : s->disp_mode == 2 ? "D"
                                                                     : "";
    chop_size_label(chop_fmt_buf, sizeof(chop_fmt_buf),
                    chop_sizes[chop_size_index(val)], suffix);
  }
  return chop_fmt_buf;
}

const UnitDef unit_chopper = {
    .id = "chopper",
    .name = "CHOPPER",
    .is_source = false,
    .num_params = 4,
    .param_names = {"ON", "MODE", "SIZE", "SYNC"},
    // 0x92 lands on 1/8 in the SIZE table — audible immediately when switched
    // on, without having to dial anything in first.
    .param_defaults = {0, 0, 0x92, 0},
    .param_enums = {chop_on_names, chop_mode_names, NULL, chop_sync_names},
    .param_enum_count = {2, 4, 0, 2},
    .format_param_val = chop_format_param,
    .create = chopper_create,
    .destroy = chopper_destroy,
    .kill = chopper_kill,
    .render = chopper_render,
};
