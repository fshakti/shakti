/* ws — RFC6455 WebSocket client/server (ws:// and wss://). */
#include "ws.h"
#include "rest.h"
#include "tls.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

#define WS_MAX_HANDLES 128
#define WS_MAX_PAYLOAD (1024 * 1024)
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct {
    int in_use;
    int closed;
    int fd;
    void *ssl; /* SSL* or NULL */
    int is_client;
    unsigned char *rx;
    size_t rx_len;
    size_t rx_cap;
} WsHandle;

#ifndef SHAKTI_WASM
static WsHandle g_ws_handles[WS_MAX_HANDLES];
static int g_ws_sigpipe_ignored;

static void ws_ignore_sigpipe_once(void) {
    if (g_ws_sigpipe_ignored) return;
    g_ws_sigpipe_ignored = 1;
    signal(SIGPIPE, SIG_IGN);
}
#endif

static int ws_fill_random(unsigned char *buf, size_t n) {
#if defined(__linux__)
    ssize_t r = getrandom(buf, n, 0);
    if (r == (ssize_t)n) return 0;
#endif
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

static void ws_b64_encode(const unsigned char *src, size_t nsrc, char *out, size_t out_cap) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < nsrc; i += 3) {
        unsigned v = (unsigned)src[i] << 16;
        int rem = (int)(nsrc - i);
        if (rem > 1) v |= (unsigned)src[i + 1] << 8;
        if (rem > 2) v |= (unsigned)src[i + 2];
        if (o + 4 >= out_cap) {
            out[0] = 0;
            return;
        }
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = rem > 1 ? tab[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? tab[v & 63] : '=';
    }
    out[o] = 0;
}

static int ws_accept_key(const char *client_key, char *out, size_t out_cap) {
    char buf[128];
    int n = snprintf(buf, sizeof buf, "%s%s", client_key, WS_GUID);
    if (n < 0 || (size_t)n >= sizeof buf) return -1;
    unsigned char dig[20];
    if (tls_sha1(buf, (size_t)n, dig) != 0) return -1;
    ws_b64_encode(dig, 20, out, out_cap);
    return out[0] ? 0 : -1;
}

typedef struct {
    int fd;
    void *ssl;
    WsHandle *h; /* non-NULL: stash incomplete reads on handle */
} WsIo;

#define WS_RX_MAX (WS_MAX_PAYLOAD + 14)

static int ws_rx_reserve(WsHandle *h, size_t need) {
    if (!h) return -1;
    if (need > WS_RX_MAX) return -1;
    if (h->rx_cap >= need) return 0;
    size_t cap = h->rx_cap ? h->rx_cap : 256;
    while (cap < need) cap *= 2;
    if (cap > WS_RX_MAX) cap = WS_RX_MAX;
    if (cap < need) return -1;
    unsigned char *p = realloc(h->rx, cap);
    if (!p) return -1;
    h->rx = p;
    h->rx_cap = cap;
    return 0;
}

static void ws_rx_drop(WsHandle *h, size_t n) {
    if (!h || n == 0) return;
    if (n >= h->rx_len) {
        h->rx_len = 0;
        return;
    }
    h->rx_len -= n;
    memmove(h->rx, h->rx + n, h->rx_len);
}

/* Ensure h->rx_len >= need. 0 ok, -2 wouldblock (bytes kept), -1 error. Does not consume. */
static int ws_rx_need(WsIo *io, size_t need) {
    if (!io || !io->h) return -1;
    WsHandle *h = io->h;
    while (h->rx_len < need) {
        if (ws_rx_reserve(h, need) != 0) return -1;
        size_t space = h->rx_cap - h->rx_len;
        if (!space) return -1;
        ssize_t r;
        if (io->ssl)
            r = tls_read(io->ssl, h->rx + h->rx_len, space);
        else
            r = read(io->fd, h->rx + h->rx_len, space);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
            return -1;
        }
        if (r == 0) return -1;
        h->rx_len += (size_t)r;
    }
    return 0;
}

