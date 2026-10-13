#include "synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SILENCE (2147483648.0 * 1e-5)  // -100 dBFS
#define SILENT_BLOCKS 32               // ~21 ms of silence before a voice is parked

typedef struct {
  int key;
  bool active;  // triggered and not parked yet
  bool held;    // note-on without its note-off
  float gain;
  float env;  // release fade: 1 until note-off, then down to 0
  uint32_t age;
  int silent;
} voice_t;

struct synth {
  st_engine* eng;
  double params[PARAM_COUNT];
  voice_t voices[ST_VOICES];
  uint32_t clock;
  uint32_t trig, release;
  // engine output at 48 kHz, read through a 4-point window
  float block[ST_FRAMES];
  int block_pos;
  float w[4];
  double frac, step;
};

// --- params ------------------------------------------------------------------

static const char* global_names[PID_GLOBAL_COUNT] = {"Machine", "Level", "Gate", "Voices", "Velocity", "Release"};

bool param_info_by_id(uint32_t id, param_info_t* out) {
  memset(out, 0, sizeof(*out));
  out->id = id;
  if (id < PID_GLOBAL_COUNT) {
    out->name = global_names[id];
    out->module = "";
    switch (id) {
      case PID_MACHINE:
        out->max = MACHINE_COUNT - 1, out->stepped = out->is_enum = true;
        break;
      case PID_LEVEL:
        out->max = 1, out->def = 0.5;
        break;
      case PID_GATE:
        out->max = 1, out->stepped = true;
        break;
      case PID_VOICES:
        out->min = 1, out->max = ST_VOICES, out->def = ST_VOICES, out->stepped = true;
        break;
      case PID_VELOCITY:
        out->max = 1, out->def = 1;
        break;
      case PID_RELEASE:
        out->max = 1, out->def = 1;
        break;
    }
    return true;
  }
  if (id < PID_KNOB0 || id >= PID_KNOB0 + MACHINE_COUNT * KNOB_COUNT)
    return false;
  const machine_t* m = &machines[(id - PID_KNOB0) / KNOB_COUNT];
  const knob_t* k = &m->knobs[(id - PID_KNOB0) % KNOB_COUNT];
  out->name = k->code;
  out->module = m->name;
  out->min = k->min;
  out->max = k->max;
  out->def = k->def;
  out->stepped = true;
  return true;
}

bool param_info(uint32_t index, param_info_t* out) {
  if (index < PID_GLOBAL_COUNT)
    return param_info_by_id(index, out);
  index -= PID_GLOBAL_COUNT;
  if (index >= MACHINE_COUNT * KNOB_COUNT)
    return false;
  return param_info_by_id(PID_KNOB0 + index, out);
}

void param_text(uint32_t id, double value, char* out, uint32_t size) {
  if (id == PID_MACHINE) {
    int m = (int)lround(value);
    snprintf(out, size, "%s", machines[m < 0 ? 0 : m >= MACHINE_COUNT ? MACHINE_COUNT - 1
                                                                      : m]
                                  .name);
  } else if (id == PID_GATE) {
    snprintf(out, size, "%s", value >= 0.5 ? "ON" : "OFF");
  } else if (id == PID_RELEASE) {
    if (value >= 1.0)
      snprintf(out, size, "OFF");
    else
      snprintf(out, size, "%dms", (int)lround(value * RELEASE_MAX * 1000));
  } else if (id == PID_LEVEL || id == PID_VELOCITY) {
    snprintf(out, size, "%d%%", (int)lround(value * 100));
  } else if (id >= PID_KNOB0) {
    const knob_t* k = &machines[(id - PID_KNOB0) / KNOB_COUNT].knobs[(id - PID_KNOB0) % KNOB_COUNT];
    int v = (int)lround(value);
    if (k->min == 40 && k->max == 88)
      snprintf(out, size, "%s %+d", k->code, v - 64);  // bipolar, like the Syntakt shows it
    else
      snprintf(out, size, "%s %d", k->code, v);
  } else {
    snprintf(out, size, "%d", (int)lround(value));
  }
}

