/* midi — ALSA sequencer (Linux) + CoreMIDI (macOS / Apple Silicon).
 *
 *   import midi
 *   midi.open()
 *   midi.connect("Scarlett")   # name substring or "client:port" / "dest:N"
 *   midi.note_on(60, 100)
 *   ev : midi.poll()
 *
 * System MIDI headers must come before shakti.h (a.h macros break ALSA/CoreMIDI).
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(__has_include)
#  if __has_include(<alsa/asoundlib.h>)
#    define MIDI_HAVE_ALSA 1
#    include <alsa/asoundlib.h>
#  endif
#endif

#if defined(__APPLE__)
#  define MIDI_HAVE_COREMIDI 1
#  include <CoreFoundation/CoreFoundation.h>
#  include <CoreMIDI/CoreMIDI.h>
#endif

#include "midi.h"
enum {
    MIDI_EV_NOTE_OFF = 0,
    MIDI_EV_NOTE_ON = 1,
    MIDI_EV_CC = 2,
    MIDI_EV_PROGRAM = 3,
    MIDI_EV_PITCH_BEND = 4,
    MIDI_EV_RAW = 5
};

typedef struct {
    int type;
    int ch;
    int note;
    int vel;
    int ctrl;
    int val;
    int status;
    int d1;
    int d2;
} MidiEv;

#define MIDI_Q 256

static MidiEv g_q[MIDI_Q];
static int g_q_head, g_q_tail;
static pthread_mutex_t g_q_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_open;

#if defined(MIDI_HAVE_ALSA)
static snd_seq_t *g_seq;
static int g_out_port = -1;
static int g_in_port = -1;
static int g_dst_client = -1;
static int g_dst_port = -1;
static int g_src_client = -1;
static int g_src_port = -1;
#endif

#if defined(MIDI_HAVE_COREMIDI)
static MIDIClientRef g_client;
static MIDIPortRef g_out_port_ref;
static MIDIPortRef g_in_port_ref;
static MIDIEndpointRef g_dest;
static MIDIEndpointRef g_source;
#endif

static void midi_q_push(const MidiEv *ev) {
    pthread_mutex_lock(&g_q_mu);
    int next = (g_q_tail + 1) % MIDI_Q;
    if (next != g_q_head) {
        g_q[g_q_tail] = *ev;
        g_q_tail = next;
    }
    pthread_mutex_unlock(&g_q_mu);
}

static int midi_q_pop(MidiEv *ev) {
    pthread_mutex_lock(&g_q_mu);
    if (g_q_head == g_q_tail) {
        pthread_mutex_unlock(&g_q_mu);
        return 0;
    }
    *ev = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % MIDI_Q;
    pthread_mutex_unlock(&g_q_mu);
    return 1;
}

static void midi_q_clear(void) {
    pthread_mutex_lock(&g_q_mu);
    g_q_head = g_q_tail = 0;
    pthread_mutex_unlock(&g_q_mu);
}

static V *midi_err(const char *msg) {
    char buf[256];
    snprintf(buf, sizeof buf, "midi: %s", msg);
    return v_err(buf);
}

static void dput(V *d, const char *k, V *v) {
    v_dict_set(d, k, v);
    v_free(v);
}

static V *midi_ev_dict(const MidiEv *ev) {
    V *d = v_dict_empty();
    const char *type = "raw";
    if (ev->type == MIDI_EV_NOTE_ON) type = "note_on";
    else if (ev->type == MIDI_EV_NOTE_OFF) type = "note_off";
    else if (ev->type == MIDI_EV_CC) type = "cc";
    else if (ev->type == MIDI_EV_PROGRAM) type = "program";
    else if (ev->type == MIDI_EV_PITCH_BEND) type = "pitch_bend";
    dput(d, "type", v_str(type));
    dput(d, "ch", v_int(ev->ch));
    dput(d, "status", v_int(ev->status));
    dput(d, "d1", v_int(ev->d1));
    dput(d, "d2", v_int(ev->d2));
    if (ev->type == MIDI_EV_NOTE_ON || ev->type == MIDI_EV_NOTE_OFF) {
        dput(d, "note", v_int(ev->note));
        dput(d, "vel", v_int(ev->vel));
    } else if (ev->type == MIDI_EV_CC) {
        dput(d, "cc", v_int(ev->ctrl));
        dput(d, "val", v_int(ev->val));
    } else if (ev->type == MIDI_EV_PROGRAM) {
        dput(d, "program", v_int(ev->val));
    } else if (ev->type == MIDI_EV_PITCH_BEND) {
        dput(d, "val", v_int(ev->val));
        dput(d, "bend", v_float(((double)ev->val - 8192.0) / 8192.0));
    }
    return d;
}

static void midi_decode_bytes(const unsigned char *data, int len) {
    if (len < 1) return;
    MidiEv ev;
    memset(&ev, 0, sizeof ev);
    ev.status = data[0];
    ev.d1 = len > 1 ? data[1] : 0;
    ev.d2 = len > 2 ? data[2] : 0;
    ev.ch = ev.status & 0x0f;
    int cmd = ev.status & 0xf0;
    if (cmd == 0x90) {
        ev.type = ev.d2 ? MIDI_EV_NOTE_ON : MIDI_EV_NOTE_OFF;
        ev.note = ev.d1;
        ev.vel = ev.d2;
    } else if (cmd == 0x80) {
        ev.type = MIDI_EV_NOTE_OFF;
        ev.note = ev.d1;
        ev.vel = ev.d2;
    } else if (cmd == 0xb0) {
        ev.type = MIDI_EV_CC;
        ev.ctrl = ev.d1;
        ev.val = ev.d2;
    } else if (cmd == 0xc0) {
        ev.type = MIDI_EV_PROGRAM;
        ev.val = ev.d1;
    } else if (cmd == 0xe0) {
        ev.type = MIDI_EV_PITCH_BEND;
        ev.val = ev.d1 | (ev.d2 << 7);
    } else {
        ev.type = MIDI_EV_RAW;
    }
    midi_q_push(&ev);
}

/* ---------- ALSA ---------- */
#if defined(MIDI_HAVE_ALSA)

