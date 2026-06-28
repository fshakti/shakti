#include "ipc.h"
#include "ipc_rdma.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#endif

#define IPC_MAX_HANDLES 128

typedef enum {
    IPC_KIND_NONE = 0,
    IPC_KIND_SOCK_LISTEN,
    IPC_KIND_SOCK_CONN,
#ifdef SHAKTI_HAVE_RDMA
    IPC_KIND_RDMA_LISTEN,
    IPC_KIND_RDMA_CONN,
#endif
} IpcKind;

typedef struct {
    unsigned char *data;
    size_t cap;
    size_t len;
    int have_len;
    uint32_t msg_len;
} IpcRxBuf;

typedef struct {
    int in_use;
    int closed;
    IpcKind kind;
    int fd;
    int nonblock;
    char uds_path[108];
    IpcRxBuf rx;
#ifdef SHAKTI_HAVE_RDMA
    IpcRdmaConn *rdma;
#endif
} IpcHandle;

typedef struct {
    int in_use;
    void *ptr;
    size_t size;
    char name[256];
} IpcShmSlot;

static IpcHandle g_handles[IPC_MAX_HANDLES];
static IpcShmSlot g_shm[IPC_MAX_HANDLES];

#ifndef SHAKTI_HAVE_RDMA
int ipc_rdma_available(void) { return 0; }
#endif

static IpcTransport ipc_parse_transport(const char *s) {
    const char *env = getenv("SHAKTI_IPC_TRANSPORT");
    if (!s || !s[0]) s = env;
    if (!s || !s[0] || !strcmp(s, "auto")) return IPC_TR_AUTO;
    if (!strcmp(s, "tcp")) return IPC_TR_TCP;
    if (!strcmp(s, "uds") || !strcmp(s, "unix")) return IPC_TR_UDS;
    if (!strcmp(s, "rdma")) return IPC_TR_RDMA;
    return IPC_TR_AUTO;
}

static int ipc_is_localhost(const char *host) {
    if (!host || !host[0]) return 1;
    return !strcmp(host, "127.0.0.1") || !strcmp(host, "localhost") || !strcmp(host, "::1");
}

static void ipc_uds_path(int port, char *out, size_t cap) {
    const char *dir = getenv("SHAKTI_IPC_DIR");
    if (!dir || !dir[0]) dir = "/tmp";
    snprintf(out, cap, "%s/shakti-%d.sock", dir, port);
}

static IpcHandle *ipc_slot(int h) {
    if (h < 1 || h >= IPC_MAX_HANDLES) return NULL;
    IpcHandle *s = &g_handles[h];
    return (s->in_use && !s->closed) ? s : NULL;
}

static int ipc_alloc(void) {
    for (int i = 1; i < IPC_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            memset(&g_handles[i], 0, sizeof g_handles[i]);
            g_handles[i].in_use = 1;
            g_handles[i].fd = -1;
            return i;
        }
    }
    return -1;
}

static void ipc_rx_free(IpcRxBuf *rx) {
    free(rx->data);
    rx->data = NULL;
    rx->cap = rx->len = 0;
    rx->have_len = 0;
    rx->msg_len = 0;
}

static void ipc_free_handle(int h) {
    IpcHandle *s = &g_handles[h];
    if (!s->in_use) return;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->kind == IPC_KIND_SOCK_LISTEN && s->uds_path[0])
        unlink(s->uds_path);
#ifdef SHAKTI_HAVE_RDMA
    if (s->rdma) {
        ipc_rdma_close(s->rdma);
        s->rdma = NULL;
    }
#endif
    ipc_rx_free(&s->rx);
    memset(s, 0, sizeof *s);
}

static int sock_set_block(int fd, int block) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    if (block)
        fl &= ~O_NONBLOCK;
    else
        fl |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, fl);
}

static void sock_set_tcp_nodelay(int fd) {
#ifdef TCP_NODELAY
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#else
    (void)fd;
#endif
}

