#include "midi_in.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RING_SIZE 512

static MidiInMsg g_ring[RING_SIZE];
static atomic_int g_head = 0;
static atomic_int g_tail = 0;

static void push_msg(uint8_t status, uint8_t d1, uint8_t d2, int port_idx) {
    int h = atomic_load_explicit(&g_head, memory_order_relaxed);
    int next = (h + 1) % RING_SIZE;
    if (next == atomic_load_explicit(&g_tail, memory_order_acquire))
        return; // ring full — drop
    g_ring[h].status   = status;
    g_ring[h].data1    = d1;
    g_ring[h].data2    = d2;
    g_ring[h].port_idx = port_idx;
    atomic_store_explicit(&g_head, next, memory_order_release);
}

bool midi_in_poll(MidiInMsg *msg) {
    int t = atomic_load_explicit(&g_tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&g_head, memory_order_acquire))
        return false;
    *msg = g_ring[t];
    atomic_store_explicit(&g_tail, (t + 1) % RING_SIZE, memory_order_release);
    return true;
}

// ============================================================
// macOS — CoreMIDI
// ============================================================
#if defined(__APPLE__)

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

#define MAX_IN_PORTS 32
static char g_port_names[MAX_IN_PORTS][256];
static int  g_port_count = 0;

static MIDIClientRef g_in_client = 0;
static MIDIPortRef   g_in_port   = 0;

static void midi_in_cb(const MIDIPacketList *pktlist, void *readProc, void *srcRefCon) {
    (void)readProc;
    int port_idx = (int)(intptr_t)srcRefCon;
    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        if (pkt->length >= 1) {
            uint8_t status = pkt->data[0];
            uint8_t d1 = pkt->length >= 2 ? pkt->data[1] : 0;
            uint8_t d2 = pkt->length >= 3 ? pkt->data[2] : 0;
            push_msg(status, d1, d2, port_idx);
        }
        pkt = MIDIPacketNext(pkt);
    }
}

void midi_in_global_init(void) {
    MIDIClientCreate(CFSTR("raypoketrack_in"), NULL, NULL, &g_in_client);
    MIDIInputPortCreate(g_in_client, CFSTR("input"), midi_in_cb, NULL, &g_in_port);
    g_port_count = (int)MIDIGetNumberOfSources();
    if (g_port_count > MAX_IN_PORTS) g_port_count = MAX_IN_PORTS;
    for (int i = 0; i < g_port_count; i++) {
        MIDIEndpointRef src = MIDIGetSource((ItemCount)i);
        CFStringRef name = NULL;
        MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &name);
        if (name) {
            CFStringGetCString(name, g_port_names[i], 256, kCFStringEncodingUTF8);
            CFRelease(name);
        } else {
            snprintf(g_port_names[i], 256, "Source %d", i);
        }
        MIDIPortConnectSource(g_in_port, src, (void *)(intptr_t)i);
    }
}

void midi_in_global_shutdown(void) {
    if (g_in_port)   { MIDIPortDispose(g_in_port);    g_in_port   = 0; }
    if (g_in_client) { MIDIClientDispose(g_in_client); g_in_client = 0; }
}

int midi_in_port_count(void) { return g_port_count; }

const char *midi_in_port_name(int idx) {
    if (idx < 0 || idx >= g_port_count) return "?";
    return g_port_names[idx];
}

// ============================================================
// Linux — ALSA sequencer
// ============================================================
#elif defined(__linux__) && defined(HAVE_ALSA)

#include <alsa/asoundlib.h>
#include <pthread.h>

#define MAX_IN_PORTS 64

typedef struct { int client; int port; char name[256]; } AlsaInPort;
static AlsaInPort g_in_ports[MAX_IN_PORTS];
static int        g_in_nports = 0;

static snd_seq_t *g_in_seq    = NULL;
static int        g_in_myport = -1;
static pthread_t  g_in_thread;
static bool       g_in_running = false;

static void refresh_in_ports(void) {
    g_in_nports = 0;
    if (!g_in_seq) return;
    snd_seq_client_info_t *ci; snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(g_in_seq, ci) >= 0 && g_in_nports < MAX_IN_PORTS) {
        int c = snd_seq_client_info_get_client(ci);
        if (c == 0 || c == snd_seq_client_id(g_in_seq)) continue;
        snd_seq_port_info_set_client(pi, c);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(g_in_seq, pi) >= 0 && g_in_nports < MAX_IN_PORTS) {
            unsigned int caps = snd_seq_port_info_get_capability(pi);
            if (!(caps & SND_SEQ_PORT_CAP_READ)) continue;
            if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
            g_in_ports[g_in_nports].client = c;
            g_in_ports[g_in_nports].port   = snd_seq_port_info_get_port(pi);
            snprintf(g_in_ports[g_in_nports].name, 256, "%s:%s",
                snd_seq_client_info_get_name(ci),
                snd_seq_port_info_get_name(pi));
            snd_seq_connect_from(g_in_seq, g_in_myport, c, g_in_ports[g_in_nports].port);
            g_in_nports++;
        }
    }
}

static int find_port_idx(int client, int port) {
    for (int i = 0; i < g_in_nports; i++)
        if (g_in_ports[i].client == client && g_in_ports[i].port == port)
            return i;
    return 0;
}