static int midi_alsa_open(char *err, size_t err_cap) {
    if (g_seq) return 0;
    int rc = snd_seq_open(&g_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (rc < 0) {
        snprintf(err, err_cap, "alsa open: %s", snd_strerror(rc));
        g_seq = NULL;
        return -1;
    }
    snd_seq_set_client_name(g_seq, "Shakti");
    g_out_port = snd_seq_create_simple_port(
        g_seq, "out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (g_out_port < 0) {
        snprintf(err, err_cap, "alsa out port: %s", snd_strerror(g_out_port));
        snd_seq_close(g_seq);
        g_seq = NULL;
        return -1;
    }
    g_in_port = snd_seq_create_simple_port(
        g_seq, "in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (g_in_port < 0) {
        snprintf(err, err_cap, "alsa in port: %s", snd_strerror(g_in_port));
        snd_seq_delete_simple_port(g_seq, g_out_port);
        snd_seq_close(g_seq);
        g_seq = NULL;
        g_out_port = -1;
        return -1;
    }
    g_dst_client = g_dst_port = -1;
    g_src_client = g_src_port = -1;
    return 0;
}

static void midi_alsa_close(void) {
    if (!g_seq) return;
    if (g_dst_client >= 0)
        snd_seq_disconnect_to(g_seq, g_out_port, g_dst_client, g_dst_port);
    if (g_src_client >= 0)
        snd_seq_disconnect_from(g_seq, g_in_port, g_src_client, g_src_port);
    if (g_in_port >= 0) snd_seq_delete_simple_port(g_seq, g_in_port);
    if (g_out_port >= 0) snd_seq_delete_simple_port(g_seq, g_out_port);
    snd_seq_close(g_seq);
    g_seq = NULL;
    g_out_port = g_in_port = -1;
    g_dst_client = g_dst_port = -1;
    g_src_client = g_src_port = -1;
}

static V *midi_alsa_list(void) {
    V *out = v_list(0);
    if (!g_seq) return out;
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(g_seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == snd_seq_client_id(g_seq)) continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(g_seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            int port = snd_seq_port_info_get_port(pinfo);
            char id[32];
            snprintf(id, sizeof id, "%d:%d", client, port);
            V *d = v_dict_empty();
            dput(d, "id", v_str(id));
            dput(d, "client", v_int(client));
            dput(d, "port", v_int(port));
            dput(d, "name", v_str(snd_seq_port_info_get_name(pinfo)));
            dput(d, "client_name", v_str(snd_seq_client_info_get_name(cinfo)));
            dput(d, "can_out", v_int((caps & SND_SEQ_PORT_CAP_WRITE) ? 1 : 0));
            dput(d, "can_in", v_int((caps & SND_SEQ_PORT_CAP_READ) ? 1 : 0));
            v_list_append(out, d);
            v_free(d);
        }
    }
    return out;
}

