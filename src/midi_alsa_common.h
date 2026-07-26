#pragma once
// Shared ALSA sequencer port enumeration for midi_in.c/midi_out.c (Linux,
// HAVE_ALSA only). Header-only so neither translation unit needs a new
// source file or CMakeLists.txt change.
#include <alsa/asoundlib.h>
#include <stdio.h>

typedef struct {
  int client;
  int port;
  char name[256];
} AlsaPort;

// Enumerates ALSA sequencer ports with the given capability (SND_SEQ_PORT_CAP_READ
// or _WRITE), skipping the system announce client (0) and our own client. Writes
// up to max_ports entries into out, returns the count found.
//
// If connect_from_myport >= 0, also subscribes that port on seq to each
// discovered port as it's found — used by MIDI-in to receive from every
// currently-visible source; MIDI-out passes -1 since it connects lazily,
// only to the one port the user actually selects.
static inline int alsa_enumerate_ports(snd_seq_t* seq, unsigned int cap_flag,
                                       AlsaPort* out, int max_ports,
                                       int connect_from_myport) {
  int n = 0;
  if (!seq)
    return 0;
  snd_seq_client_info_t* ci;
  snd_seq_port_info_t* pi;
  snd_seq_client_info_alloca(&ci);
  snd_seq_port_info_alloca(&pi);
  snd_seq_client_info_set_client(ci, -1);
  while (snd_seq_query_next_client(seq, ci) >= 0 && n < max_ports) {
    int c = snd_seq_client_info_get_client(ci);
    if (c == 0 || c == snd_seq_client_id(seq))
      continue;
    snd_seq_port_info_set_client(pi, c);
    snd_seq_port_info_set_port(pi, -1);
    while (snd_seq_query_next_port(seq, pi) >= 0 && n < max_ports) {
      unsigned int caps = snd_seq_port_info_get_capability(pi);
      if (!(caps & cap_flag))
        continue;
      if (caps & SND_SEQ_PORT_CAP_NO_EXPORT)
        continue;
      out[n].client = c;
      out[n].port = snd_seq_port_info_get_port(pi);
      snprintf(out[n].name, sizeof(out[n].name), "%s:%s",
               snd_seq_client_info_get_name(ci),
               snd_seq_port_info_get_name(pi));
      if (connect_from_myport >= 0)
        snd_seq_connect_from(seq, connect_from_myport, c, out[n].port);
      n++;
    }
  }
  return n;
}
