// MIDI output unit
// P0       : MIDI channel (enum CH01-CH16)
// P1-P(N)  : user-assigned CC values, one slot per picked CC number (max 7)
//
// DATA row : press A → device picker
// ADD row  : press A → CC number picker (0-127)
//
// Data string: "device_name\tcc1\tcc2\t..." (tab-separated decimal CC numbers)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"
#include "../midi_out.h"

#define MAX_CC_SLOTS 7

struct UnitState {
    MidiOut *out;
    char     device_name[256];
    int      cc_nums[MAX_CC_SLOTS];  // CC numbers assigned to P1..Pn (-1 = empty)
    int      cc_count;               // how many slots are active
    uint8_t  last_note;
    uint8_t  last_channel;
    bool     note_active;
};

// Standard short names for well-known CCs
static const char *cc_short_name(int cc) {
    switch (cc) {
        case 1:  return "MOD";
        case 2:  return "BRE";
        case 7:  return "VOL";
        case 10: return "PAN";
        case 11: return "EXP";
        case 64: return "SUS";
        case 65: return "PRT";
        case 71: return "RES";
        case 74: return "CUT";
        case 91: return "REV";
        case 93: return "CHO";
        default: return NULL;
    }
}

// Longer names for picker list
static const char *cc_long_name(int cc) {
    switch (cc) {
        case 0:  return "Bank Select";
        case 1:  return "Modulation";
        case 2:  return "Breath";
        case 7:  return "Volume";
        case 10: return "Pan";
        case 11: return "Expression";
        case 64: return "Sustain";
        case 65: return "Portamento";
        case 66: return "Sostenuto";
        case 67: return "Soft Pedal";
        case 71: return "Resonance";
        case 72: return "Release";
        case 73: return "Attack";
        case 74: return "Cutoff";
        case 75: return "Decay";
        case 91: return "Reverb";
        case 93: return "Chorus";
        case 94: return "Detune";
        default: return NULL;
    }
}

static UnitState *midi_create(float sr) {
    (void)sr;
    UnitState *s = calloc(1, sizeof(*s));
    for (int i = 0; i < MAX_CC_SLOTS; i++) s->cc_nums[i] = -1;
    return s;
}

static void midi_destroy(UnitState *s) {
    if (s->out) midi_out_close(s->out);
    free(s);
}

static void open_device_by_name(UnitState *s) {
    if (s->out) { midi_out_close(s->out); s->out = NULL; }
    int n = midi_out_port_count();
    for (int i = 0; i < n; i++) {
        const char *name = midi_out_port_name(i);
        if (name && strcmp(name, s->device_name) == 0) {
            s->out = midi_out_open(i);
            return;
        }
    }
}

static void midi_set_data(UnitState *s, const char *data, const char *base_dir) {
    (void)base_dir;
    if (!data || !data[0]) return;

    // Parse: device_name[\tcc1\tcc2\t...]
    char buf[512];
    strncpy(buf, data, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "\t");
    if (!tok) return;
    strncpy(s->device_name, tok, sizeof(s->device_name) - 1);

    s->cc_count = 0;
    for (int i = 0; i < MAX_CC_SLOTS; i++) s->cc_nums[i] = -1;

    while ((tok = strtok(NULL, "\t")) != NULL && s->cc_count < MAX_CC_SLOTS) {
        int cc = atoi(tok);
        if (cc >= 0 && cc <= 127)
            s->cc_nums[s->cc_count++] = cc;
    }

    open_device_by_name(s);
}

static void midi_sync_to_data(UnitState *s, char *buf, size_t sz) {
    int off = snprintf(buf, sz, "%s", s->device_name);
    for (int i = 0; i < s->cc_count && off < (int)sz - 5; i++)
        off += snprintf(buf + off, sz - off, "\t%d", s->cc_nums[i]);
    buf[sz - 1] = '\0';
}

// ---- note_on / note_off / kill ----

static void midi_note_on(UnitState *s, uint8_t note, uint8_t vel, const uint8_t *params) {
    if (!s->out) return;
    uint8_t ch = params[0] & 0x0F;

    if (s->note_active) {
        uint8_t off[3] = {(uint8_t)(0x80 | s->last_channel), s->last_note, 0};
        midi_out_send(s->out, off, 3);
    }

    for (int i = 0; i < s->cc_count; i++) {
        uint8_t cc_val = params[i + 1] >> 1;  // 0-255 → 0-127
        uint8_t cc[3] = {(uint8_t)(0xB0 | ch), (uint8_t)s->cc_nums[i], cc_val};
        midi_out_send(s->out, cc, 3);
    }

    uint8_t on[3] = {(uint8_t)(0x90 | ch), note, vel > 127 ? 127 : vel};
    midi_out_send(s->out, on, 3);
    s->last_note    = note;
    s->last_channel = ch;
    s->note_active  = true;
}