static int midi_alsa_parse_id(const char *s, int *client, int *port) {
    char *end = NULL;
    long c = strtol(s, &end, 10);
    if (!end || *end != ':') return -1;
    long p = strtol(end + 1, &end, 10);
    if (end == s || c < 0 || p < 0) return -1;
    *client = (int)c;
    *port = (int)p;
    return 0;
}

static int midi_alsa_find_name(const char *needle, int want_write, int *client, int *port) {
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(g_seq, cinfo) >= 0) {
        int cl = snd_seq_client_info_get_client(cinfo);
        if (cl == snd_seq_client_id(g_seq)) continue;
        snd_seq_port_info_set_client(pinfo, cl);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(g_seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if (want_write && !(caps & SND_SEQ_PORT_CAP_WRITE)) continue;
            if (!want_write && !(caps & SND_SEQ_PORT_CAP_READ)) continue;
            const char *pn = snd_seq_port_info_get_name(pinfo);
            const char *cn = snd_seq_client_info_get_name(cinfo);
            if ((pn && strstr(pn, needle)) || (cn && strstr(cn, needle))) {
                *client = cl;
                *port = snd_seq_port_info_get_port(pinfo);
                return 0;
            }
        }
    }
    return -1;
}

static int midi_alsa_connect(const char *target, char *err, size_t err_cap) {
    int client = -1, port = -1;
    if (midi_alsa_parse_id(target, &client, &port) != 0) {
        if (midi_alsa_find_name(target, 1, &client, &port) != 0) {
            snprintf(err, err_cap, "no writable port matching '%s'", target);
            return -1;
        }
    }
    if (g_dst_client >= 0)
        snd_seq_disconnect_to(g_seq, g_out_port, g_dst_client, g_dst_port);
    int rc = snd_seq_connect_to(g_seq, g_out_port, client, port);
    if (rc < 0) {
        snprintf(err, err_cap, "connect_to %d:%d: %s", client, port, snd_strerror(rc));
        return -1;
    }
    g_dst_client = client;
    g_dst_port = port;

    /* Best-effort: also subscribe readable side with same id/name for input. */
    int sc = -1, sp = -1;
    if (midi_alsa_parse_id(target, &sc, &sp) == 0 ||
        midi_alsa_find_name(target, 0, &sc, &sp) == 0) {
        if (g_src_client >= 0)
            snd_seq_disconnect_from(g_seq, g_in_port, g_src_client, g_src_port);
        if (snd_seq_connect_from(g_seq, g_in_port, sc, sp) == 0) {
            g_src_client = sc;
            g_src_port = sp;
        }
    }
    return 0;
}

static void midi_alsa_disconnect(void) {
    if (!g_seq) return;
    if (g_dst_client >= 0) {
        snd_seq_disconnect_to(g_seq, g_out_port, g_dst_client, g_dst_port);
        g_dst_client = g_dst_port = -1;
    }
    if (g_src_client >= 0) {
        snd_seq_disconnect_from(g_seq, g_in_port, g_src_client, g_src_port);
        g_src_client = g_src_port = -1;
    }
}

static int midi_alsa_send3(unsigned char status, unsigned char d1, unsigned char d2) {
    if (!g_seq) return -1;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, g_out_port);
    if (g_dst_client >= 0)
        snd_seq_ev_set_dest(&ev, g_dst_client, g_dst_port);
    else
        snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    int cmd = status & 0xf0;
    int ch = status & 0x0f;
    if (cmd == 0x90) snd_seq_ev_set_noteon(&ev, ch, d1, d2);
    else if (cmd == 0x80) snd_seq_ev_set_noteoff(&ev, ch, d1, d2);
    else if (cmd == 0xb0) snd_seq_ev_set_controller(&ev, ch, d1, d2);
    else if (cmd == 0xc0) snd_seq_ev_set_pgmchange(&ev, ch, d1);
    else {
        /* fallback: raw bytes via sysex-style not ideal; use noteon path only for channel msgs */
        return -1;
    }
    return snd_seq_event_output_direct(g_seq, &ev) < 0 ? -1 : 0;
}

