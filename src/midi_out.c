#include "midi_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// macOS — CoreMIDI
// ============================================================
#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

static MIDIClientRef g_client = 0;
static MIDIPortRef g_port = 0;

struct MidiOut {
  MIDIEndpointRef dest;
  int port_idx;
};

void midi_out_global_init(void) {
  MIDIClientCreate(CFSTR("raypoketrack"), NULL, NULL, &g_client);
  MIDIOutputPortCreate(g_client, CFSTR("output"), &g_port);
}

void midi_out_global_shutdown(void) {
  if (g_port) {
    MIDIPortDispose(g_port);
    g_port = 0;
  }
  if (g_client) {
    MIDIClientDispose(g_client);
    g_client = 0;
  }
}

int midi_out_port_count(void) {
  return (int)MIDIGetNumberOfDestinations();
}

const char *midi_out_port_name(int idx) {
  static char buf[256];
  MIDIEndpointRef ep = MIDIGetDestination((ItemCount)idx);
  if (!ep) {
    snprintf(buf, sizeof(buf), "Device %d", idx);
    return buf;
  }
  CFStringRef name = NULL;
  MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name);
  if (name) {
    CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(name);
  } else {
    snprintf(buf, sizeof(buf), "Device %d", idx);
  }
  return buf;
}

MidiOut *midi_out_open(int idx) {
  MIDIEndpointRef ep = MIDIGetDestination((ItemCount)idx);
  if (!ep || !g_client || !g_port)
    return NULL;
  MidiOut *m = calloc(1, sizeof(*m));
  m->dest = ep;
  m->port_idx = idx;
  return m;
}

void midi_out_close(MidiOut *m) { free(m); }

void midi_out_send(MidiOut *m, const uint8_t *msg, int len) {
  if (!m || !m->dest || !g_port || len < 1 || len > 3)
    return;
  // Stack-allocated packet list (enough for one short message)
  struct {
    MIDIPacketList list;
    MIDIPacket pad[2];
  } buf;
  MIDIPacket *pkt = MIDIPacketListInit(&buf.list);
  pkt = MIDIPacketListAdd(&buf.list, sizeof(buf), pkt, 0, (ByteCount)len, msg);
  if (pkt)
    MIDISend(g_port, m->dest, &buf.list);
}

int midi_out_port_idx(const MidiOut *m) { return m ? m->port_idx : -1; }

// ============================================================
// Linux — ALSA sequencer
// ============================================================
#elif defined(__linux__) && defined(HAVE_ALSA)

#include <alsa/asoundlib.h>

static snd_seq_t *g_seq = NULL;
static int g_myport = -1;

#define MAX_PORTS 64
typedef struct {
  int client;
  int port;
  char name[256];
} AlsaPort;
static AlsaPort g_ports[MAX_PORTS];
static int g_nports = 0;

struct MidiOut {
  int dest_client;
  int dest_port;
  int port_idx;
};

static void refresh_ports(void) {
  g_nports = 0;
  if (!g_seq)
    return;
  snd_seq_client_info_t *ci;
  snd_seq_port_info_t *pi;
  snd_seq_client_info_alloca(&ci);
  snd_seq_port_info_alloca(&pi);
  snd_seq_client_info_set_client(ci, -1);
  while (snd_seq_query_next_client(g_seq, ci) >= 0 && g_nports < MAX_PORTS) {
    int c = snd_seq_client_info_get_client(ci);
    if (c == 0 || c == snd_seq_client_id(g_seq))
      continue;
    snd_seq_port_info_set_client(pi, c);
    snd_seq_port_info_set_port(pi, -1);
    while (snd_seq_query_next_port(g_seq, pi) >= 0 && g_nports < MAX_PORTS) {
      unsigned int caps = snd_seq_port_info_get_capability(pi);
      if (!(caps & SND_SEQ_PORT_CAP_WRITE))
        continue;
      if (caps & SND_SEQ_PORT_CAP_NO_EXPORT)
        continue;
      g_ports[g_nports].client = c;
      g_ports[g_nports].port = snd_seq_port_info_get_port(pi);
      snprintf(g_ports[g_nports].name, 256, "%s:%s",
               snd_seq_client_info_get_name(ci),
               snd_seq_port_info_get_name(pi));
      g_nports++;
    }
  }
}