static void *alsa_in_thread(void *arg) {
    (void)arg;
    while (g_in_running) {
        snd_seq_event_t *ev = NULL;
        int r = snd_seq_event_input(g_in_seq, &ev);
        if (r < 0 || !ev) continue;
        int idx = find_port_idx(ev->source.client, ev->source.port);
        if (ev->type == SND_SEQ_EVENT_NOTEON)
            push_msg((uint8_t)(0x90 | ev->data.note.channel), ev->data.note.note, ev->data.note.velocity, idx);
        else if (ev->type == SND_SEQ_EVENT_NOTEOFF)
            push_msg((uint8_t)(0x80 | ev->data.note.channel), ev->data.note.note, 0, idx);
        else if (ev->type == SND_SEQ_EVENT_CONTROLLER)
            push_msg((uint8_t)(0xB0 | ev->data.control.channel), (uint8_t)ev->data.control.param, (uint8_t)ev->data.control.value, idx);
    }
    return NULL;
}

void midi_in_global_init(void) {
    if (snd_seq_open(&g_in_seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) { g_in_seq = NULL; return; }
    snd_seq_set_client_name(g_in_seq, "raypoketrack_in");
    g_in_myport = snd_seq_create_simple_port(g_in_seq, "input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    refresh_in_ports();
    g_in_running = true;
    pthread_create(&g_in_thread, NULL, alsa_in_thread, NULL);
}

void midi_in_global_shutdown(void) {
    g_in_running = false;
    if (g_in_seq) { snd_seq_close(g_in_seq); g_in_seq = NULL; }
    pthread_join(g_in_thread, NULL);
}

int         midi_in_port_count(void)      { return g_in_nports; }
const char *midi_in_port_name(int idx)    { return (idx >= 0 && idx < g_in_nports) ? g_in_ports[idx].name : "?"; }

// ============================================================
// Windows — WinMM
// ============================================================
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#define MAX_IN_DEVS 32
static HMIDIIN g_in_handles[MAX_IN_DEVS];
static int     g_in_count = 0;

static void CALLBACK winmm_in_cb(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                  DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hMidiIn; (void)dwParam2;
    if (wMsg != MIM_DATA) return;
    int idx = (int)dwInstance;
    uint8_t status = (uint8_t)(dwParam1 & 0xFF);
    uint8_t d1     = (uint8_t)((dwParam1 >> 8) & 0xFF);
    uint8_t d2     = (uint8_t)((dwParam1 >> 16) & 0xFF);
    push_msg(status, d1, d2, idx);
}

void midi_in_global_init(void) {
    g_in_count = (int)midiInGetNumDevs();
    if (g_in_count > MAX_IN_DEVS) g_in_count = MAX_IN_DEVS;
    for (int i = 0; i < g_in_count; i++) {
        if (midiInOpen(&g_in_handles[i], (UINT)i, (DWORD_PTR)winmm_in_cb, (DWORD_PTR)i, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
            g_in_handles[i] = NULL;
        else
            midiInStart(g_in_handles[i]);
    }
}

void midi_in_global_shutdown(void) {
    for (int i = 0; i < g_in_count; i++) {
        if (g_in_handles[i]) { midiInStop(g_in_handles[i]); midiInClose(g_in_handles[i]); }
    }
}

int midi_in_port_count(void) { return g_in_count; }

const char *midi_in_port_name(int idx) {
    static char buf[MAXPNAMELEN + 4];
    MIDIINCAPSA caps;
    if (idx >= 0 && idx < g_in_count && midiInGetDevCapsA((UINT)idx, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        strncpy(buf, caps.szPname, sizeof(buf) - 1);
    else
        snprintf(buf, sizeof(buf), "Device %d", idx);
    return buf;
}

// ============================================================
// Web — Web MIDI API (JS calls into C via ccall)
// ============================================================
#elif defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <string.h>

#define MAX_IN_PORTS 32
static char g_web_names[MAX_IN_PORTS][256];
static int  g_web_count = 0;

void midi_in_global_init(void)     {}
void midi_in_global_shutdown(void) {}
int  midi_in_port_count(void)      { return g_web_count; }

const char *midi_in_port_name(int idx) {
    if (idx < 0 || idx >= g_web_count) return "?";
    return g_web_names[idx];
}

EMSCRIPTEN_KEEPALIVE void midi_in_web_set_port_count(int n) {
    g_web_count = (n < MAX_IN_PORTS) ? n : MAX_IN_PORTS;
}
EMSCRIPTEN_KEEPALIVE void midi_in_web_set_port_name(int idx, const char *name) {
    if (idx < 0 || idx >= MAX_IN_PORTS || !name) return;
    strncpy(g_web_names[idx], name, 255);
    g_web_names[idx][255] = '\0';
}
EMSCRIPTEN_KEEPALIVE void midi_in_web_push(int status, int d1, int d2, int port_idx) {
    push_msg((uint8_t)status, (uint8_t)d1, (uint8_t)d2, port_idx);
}

// Lazy, idempotent: calls window._midiWebRequestAccess() defined in index.html.
// That function requests browser MIDI access and populates C port tables via ccall.
void midi_web_request_access(void) {
    emscripten_run_script(
        "typeof window._midiWebRequestAccess==='function'&&window._midiWebRequestAccess();"
    );
}

#endif