static void midi_alsa_poll_into_q(void) {
    if (!g_seq) return;
    snd_seq_event_t *ev;
    while (snd_seq_event_input(g_seq, &ev) >= 0) {
        unsigned char status = 0, d1 = 0, d2 = 0;
        switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
            status = (unsigned char)(0x90 | (ev->data.note.channel & 0x0f));
            d1 = (unsigned char)ev->data.note.note;
            d2 = (unsigned char)ev->data.note.velocity;
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            status = (unsigned char)(0x80 | (ev->data.note.channel & 0x0f));
            d1 = (unsigned char)ev->data.note.note;
            d2 = (unsigned char)ev->data.note.velocity;
            break;
        case SND_SEQ_EVENT_CONTROLLER:
            status = (unsigned char)(0xb0 | (ev->data.control.channel & 0x0f));
            d1 = (unsigned char)ev->data.control.param;
            d2 = (unsigned char)ev->data.control.value;
            break;
        case SND_SEQ_EVENT_PGMCHANGE:
            status = (unsigned char)(0xc0 | (ev->data.control.channel & 0x0f));
            d1 = (unsigned char)ev->data.control.value;
            break;
        case SND_SEQ_EVENT_PITCHBEND: {
            int pb = ev->data.control.value + 8192;
            if (pb < 0) pb = 0;
            if (pb > 16383) pb = 16383;
            status = (unsigned char)(0xe0 | (ev->data.control.channel & 0x0f));
            d1 = (unsigned char)(pb & 0x7f);
            d2 = (unsigned char)((pb >> 7) & 0x7f);
            break;
        }
        default:
            snd_seq_free_event(ev);
            continue;
        }
        unsigned char bytes[3] = {status, d1, d2};
        midi_decode_bytes(bytes, (status & 0xf0) == 0xc0 ? 2 : 3);
        snd_seq_free_event(ev);
    }
}

#endif /* MIDI_HAVE_ALSA */

/* ---------- CoreMIDI (macOS / Apple Silicon incl. M-series) ---------- */
#if defined(MIDI_HAVE_COREMIDI)

static char *cf_to_c(CFStringRef s) {
    if (!s) return strdup("");
    CFIndex len = CFStringGetLength(s);
    CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    char *buf = (char *)malloc((size_t)max);
    if (!buf) return strdup("");
    if (!CFStringGetCString(s, buf, max, kCFStringEncodingUTF8)) {
        free(buf);
        return strdup("");
    }
    return buf;
}

static void midi_cm_read(const MIDIPacketList *pktlist, void *refCon, void *connSrc) {
    (void)refCon;
    (void)connSrc;
    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        const Byte *data = packet->data;
        UInt16 len = packet->length;
        UInt16 off = 0;
        while (off < len) {
            unsigned char status = data[off];
            if (status < 0x80) { off++; continue; }
            int need = 3;
            int cmd = status & 0xf0;
            if (cmd == 0xc0 || cmd == 0xd0) need = 2;
            else if (status >= 0xf8) need = 1;
            else if (status == 0xf1 || status == 0xf3) need = 2;
            else if (status == 0xf2) need = 3;
            if (off + need > len) break;
            midi_decode_bytes(data + off, need);
            off = (UInt16)(off + need);
        }
        packet = MIDIPacketNext(packet);
    }
}

static int midi_cm_open(char *err, size_t err_cap) {
    if (g_client) return 0;
    OSStatus ostatus = MIDIClientCreate(CFSTR("Shakti"), NULL, NULL, &g_client);
    if (ostatus != noErr) {
        snprintf(err, err_cap, "MIDIClientCreate %d", (int)ostatus);
        g_client = 0;
        return -1;
    }
    ostatus = MIDIOutputPortCreate(g_client, CFSTR("out"), &g_out_port_ref);
    if (ostatus != noErr) {
        snprintf(err, err_cap, "MIDIOutputPortCreate %d", (int)ostatus);
        MIDIClientDispose(g_client);
        g_client = 0;
        return -1;
    }
    ostatus = MIDIInputPortCreate(g_client, CFSTR("in"), midi_cm_read, NULL, &g_in_port_ref);
    if (ostatus != noErr) {
        snprintf(err, err_cap, "MIDIInputPortCreate %d", (int)ostatus);
        MIDIPortDispose(g_out_port_ref);
        MIDIClientDispose(g_client);
        g_out_port_ref = 0;
        g_client = 0;
        return -1;
    }
    g_dest = 0;
    g_source = 0;
    return 0;
}

