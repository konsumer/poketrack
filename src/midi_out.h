#pragma once
#include <stdbool.h>
#include <stdint.h>

// Opaque per-instance handle
typedef struct MidiOut MidiOut;

// Call once at startup / shutdown
void midi_out_global_init(void);
void midi_out_global_shutdown(void);

// Port enumeration (call any time; strings valid until next call on same thread)
int         midi_out_port_count(void);
const char *midi_out_port_name(int idx);

// Per-instance open / close / send
MidiOut *midi_out_open(int port_idx);
void     midi_out_close(MidiOut *m);
void     midi_out_send(MidiOut *m, const uint8_t *msg, int len);
int      midi_out_port_idx(const MidiOut *m);  // -1 if invalid