static void midi_note_off(UnitState *s, uint8_t note) {
    if (!s->out || !s->note_active) return;
    uint8_t off[3] = {(uint8_t)(0x80 | s->last_channel), note, 0};
    midi_out_send(s->out, off, 3);
    if (s->last_note == note) s->note_active = false;
}

static void midi_kill(UnitState *s) {
    if (!s->out) return;
    for (int ch = 0; ch < 16; ch++) {
        uint8_t msg[3] = {(uint8_t)(0xB0 | ch), 123, 0};
        midi_out_send(s->out, msg, 3);
    }
    s->note_active = false;
}

static void midi_render(UnitState *s, const uint8_t *params,
                        const float *in_l, const float *in_r,
                        float *out_l, float *out_r, uint32_t frames) {
    (void)s; (void)params;
    (void)in_l; (void)in_r; (void)out_l; (void)out_r; (void)frames;
}

// ---- Dynamic param display ----

static int midi_dyn_num_params(UnitState *s) {
    return 1 + s->cc_count;
}

static const char *midi_dyn_param_name(UnitState *s, int idx) {
    if (idx == 0) return "CHAN";
    int cc = s->cc_nums[idx - 1];
    if (cc < 0) return "?";
    const char *sn = cc_short_name(cc);
    if (sn) return sn;
    static char buf[8];
    snprintf(buf, sizeof(buf), "C%d", cc);
    return buf;
}

static const char *midi_fmt_val(UnitState *s, int idx, uint8_t val) {
    (void)s;
    if (idx == 0) return NULL;  // channel uses enum display
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d", val >> 1);
    return buf;
}

// ---- CC picker (ADD row) ----

static int midi_picker_count(UnitState *s) { (void)s; return 128; }

static const char *midi_picker_name(UnitState *s, int i) {
    (void)s;
    static char buf[32];
    const char *ln = cc_long_name(i);
    if (ln)
        snprintf(buf, sizeof(buf), "CC%03d %s", i, ln);
    else
        snprintf(buf, sizeof(buf), "CC%03d", i);
    return buf;
}

static void midi_picker_add(UnitState *s, int idx) {
    if (s->cc_count >= MAX_CC_SLOTS) return;
    // Don't add duplicates
    for (int i = 0; i < s->cc_count; i++)
        if (s->cc_nums[i] == idx) return;
    s->cc_nums[s->cc_count++] = idx;
}

static void midi_mapping_remove(UnitState *s, int map_idx) {
    // map_idx is the param row index (0=CHAN, 1..n=CCs); remove the CC slot
    int cc_slot = map_idx - 1;
    if (cc_slot < 0 || cc_slot >= s->cc_count) return;
    for (int i = cc_slot; i < s->cc_count - 1; i++)
        s->cc_nums[i] = s->cc_nums[i + 1];
    s->cc_nums[--s->cc_count] = -1;
}

// ---- Device picker (DATA row) ----

static int         midi_dev_picker_count(UnitState *s) { (void)s; return midi_out_port_count(); }
static const char *midi_dev_picker_name(UnitState *s, int i) { (void)s; return midi_out_port_name(i); }

static void midi_dev_picker_set(UnitState *s, int idx) {
    const char *name = midi_out_port_name(idx);
    strncpy(s->device_name, name ? name : "", sizeof(s->device_name) - 1);
    if (s->out) { midi_out_close(s->out); s->out = NULL; }
    s->out = midi_out_open(idx);
}

// ---- Channel enum ----

static const char * const CH_NAMES[] = {
    "CH01","CH02","CH03","CH04","CH05","CH06","CH07","CH08",
    "CH09","CH10","CH11","CH12","CH13","CH14","CH15","CH16",
};

const UnitDef unit_midi = {
    .id          = "midi",
    .name        = "MIDI OUT",
    .data_hint   = "no device",
    .file_filter = NULL,
    .is_source   = true,
    .num_params  = 1,
    .param_names = {"CHAN"},
    .param_defaults = {0},
    .param_enums      = {CH_NAMES},
    .param_enum_count = {16},

    .dyn_num_params   = midi_dyn_num_params,
    .dyn_param_name   = midi_dyn_param_name,
    .format_param_val = midi_fmt_val,

    // CC picker (ADD row)
    .picker_title   = "ADD CC",
    .picker_count   = midi_picker_count,
    .picker_name    = midi_picker_name,
    .picker_add     = midi_picker_add,
    .mapping_remove = midi_mapping_remove,
    .sync_to_data   = midi_sync_to_data,

    // Device picker (DATA row)
    .dev_picker_title = "SELECT DEVICE",
    .dev_picker_count = midi_dev_picker_count,
    .dev_picker_name  = midi_dev_picker_name,
    .dev_picker_set   = midi_dev_picker_set,

    .create   = midi_create,
    .destroy  = midi_destroy,
    .set_data = midi_set_data,

    .note_on  = midi_note_on,
    .note_off = midi_note_off,
    .kill     = midi_kill,
    .render   = midi_render,
};