static void midi_cm_close(void) {
    if (g_source && g_in_port_ref) MIDIPortDisconnectSource(g_in_port_ref, g_source);
    if (g_out_port_ref) MIDIPortDispose(g_out_port_ref);
    if (g_in_port_ref) MIDIPortDispose(g_in_port_ref);
    if (g_client) MIDIClientDispose(g_client);
    g_out_port_ref = g_in_port_ref = 0;
    g_client = 0;
    g_dest = g_source = 0;
}

static V *midi_cm_endpoint_dict(MIDIEndpointRef ep, const char *kind, ItemCount idx) {
    CFStringRef name = NULL;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
    char *cname = cf_to_c(name);
    if (name) CFRelease(name);
    char id[32];
    snprintf(id, sizeof id, "%s:%lu", kind, (unsigned long)idx);
    V *d = v_dict_empty();
    dput(d, "id", v_str(id));
    dput(d, "name", v_str(cname));
    dput(d, "kind", v_str(kind));
    dput(d, "index", v_int((int64_t)idx));
    dput(d, "can_out", v_int(strcmp(kind, "dest") == 0 ? 1 : 0));
    dput(d, "can_in", v_int(strcmp(kind, "src") == 0 ? 1 : 0));
    free(cname);
    return d;
}

static V *midi_cm_list(void) {
    V *out = v_list(0);
    ItemCount nd = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < nd; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        if (!ep) continue;
        V *d = midi_cm_endpoint_dict(ep, "dest", i);
        v_list_append(out, d);
        v_free(d);
    }
    ItemCount ns = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < ns; i++) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        if (!ep) continue;
        V *d = midi_cm_endpoint_dict(ep, "src", i);
        v_list_append(out, d);
        v_free(d);
    }
    return out;
}

static int midi_cm_parse_id(const char *s, const char *kind, ItemCount *idx) {
    size_t klen = strlen(kind);
    if (strncmp(s, kind, klen) != 0 || s[klen] != ':') return -1;
    char *end = NULL;
    long v = strtol(s + klen + 1, &end, 10);
    if (!end || end == s + klen + 1 || v < 0) return -1;
    *idx = (ItemCount)v;
    return 0;
}

static MIDIEndpointRef midi_cm_find_dest(const char *target) {
    ItemCount idx;
    if (midi_cm_parse_id(target, "dest", &idx) == 0)
        return MIDIGetDestination(idx);
    ItemCount nd = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < nd; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        CFStringRef name = NULL;
        MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
        char *cname = cf_to_c(name);
        if (name) CFRelease(name);
        int hit = cname && strstr(cname, target) != NULL;
        free(cname);
        if (hit) return ep;
    }
    return 0;
}

static MIDIEndpointRef midi_cm_find_src(const char *target) {
    ItemCount idx;
    if (midi_cm_parse_id(target, "src", &idx) == 0)
        return MIDIGetSource(idx);
    ItemCount ns = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < ns; i++) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        CFStringRef name = NULL;
        MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
        char *cname = cf_to_c(name);
        if (name) CFRelease(name);
        int hit = cname && strstr(cname, target) != NULL;
        free(cname);
        if (hit) return ep;
    }
    return 0;
}

static int midi_cm_connect(const char *target, char *err, size_t err_cap) {
    MIDIEndpointRef dest = midi_cm_find_dest(target);
    MIDIEndpointRef src = midi_cm_find_src(target);
    if (!dest && !src) {
        snprintf(err, err_cap, "no endpoint matching '%s'", target);
        return -1;
    }
    if (dest) g_dest = dest;
    if (g_source && g_in_port_ref)
        MIDIPortDisconnectSource(g_in_port_ref, g_source);
    g_source = 0;
    if (src && g_in_port_ref) {
        OSStatus ostatus = MIDIPortConnectSource(g_in_port_ref, src, NULL);
        if (ostatus == noErr) g_source = src;
    }
    return 0;
}

static void midi_cm_disconnect(void) {
    if (g_source && g_in_port_ref)
        MIDIPortDisconnectSource(g_in_port_ref, g_source);
    g_source = 0;
    g_dest = 0;
}