static int sock_read_some(int fd, void *buf, size_t n, int block, size_t *got) {
    size_t off = 0;
    *got = 0;
    while (off < n) {
        ssize_t r = block ? read(fd, (char *)buf + off, n - off)
                          : recv(fd, (char *)buf + off, n - off, MSG_DONTWAIT);
        if (r < 0) {
            if (!block && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                *got = off;
                return off ? 0 : -2;
            }
            return -1;
        }
        if (r == 0) return -1;
        off += (size_t)r;
    }
    *got = off;
    return 0;
}

static int sock_write_full(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, (const char *)buf + off, n - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int sock_write_frame(int fd, const void *hdr, size_t hdr_len, const void *data, size_t data_len) {
#if !defined(_WIN32)
    struct iovec iov[2];
    iov[0].iov_base = (void *)hdr;
    iov[0].iov_len = hdr_len;
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = data_len;
    int iovcnt = data_len ? 2 : 1;
    struct iovec *cur = iov;
    while (iovcnt > 0) {
        ssize_t w = writev(fd, cur, iovcnt);
        if (w <= 0) return -1;
        size_t wrote = (size_t)w;
        while (iovcnt > 0 && wrote >= cur[0].iov_len) {
            wrote -= cur[0].iov_len;
            cur++;
            iovcnt--;
        }
        if (iovcnt > 0 && wrote > 0) {
            cur[0].iov_base = (char *)cur[0].iov_base + wrote;
            cur[0].iov_len -= wrote;
        }
    }
    return 0;
#else
    return sock_write_full(fd, hdr, hdr_len) || sock_write_full(fd, data, data_len);
#endif
}

static int sock_listen_tcp(const char *host, int port, char *err, size_t err_cap) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: socket: %s", strerror(errno));
        return -1;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (!host || !host[0] || ipc_is_localhost(host))
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        snprintf(err, err_cap, "ipc: bad host '%s'", host);
        close(s);
        return -1;
    }
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        snprintf(err, err_cap, "ipc: bind: %s", strerror(errno));
        close(s);
        return -1;
    }
    if (listen(s, 16) < 0) {
        snprintf(err, err_cap, "ipc: listen: %s", strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static int sock_listen_uds(int port, char *path_out, char *err, size_t err_cap) {
#if defined(_WIN32)
    (void)port;
    (void)path_out;
    snprintf(err, err_cap, "ipc: uds not supported on this platform");
    return -1;
#else
    ipc_uds_path(port, path_out, 108);
    unlink(path_out);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: uds socket: %s", strerror(errno));
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path_out, sizeof addr.sun_path - 1);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        snprintf(err, err_cap, "ipc: uds bind: %s", strerror(errno));
        close(s);
        return -1;
    }
    if (listen(s, 16) < 0) {
        snprintf(err, err_cap, "ipc: uds listen: %s", strerror(errno));
        close(s);
        unlink(path_out);
        return -1;
    }
    return s;
#endif
}

static int sock_connect_tcp(const char *host, int port, char *err, size_t err_cap) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: socket: %s", strerror(errno));
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (!host || !host[0]) host = "127.0.0.1";
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        snprintf(err, err_cap, "ipc: bad host '%s'", host);
        close(s);
        return -1;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        snprintf(err, err_cap, "ipc: connect: %s", strerror(errno));
        close(s);
        return -1;
    }
    sock_set_tcp_nodelay(s);
    return s;
}

static int sock_connect_uds(int port, char *err, size_t err_cap) {
#if defined(_WIN32)
    (void)port;
    snprintf(err, err_cap, "ipc: uds not supported on this platform");
    return -1;
#else
    char path[108];
    ipc_uds_path(port, path, sizeof path);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: uds socket: %s", strerror(errno));
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        snprintf(err, err_cap, "ipc: uds connect: %s", strerror(errno));
        close(s);
        return -1;
    }
    return s;
#endif
}