static int machine_index(const synth_t* s) {
  int m = (int)lround(s->params[PID_MACHINE]);
  return m < 0 ? 0 : m >= MACHINE_COUNT ? MACHINE_COUNT - 1
                                        : m;
}

static uint32_t param_index(uint32_t id) {
  return id < PID_GLOBAL_COUNT ? id : PID_GLOBAL_COUNT + (id - PID_KNOB0);
}

// push the current machine and its knobs to all 8 engine tracks
static void apply_machine(synth_t* s) {
  if (!s->eng)
    return;
  int mi = machine_index(s);
  const machine_t* m = &machines[mi];
  for (int t = 0; t < ST_VOICES; t++) {
    st_machine(s->eng, t, m->type);
    for (int k = 0; k < KNOB_COUNT; k++) {
      double v = s->params[param_index(PID_KNOB(mi, k))];
      st_param(s->eng, t, m->knobs[k].slot, (uint16_t)lround(v * 256));
    }
    st_param(s->eng, t, ST_GATE, s->params[PID_GATE] >= 0.5 ? 256 : 0);
  }
}

void synth_set_param(synth_t* s, uint32_t id, double value) {
  param_info_t pi;
  if (!param_info_by_id(id, &pi))
    return;
  if (value < pi.min)
    value = pi.min;
  if (value > pi.max)
    value = pi.max;
  if (pi.stepped)
    value = round(value);
  s->params[param_index(id)] = value;
  if (id == PID_MACHINE || id == PID_GATE || id >= PID_KNOB0)
    apply_machine(s);
}

double synth_get_param(const synth_t* s, uint32_t id) {
  param_info_t pi;
  if (!param_info_by_id(id, &pi))
    return 0;
  return s->params[param_index(id)];
}

// --- voices ------------------------------------------------------------------

void synth_note_on(synth_t* s, int key, double velocity) {
  if (!s->eng)
    return;
  int n = (int)lround(s->params[PID_VOICES]);
  int best = 0;
  // a free voice, else the same key, else the oldest
  for (int i = 0, score = -1; i < n; i++) {
    voice_t* v = &s->voices[i];
    int sc = !v->active ? 3 : v->key == key ? 2
                                            : 1;
    if (sc > score || (sc == score && sc == 1 && v->age < s->voices[best].age)) {
      score = sc;
      best = i;
    }
  }
  voice_t* v = &s->voices[best];
  double amt = s->params[PID_VELOCITY];
  // hosts may send more than 1.0 (poketrack's 0-255 byte / 127)
  if (velocity > 1.0)
    velocity = 1.0;
  if (velocity < 0.0)
    velocity = 0.0;
  v->key = key;
  v->active = true;
  v->held = true;
  v->gain = (float)(1.0 - amt + amt * velocity);
  v->env = 1.0f;
  v->age = s->clock++;
  v->silent = 0;
  st_note(s->eng, best, (int32_t)(key - 60) * 65536);
  s->trig |= 1u << best;
  s->release &= ~(1u << best);
}

void synth_note_off(synth_t* s, int key) {
  for (int i = 0; i < ST_VOICES; i++) {
    voice_t* v = &s->voices[i];
    if (v->held && v->key == key) {
      v->held = false;
      s->release |= 1u << i;
    }
  }
}

// --- rendering -----------------------------------------------------------------