static int midi_cm_send3(unsigned char status, unsigned char d1, unsigned char d2) {
    if (!g_out_port_ref || !g_dest) return -1;
    Byte buf[64];
    MIDIPacketList *pktlist = (MIDIPacketList *)buf;
    MIDIPacket *packet = MIDIPacketListInit(pktlist);
    Byte data[3] = {status, d1, d2};
    int n = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 2 : 3;
    packet = MIDIPacketListAdd(pktlist, sizeof buf, packet, 0, (ByteCount)n, data);
    if (!packet) return -1;
    return MIDISend(g_out_port_ref, g_dest, pktlist) == noErr ? 0 : -1;
}

#endif /* MIDI_HAVE_COREMIDI */

/* ---------- shared dispatch ---------- */

static const char *midi_backend_name(void) {
#if defined(MIDI_HAVE_ALSA)
    return "alsa";
#elif defined(MIDI_HAVE_COREMIDI)
    return "coremidi";
#else
    return "none";
#endif
}

static int midi_platform_open(char *err, size_t err_cap) {
#if defined(MIDI_HAVE_ALSA)
    return midi_alsa_open(err, err_cap);
#elif defined(MIDI_HAVE_COREMIDI)
    return midi_cm_open(err, err_cap);
#else
    snprintf(err, err_cap, "no MIDI backend (need ALSA or CoreMIDI)");
    return -1;
#endif
}

static void midi_platform_close(void) {
#if defined(MIDI_HAVE_ALSA)
    midi_alsa_close();
#elif defined(MIDI_HAVE_COREMIDI)
    midi_cm_close();
#endif
}

static V *midi_platform_list(void) {
#if defined(MIDI_HAVE_ALSA)
    return midi_alsa_list();
#elif defined(MIDI_HAVE_COREMIDI)
    return midi_cm_list();
#else
    return v_list(0);
#endif
}

static int midi_platform_connect(const char *target, char *err, size_t err_cap) {
#if defined(MIDI_HAVE_ALSA)
    return midi_alsa_connect(target, err, err_cap);
#elif defined(MIDI_HAVE_COREMIDI)
    return midi_cm_connect(target, err, err_cap);
#else
    snprintf(err, err_cap, "no MIDI backend");
    return -1;
#endif
}

static void midi_platform_disconnect(void) {
#if defined(MIDI_HAVE_ALSA)
    midi_alsa_disconnect();
#elif defined(MIDI_HAVE_COREMIDI)
    midi_cm_disconnect();
#endif
}

static int midi_platform_send3(unsigned char status, unsigned char d1, unsigned char d2) {
#if defined(MIDI_HAVE_ALSA)
    return midi_alsa_send3(status, d1, d2);
#elif defined(MIDI_HAVE_COREMIDI)
    return midi_cm_send3(status, d1, d2);
#else
    (void)status; (void)d1; (void)d2;
    return -1;
#endif
}

static void midi_platform_poll(void) {
#if defined(MIDI_HAVE_ALSA)
    midi_alsa_poll_into_q();
#endif
    /* CoreMIDI fills queue from callback thread */
}

static int arg_int(V *v, int *out) {
    if (v->t == T_INT) { *out = (int)v->j; return 0; }
    if (v->t == T_FLOAT) { *out = (int)v->f; return 0; }
    return -1;
}

V *bi_midi_open(V **a, int n) {
    (void)a; (void)n;
    if (g_open) return v_int(1);
    char err[192];
    if (midi_platform_open(err, sizeof err) != 0) return midi_err(err);
    midi_q_clear();
    g_open = 1;
    return v_int(1);
}

V *bi_midi_close(V **a, int n) {
    (void)a; (void)n;
    if (!g_open) return v_nil();
    midi_platform_close();
    midi_q_clear();
    g_open = 0;
    return v_nil();
}

V *bi_midi_alive(V **a, int n) {
    (void)a; (void)n;
    return v_int(g_open ? 1 : 0);
}

V *bi_midi_backend(V **a, int n) {
    (void)a; (void)n;
    return v_str(midi_backend_name());
}

V *bi_midi_list(V **a, int n) {
    (void)a; (void)n;
    P(!g_open, midi_err("not open (call midi.open())"))
    return midi_platform_list();
}

V *bi_midi_connect(V **a, int n) {
    P(!g_open, midi_err("not open (call midi.open())"))
    P(n < 1 || a[0]->t != T_STR, midi_err("connect(id_or_name)"))
    char err[192];
    if (midi_platform_connect(a[0]->s, err, sizeof err) != 0) return midi_err(err);
    return v_int(1);
}