static IpcTransport ipc_resolve_listen(int port, const char *host, IpcTransport tr) {
    (void)port;
    if (tr == IPC_TR_TCP || tr == IPC_TR_RDMA) return tr;
    if (tr == IPC_TR_UDS) return IPC_TR_UDS;
    if (ipc_is_localhost(host)) return IPC_TR_UDS;
#ifdef SHAKTI_HAVE_RDMA
    if (tr == IPC_TR_AUTO && ipc_rdma_available()) return IPC_TR_RDMA;
#endif
    return IPC_TR_TCP;
}

static IpcTransport ipc_resolve_connect(const char *host, IpcTransport tr) {
    if (tr == IPC_TR_TCP || tr == IPC_TR_RDMA || tr == IPC_TR_UDS) return tr;
    if (ipc_is_localhost(host)) return IPC_TR_UDS;
#ifdef SHAKTI_HAVE_RDMA
    if (ipc_rdma_available()) return IPC_TR_RDMA;
#endif
    return IPC_TR_TCP;
}

static int ipc_sock_send(IpcHandle *s, const char *data, size_t len, char *err, size_t err_cap) {
    if (len > IPC_MAX_MSG) {
        snprintf(err, err_cap, "ipc: message too large (%zu)", len);
        return -1;
    }
    uint32_t be = htonl((uint32_t)len);
    if (sock_write_frame(s->fd, &be, 4, data, len) < 0) {
        snprintf(err, err_cap, "ipc: send: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int ipc_sock_recv_msg(IpcHandle *s, int block, char **out, size_t *out_len, char *err, size_t err_cap) {
    IpcRxBuf *rx = &s->rx;
    if (!rx->have_len) {
        if (rx->cap < 4) {
            unsigned char *nb = realloc(rx->data, 4);
            if (!nb) {
                snprintf(err, err_cap, "ipc: oom");
                return -1;
            }
            rx->data = nb;
            rx->cap = 4;
        }
        while (rx->len < 4) {
            size_t got;
            int rc = sock_read_some(s->fd, rx->data + rx->len, 4 - rx->len, block, &got);
            if (rc == -2) return -2;
            if (rc < 0) {
                snprintf(err, err_cap, "ipc: recv: disconnected");
                return -1;
            }
            rx->len += got;
        }
        uint32_t be;
        memcpy(&be, rx->data, 4);
        rx->msg_len = ntohl(be);
        rx->have_len = 1;
        rx->len = 0;
    }
    if (rx->msg_len > IPC_MAX_MSG) {
        snprintf(err, err_cap, "ipc: message too large (%u)", rx->msg_len);
        return -1;
    }
    if (rx->cap < rx->msg_len) {
        unsigned char *nb = realloc(rx->data, rx->msg_len);
        if (!nb) {
            snprintf(err, err_cap, "ipc: oom");
            return -1;
        }
        rx->data = nb;
        rx->cap = rx->msg_len;
    }
    while (rx->len < rx->msg_len) {
        size_t got;
        int rc = sock_read_some(s->fd, rx->data + rx->len, rx->msg_len - rx->len, block, &got);
        if (rc == -2) return -2;
        if (rc < 0) {
            snprintf(err, err_cap, "ipc: recv body: disconnected");
            return -1;
        }
        rx->len += got;
    }
    char *msg = malloc(rx->msg_len + 1);
    if (!msg) {
        snprintf(err, err_cap, "ipc: oom");
        return -1;
    }
    memcpy(msg, rx->data, rx->msg_len);
    msg[rx->msg_len] = 0;
    *out = msg;
    *out_len = rx->msg_len;
    rx->have_len = 0;
    rx->len = 0;
    rx->msg_len = 0;
    return 0;
}

V *bi_ipc_listen(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_listen(port[, host, transport])"))
    int port = (int)a[0]->j;
    const char *host = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "127.0.0.1";
    IpcTransport tr = ipc_parse_transport(n > 2 && a[2]->t == T_STR ? a[2]->s : NULL);
    tr = ipc_resolve_listen(port, host, tr);
    char err[512];
    err[0] = 0;

#ifdef SHAKTI_HAVE_RDMA
    if (tr == IPC_TR_RDMA) {
        IpcRdmaConn *rdma = NULL;
        if (ipc_rdma_listen(host, port, &rdma, err, sizeof err) != 0)
            return v_err(err[0] ? err : "ipc_listen: rdma failed");
        int h = ipc_alloc();
        if (h < 0) {
            ipc_rdma_close(rdma);
            return v_err("ipc_listen: no handles");
        }
        g_handles[h].kind = IPC_KIND_RDMA_LISTEN;
        g_handles[h].rdma = rdma;
        return v_int(h);
    }
#endif

    int fd = -1;
    char uds_path[108];
    uds_path[0] = 0;
    if (tr == IPC_TR_UDS)
        fd = sock_listen_uds(port, uds_path, err, sizeof err);
    else
        fd = sock_listen_tcp(host, port, err, sizeof err);
    if (fd < 0) return v_err(err[0] ? err : "ipc_listen failed");

    int h = ipc_alloc();
    if (h < 0) {
        close(fd);
        if (uds_path[0]) unlink(uds_path);
        return v_err("ipc_listen: no handles");
    }
    g_handles[h].kind = IPC_KIND_SOCK_LISTEN;
    g_handles[h].fd = fd;
    if (uds_path[0]) strncpy(g_handles[h].uds_path, uds_path, sizeof g_handles[h].uds_path - 1);
    return v_int(h);
}

V *bi_ipc_accept(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_accept(listen_h)"))
    IpcHandle *ls = ipc_slot((int)a[0]->j);
    P(!ls, v_err("ipc_accept: bad handle"))
    char err[512];
    err[0] = 0;

#ifdef SHAKTI_HAVE_RDMA
    if (ls->kind == IPC_KIND_RDMA_LISTEN) {
        IpcRdmaConn *rdma = NULL;
        if (ipc_rdma_accept(ls->rdma, &rdma, err, sizeof err) != 0)
            return v_err(err[0] ? err : "ipc_accept: rdma failed");
        int h = ipc_alloc();
        if (h < 0) {
            ipc_rdma_close(rdma);
            return v_err("ipc_accept: no handles");
        }
        g_handles[h].kind = IPC_KIND_RDMA_CONN;
        g_handles[h].rdma = rdma;
        return v_int(h);
    }
#endif

    P(ls->kind != IPC_KIND_SOCK_LISTEN, v_err("ipc_accept: not a listen handle"))
    int cfd = accept(ls->fd, NULL, NULL);
    if (cfd < 0) return v_err("ipc_accept: accept failed");
    sock_set_tcp_nodelay(cfd);
    int h = ipc_alloc();
    if (h < 0) {
        close(cfd);
        return v_err("ipc_accept: no handles");
    }
    g_handles[h].kind = IPC_KIND_SOCK_CONN;
    g_handles[h].fd = cfd;
    return v_int(h);
}

static V *ipc_do_connect(const char *host, int port, IpcTransport req) {
    char err[512];
    err[0] = 0;
    IpcTransport tr = ipc_resolve_connect(host, req);

#ifdef SHAKTI_HAVE_RDMA
    if (tr == IPC_TR_RDMA) {
        IpcRdmaConn *rdma = NULL;
        if (ipc_rdma_connect(host, port, &rdma, err, sizeof err) != 0) {
            if (req == IPC_TR_RDMA)
                return v_err(err[0] ? err : "ipc_connect: rdma failed");
            fprintf(stderr, "[ipc] rdma connect failed (%s), falling back\n",
                    err[0] ? err : "unknown");
            tr = ipc_is_localhost(host) ? IPC_TR_UDS : IPC_TR_TCP;
        } else {
            int h = ipc_alloc();
            if (h < 0) {
                ipc_rdma_close(rdma);
                return v_err("ipc_connect: no handles");
            }
            g_handles[h].kind = IPC_KIND_RDMA_CONN;
            g_handles[h].rdma = rdma;
            return v_int(h);
        }
    }
#endif

    int fd = -1;
    if (tr == IPC_TR_UDS)
        fd = sock_connect_uds(port, err, sizeof err);
    else
        fd = sock_connect_tcp(host, port, err, sizeof err);
    if (fd < 0) return v_err(err[0] ? err : "ipc_connect failed");

    int h = ipc_alloc();
    if (h < 0) {
        close(fd);
        return v_err("ipc_connect: no handles");
    }
    g_handles[h].kind = IPC_KIND_SOCK_CONN;
    g_handles[h].fd = fd;
    return v_int(h);
}

V *bi_ipc_connect(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_INT, v_err("ipc_connect(host, port[, transport])"))
    IpcTransport tr = ipc_parse_transport(n > 2 && a[2]->t == T_STR ? a[2]->s : NULL);
    return ipc_do_connect(a[0]->s, (int)a[1]->j, tr);
}

V *bi_ipc_send(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_STR, v_err("ipc_send(h, str)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_send: bad handle"))
    char err[512];
    err[0] = 0;
    size_t len = strlen(a[1]->s);

#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN) {
        P(ipc_rdma_send(s->rdma, a[1]->s, len, err, sizeof err) != 0,
          v_err(err[0] ? err : "ipc_send failed"))
        return v_nil();
    }