void midi_out_global_init(void) {
  if (snd_seq_open(&g_seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0) {
    g_seq = NULL;
    return;
  }
  snd_seq_set_client_name(g_seq, "raypoketrack");
  g_myport = snd_seq_create_simple_port(g_seq, "output",
                                        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
  refresh_ports();
}

void midi_out_global_shutdown(void) {
  if (g_seq) {
    snd_seq_close(g_seq);
    g_seq = NULL;
  }
}

int midi_out_port_count(void) {
  refresh_ports();
  return g_nports;
}
const char *midi_out_port_name(int idx) {
  refresh_ports();
  return (idx >= 0 && idx < g_nports) ? g_ports[idx].name : "?";
}

MidiOut *midi_out_open(int idx) {
  refresh_ports();
  if (!g_seq || idx < 0 || idx >= g_nports)
    return NULL;
  MidiOut *m = calloc(1, sizeof(*m));
  m->dest_client = g_ports[idx].client;
  m->dest_port = g_ports[idx].port;
  m->port_idx = idx;
  snd_seq_connect_to(g_seq, g_myport, m->dest_client, m->dest_port);
  return m;
}

void midi_out_close(MidiOut *m) {
  if (!m)
    return;
  if (g_seq)
    snd_seq_disconnect_to(g_seq, g_myport, m->dest_client, m->dest_port);
  free(m);
}

void midi_out_send(MidiOut *m, const uint8_t *msg, int len) {
  if (!m || !g_seq || len < 1)
    return;
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, g_myport);
  snd_seq_ev_set_dest(&ev, m->dest_client, m->dest_port);
  snd_seq_ev_set_direct(&ev);
  uint8_t status = msg[0] & 0xF0;
  uint8_t ch = msg[0] & 0x0F;
  if (status == 0x90 && len >= 3) {
    snd_seq_ev_set_noteon(&ev, ch, msg[1], msg[2]);
  } else if (status == 0x80 && len >= 3) {
    snd_seq_ev_set_noteoff(&ev, ch, msg[1], msg[2]);
  } else if (status == 0xB0 && len >= 3) {
    snd_seq_ev_set_controller(&ev, ch, msg[1], msg[2]);
  } else {
    // raw bytes fallback
    snd_seq_ev_set_sysex(&ev, len, (void *)msg);
  }
  snd_seq_event_output_direct(g_seq, &ev);
}

int midi_out_port_idx(const MidiOut *m) { return m ? m->port_idx : -1; }

// ============================================================
// Windows — WinMM
// ============================================================
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

struct MidiOut {
  HMIDIOUT handle;
  int port_idx;
};

void midi_out_global_init(void) {}
void midi_out_global_shutdown(void) {}

int midi_out_port_count(void) { return (int)midiOutGetNumDevs(); }

const char *midi_out_port_name(int idx) {
  static char buf[MAXPNAMELEN + 4];
  MIDIOUTCAPSA caps;
  if (midiOutGetDevCapsA((UINT)idx, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
    strncpy(buf, caps.szPname, sizeof(buf) - 1);
  else
    snprintf(buf, sizeof(buf), "Device %d", idx);
  return buf;
}

MidiOut *midi_out_open(int idx) {
  MidiOut *m = calloc(1, sizeof(*m));
  if (midiOutOpen(&m->handle, (UINT)idx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    free(m);
    return NULL;
  }
  m->port_idx = idx;
  return m;
}

void midi_out_close(MidiOut *m) {
  if (!m)
    return;
  midiOutClose(m->handle);
  free(m);
}

void midi_out_send(MidiOut *m, const uint8_t *msg, int len) {
  if (!m || len < 1 || len > 3)
    return;
  DWORD word = 0;
  memcpy(&word, msg, len);
  midiOutShortMsg(m->handle, word);
}

int midi_out_port_idx(const MidiOut *m) { return m ? m->port_idx : -1; }

// ============================================================
// Web — Web MIDI API outputs (JS bridge via ccall)
// ============================================================
#elif defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <string.h>

#define MAX_OUT_PORTS 32
static char g_web_out_names[MAX_OUT_PORTS][256];
static int g_web_out_count = 0;

struct MidiOut {
  int port_idx;
};

void midi_out_global_init(void) {}
void midi_out_global_shutdown(void) {}
int midi_out_port_count(void) { return g_web_out_count; }

const char *midi_out_port_name(int idx) {
  if (idx < 0 || idx >= g_web_out_count)
    return "?";
  return g_web_out_names[idx];
}

EMSCRIPTEN_KEEPALIVE void midi_out_web_set_port_count(int n) {
  g_web_out_count = n < MAX_OUT_PORTS ? n : MAX_OUT_PORTS;
}

EMSCRIPTEN_KEEPALIVE void midi_out_web_set_port_name(int idx, const char *name) {
  if (idx < 0 || idx >= MAX_OUT_PORTS || !name)
    return;
  strncpy(g_web_out_names[idx], name, 255);
  g_web_out_names[idx][255] = '\0';
}

MidiOut *midi_out_open(int idx) {
  if (idx < 0 || idx >= g_web_out_count)
    return NULL;
  MidiOut *m = calloc(1, sizeof(*m));
  m->port_idx = idx;
  return m;
}

void midi_out_close(MidiOut *m) { free(m); }

EM_JS(void, midi_out_web_send_js, (int idx, const uint8_t *msg, int len), {
  var outputs = window._midiOutputs;
  if (!outputs || idx < 0 || idx >= outputs.length)
    return;
  var port = outputs[idx];
  if (port && port.state === "connected")
    port.send(HEAPU8.subarray(msg, msg + len));
});

void midi_out_send(MidiOut *m, const uint8_t *msg, int len) {
  if (!m || len < 1)
    return;
  midi_out_web_send_js(m->port_idx, msg, len);
}

int midi_out_port_idx(const MidiOut *m) { return m ? m->port_idx : -1; }

// ============================================================
// Fallback — no MIDI output support
// ============================================================
#else

struct MidiOut { int port_idx; };

void midi_out_global_init(void) {}
void midi_out_global_shutdown(void) {}
int midi_out_port_count(void) { return 0; }
const char *midi_out_port_name(int idx) { (void)idx; return "?"; }
MidiOut *midi_out_open(int idx) { (void)idx; return NULL; }
void midi_out_close(MidiOut *m) { (void)m; }
void midi_out_send(MidiOut *m, const uint8_t *msg, int len) { (void)m; (void)msg; (void)len; }
int midi_out_port_idx(const MidiOut *m) { (void)m; return -1; }

#endif