/* Unbuffered path (handshake / ws_raw): blocking gather. */
static ssize_t ws_io_read(WsIo *io, void *buf, size_t n) {
    if (!io || io->fd < 0 || !buf) return -1;
    if (io->h) {
        int er = ws_rx_need(io, n);
        if (er == -2) return -2;
        if (er != 0) return -1;
        memcpy(buf, io->h->rx, n);
        ws_rx_drop(io->h, n);
        return (ssize_t)n;
    }
    size_t got = 0;
    while (got < n) {
        ssize_t r;
        if (io->ssl)
            r = tls_read(io->ssl, (char *)buf + got, n - got);
        else
            r = read(io->fd, (char *)buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return (ssize_t)got;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static int ws_io_poll_out(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    for (;;) {
        int r = poll(&pfd, 1, timeout_ms);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

static ssize_t ws_io_write(WsIo *io, const void *buf, size_t n) {
    if (!io || io->fd < 0) return -1;
    if (!buf && n) return -1;
    size_t sent = 0;
    while (sent < n) {
        ssize_t w;
        if (io->ssl)
            w = tls_write(io->ssl, (const char *)buf + sent, n - sent);
        else
            w = write(io->fd, (const char *)buf + sent, n - sent);
        if (w > 0) {
            sent += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (ws_io_poll_out(io->fd, 5000) > 0) continue;
            return -1;
        }
        if (w == 0) return -1;
        return -1;
    }
    return (ssize_t)sent;
}

static void ws_mask_bytes(unsigned char *data, size_t n, const unsigned char mask[4]) {
    for (size_t i = 0; i < n; i++) data[i] ^= mask[i & 3];
}

/* Send a single FIN frame. is_client=1 applies masking. */
static int ws_frame_send(WsIo *io, int is_client, int opcode, const void *data, size_t n) {
    if (n > WS_MAX_PAYLOAD) return -1;
#ifndef SHAKTI_WASM
    ws_ignore_sigpipe_once();
#endif
    unsigned char hdr[14];
    size_t hlen = 2;
    hdr[0] = (unsigned char)(0x80 | (opcode & 0x0f));
    if (n < 126) {
        hdr[1] = (unsigned char)n;
        if (is_client) hdr[1] |= 0x80;
    } else if (n <= 0xffff) {
        hdr[1] = 126;
        if (is_client) hdr[1] |= 0x80;
        hdr[2] = (unsigned char)((n >> 8) & 0xff);
        hdr[3] = (unsigned char)(n & 0xff);
        hlen = 4;
    } else {
        hdr[1] = 127;
        if (is_client) hdr[1] |= 0x80;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (unsigned char)((n >> (56 - 8 * i)) & 0xff);
        hlen = 10;
    }
    unsigned char mask[4] = {0};
    unsigned char *payload = NULL;
    if (is_client) {
        if (ws_fill_random(mask, 4) != 0) return -1;
        memcpy(hdr + hlen, mask, 4);
        hlen += 4;
        if (n) {
            payload = malloc(n);
            if (!payload) return -1;
            memcpy(payload, data, n);
            ws_mask_bytes(payload, n, mask);
        }
    }
    if (ws_io_write(io, hdr, hlen) != (ssize_t)hlen) {
        free(payload);
        return -1;
    }
    if (n) {
        const void *p = is_client ? (const void *)payload : data;
        if (ws_io_write(io, p, n) != (ssize_t)n) {
            free(payload);
            return -1;
        }
    }
    free(payload);
    return 0;
}

/* Exact read helper: 0 ok, -3 wouldblock, -1 error. Consumes on success (unbuffered ok;
 * handle path should use ws_frame_recv_buffered instead). */
static int ws_read_exact(WsIo *io, void *buf, size_t n) {
    ssize_t r = ws_io_read(io, buf, n);
    if (r == -2) return -3;
    if (r != (ssize_t)n) return -1;
    return 0;
}

/* Parse one frame from handle RX without dropping until the whole frame is present. */
static int ws_frame_recv_buffered(WsIo *io, int is_client, int *out_opcode, char *out,
                                  size_t out_cap) {
    WsHandle *h = io->h;
    for (;;) {
        int er = ws_rx_need(io, 2);
        if (er != 0) return er == -2 ? -3 : -1;
        unsigned char h0 = h->rx[0], h1 = h->rx[1];
        int fin = (h0 & 0x80) != 0;
        int opcode = h0 & 0x0f;
        int masked = (h1 & 0x80) != 0;
        uint64_t n = h1 & 0x7f;
        if (!fin) return -1;
        if (is_client && masked) return -1;
        if (!is_client && !masked) return -1;
        if ((opcode == 0x8 || opcode == 0x9 || opcode == 0xA) && n > 125) return -1;
        size_t off = 2;
        if (n == 126) {
            er = ws_rx_need(io, off + 2);
            if (er == -2) return -3;
            if (er != 0) return -1;
            n = ((uint64_t)h->rx[off] << 8) | h->rx[off + 1];
            off += 2;
        } else if (n == 127) {
            er = ws_rx_need(io, off + 8);
            if (er == -2) return -3;
            if (er != 0) return -1;
            n = 0;
            for (int i = 0; i < 8; i++) n = (n << 8) | h->rx[off + (size_t)i];
            off += 8;
        }
        if (n > WS_MAX_PAYLOAD) return -1;
        if (masked) {
            er = ws_rx_need(io, off + 4);
            if (er == -2) return -3;
            if (er != 0) return -1;
            off += 4;
        }
        er = ws_rx_need(io, off + (size_t)n);
        if (er == -2) return -3;
        if (er != 0) return -1;

        unsigned char mask[4] = {0};
        size_t pay_off = off - (masked ? 4 : 0);
        if (masked) memcpy(mask, h->rx + pay_off, 4);
        size_t data_off = off;
        /* RFC6455: client→server masked, server→client unmasked (enforced above). */
        if (n + 1 > out_cap && opcode != 0x9 && opcode != 0xA && opcode != 0x8) return -1;
        char *tmp = NULL;
        char *dest = out;
        if (opcode == 0x9 || opcode == 0xA || (opcode == 0x8 && n + 1 > out_cap)) {
            tmp = malloc((size_t)n + 1);
            if (!tmp) return -1;
            dest = tmp;
        }
        if (n) memcpy(dest, h->rx + data_off, (size_t)n);
        dest[n] = 0;
        if (masked) ws_mask_bytes((unsigned char *)dest, (size_t)n, mask);
        ws_rx_drop(h, data_off + (size_t)n);

        if (opcode == 0x9) {
            ws_frame_send(io, is_client, 0xA, dest, (size_t)n);
            free(tmp);
            continue;
        }
        if (opcode == 0xA) {
            free(tmp);
            continue;
        }
        if (opcode == 0x8) {
            free(tmp);
            if (out_opcode) *out_opcode = 0x8;
            return -2;
        }
        if (opcode != 0x1 && opcode != 0x2) {
            free(tmp);
            return -1;
        }
        if (tmp) {
            if (n + 1 > out_cap) {
                free(tmp);
                return -1;
            }
            memcpy(out, tmp, (size_t)n + 1);
            free(tmp);
        }
        if (out_opcode) *out_opcode = opcode;
        return (int)n;
    }
}

/* Recv one frame. Auto-answers ping. Returns payload len; opcode out.
 * -3 = wouldblock, -2 = peer close, -1 = error. FIN required. */
static int ws_frame_recv(WsIo *io, int is_client, int *out_opcode, char *out, size_t out_cap) {
    if (io && io->h) return ws_frame_recv_buffered(io, is_client, out_opcode, out, out_cap);
    for (;;) {
        unsigned char hdr[2];
        int er = ws_read_exact(io, hdr, 2);
        if (er != 0) return er;
        int fin = (hdr[0] & 0x80) != 0;
        int opcode = hdr[0] & 0x0f;
        int masked = (hdr[1] & 0x80) != 0;
        uint64_t n = hdr[1] & 0x7f;
        if (!fin) return -1; /* no fragmentation in v1 */
        /* Server must receive masked client frames; client receives unmasked. */
        if (is_client && masked) return -1;
        if (!is_client && !masked) return -1;
        /* Control frames: payload must be <= 125 (RFC6455). */
        if ((opcode == 0x8 || opcode == 0x9 || opcode == 0xA) && n > 125) return -1;
        if (n == 126) {
            unsigned char e[2];
            er = ws_read_exact(io, e, 2);
            if (er != 0) return er;
            n = ((uint64_t)e[0] << 8) | e[1];
        } else if (n == 127) {
            unsigned char e[8];
            er = ws_read_exact(io, e, 8);
            if (er != 0) return er;
            n = 0;
            for (int i = 0; i < 8; i++) n = (n << 8) | e[i];
        }
        if (n > WS_MAX_PAYLOAD) return -1;
        unsigned char mask[4] = {0};
        if (masked) {
            er = ws_read_exact(io, mask, 4);
            if (er != 0) return er;
        }
        /* RFC6455: client→server masked, server→client unmasked (enforced above). */
        if (n + 1 > out_cap && opcode != 0x9 && opcode != 0xA && opcode != 0x8) return -1;
        char *tmp = NULL;
        char *dest = out;
        if (opcode == 0x9 || opcode == 0xA || (opcode == 0x8 && n + 1 > out_cap)) {
            tmp = malloc((size_t)n + 1);
            if (!tmp) return -1;
            dest = tmp;
        }
        if (n) {
            er = ws_read_exact(io, dest, (size_t)n);
            if (er != 0) {
                free(tmp);
                return er;
            }
        }
        if (masked) ws_mask_bytes((unsigned char *)dest, (size_t)n, mask);
        dest[n] = 0;

        if (opcode == 0x9) { /* ping → pong */
            ws_frame_send(io, is_client, 0xA, dest, (size_t)n);
            free(tmp);
            continue;
        }
        if (opcode == 0xA) { /* pong ignore */
            free(tmp);
            continue;
        }
        if (opcode == 0x8) {
            free(tmp);
            if (out_opcode) *out_opcode = 0x8;
            return -2;
        }
        if (opcode != 0x1 && opcode != 0x2) {
            free(tmp);
            return -1;
        }
        if (tmp) {
            if (n + 1 > out_cap) {
                free(tmp);
                return -1;
            }
            memcpy(out, tmp, (size_t)n + 1);
            free(tmp);
        }
        if (out_opcode) *out_opcode = opcode;
        return (int)n;
    }
}

static WsHandle *ws_slot(int h) {
#ifndef SHAKTI_WASM
    if (h < 1 || h >= WS_MAX_HANDLES) return NULL;
    WsHandle *s = &g_ws_handles[h];
    return (s->in_use && !s->closed) ? s : NULL;
#else
    (void)h;
    return NULL;
#endif
}

static int ws_alloc(int fd, void *ssl, int is_client) {
#ifndef SHAKTI_WASM
    for (int i = 1; i < WS_MAX_HANDLES; i++) {
        if (!g_ws_handles[i].in_use) {
            g_ws_handles[i].in_use = 1;
            g_ws_handles[i].closed = 0;
            g_ws_handles[i].fd = fd;
            g_ws_handles[i].ssl = ssl;
            g_ws_handles[i].is_client = is_client;
            g_ws_handles[i].rx = NULL;
            g_ws_handles[i].rx_len = 0;
            g_ws_handles[i].rx_cap = 0;
            return i;
        }
    }
#else
    (void)fd;
    (void)ssl;
    (void)is_client;
#endif
    return -1;
}

static void ws_free_handle(WsHandle *s) {
    if (!s) return;
    if (s->ssl) {
        tls_close(s->ssl, s->fd);
    } else if (s->fd >= 0) {
        close(s->fd);
    }
    free(s->rx);
    s->rx = NULL;
    s->rx_len = 0;
    s->rx_cap = 0;
    s->ssl = NULL;
    s->fd = -1;
    s->closed = 1;
    s->in_use = 0;
}

static int ws_parse_url(const char *url, int *use_tls, char *host, size_t host_cap, int *port,
                        char *path, size_t path_cap) {
    if (!url) return -1;
    const char *p;
    if (!strncmp(url, "ws://", 5)) {
        *use_tls = 0;
        p = url + 5;
        *port = 80;
    } else if (!strncmp(url, "wss://", 6)) {
        *use_tls = 1;
        p = url + 6;
        *port = 443;
    } else {
        return -1;
    }
    const char *slash = strchr(p, '/');
    const char *colon = NULL;
    size_t hlen;
    if (slash) {
        colon = memchr(p, ':', (size_t)(slash - p));
        hlen = (size_t)((colon ? colon : slash) - p);
        if (snprintf(path, path_cap, "%s", slash) >= (int)path_cap) return -1;
    } else {
        colon = strchr(p, ':');
        hlen = colon ? (size_t)(colon - p) : strlen(p);
        if (snprintf(path, path_cap, "/") >= (int)path_cap) return -1;
    }
    if (hlen == 0 || hlen >= host_cap) return -1;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (colon && (!slash || colon < slash)) *port = atoi(colon + 1);
    /* Reject CR/LF (and other CTL) in host/path to prevent header injection. */
    for (const char *q = host; *q; q++) {
        if ((unsigned char)*q < 0x20 || *q == 0x7f) return -1;
    }
    for (const char *q = path; *q; q++) {
        if ((unsigned char)*q < 0x20 || *q == 0x7f) return -1;
    }
    return 0;
}

static int ws_tcp_connect(const char *host, int port) {
    char port_s[16];
    snprintf(port_s, sizeof port_s, "%d", port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_s, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef TCP_NODELAY
        {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        }
#endif
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int ws_header_has(const char *resp, const char *name, const char *want_sub) {
    /* Case-insensitive scan for "name: ...want_sub..." */
    const char *p = resp;
    size_t nlen = strlen(name);
    while (p && *p) {
        const char *eol = strstr(p, "\r\n");
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        if (llen >= nlen + 1 && !strncasecmp(p, name, nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (v < p + llen && (*v == ' ' || *v == '\t')) v++;
            size_t vlen = (size_t)((p + llen) - v);
            if (want_sub) {
                /* substring search case-insensitive */
                size_t wlen = strlen(want_sub);
                for (size_t i = 0; i + wlen <= vlen; i++) {
                    if (!strncasecmp(v + i, want_sub, wlen)) return 1;
                }
                return 0;
            }
            return 1;
        }
        if (!eol) break;
        p = eol + 2;
        if (p[0] == '\r' && p[1] == '\n') break;
    }
    return 0;
}

static int ws_extract_header(const char *resp, const char *name, char *out, size_t out_cap) {
    const char *p = resp;
    size_t nlen = strlen(name);
    while (p && *p) {
        const char *eol = strstr(p, "\r\n");
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);
        if (llen >= nlen + 1 && !strncasecmp(p, name, nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (v < p + llen && (*v == ' ' || *v == '\t')) v++;
            size_t vlen = (size_t)((p + llen) - v);
            if (vlen + 1 > out_cap) return -1;
            memcpy(out, v, vlen);
            out[vlen] = 0;
            return 0;
        }
        if (!eol) break;
        p = eol + 2;
        if (p[0] == '\r' && p[1] == '\n') break;
    }
    return -1;
}

static int ws_client_handshake(WsIo *io, const char *host, int port, const char *path,
                               const char *key_b64) {
    char expect[64];
    if (ws_accept_key(key_b64, expect, sizeof expect) != 0) return -1;
    char req[2048];
    int n = snprintf(req, sizeof req,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%d\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     path, host, port, key_b64);
    if (n < 0 || (size_t)n >= sizeof req) return -1;
    if (ws_io_write(io, req, (size_t)n) != n) return -1;
    char resp[4096];
    size_t got = 0;
    while (got + 1 < sizeof resp) {
        ssize_t r = ws_io_read(io, resp + got, 1);
        if (r <= 0) break;
        got += (size_t)r;
        resp[got] = 0;
        if (got >= 4 && strstr(resp, "\r\n\r\n")) break;
    }
    if (!strstr(resp, "101")) return -1;
    char accept[64];
    if (ws_extract_header(resp, "Sec-WebSocket-Accept", accept, sizeof accept) != 0) return -1;
    if (strcmp(accept, expect) != 0) return -1;
    (void)ws_header_has;
    return 0;
}

int ws_raw_connect(const char *ws_url, int *out_fd) {
    int use_tls = 0, port = 80;
    char host[256], path[1024];
    if (ws_parse_url(ws_url, &use_tls, host, sizeof host, &port, path, sizeof path) != 0)
        return -1;
    if (use_tls) return -1; /* CDP uses plain ws:// */
    int fd = ws_tcp_connect(host, port);
    if (fd < 0) return -1;
    unsigned char key_raw[16];
    if (ws_fill_random(key_raw, sizeof key_raw) != 0) {
        close(fd);
        return -1;
    }
    char key_b64[32];
    ws_b64_encode(key_raw, 16, key_b64, sizeof key_b64);
    WsIo io = {.fd = fd, .ssl = NULL};
    if (ws_client_handshake(&io, host, port, path, key_b64) != 0) {
        close(fd);
        return -1;
    }
    *out_fd = fd;
    return 0;
}

int ws_raw_send_text(int fd, const char *msg) {
    WsIo io = {.fd = fd, .ssl = NULL};
    return ws_frame_send(&io, 1, 0x1, msg, msg ? strlen(msg) : 0);
}

int ws_raw_recv_text(int fd, char *out, size_t out_cap) {
    WsIo io = {.fd = fd, .ssl = NULL};
    int opcode = 0;
    int n = ws_frame_recv(&io, 1, &opcode, out, out_cap);
    if (n == -2) return -2;
    if (n < 0) return -1;
    return n;
}

void ws_raw_set_recv_timeout_ms(int fd, int ms) {
    if (fd < 0) return;
    struct timeval tv;
    if (ms <= 0) {
        tv.tv_sec = 30;
        tv.tv_usec = 0;
    } else {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

#ifndef SHAKTI_WASM

static V *ws_do_connect(const char *url, int insecure) {
    int use_tls = 0, port = 80;
    char host[256], path[1024];
    if (ws_parse_url(url, &use_tls, host, sizeof host, &port, path, sizeof path) != 0)
        return v_err("ws_connect: url must be ws:// or wss://");
    int fd = ws_tcp_connect(host, port);
    if (fd < 0) return v_err("ws_connect: tcp connect failed");
    void *ssl = NULL;
    if (use_tls) {
        ssl = tls_connect(fd, host, insecure);
        if (!ssl) {
            close(fd);
            return v_err("ws_connect: tls handshake failed");
        }
    }
    unsigned char key_raw[16];
    if (ws_fill_random(key_raw, sizeof key_raw) != 0) {
        if (ssl) tls_close(ssl, fd);
        else close(fd);
        return v_err("ws_connect: random failed");
    }
    char key_b64[32];
    ws_b64_encode(key_raw, 16, key_b64, sizeof key_b64);
    WsIo io = {.fd = fd, .ssl = ssl};
    if (ws_client_handshake(&io, host, port, path, key_b64) != 0) {
        if (ssl) tls_close(ssl, fd);
        else close(fd);
        return v_err("ws_connect: websocket handshake failed");
    }
    int h = ws_alloc(fd, ssl, 1);
    if (h < 0) {
        if (ssl) tls_close(ssl, fd);
        else close(fd);
        return v_err("ws_connect: too many handles");
    }
    return v_int(h);
}

static int ws_hdr_get_ci(V *headers, const char *name, char *out, size_t out_cap) {
    if (!headers || headers->t != T_DICT || !name) return -1;
    for (int64_t i = 0; i < headers->keys->n; i++) {
        V *k = headers->keys->L[i];
        if (k->t != T_STR || strcasecmp(k->s, name) != 0) continue;
        V *v = headers->vals->L[i];
        if (v->t != T_STR) return -1;
        if (snprintf(out, out_cap, "%s", v->s) >= (int)out_cap) return -1;
        return 0;
    }
    return -1;
}

static V *ws_do_accept(int rest_h, V *req_or_headers) {
    char key[128];
    char proto[128];
    key[0] = 0;
    proto[0] = 0;
    V *headers = req_or_headers;
    if (headers && headers->t == T_DICT) {
        V *inner = v_dict_get(headers, "headers");
        if (inner && inner->t == T_DICT) headers = inner;
    }
    /* Prefer key stashed on rest conn from rest.read; fall back to passed headers. */
    if (rest_peek_ws_key(rest_h, key, sizeof key) != 0 || !key[0]) {
        if (!headers || ws_hdr_get_ci(headers, "Sec-WebSocket-Key", key, sizeof key) != 0)
            return v_err("ws_accept: missing Sec-WebSocket-Key");
    }
    rest_peek_ws_protocol(rest_h, proto, sizeof proto);
    if (!proto[0] && headers)
        ws_hdr_get_ci(headers, "Sec-WebSocket-Protocol", proto, sizeof proto);
    /* RFC6455 token-ish: reject CTL / separators that enable response splitting. */
    if (proto[0]) {
        for (char *q = proto; *q; q++) {
            unsigned char c = (unsigned char)*q;
            if (c < 0x21 || c > 0x7e || c == '(' || c == ')' || c == '<' || c == '>' ||
                c == '@' || c == ',' || c == ';' || c == ':' || c == '\\' || c == '"' ||
                c == '/' || c == '[' || c == ']' || c == '?' || c == '=' || c == '{' ||
                c == '}' || c == ' ') {
                proto[0] = 0;
                break;
            }
        }
    }

    char accept[64];
    if (ws_accept_key(key, accept, sizeof accept) != 0)
        return v_err("ws_accept: accept key failed");

    int fd = -1;
    void *ssl = NULL;
    if (rest_take_conn(rest_h, &fd, &ssl) != 0)
        return v_err("ws_accept: invalid rest connection");

    char resp[512];
    int n;
    if (proto[0]) {
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n"
                     "Sec-WebSocket-Protocol: %s\r\n"
                     "\r\n",
                     accept, proto);
    } else {
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n"
                     "\r\n",
                     accept);
    }
    WsIo io = {.fd = fd, .ssl = ssl};
    if (n < 0 || (size_t)n >= sizeof resp || ws_io_write(&io, resp, (size_t)n) != n) {
        if (ssl) tls_close(ssl, fd);
        else close(fd);
        return v_err("ws_accept: write 101 failed");
    }
    int h = ws_alloc(fd, ssl, 0);
    if (h < 0) {
        if (ssl) tls_close(ssl, fd);
        else close(fd);
        return v_err("ws_accept: too many handles");
    }
    return v_int(h);
}

static V *ws_msg_dict(const char *type, V *data) {
    V *d = v_dict_empty();
    v_dict_put(d, "type", v_str((char *)type));
    if (data)
        v_dict_put(d, "data", data);
    else
        v_dict_put(d, "data", v_nil());
    return d;
}

#endif /* !SHAKTI_WASM */

V *bi_ws_connect(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("ws_connect(url[, insecure])"))
    int insecure = 0;
    if (n > 1) {
        if (a[1]->t == T_INT) insecure = a[1]->j != 0;
        else if (a[1]->t == T_FLOAT) insecure = a[1]->f != 0;
        else return v_err("ws_connect: insecure must be int");
    }
    return ws_do_connect(a[0]->s, insecure);
#endif
}

V *bi_ws_accept(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("ws_accept(conn[, req_or_headers])"))
    V *hdrs = (n > 1 && a[1]->t == T_DICT) ? a[1] : NULL;
    return ws_do_accept((int)a[0]->j, hdrs);
#endif
}

V *bi_ws_send(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_STR, v_err("ws_send(h, text)"))
    WsHandle *s = ws_slot((int)a[0]->j);
    if (!s) return v_err("ws_send: invalid handle");
    WsIo io = {.fd = s->fd, .ssl = s->ssl, .h = s};
    if (ws_frame_send(&io, s->is_client, 0x1, a[1]->s, strlen(a[1]->s)) != 0)
        return v_err("ws_send: failed");
    return v_nil();
#endif
}

V *bi_ws_send_bin(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_INT, v_err("ws_send_bin(h, data)"))
    WsHandle *s = ws_slot((int)a[0]->j);
    if (!s) return v_err("ws_send_bin: invalid handle");
    const unsigned char *data = (const unsigned char *)"";
    size_t len = 0;
    if (a[1]->t == T_STR) {
        data = (const unsigned char *)(a[1]->s ? a[1]->s : "");
        len = a[1]->s ? strlen(a[1]->s) : 0;
    } else if (a[1]->t == T_CVEC) {
        len = (size_t)a[1]->n;
        data = len && a[1]->B ? a[1]->B : (const unsigned char *)"";
    } else {
        return v_err("ws_send_bin: data must be str or list[char]");
    }
    WsIo io = {.fd = s->fd, .ssl = s->ssl, .h = s};
    if (ws_frame_send(&io, s->is_client, 0x2, data, len) != 0)
        return v_err("ws_send_bin: failed");
    return v_nil();
#endif
}

V *bi_ws_recv(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("ws_recv(h)"))
    WsHandle *s = ws_slot((int)a[0]->j);
    if (!s) return v_err("ws_recv: invalid handle");
    char *buf = malloc(WS_MAX_PAYLOAD + 1);
    if (!buf) return v_err("ws_recv: out of memory");
    WsIo io = {.fd = s->fd, .ssl = s->ssl, .h = s};
    int opcode = 0;
    int rn = ws_frame_recv(&io, s->is_client, &opcode, buf, WS_MAX_PAYLOAD + 1);
    if (rn == -3) {
        free(buf);
        return v_err("ws_recv: wouldblock");
    }
    if (rn == -2) {
        free(buf);
        return ws_msg_dict("close", NULL);
    }
    if (rn < 0) {
        free(buf);
        return v_err("ws_recv: failed");
    }
    if (opcode == 0x2) {
        V *u = v_cvec(rn);
        if (rn && u->B) memcpy(u->B, buf, (size_t)rn);
        free(buf);
        return ws_msg_dict("bin", u);
    }
    V *t = v_str(buf);
    free(buf);
    return ws_msg_dict("text", t);
#endif
}

V *bi_ws_close(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("ws_close(h)"))
    WsHandle *s = ws_slot((int)a[0]->j);
    if (!s) return v_err("ws_close: invalid handle");
    WsIo io = {.fd = s->fd, .ssl = s->ssl, .h = s};
    unsigned char body[2] = {0x03, 0xe8}; /* 1000 */
    ws_frame_send(&io, s->is_client, 0x8, body, 2);
    ws_free_handle(s);
    return v_nil();
#endif
}

V *bi_ws_set_nonblock(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_INT, v_err("ws_set_nonblock(h, enabled)"))
    WsHandle *s = ws_slot((int)a[0]->j);
    if (!s) return v_err("ws_set_nonblock: invalid handle");
    int en = 0;
    if (a[1]->t == T_INT) en = a[1]->j != 0;
    else if (a[1]->t == T_FLOAT) en = a[1]->f != 0;
    else return v_err("ws_set_nonblock: enabled must be int");
    int flags = fcntl(s->fd, F_GETFL, 0);
    if (flags < 0) return v_err("ws_set_nonblock: fcntl failed");
    if (en) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    if (fcntl(s->fd, F_SETFL, flags) < 0) return v_err("ws_set_nonblock: fcntl failed");
    return v_nil();
#endif
}

V *bi_ws_poll(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("ws: not available in WASM");
#else
    P(n < 2 || (a[0]->t != T_LIST && a[0]->t != T_IVEC), v_err("ws_poll(handles, timeout_ms)"))
    int timeout;
    if (a[1]->t == T_INT) timeout = (int)a[1]->j;
    else if (a[1]->t == T_FLOAT) timeout = (int)a[1]->f;
    else return v_err("ws_poll(handles, timeout_ms)");
    V *handles = a[0];
    int nh = (int)handles->n;
    if (nh <= 0) return v_list(0);

    /* SSL_pending first */
    V *ready = v_list(0);
    struct pollfd pfds[WS_MAX_HANDLES];
    int map[WS_MAX_HANDLES];
    int np = 0;
    for (int i = 0; i < nh; i++) {
        int h;
        if (handles->t == T_IVEC) h = (int)handles->J[i];
        else if (handles->L[i]->t == T_INT) h = (int)handles->L[i]->j;
        else continue;
        WsHandle *s = ws_slot(h);
        if (!s) continue;
        if (s->ssl && tls_pending(s->ssl) > 0) {
            v_list_append_own(ready, v_int(h));
            continue;
        }
        if (np < WS_MAX_HANDLES) {
            pfds[np].fd = s->fd;
            pfds[np].events = POLLIN;
            pfds[np].revents = 0;
            map[np] = h;
            np++;
        }
    }
    if (ready->n > 0) return ready;
    if (np == 0) return ready;
    int pr = poll(pfds, (nfds_t)np, timeout);
    if (pr < 0) {
        v_free(ready);
        return v_err("ws_poll: poll failed");
    }
    for (int i = 0; i < np; i++) {
        if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
            v_list_append_own(ready, v_int(map[i]));
    }
    return ready;
#endif
}