#endif

    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_send: not a connection"))
    P(ipc_sock_send(s, a[1]->s, len, err, sizeof err) != 0, v_err(err[0] ? err : "ipc_send failed"))
    return v_nil();
}

static V *ipc_do_recv(IpcHandle *s, int block) {
    char err[512];
    err[0] = 0;
    char *msg = NULL;
    size_t msg_len = 0;

#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN) {
        int rc = ipc_rdma_recv(s->rdma, block, &msg, &msg_len, err, sizeof err);
        if (rc == -2) return v_str("");
        P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
        V *r = v_str(msg);
        free(msg);
        return r;
    }
#endif

    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_recv: not a connection"))
    int rc = ipc_sock_recv_msg(s, block, &msg, &msg_len, err, sizeof err);
    if (rc == -2) return v_str("");
    P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
    V *r = v_str(msg);
    free(msg);
    return r;
}

V *bi_ipc_recv(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv: bad handle"))
    return ipc_do_recv(s, 1);
}

V *bi_ipc_recv_nowait(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_nowait(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_nowait: bad handle"))
    return ipc_do_recv(s, 0);
}

V *bi_ipc_set_nonblock(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT, v_err("ipc_set_nonblock(h, on)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_set_nonblock: bad handle"))
    int on = (a[1]->t == T_BOOL) ? a[1]->b : (a[1]->t == T_INT && a[1]->j);
#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN || s->kind == IPC_KIND_RDMA_LISTEN) {
        if (s->rdma) ipc_rdma_set_nonblock(s->rdma, on);
        s->nonblock = on;
        return v_nil();
    }
