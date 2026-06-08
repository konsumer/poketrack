#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t status;   // MIDI status byte (0x80=note_off, 0x90=note_on, 0xB0=CC, ...)
    uint8_t data1;
    uint8_t data2;
    int     port_idx;
} MidiInMsg;

void        midi_in_global_init(void);
void        midi_in_global_shutdown(void);
int         midi_in_port_count(void);
const char *midi_in_port_name(int idx);

// Returns true if a message was available (msg written); false if queue empty.
bool midi_in_poll(MidiInMsg *msg);