V *bi_midi_disconnect(V **a, int n) {
    (void)a; (void)n;
    P(!g_open, midi_err("not open"))
    midi_platform_disconnect();
    return v_nil();
}

V *bi_midi_note_on(V **a, int n) {
    P(!g_open, midi_err("not open"))
    int note = 0, vel = 100, ch = 0;
    P(n < 1 || arg_int(a[0], &note) != 0, midi_err("note_on(note[, vel[, ch]])"))
    if (n >= 2) P(arg_int(a[1], &vel) != 0, midi_err("note_on: bad vel"))
    if (n >= 3) P(arg_int(a[2], &ch) != 0, midi_err("note_on: bad ch"))
    if (note < 0 || note > 127 || vel < 0 || vel > 127 || ch < 0 || ch > 15)
        return midi_err("note_on: note/vel/ch out of range");
    if (midi_platform_send3((unsigned char)(0x90 | ch), (unsigned char)note, (unsigned char)vel) != 0)
        return midi_err("note_on send failed (connect a destination?)");
    return v_nil();
}

V *bi_midi_note_off(V **a, int n) {
    P(!g_open, midi_err("not open"))
    int note = 0, vel = 0, ch = 0;
    P(n < 1 || arg_int(a[0], &note) != 0, midi_err("note_off(note[, vel[, ch]])"))
    if (n >= 2) P(arg_int(a[1], &vel) != 0, midi_err("note_off: bad vel"))
    if (n >= 3) P(arg_int(a[2], &ch) != 0, midi_err("note_off: bad ch"))
    if (note < 0 || note > 127 || vel < 0 || vel > 127 || ch < 0 || ch > 15)
        return midi_err("note_off: note/vel/ch out of range");
    if (midi_platform_send3((unsigned char)(0x80 | ch), (unsigned char)note, (unsigned char)vel) != 0)
        return midi_err("note_off send failed");
    return v_nil();
}

V *bi_midi_cc(V **a, int n) {
    P(!g_open, midi_err("not open"))
    int ctrl = 0, val = 0, ch = 0;
    P(n < 2 || arg_int(a[0], &ctrl) != 0 || arg_int(a[1], &val) != 0, midi_err("cc(controller, val[, ch])"))
    if (n >= 3) P(arg_int(a[2], &ch) != 0, midi_err("cc: bad ch"))
    if (ctrl < 0 || ctrl > 127 || val < 0 || val > 127 || ch < 0 || ch > 15)
        return midi_err("cc: out of range");
    if (midi_platform_send3((unsigned char)(0xb0 | ch), (unsigned char)ctrl, (unsigned char)val) != 0)
        return midi_err("cc send failed");
    return v_nil();
}

V *bi_midi_program(V **a, int n) {
    P(!g_open, midi_err("not open"))
    int prog = 0, ch = 0;
    P(n < 1 || arg_int(a[0], &prog) != 0, midi_err("program(prog[, ch])"))
    if (n >= 2) P(arg_int(a[1], &ch) != 0, midi_err("program: bad ch"))
    if (prog < 0 || prog > 127 || ch < 0 || ch > 15)
        return midi_err("program: out of range");
    if (midi_platform_send3((unsigned char)(0xc0 | ch), (unsigned char)prog, 0) != 0)
        return midi_err("program send failed");
    return v_nil();
}

V *bi_midi_raw(V **a, int n) {
    P(!g_open, midi_err("not open"))
    int status = 0, d1 = 0, d2 = 0;
    P(n < 1 || arg_int(a[0], &status) != 0, midi_err("raw(status[, d1[, d2]])"))
    if (n >= 2) P(arg_int(a[1], &d1) != 0, midi_err("raw: bad d1"))
    if (n >= 3) P(arg_int(a[2], &d2) != 0, midi_err("raw: bad d2"))
    if (status < 0 || status > 255 || d1 < 0 || d1 > 127 || d2 < 0 || d2 > 127)
        return midi_err("raw: out of range");
    if (midi_platform_send3((unsigned char)status, (unsigned char)d1, (unsigned char)d2) != 0)
        return midi_err("raw send failed");
    return v_nil();
}

V *bi_midi_poll(V **a, int n) {
    (void)a; (void)n;
    P(!g_open, midi_err("not open"))
    midi_platform_poll();
    MidiEv ev;
    if (!midi_q_pop(&ev)) return v_nil();
    return midi_ev_dict(&ev);
}