#endif
    P(s->fd < 0, v_err("ipc_set_nonblock: no fd"))
    P(sock_set_block(s->fd, !on) != 0, v_err("ipc_set_nonblock failed"))
    s->nonblock = on;
    return v_nil();
}

static int ipc_handle_from_elem(V *handles, int i, int *out_h) {
    if (handles->t == T_LIST) {
        V *hv = handles->L[i];
        if (!hv || hv->t != T_INT) return -1;
        *out_h = (int)hv->j;
        return 0;
    }
    if (handles->t == T_IVEC) {
        *out_h = (int)handles->J[i];
        return 0;
    }
    return -1;
}

V *bi_ipc_poll(V **a, int n) {
    P(n < 2 || (a[0]->t != T_LIST && a[0]->t != T_IVEC), v_err("ipc_poll(handles, timeout_ms)"))
    int timeout;
    if (a[1]->t == T_INT)
        timeout = (int)a[1]->j;
    else if (a[1]->t == T_FLOAT)
        timeout = (int)a[1]->f;
    else
        return v_err("ipc_poll(handles, timeout_ms)");
    V *handles = a[0];
    int nh = (int)handles->n;
    if (nh <= 0) return v_list(0);

    struct pollfd pfds[IPC_MAX_HANDLES];
    int map[IPC_MAX_HANDLES];
    int np = 0;

#ifdef SHAKTI_HAVE_RDMA
    int rdma_fd = ipc_rdma_poll_fd();
    if (rdma_fd >= 0 && np < IPC_MAX_HANDLES) {
        pfds[np].fd = rdma_fd;
        pfds[np].events = POLLIN;
        pfds[np].revents = 0;
        map[np] = 0;
        np++;
    }
#endif

    for (int i = 0; i < nh; i++) {
        int h;
        if (ipc_handle_from_elem(handles, i, &h) != 0) continue;
        IpcHandle *s = ipc_slot(h);
        if (!s) continue;
#ifdef SHAKTI_HAVE_RDMA
        if (s->kind == IPC_KIND_RDMA_CONN) {
            if (ipc_rdma_has_message(s->rdma)) {
                V *out = v_list(0);
                v_list_append(out, v_int(h));
                return out;
            }
            continue;
        }
#endif
        if (s->fd < 0) continue;
        if (np >= IPC_MAX_HANDLES) break;
        pfds[np].fd = s->fd;
        pfds[np].events = POLLIN;
        pfds[np].revents = 0;
        map[np] = h;
        np++;
    }

    if (np == 0) return v_list(0);
    int rc = poll(pfds, (nfds_t)np, timeout);
    if (rc <= 0) return v_list(0);

    V *out = v_list(0);
    for (int i = 0; i < np; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        int h = map[i];
        if (h == 0) {
#ifdef SHAKTI_HAVE_RDMA
            for (int j = 0; j < nh; j++) {
                int rh;
                if (ipc_handle_from_elem(handles, j, &rh) != 0) continue;
                IpcHandle *s = ipc_slot(rh);
                if (s && s->kind == IPC_KIND_RDMA_CONN && ipc_rdma_has_message(s->rdma))
                    v_list_append(out, v_int(rh));
            }
#endif
        } else {
            v_list_append(out, v_int(h));
        }
    }
    return out;
}