static void render_block(synth_t* s) {
  memset(s->block, 0, sizeof(s->block));
  if (!s->eng)
    return;
  int32_t out[ST_VOICES][ST_FRAMES];
  if (st_block(s->eng, s->trig, s->release, out)) {
    // the engine hit something it can't run: stop rather than make noise
    cf_cpu* c = s->eng->cpu;
    fprintf(stderr, "syntakt: engine error %d at 0x%08x (op 0x%04x)\n", c->err, c->err_pc, c->err_op);
    st_free(s->eng);
    s->eng = NULL;
    return;
  }
  s->trig = s->release = 0;
  float level = (float)(s->params[PID_LEVEL] / 2147483648.0);
  double rel = s->params[PID_RELEASE];
  // per-sample fade step while released (0 = no fade)
  float fade = rel >= 1.0 ? 0.0f : (float)(1.0 / (rel * RELEASE_MAX * ST_RATE + 1.0));
  for (int t = 0; t < ST_VOICES; t++) {
    voice_t* v = &s->voices[t];
    if (!v->active)
      continue;
    float g = v->gain * level;
    float df = v->held ? 0.0f : fade;
    int32_t peak = 0;
    for (int i = 0; i < ST_FRAMES; i++) {
      int32_t x = out[t][i];
      if (df > 0.0f)
        v->env = v->env > df ? v->env - df : 0.0f;
      s->block[i] += (float)x * g * v->env;
      int32_t a = x < 0 ? (x == INT32_MIN ? INT32_MAX : -x) : x;
      if (a > peak)
        peak = a;
    }
    if (v->env <= 0.0f) {
      v->active = false;
      st_park(s->eng, t);
    } else if (peak < SILENCE && !v->held) {
      if (++v->silent >= SILENT_BLOCKS) {
        v->active = false;
        st_park(s->eng, t);
      }
    } else {
      v->silent = 0;
    }
  }
}

static float next48k(synth_t* s) {
  if (s->block_pos >= ST_FRAMES) {
    render_block(s);
    s->block_pos = 0;
  }
  return s->block[s->block_pos++];
}

void synth_render(synth_t* s, float* left, float* right, uint32_t frames) {
  for (uint32_t i = 0; i < frames; i++) {
    float y;
    if (s->step == 1.0) {
      y = next48k(s);
    } else {
      while (s->frac >= 1.0) {
        s->w[0] = s->w[1];
        s->w[1] = s->w[2];
        s->w[2] = s->w[3];
        s->w[3] = next48k(s);
        s->frac -= 1.0;
      }
      // 4-point Hermite between w[1] and w[2]
      float t = (float)s->frac, x0 = s->w[0], x1 = s->w[1], x2 = s->w[2], x3 = s->w[3];
      float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
      y = ((c3 * t + c2) * t + c1) * t + x1;
      s->frac += s->step;
    }
    left[i] = y;
    if (right != left)
      right[i] = y;
  }
}

// --- lifetime ------------------------------------------------------------------

void synth_set_rate(synth_t* s, double rate) {
  s->step = rate > 0 ? ST_RATE / rate : 1.0;
  s->frac = 1.0;
  memset(s->w, 0, sizeof(s->w));
}

void synth_reset(synth_t* s) {
  for (int t = 0; t < ST_VOICES; t++) {
    s->voices[t].active = s->voices[t].held = false;
    if (s->eng)
      st_park(s->eng, t);
  }
  s->trig = s->release = 0;
  s->block_pos = ST_FRAMES;
  synth_set_rate(s, ST_RATE / s->step);
}

synth_t* synth_new(const uint8_t* image, double rate) {
  synth_t* s = calloc(1, sizeof(synth_t));
  if (!s)
    return NULL;
  if (image)
    s->eng = st_new(image, ST_IMAGE_SIZE);
  param_info_t pi;
  for (uint32_t i = 0; param_info(i, &pi); i++) s->params[param_index(pi.id)] = pi.def;
  s->step = 1.0;
  synth_set_rate(s, rate);
  synth_reset(s);
  apply_machine(s);
  return s;
}

bool synth_has_engine(const synth_t* s) {
  return s->eng != NULL;
}

void synth_free(synth_t* s) {
  if (!s)
    return;
  st_free(s->eng);
  free(s);
}