V *bi_ipc_close(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_close(h)"))
    int h = (int)a[0]->j;
    P(h < 1 || h >= IPC_MAX_HANDLES || !g_handles[h].in_use, v_err("ipc_close: bad handle"))
    ipc_free_handle(h);
    return v_nil();
}

V *bi_ipc_shm_open(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_INT, v_err("ipc_shm_open(name, size)"))
#if !defined(__linux__) && !defined(__APPLE__)
    return v_err("ipc_shm_open: not supported on this platform");
#else
    size_t size = (size_t)a[1]->j;
    if (size == 0) return v_err("ipc_shm_open: size=0");
    int slot = -1;
    for (int i = 1; i < IPC_MAX_HANDLES; i++) {
        if (!g_shm[i].in_use) {
            slot = i;
            break;
        }
    }
    P(slot < 0, v_err("ipc_shm_open: no slots"))
    char name[256];
    snprintf(name, sizeof name, "/shakti_%s", a[0]->s);
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return v_err("ipc_shm_open: shm_open failed");
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        shm_unlink(name);
        return v_err("ipc_shm_open: ftruncate failed");
    }
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        shm_unlink(name);
        return v_err("ipc_shm_open: mmap failed");
    }
    g_shm[slot].in_use = 1;
    g_shm[slot].ptr = ptr;
    g_shm[slot].size = size;
    strncpy(g_shm[slot].name, name, sizeof g_shm[slot].name - 1);
    return v_int(slot);
#endif
}

V *bi_ipc_shm_close(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_shm_close(token)"))
    int slot = (int)a[0]->j;
    P(slot < 1 || slot >= IPC_MAX_HANDLES || !g_shm[slot].in_use, v_err("ipc_shm_close: bad token"))
#if defined(__linux__) || defined(__APPLE__)
    munmap(g_shm[slot].ptr, g_shm[slot].size);
    shm_unlink(g_shm[slot].name);
#endif
    memset(&g_shm[slot], 0, sizeof g_shm[slot]);
    return v_nil();
}

V *bi_ipc_rdma_available(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(ipc_rdma_available());
}
