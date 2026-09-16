#include "ipc_internal.h"
#include "shakti_internal.h"

static IpcHandle g_handles[IPC_MAX_HANDLES];
static IpcShmSlot g_shm[IPC_MAX_HANDLES];

#define IPC_RECV_REQ_ASYNC_MAX 64

static void ipc_conn_poison(IpcHandle *s) {
    if (!s) return;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->reply_fd >= 0) {
        close(s->reply_fd);
        s->reply_fd = -1;
    }
    ipc_rx_free(&s->rx);
    s->closed = 1;
}

#ifndef SHAKTI_HAVE_RDMA
int ipc_rdma_available(void) { return 0; }
#endif

int ipc_copy_cstr(char *dst, size_t cap, const char *src) {
    int n;
    if (!dst || cap == 0) return -1;
    n = snprintf(dst, cap, "%s", src ? src : "");
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

uint32_t ipc_rand_corr_seed(void) {
    uint32_t v = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        (void)read(fd, &v, sizeof v);
        close(fd);
    }
    if (v == 0) v = ((uint32_t)getpid() << 16) ^ (uint32_t)time(NULL);
    if (v == 0) v = 0xA5A5u;
    return v;
}

int ipc_valid_shm_user_name(const char *s) {
    if (!s || !s[0]) return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

size_t ipc_posix_shm_name_max(void) {
#if defined(__APPLE__) && defined(PSHMNAMLEN)
    return (size_t)PSHMNAMLEN;
#else
    return 255;
#endif
}

int ipc_format_posix_shm_name(char *dst, size_t cap, const char *prefix, const char *user) {
    int nn;
    if (!dst || !prefix || !user || cap == 0) return -1;
    nn = snprintf(dst, cap, "%s%s", prefix, user);
    if (nn < 0 || (size_t)nn >= cap) return -1;
    if ((size_t)nn > ipc_posix_shm_name_max()) return -1;
    return 0;
}

static void ipc_set_cloexec(int fd) {
    int fl;
    if (fd < 0) return;
    fl = fcntl(fd, F_GETFD);
    if (fl >= 0)
        (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}

IpcHandle *ipc_handle_at(int h) {
    if (h < 1 || h >= IPC_MAX_HANDLES) return NULL;
    IpcHandle *s = &g_handles[h];
    return s->in_use ? s : NULL;
}

IpcHandle *ipc_slot(int h) {
    IpcHandle *s = ipc_handle_at(h);
    return (s && !s->closed) ? s : NULL;
}

int ipc_alloc(void) {
    for (int i = 1; i < IPC_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            memset(&g_handles[i], 0, sizeof g_handles[i]);
            g_handles[i].in_use = 1;
            g_handles[i].fd = -1;
            g_handles[i].reply_fd = -1;
            g_handles[i].next_corr = ipc_rand_corr_seed();
            if (g_handles[i].next_corr == 0) g_handles[i].next_corr = 1;
            return i;
        }
    }
    return -1;
}

void ipc_rx_free(IpcRxBuf *rx) {
    free(rx->data);
    rx->data = NULL;
    rx->cap = rx->len = 0;
    rx->have_len = 0;
    rx->msg_len = 0;
}

void ipc_inbox_free(IpcInbox *box) {
    for (int i = 0; i < box->n; i++) free(box->msgs[i].data);
    box->n = 0;
}

int ipc_inbox_push(IpcInbox *box, uint8_t kind, uint32_t corr, const unsigned char *data, size_t len) {
    if (box->n >= IPC_INBOX_MAX) return -1;
    unsigned char *copy = malloc(len ? len : 1);
    if (!copy) return -1;
    if (len) memcpy(copy, data, len);
    box->msgs[box->n].data = copy;
    box->msgs[box->n].len = len;
    box->msgs[box->n].kind = kind;
    box->msgs[box->n].corr_id = corr;
    box->n++;
    return 0;
}

int ipc_inbox_take(IpcInbox *box, int want_corr, uint32_t corr,
                   uint8_t *kind, uint32_t *corr_out,
                   unsigned char **data, size_t *len) {
    for (int i = 0; i < box->n; i++) {
        if (want_corr && !(box->msgs[i].kind == IPC_ENV_REP && box->msgs[i].corr_id == corr))
            continue;
        *kind = box->msgs[i].kind;
        *corr_out = box->msgs[i].corr_id;
        *data = box->msgs[i].data;
        *len = box->msgs[i].len;
        for (int j = i; j < box->n - 1; j++) box->msgs[j] = box->msgs[j + 1];
        box->n--;
        return 0;
    }
    return -1;
}

void ipc_free_handle(int h) {
    IpcHandle *s = &g_handles[h];
    if (!s->in_use) return;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->reply_fd >= 0) {
        close(s->reply_fd);
        s->reply_fd = -1;
    }
    if (s->kind == IPC_KIND_SOCK_LISTEN && s->uds_path[0])
        unlink(s->uds_path);
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
    if (s->kind == IPC_KIND_SHM_CHAN && s->shm_ptr) {
        if (s->shm_bcast && s->shm_side >= 0) {
            IpcShmBcastHdr *bh = (IpcShmBcastHdr *)s->shm_ptr;
            if (bh->magic == IPC_SHM_MAGIC_BCAST && (uint32_t)s->shm_side < bh->max_readers) {
                atomic_uint *actives = (atomic_uint *)((uint8_t *)s->shm_ptr + IPC_SHM_HDR + bh->capacity);
                atomic_store(&actives[s->shm_side], 0);
            }
        } else if (!s->shm_bcast && s->shm_side == 1) {
            IpcShmHdr *hdr = (IpcShmHdr *)s->shm_ptr;
            if (hdr->magic == IPC_SHM_MAGIC)
                atomic_store_explicit(&hdr->side_b_claimed, 0, memory_order_release);
        }
        munmap(s->shm_ptr, s->shm_size);
        if (s->shm_owner && s->shm_name[0]) shm_unlink(s->shm_name);
        s->shm_ptr = NULL;
    }
#endif
#ifdef SHAKTI_HAVE_RDMA
    if (s->rdma) {
        ipc_rdma_close(s->rdma);
        s->rdma = NULL;
    }
#endif
    ipc_rx_free(&s->rx);
    ipc_inbox_free(&s->inbox);
    memset(s, 0, sizeof *s);
}

int sock_set_block(int fd, int block) {
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

#ifdef _WIN32
static int sock_write_full(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, (const char *)buf + off, n - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}
#endif

static int sock_writev_all(int fd, struct iovec *iov, int iovcnt) {
#if !defined(_WIN32)
    while (iovcnt > 0) {
        ssize_t w = writev(fd, iov, iovcnt);
        if (w <= 0) return -1;
        size_t wrote = (size_t)w;
        while (iovcnt > 0 && wrote >= (size_t)iov[0].iov_len) {
            wrote -= iov[0].iov_len;
            iov++;
            iovcnt--;
        }
        if (iovcnt > 0 && wrote > 0) {
            iov[0].iov_base = (char *)iov[0].iov_base + wrote;
            iov[0].iov_len -= wrote;
        }
    }
    return 0;
#else
    (void)fd; (void)iov; (void)iovcnt;
    return -1;
#endif
}

static int sock_write_frame(int fd, const void *hdr, size_t hdr_len, const void *data, size_t data_len) {
#if !defined(_WIN32)
    struct iovec iov[2];
    iov[0].iov_base = (void *)hdr;
    iov[0].iov_len = hdr_len;
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = data_len;
    return sock_writev_all(fd, iov, data_len ? 2 : 1);
#else
    return sock_write_full(fd, hdr, hdr_len) || sock_write_full(fd, data, data_len);
#endif
}

int ipc_is_localhost(const char *host) {
    if (!host || !host[0]) return 1;
    return !strcmp(host, "127.0.0.1") || !strcmp(host, "localhost") || !strcmp(host, "::1");
}

static int ipc_uds_path(int port, char *out, size_t cap) {
    const char *dir = getenv("SHAKTI_IPC_DIR");
    int n;
    if (!dir || !dir[0]) dir = "/tmp";
    n = snprintf(out, cap, "%s/shakti-%d.sock", dir, port);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

IpcTransport ipc_parse_transport(const char *s) {
    const char *env = getenv("SHAKTI_IPC_TRANSPORT");
    if (!s || !s[0]) s = env;
    if (!s || !s[0] || !strcmp(s, "auto")) return IPC_TR_AUTO;
    if (!strcmp(s, "tcp")) return IPC_TR_TCP;
    if (!strcmp(s, "uds") || !strcmp(s, "unix")) return IPC_TR_UDS;
    if (!strcmp(s, "rdma")) return IPC_TR_RDMA;
    if (!strcmp(s, "udp")) return IPC_TR_UDP;
    if (!strcmp(s, "mcast") || !strcmp(s, "multicast")) return IPC_TR_MCAST;
    return IPC_TR_AUTO;
}

static int sock_listen_tcp(const char *host, int port, char *err, size_t err_cap) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: socket: %s", strerror(errno));
        return -1;
    }
    ipc_set_cloexec(s);
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
    } else {
        const char *allow = getenv("SHAKTI_IPC_ALLOW_PUBLIC");
        if (!allow || strcmp(allow, "1") != 0) {
            snprintf(err, err_cap,
                     "ipc: non-loopback bind requires SHAKTI_IPC_ALLOW_PUBLIC=1");
            close(s);
            return -1;
        }
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
    if (ipc_uds_path(port, path_out, 108) != 0) {
        snprintf(err, err_cap, "ipc: uds path too long");
        return -1;
    }
    unlink(path_out);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: uds socket: %s", strerror(errno));
        return -1;
    }
    ipc_set_cloexec(s);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (ipc_copy_cstr(addr.sun_path, sizeof addr.sun_path, path_out) != 0) {
        snprintf(err, err_cap, "ipc: uds path too long");
        close(s);
        return -1;
    }
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
    ipc_set_cloexec(s);
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
    if (ipc_uds_path(port, path, sizeof path) != 0) {
        snprintf(err, err_cap, "ipc: uds path too long");
        return -1;
    }
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(err, err_cap, "ipc: uds socket: %s", strerror(errno));
        return -1;
    }
    ipc_set_cloexec(s);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (ipc_copy_cstr(addr.sun_path, sizeof addr.sun_path, path) != 0) {
        snprintf(err, err_cap, "ipc: uds path too long");
        close(s);
        return -1;
    }
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

int ipc_sock_send(IpcHandle *s, const char *data, size_t len, char *err, size_t err_cap) {
    if (s->closed) {
        snprintf(err, err_cap, "ipc: send: connection closed");
        return -1;
    }
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

int ipc_sock_send_parts(IpcHandle *s,
                        const void *p0, size_t n0,
                        const void *p1, size_t n1,
                        const void *p2, size_t n2,
                        char *err, size_t err_cap) {
    if (s->closed) {
        snprintf(err, err_cap, "ipc: send: connection closed");
        return -1;
    }
    size_t total = n0 + n1 + n2;
    if (total > IPC_MAX_MSG) {
        snprintf(err, err_cap, "ipc: message too large (%zu)", total);
        return -1;
    }
    uint32_t be = htonl((uint32_t)total);
#if !defined(_WIN32)
    struct iovec iov[4];
    int iovcnt = 0;
    iov[iovcnt].iov_base = &be;
    iov[iovcnt].iov_len = 4;
    iovcnt++;
    if (n0) { iov[iovcnt].iov_base = (void *)p0; iov[iovcnt].iov_len = n0; iovcnt++; }
    if (n1) { iov[iovcnt].iov_base = (void *)p1; iov[iovcnt].iov_len = n1; iovcnt++; }
    if (n2) { iov[iovcnt].iov_base = (void *)p2; iov[iovcnt].iov_len = n2; iovcnt++; }
    if (sock_writev_all(s->fd, iov, iovcnt) != 0) {
        snprintf(err, err_cap, "ipc: send: %s", strerror(errno));
        return -1;
    }
    return 0;
#else
    if (sock_write_full(s->fd, &be, 4) ||
        (n0 && sock_write_full(s->fd, p0, n0)) ||
        (n1 && sock_write_full(s->fd, p1, n1)) ||
        (n2 && sock_write_full(s->fd, p2, n2))) {
        snprintf(err, err_cap, "ipc: send: %s", strerror(errno));
        return -1;
    }
    return 0;
#endif
}

int ipc_sock_recv_msg(IpcHandle *s, int block, char **out, size_t *out_len, char *err, size_t err_cap) {
    IpcRxBuf *rx = &s->rx;
    if (s->closed) {
        snprintf(err, err_cap, "ipc: recv: connection closed");
        return -1;
    }
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
        ipc_conn_poison(s);
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
    if (rx->len < rx->msg_len || rx->cap < rx->msg_len) {
        snprintf(err, err_cap, "ipc: internal framing error");
        return -1;
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

void ipc_env_pack(unsigned char hdr[IPC_ENV_HDR], uint8_t kind, uint32_t corr) {
    hdr[0] = (unsigned char)IPC_ENV_MAGIC;
    hdr[1] = kind;
    uint32_t be = htonl(corr);
    memcpy(hdr + 2, &be, 4);
    hdr[6] = 0;
    hdr[7] = 0;
}

int ipc_env_parse(const unsigned char *buf, size_t len, uint8_t *kind, uint32_t *corr,
                  const unsigned char **payload, size_t *payload_len) {
    if (len < IPC_ENV_HDR || buf[0] != IPC_ENV_MAGIC) return -1;
    *kind = buf[1];
    if (*kind != IPC_ENV_ASYNC && *kind != IPC_ENV_REQ && *kind != IPC_ENV_REP) return -1;
    uint32_t be;
    memcpy(&be, buf + 2, 4);
    *corr = ntohl(be);
    *payload = buf + IPC_ENV_HDR;
    *payload_len = len - IPC_ENV_HDR;
    return 0;
}

V *ipc_u8vec_from_raw(const unsigned char *p, size_t n) {
    V *v = v_cvec((int64_t)n);
    if (n && p && v->B) memcpy(v->B, p, n);
    return v;
}

V *ipc_u8vec_take(unsigned char *p, size_t n) {
    V *v = v_alloc(T_CVEC);
    v->n = (int64_t)n;
    if (!p) {
        v->B = x_malloc(1, "ipc");
        v->B[0] = 0;
    } else {
        v->B = p;
    }
    return v;
}

int ipc_payload_bytes(V *v, const unsigned char **out, size_t *out_len) {
    if (!v || !out || !out_len) return -1;
    if (v->t == T_STR) {
        *out = (const unsigned char *)(v->s ? v->s : "");
        *out_len = v->s ? strlen(v->s) : 0;
        return 0;
    }
    if (v->t == T_CVEC) {
        if (v->n < 0) return -1;
        *out_len = (size_t)v->n;
        *out = (*out_len && v->B) ? v->B : (const unsigned char *)"";
        return 0;
    }
    return -1;
}

V *ipc_msg_dict(uint8_t kind, uint32_t corr, const unsigned char *data, size_t len, int as_bin) {
    V *d = v_dict_empty();
    const char *ks = "async";
    if (kind == IPC_ENV_REQ) ks = "req";
    else if (kind == IPC_ENV_REP) ks = "rep";
    v_dict_put(d, "kind", v_str(ks));
    v_dict_put(d, "corr_id", v_int((int64_t)corr));
    if (as_bin) {
        v_dict_put(d, "data", ipc_u8vec_from_raw(data, len));
    } else {
        char *s = malloc(len + 1);
        if (!s) {
            v_dict_put(d, "data", v_str(""));
        } else {
            if (len) memcpy(s, data, len);
            s[len] = 0;
            v_dict_put(d, "data", v_str_take(s));
        }
    }
    return d;
}

int ipc_raw_send(IpcHandle *s, const unsigned char *data, size_t len, char *err, size_t err_cap) {
#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN)
        return ipc_rdma_send(s->rdma, data, len, err, err_cap);
#endif
    if (s->kind == IPC_KIND_SOCK_CONN)
        return ipc_sock_send(s, (const char *)data, len, err, err_cap);
    if (s->kind == IPC_KIND_MCAST)
        return ipc_mcast_publish(s, data, len, err, err_cap);
    if (s->kind == IPC_KIND_SHM_CHAN)
        return ipc_shm_send_side(s, data, len, err, err_cap);
    snprintf(err, err_cap, "ipc: send: unsupported handle");
    return -1;
}

int ipc_raw_recv(IpcHandle *s, int block, int timeout_ms,
                 char **out, size_t *out_len, char *err, size_t err_cap) {
#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN)
        return ipc_rdma_recv(s->rdma, block, out, out_len, err, err_cap);
#endif
    if (s->kind == IPC_KIND_SOCK_CONN) {
        if (timeout_ms >= 0 && s->fd >= 0) {
            struct pollfd pfd = {.fd = s->fd, .events = POLLIN, .revents = 0};
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr == 0) return -2;
            if (pr < 0) {
                snprintf(err, err_cap, "ipc: recv: poll failed");
                return -1;
            }
            block = 1;
        }
        return ipc_sock_recv_msg(s, block, out, out_len, err, err_cap);
    }
    if (s->kind == IPC_KIND_MCAST)
        return ipc_mcast_recv(s, block, out, out_len, NULL, NULL, err, err_cap);
    if (s->kind == IPC_KIND_SHM_CHAN)
        return ipc_shm_recv_side(s, block, timeout_ms, out, out_len, err, err_cap);
    snprintf(err, err_cap, "ipc: recv: unsupported handle");
    return -1;
}

int ipc_env_send_ex(IpcHandle *s, uint8_t kind, uint32_t corr,
                    const unsigned char *extra, size_t elen,
                    const unsigned char *payload, size_t plen, char *err, size_t err_cap) {
    if (plen + elen > IPC_MAX_MSG - IPC_ENV_HDR) {
        snprintf(err, err_cap, "ipc: message too large");
        return -1;
    }
    unsigned char env[IPC_ENV_HDR];
    ipc_env_pack(env, kind, corr);
    if (s->kind == IPC_KIND_SOCK_CONN) {
        if (elen)
            return ipc_sock_send_parts(s, env, IPC_ENV_HDR, extra, elen, payload, plen, err, err_cap);
        return ipc_sock_send_parts(s, env, IPC_ENV_HDR, payload, plen, NULL, 0, err, err_cap);
    }
    size_t total = IPC_ENV_HDR + elen + plen;
    unsigned char stack[4096];
    unsigned char *buf = stack;
    int heap = 0;
    if (total > sizeof stack) {
        buf = malloc(total);
        if (!buf) {
            snprintf(err, err_cap, "ipc: oom");
            return -1;
        }
        heap = 1;
    }
    memcpy(buf, env, IPC_ENV_HDR);
    if (elen) memcpy(buf + IPC_ENV_HDR, extra, elen);
    if (plen) memcpy(buf + IPC_ENV_HDR + elen, payload, plen);
    int rc = ipc_raw_send(s, buf, total, err, err_cap);
    if (heap) free(buf);
    return rc;
}

int ipc_env_send(IpcHandle *s, uint8_t kind, uint32_t corr,
                 const unsigned char *payload, size_t plen, char *err, size_t err_cap) {
    return ipc_env_send_ex(s, kind, corr, NULL, 0, payload, plen, err, err_cap);
}

int ipc_env_recv_ex(IpcHandle *s, int block, int timeout_ms, int want_corr, uint32_t corr,
                    int use_inbox,
                    uint8_t *kind, uint32_t *corr_out,
                    unsigned char **data, size_t *len, char *err, size_t err_cap) {
    if (use_inbox && ipc_inbox_take(&s->inbox, want_corr, corr, kind, corr_out, data, len) == 0)
        return 0;

    struct timespec start;
    int have_start = 0;

    for (;;) {
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            if (!have_start) {
                clock_gettime(CLOCK_MONOTONIC, &start);
                have_start = 1;
                wait_ms = timeout_ms;
            } else {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed = (now.tv_sec - start.tv_sec) * 1000L +
                               (now.tv_nsec - start.tv_nsec) / 1000000L;
                if (elapsed >= timeout_ms) return -2;
                wait_ms = (int)(timeout_ms - elapsed);
            }
        } else if (!block) {
            wait_ms = 0;
        }

        int use_block = block;
        if (wait_ms == 0) use_block = 0;

        char *raw = NULL;
        size_t raw_len = 0;
        int rc = ipc_raw_recv(s, use_block, wait_ms, &raw, &raw_len, err, err_cap);
        if (rc == -2) return -2;
        if (rc != 0) return rc;

        uint8_t k;
        uint32_t c;
        const unsigned char *pay;
        size_t plen;
        if (ipc_env_parse((const unsigned char *)raw, raw_len, &k, &c, &pay, &plen) != 0) {
            k = IPC_ENV_ASYNC;
            c = 0;
            pay = (const unsigned char *)raw;
            plen = raw_len;
        }

        if (want_corr) {
            if (k == IPC_ENV_REP && c == corr) {
                if (plen && pay != (const unsigned char *)raw)
                    memmove(raw, pay, plen);
                *kind = k;
                *corr_out = c;
                *data = (unsigned char *)raw;
                *len = plen;
                return 0;
            }
            if (ipc_inbox_push(&s->inbox, k, c, pay, plen) != 0) {
                free(raw);
                snprintf(err, err_cap, "ipc: inbox full");
                return -1;
            }
            free(raw);
            continue;
        }

        if (plen && pay != (const unsigned char *)raw)
            memmove(raw, pay, plen);
        *kind = k;
        *corr_out = c;
        *data = (unsigned char *)raw;
        *len = plen;
        return 0;
    }
}

int ipc_env_recv(IpcHandle *s, int block, int timeout_ms, int want_corr, uint32_t corr,
                 uint8_t *kind, uint32_t *corr_out,
                 unsigned char **data, size_t *len, char *err, size_t err_cap) {
    return ipc_env_recv_ex(s, block, timeout_ms, want_corr, corr, 1,
                           kind, corr_out, data, len, err, err_cap);
}

static V *ipc_v_str(char *msg, size_t len) {
    char *z = malloc(len + 1);
    if (!z) {
        free(msg);
        return v_err("ipc_recv: oom");
    }
    if (len) memcpy(z, msg, len);
    z[len] = 0;
    free(msg);
    V *r = v_str(z);
    free(z);
    return r;
}

/* ---- builtins ---- */

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
        if (host && host[0] && !ipc_is_localhost(host)) {
            const char *allow = getenv("SHAKTI_IPC_ALLOW_PUBLIC");
            if (!allow || strcmp(allow, "1") != 0)
                return v_err("ipc: non-loopback bind requires SHAKTI_IPC_ALLOW_PUBLIC=1");
        }
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
    if (uds_path[0] && ipc_copy_cstr(g_handles[h].uds_path, sizeof g_handles[h].uds_path, uds_path) != 0) {
        ipc_free_handle(h);
        unlink(uds_path);
        return v_err("ipc_listen: uds path too long");
    }
    return v_int(h);
}

V *bi_ipc_accept(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_accept(listen_h)"))
    IpcHandle *ls = ipc_slot((int)a[0]->j);
    P(!ls, v_err("ipc_accept: bad handle"))

#ifdef SHAKTI_HAVE_RDMA
    char err[512];
    err[0] = 0;
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
    ipc_set_cloexec(cfd);
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

V *bi_ipc_join(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_INT, v_err("ipc_join(group, port[, iface[, source]])"))
    const char *group = a[0]->s;
    int port = (int)a[1]->j;
    const char *iface = (n > 2 && a[2]->t == T_STR) ? a[2]->s : NULL;
    const char *source = (n > 3 && a[3]->t == T_STR) ? a[3]->s : NULL;
    char err[512];
    err[0] = 0;
    int af = 0;
    struct sockaddr_storage grp;
    socklen_t grp_len = 0;
    int fd = ipc_mcast_open(group, port, iface, source, &af, &grp, &grp_len, err, sizeof err);
    if (fd < 0) return v_err(err[0] ? err : "ipc_join failed");
    uint16_t rport = 0;
    int rfd = ipc_mcast_make_reply_sock(af, &rport, err, sizeof err);
    if (rfd < 0) {
        close(fd);
        return v_err(err[0] ? err : "ipc_join: reply sock failed");
    }
    int h = ipc_alloc();
    if (h < 0) {
        close(fd);
        close(rfd);
        return v_err("ipc_join: no handles");
    }
    g_handles[h].kind = IPC_KIND_MCAST;
    g_handles[h].fd = fd;
    g_handles[h].reply_fd = rfd;
    g_handles[h].reply_port = rport;
    g_handles[h].mcast_af = af;
    g_handles[h].mcast_addr = grp;
    g_handles[h].mcast_addrlen = grp_len;
    g_handles[h].mcast_addr_ok = 1;
    if (source && source[0])
        ipc_copy_cstr(g_handles[h].ssm_source, sizeof g_handles[h].ssm_source, source);
    return v_int(h);
}

V *bi_ipc_publish(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT, v_err("ipc_publish(h, str|list[char])"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_publish: bad handle"))
    P(s->kind != IPC_KIND_MCAST, v_err("ipc_publish: not a mcast handle"))
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[1], &data, &len) != 0, v_err("ipc_publish: data must be str or list[char]"))
    char err[512];
    err[0] = 0;
    P(ipc_mcast_publish(s, data, len, err, sizeof err) != 0, v_err(err[0] ? err : "ipc_publish failed"))
    return v_nil();
}

V *bi_ipc_send(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT, v_err("ipc_send(h, str|list[char])"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_send: bad handle"))
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[1], &data, &len) != 0, v_err("ipc_send: data must be str or list[char]"))
    char err[512];
    err[0] = 0;
    if (s->kind == IPC_KIND_MCAST || s->kind == IPC_KIND_SHM_CHAN) {
        P(ipc_env_send(s, IPC_ENV_ASYNC, 0, data, len, err, sizeof err) != 0,
          v_err(err[0] ? err : "ipc_send failed"))
        return v_nil();
    }
#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN) {
        P(ipc_rdma_send(s->rdma, data, len, err, sizeof err) != 0,
          v_err(err[0] ? err : "ipc_send failed"))
        return v_nil();
    }
#endif
    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_send: not a connection"))
    P(ipc_sock_send(s, (const char *)data, len, err, sizeof err) != 0,
      v_err(err[0] ? err : "ipc_send failed"))
    return v_nil();
}

V *bi_ipc_send_async(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT, v_err("ipc_send_async(h, str|list[char])"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_send_async: bad handle"))
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[1], &data, &len) != 0, v_err("ipc_send_async: data must be str or list[char]"))
    char err[512];
    err[0] = 0;
    P(s->kind != IPC_KIND_SOCK_CONN && s->kind != IPC_KIND_SHM_CHAN && s->kind != IPC_KIND_MCAST
#ifdef SHAKTI_HAVE_RDMA
          && s->kind != IPC_KIND_RDMA_CONN
#endif
      ,
      v_err("ipc_send_async: unsupported handle"))
    P(ipc_env_send(s, IPC_ENV_ASYNC, 0, data, len, err, sizeof err) != 0,
      v_err(err[0] ? err : "ipc_send_async failed"))
    return v_nil();
}

V *bi_ipc_send_sync(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT, v_err("ipc_send_sync(h, msg[, timeout_ms])"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_send_sync: bad handle"))
    P(s->kind != IPC_KIND_SOCK_CONN && s->kind != IPC_KIND_SHM_CHAN && s->kind != IPC_KIND_MCAST
#ifdef SHAKTI_HAVE_RDMA
          && s->kind != IPC_KIND_RDMA_CONN
#endif
      ,
      v_err("ipc_send_sync: unsupported handle"))
    if (s->kind == IPC_KIND_SHM_CHAN && s->shm_bcast)
        return v_err("ipc_send_sync: not supported on shm broadcast");
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[1], &data, &len) != 0, v_err("ipc_send_sync: data must be str or list[char]"))
    int timeout_ms = 5000;
    if (n > 2) {
        if (a[2]->t == T_INT) timeout_ms = (int)a[2]->j;
        else if (a[2]->t == T_FLOAT) timeout_ms = (int)a[2]->f;
        else return v_err("ipc_send_sync: bad timeout");
    }
    uint32_t corr = s->next_corr++;
    if (corr == 0) corr = s->next_corr++;
    char err[512];
    err[0] = 0;

    if (s->kind == IPC_KIND_MCAST)
        return v_err("ipc_send_sync: not supported on multicast");

    if (ipc_env_send(s, IPC_ENV_REQ, corr, data, len, err, sizeof err) != 0)
        return v_err(err[0] ? err : "ipc_send_sync: send failed");
    uint8_t kind;
    uint32_t got_corr;
    unsigned char *rep = NULL;
    size_t rep_len = 0;
    int rc = ipc_env_recv(s, 1, timeout_ms, 1, corr, &kind, &got_corr, &rep, &rep_len, err, sizeof err);
    if (rc == -2) return v_err("ipc_send_sync: timeout");
    if (rc != 0) return v_err(err[0] ? err : "ipc_send_sync: recv failed");
    return ipc_u8vec_take(rep, rep_len);
}

V *bi_ipc_reply(V **a, int n) {
    P(n < 3 || a[0]->t != T_INT || a[1]->t != T_INT, v_err("ipc_reply(h, corr_id, msg)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_reply: bad handle"))
    P(s->kind != IPC_KIND_SOCK_CONN && s->kind != IPC_KIND_SHM_CHAN && s->kind != IPC_KIND_MCAST
#ifdef SHAKTI_HAVE_RDMA
          && s->kind != IPC_KIND_RDMA_CONN
#endif
      ,
      v_err("ipc_reply: unsupported handle"))
    uint32_t corr = (uint32_t)a[1]->j;
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[2], &data, &len) != 0, v_err("ipc_reply: data must be str or list[char]"))
    char err[512];
    err[0] = 0;

    if (s->kind == IPC_KIND_MCAST) {
        int af;
        uint16_t port;
        uint8_t addr[16];
        uint8_t cookie[IPC_REPLY_COOKIE];
        if (ipc_reply_to_take(s, corr, &af, &port, addr, cookie) != 0)
            return v_err("ipc_reply: unknown corr_id (no reply-to)");
        size_t total = IPC_ENV_HDR + IPC_REPLY_COOKIE + len;
        unsigned char *buf = malloc(total);
        if (!buf) return v_err("ipc_reply: oom");
        ipc_env_pack(buf, IPC_ENV_REP, corr);
        memcpy(buf + IPC_ENV_HDR, cookie, IPC_REPLY_COOKIE);
        if (len) memcpy(buf + IPC_ENV_HDR + IPC_REPLY_COOKIE, data, len);
        struct sockaddr_storage dest;
        socklen_t dlen;
        memset(&dest, 0, sizeof dest);
        if (af == AF_INET6) {
            struct sockaddr_in6 *d = (struct sockaddr_in6 *)&dest;
            d->sin6_family = AF_INET6;
            d->sin6_port = htons(port);
            memcpy(&d->sin6_addr, addr, 16);
            dlen = sizeof *d;
        } else {
            struct sockaddr_in *d = (struct sockaddr_in *)&dest;
            d->sin_family = AF_INET;
            d->sin_port = htons(port);
            memcpy(&d->sin_addr, addr, 4);
            dlen = sizeof *d;
        }
        ssize_t wn = sendto(s->reply_fd >= 0 ? s->reply_fd : s->fd, buf, total, 0,
                            (struct sockaddr *)&dest, dlen);
        free(buf);
        if (wn < 0 || (size_t)wn != total) return v_err("ipc_reply: sendto failed");
        return v_nil();
    }

    P(ipc_env_send(s, IPC_ENV_REP, corr, data, len, err, sizeof err) != 0,
      v_err(err[0] ? err : "ipc_reply failed"))
    return v_nil();
}

static V *ipc_mcast_recv_msg_body(IpcHandle *s, int block) {
    uint8_t kind0;
    uint32_t corr0;
    unsigned char *data0 = NULL;
    size_t len0 = 0;
    if (ipc_inbox_take(&s->inbox, 0, 0, &kind0, &corr0, &data0, &len0) == 0) {
        V *d = ipc_msg_dict(kind0, corr0, data0, len0, 1);
        free(data0);
        return d;
    }
    char err[512];
    err[0] = 0;
    char *msg = NULL;
    size_t len = 0;
    struct sockaddr_storage peer;
    socklen_t peer_len = 0;
    int rc = ipc_mcast_recv(s, block, &msg, &len, &peer, &peer_len, err, sizeof err);
    if (rc == -2) return v_nil();
    P(rc != 0, v_err(err[0] ? err : "ipc_recv_msg failed"))
    uint8_t kind;
    uint32_t corr;
    const unsigned char *pay;
    size_t plen;
    if (ipc_env_parse((unsigned char *)msg, len, &kind, &corr, &pay, &plen) == 0) {
        if (kind == IPC_ENV_REQ && plen >= IPC_REPLY_INFO) {
            uint8_t cookie[IPC_REPLY_COOKIE];
            memcpy(cookie, pay + 3, IPC_REPLY_COOKIE);
            if (ipc_reply_to_store_peer(s, corr, &peer, cookie) != 0) {
                free(msg);
                return v_err("ipc_recv_msg: reply-to table full");
            }
            V *d = ipc_msg_dict(kind, corr, pay + IPC_REPLY_INFO, plen - IPC_REPLY_INFO, 1);
            free(msg);
            return d;
        }
        V *d = ipc_msg_dict(kind, corr, pay, plen, 1);
        free(msg);
        return d;
    }
    V *d = ipc_msg_dict(IPC_ENV_ASYNC, 0, (unsigned char *)msg, len, 1);
    free(msg);
    return d;
}

static V *ipc_do_recv_msg(IpcHandle *s, int block) {
    char err[512];
    err[0] = 0;
    uint8_t kind;
    uint32_t corr;
    unsigned char *data = NULL;
    size_t len = 0;
    int rc = ipc_env_recv(s, block, block ? -1 : 0, 0, 0, &kind, &corr, &data, &len, err, sizeof err);
    if (rc == -2) return v_nil();
    P(rc != 0, v_err(err[0] ? err : "ipc_recv_msg failed"))
    V *d = ipc_msg_dict(kind, corr, data, len, 1);
    free(data);
    return d;
}

static V *ipc_do_recv_req(IpcHandle *s, int block) {
    char err[512];
    err[0] = 0;
    int async_drops = 0;
    for (;;) {
        {
            IpcInbox *box = &s->inbox;
            for (int i = 0; i < box->n; i++) {
                if (box->msgs[i].kind != IPC_ENV_REQ) continue;
                uint32_t corr = box->msgs[i].corr_id;
                unsigned char *data = box->msgs[i].data;
                size_t len = box->msgs[i].len;
                for (int j = i; j + 1 < box->n; j++) box->msgs[j] = box->msgs[j + 1];
                box->n--;
                V *lst = v_list(2);
                lst->L[0] = v_int((int64_t)corr);
                lst->L[1] = ipc_u8vec_take(data, len);
                return lst;
            }
        }
        uint8_t kind;
        uint32_t corr;
        unsigned char *data = NULL;
        size_t len = 0;
        int rc = ipc_env_recv_ex(s, block, block ? -1 : 0, 0, 0, 0, &kind, &corr, &data, &len, err, sizeof err);
        if (rc == -2) return v_nil();
        P(rc != 0, v_err(err[0] ? err : "ipc_recv_req failed"))
        if (kind == IPC_ENV_REQ) {
            V *lst = v_list(2);
            lst->L[0] = v_int((int64_t)corr);
            lst->L[1] = ipc_u8vec_take(data, len);
            return lst;
        }
        if (kind == IPC_ENV_ASYNC) {
            free(data);
            if (++async_drops > IPC_RECV_REQ_ASYNC_MAX) {
                ipc_conn_poison(s);
                return v_err("ipc_recv_req: async flood");
            }
            continue;
        }
        if (ipc_inbox_push(&s->inbox, kind, corr, data, len) != 0) {
            free(data);
            return v_err("ipc_recv_req: inbox full");
        }
        free(data);
    }
}

V *bi_ipc_recv_msg(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_msg(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_msg: bad handle"))
    if (s->kind == IPC_KIND_MCAST)
        return ipc_mcast_recv_msg_body(s, 1);
    P(s->kind != IPC_KIND_SOCK_CONN && s->kind != IPC_KIND_SHM_CHAN
#ifdef SHAKTI_HAVE_RDMA
          && s->kind != IPC_KIND_RDMA_CONN
#endif
      ,
      v_err("ipc_recv_msg: unsupported handle"))
    return ipc_do_recv_msg(s, 1);
}

V *bi_ipc_recv_msg_nowait(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_msg_nowait(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_msg_nowait: bad handle"))
    if (s->kind == IPC_KIND_MCAST)
        return ipc_mcast_recv_msg_body(s, 0);
    P(s->kind != IPC_KIND_SOCK_CONN && s->kind != IPC_KIND_SHM_CHAN
#ifdef SHAKTI_HAVE_RDMA
          && s->kind != IPC_KIND_RDMA_CONN
#endif
      ,
      v_err("ipc_recv_msg_nowait: unsupported handle"))
    return ipc_do_recv_msg(s, 0);
}

V *bi_ipc_recv_req(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_req(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_req: bad handle"))
    if (s->kind == IPC_KIND_MCAST) return v_err("ipc_recv_req: use recv_msg for multicast");
    return ipc_do_recv_req(s, 1);
}

V *bi_ipc_recv_req_nowait(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_req_nowait(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_req_nowait: bad handle"))
    if (s->kind == IPC_KIND_MCAST) return v_err("ipc_recv_req_nowait: use recv_msg for multicast");
    return ipc_do_recv_req(s, 0);
}

V *bi_ipc_send_n(V **a, int n) {
    P(n < 3 || a[0]->t != T_INT || a[2]->t != T_INT, v_err("ipc_send_n(h, msg, n)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_send_n: bad handle"))
    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_send_n: stream connection only"))
    const unsigned char *data = NULL;
    size_t len = 0;
    P(ipc_payload_bytes(a[1], &data, &len) != 0, v_err("ipc_send_n: data must be str or list[char]"))
    int64_t reps = a[2]->j;
    P(reps < 0, v_err("ipc_send_n: n must be >= 0"))
    if (reps > 1000000)
        return v_err("ipc_send_n: n too large (max 1000000)");
    if (len > 0 && (uint64_t)reps > (2ull << 30) / (uint64_t)len)
        return v_err("ipc_send_n: total bytes too large (max 2GiB)");
    char err[512];
    err[0] = 0;
    for (int64_t i = 0; i < reps; i++) {
        if (ipc_sock_send(s, (const char *)data, len, err, sizeof err) != 0)
            return v_err(err[0] ? err : "ipc_send_n failed");
    }
    return v_nil();
}

static V *ipc_do_recv(IpcHandle *s, int block) {
    char err[512];
    err[0] = 0;
    char *msg = NULL;
    size_t msg_len = 0;

    if (s->inbox.n > 0) {
        uint8_t kind;
        uint32_t corr;
        unsigned char *data = NULL;
        size_t len = 0;
        if (ipc_inbox_take(&s->inbox, 0, 0, &kind, &corr, &data, &len) == 0)
            return ipc_v_str((char *)data, len);
    }

    if (s->kind == IPC_KIND_MCAST) {
        int rc = ipc_mcast_recv(s, block, &msg, &msg_len, NULL, NULL, err, sizeof err);
        if (rc == -2) return v_str("");
        P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
        uint8_t kind;
        uint32_t corr;
        const unsigned char *pay;
        size_t plen;
        if (ipc_env_parse((unsigned char *)msg, msg_len, &kind, &corr, &pay, &plen) == 0) {
            if (kind == IPC_ENV_REQ && plen >= IPC_REPLY_INFO) {
                pay += IPC_REPLY_INFO;
                plen -= IPC_REPLY_INFO;
            }
            char *z = malloc(plen + 1);
            if (!z) {
                free(msg);
                return v_err("ipc_recv: oom");
            }
            if (plen) memcpy(z, pay, plen);
            z[plen] = 0;
            free(msg);
            V *r = v_str(z);
            free(z);
            return r;
        }
        return ipc_v_str(msg, msg_len);
    }

#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN) {
        int rc = ipc_rdma_recv(s->rdma, block, &msg, &msg_len, err, sizeof err);
        if (rc == -2) return v_str("");
        P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
        return ipc_v_str(msg, msg_len);
    }
#endif

    if (s->kind == IPC_KIND_SHM_CHAN) {
        uint8_t kind;
        uint32_t corr;
        unsigned char *data = NULL;
        size_t len = 0;
        int rc = ipc_env_recv(s, block, block ? -1 : 0, 0, 0, &kind, &corr, &data, &len, err, sizeof err);
        if (rc == -2) return v_str("");
        P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
        char *z = malloc(len + 1);
        if (!z) {
            free(data);
            return v_err("ipc_recv: oom");
        }
        if (len) memcpy(z, data, len);
        z[len] = 0;
        free(data);
        V *r = v_str(z);
        free(z);
        return r;
    }

    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_recv: not a connection"))
    int rc = ipc_sock_recv_msg(s, block, &msg, &msg_len, err, sizeof err);
    if (rc == -2) return v_str("");
    P(rc != 0, v_err(err[0] ? err : "ipc_recv failed"))
    return ipc_v_str(msg, msg_len);
}

static V *ipc_do_recv_bin(IpcHandle *s, int block) {
    char err[512];
    err[0] = 0;
    char *msg = NULL;
    size_t msg_len = 0;

    if (s->inbox.n > 0) {
        uint8_t kind;
        uint32_t corr;
        unsigned char *data = NULL;
        size_t len = 0;
        if (ipc_inbox_take(&s->inbox, 0, 0, &kind, &corr, &data, &len) == 0)
            return ipc_u8vec_take(data, len);
    }

    if (s->kind == IPC_KIND_MCAST) {
        struct sockaddr_storage peer;
        socklen_t peer_len = 0;
        int rc = ipc_mcast_recv(s, block, &msg, &msg_len, &peer, &peer_len, err, sizeof err);
        if (rc == -2) return v_cvec(0);
        P(rc != 0, v_err(err[0] ? err : "ipc_recv_bin failed"))
        uint8_t kind;
        uint32_t corr;
        const unsigned char *pay;
        size_t plen;
        if (ipc_env_parse((unsigned char *)msg, msg_len, &kind, &corr, &pay, &plen) == 0) {
            if (kind == IPC_ENV_REQ && plen >= IPC_REPLY_INFO) {
                uint8_t cookie[IPC_REPLY_COOKIE];
                memcpy(cookie, pay + 3, IPC_REPLY_COOKIE);
                (void)ipc_reply_to_store_peer(s, corr, &peer, cookie);
                pay += IPC_REPLY_INFO;
                plen -= IPC_REPLY_INFO;
            }
            V *r = ipc_u8vec_from_raw(pay, plen);
            free(msg);
            return r;
        }
        V *r = ipc_u8vec_from_raw((const unsigned char *)msg, msg_len);
        free(msg);
        return r;
    }

#ifdef SHAKTI_HAVE_RDMA
    if (s->kind == IPC_KIND_RDMA_CONN) {
        int rc = ipc_rdma_recv(s->rdma, block, &msg, &msg_len, err, sizeof err);
        if (rc == -2) return v_cvec(0);
        P(rc != 0, v_err(err[0] ? err : "ipc_recv_bin failed"))
        V *r = ipc_u8vec_from_raw((const unsigned char *)msg, msg_len);
        free(msg);
        return r;
    }
#endif

    if (s->kind == IPC_KIND_SHM_CHAN) {
        uint8_t kind;
        uint32_t corr;
        unsigned char *data = NULL;
        size_t len = 0;
        int rc = ipc_env_recv(s, block, block ? -1 : 0, 0, 0, &kind, &corr, &data, &len, err, sizeof err);
        if (rc == -2) return v_cvec(0);
        P(rc != 0, v_err(err[0] ? err : "ipc_recv_bin failed"))
        V *r = ipc_u8vec_from_raw(data, len);
        free(data);
        return r;
    }

    P(s->kind != IPC_KIND_SOCK_CONN, v_err("ipc_recv_bin: not a connection"))
    int rc = ipc_sock_recv_msg(s, block, &msg, &msg_len, err, sizeof err);
    if (rc == -2) return v_cvec(0);
    P(rc != 0, v_err(err[0] ? err : "ipc_recv_bin failed"))
    V *r = ipc_u8vec_from_raw((const unsigned char *)msg, msg_len);
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

V *bi_ipc_recv_bin(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_bin(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_bin: bad handle"))
    return ipc_do_recv_bin(s, 1);
}

V *bi_ipc_recv_nowait_bin(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_recv_nowait_bin(h)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_recv_nowait_bin: bad handle"))
    return ipc_do_recv_bin(s, 0);
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
    if (s->kind == IPC_KIND_SHM_CHAN) {
        s->nonblock = on;
        return v_nil();
    }
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

    V *ready = v_list(0);
    for (int i = 0; i < nh; i++) {
        int h;
        if (ipc_handle_from_elem(handles, i, &h) != 0) continue;
        IpcHandle *s = ipc_slot(h);
        if (!s) continue;
        if (s->inbox.n > 0) v_list_append_own(ready, v_int(h));
        else if (s->kind == IPC_KIND_SHM_CHAN && ipc_shm_readable(s))
            v_list_append_own(ready, v_int(h));
#ifdef SHAKTI_HAVE_RDMA
        else if (s->kind == IPC_KIND_RDMA_CONN && ipc_rdma_has_message(s->rdma))
            v_list_append_own(ready, v_int(h));
#endif
    }
    if (ready->n > 0) return ready;

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
        if (s->kind == IPC_KIND_SHM_CHAN) continue;
#ifdef SHAKTI_HAVE_RDMA
        if (s->kind == IPC_KIND_RDMA_CONN || s->kind == IPC_KIND_RDMA_LISTEN)
            continue;
#endif
        if (s->fd < 0) continue;
        if (np >= IPC_MAX_HANDLES) break;
        pfds[np].fd = s->fd;
        pfds[np].events = POLLIN;
        pfds[np].revents = 0;
        map[np] = h;
        np++;
    }

    if (np == 0) return ready;
    int rc = poll(pfds, (nfds_t)np, timeout);
    if (rc <= 0) return ready;

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
                    v_list_append_own(ready, v_int(rh));
            }
#endif
        } else {
            v_list_append_own(ready, v_int(h));
        }
    }
    return ready;
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
#if defined(SHAKTI_WASM) || (!defined(__linux__) && !defined(__APPLE__))
    return v_err("ipc_shm_open: not supported on this platform");
#else
    if (a[1]->j <= 0) return v_err("ipc_shm_open: size must be positive");
    size_t size = (size_t)a[1]->j;
    int slot = -1;
    for (int i = 1; i < IPC_MAX_HANDLES; i++) {
        if (!g_shm[i].in_use) {
            slot = i;
            break;
        }
    }
    P(slot < 0, v_err("ipc_shm_open: no slots"))
    P(!ipc_valid_shm_user_name(a[0]->s), v_err("ipc_shm_open: bad name"))
    char name[256];
    int nn = snprintf(name, sizeof name, "/shakti_%s", a[0]->s);
    P(nn < 0 || (size_t)nn >= sizeof name, v_err("ipc_shm_open: name too long"))
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
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
    if (ipc_copy_cstr(g_shm[slot].name, sizeof g_shm[slot].name, name) != 0) {
        munmap(ptr, size);
        shm_unlink(name);
        memset(&g_shm[slot], 0, sizeof g_shm[slot]);
        return v_err("ipc_shm_open: name too long");
    }
    return v_int(slot);
#endif
}

V *bi_ipc_shm_close(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("ipc_shm_close(token)"))
    int slot = (int)a[0]->j;
    P(slot < 1 || slot >= IPC_MAX_HANDLES || !g_shm[slot].in_use, v_err("ipc_shm_close: bad token"))
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
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

V *bi_ipc_shm_create(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_INT, v_err("ipc_shm_create(name, size)"))
    char err[512];
    err[0] = 0;
    void *ptr = NULL;
    size_t map_size = 0;
    char posix[256];
    int owner = 0;
    if (ipc_shm_chan_open(a[0]->s, (size_t)a[1]->j, 1, &ptr, &map_size, posix, sizeof posix, &owner, err, sizeof err) != 0)
        return v_err(err[0] ? err : "ipc_shm_create failed");
    int h = ipc_alloc();
    if (h < 0) {
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
        munmap(ptr, map_size);
        if (owner) shm_unlink(posix);
#endif
        return v_err("ipc_shm_create: no handles");
    }
    g_handles[h].kind = IPC_KIND_SHM_CHAN;
    g_handles[h].shm_ptr = ptr;
    g_handles[h].shm_size = map_size;
    g_handles[h].shm_owner = owner;
    g_handles[h].shm_side = 0;
    g_handles[h].shm_bcast = 0;
    ipc_copy_cstr(g_handles[h].shm_name, sizeof g_handles[h].shm_name, posix);
    return v_int(h);
}

V *bi_ipc_shm_attach(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("ipc_shm_attach(name)"))
    char err[512];
    err[0] = 0;
    void *ptr = NULL;
    size_t map_size = 0;
    char posix[256];
    int owner = 0;
    if (ipc_shm_chan_open(a[0]->s, 0, 0, &ptr, &map_size, posix, sizeof posix, &owner, err, sizeof err) != 0)
        return v_err(err[0] ? err : "ipc_shm_attach failed");
    IpcShmHdr *hdr = (IpcShmHdr *)ptr;
    if (hdr->magic == IPC_SHM_MAGIC_BCAST) {
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
        munmap(ptr, map_size);
#endif
        return v_err("ipc_shm_attach: use shm_broadcast_attach for broadcast segments");
    }
    uint32_t expect = 0;
    if (!atomic_compare_exchange_strong_explicit(&hdr->side_b_claimed, &expect, 1,
                                                 memory_order_acq_rel, memory_order_acquire)) {
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
        munmap(ptr, map_size);
#endif
        return v_err("ipc_shm_attach: side B already attached");
    }
    int h = ipc_alloc();
    if (h < 0) {
        atomic_store_explicit(&hdr->side_b_claimed, 0, memory_order_release);
#if (defined(__linux__) || defined(__APPLE__)) && !defined(SHAKTI_WASM)
        munmap(ptr, map_size);
#endif
        return v_err("ipc_shm_attach: no handles");
    }
    g_handles[h].kind = IPC_KIND_SHM_CHAN;
    g_handles[h].shm_ptr = ptr;
    g_handles[h].shm_size = map_size;
    g_handles[h].shm_owner = 0;
    g_handles[h].shm_side = 1;
    g_handles[h].shm_bcast = 0;
    ipc_copy_cstr(g_handles[h].shm_name, sizeof g_handles[h].shm_name, posix);
    return v_int(h);
}

V *bi_ipc_shm_broadcast_create(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_INT,
      v_err("ipc_shm_broadcast_create(name, size[, max_readers])"))
#if defined(SHAKTI_WASM) || (!defined(__linux__) && !defined(__APPLE__))
    return v_err("ipc_shm_broadcast_create: not supported");
#else
    uint32_t max_r = IPC_BCAST_MAX_READERS;
    if (n > 2 && a[2]->t == T_INT) {
        if (a[2]->j < 1 || a[2]->j > IPC_BCAST_MAX_READERS)
            return v_err("ipc_shm_broadcast_create: bad max_readers");
        max_r = (uint32_t)a[2]->j;
    }
    size_t want = (size_t)a[1]->j;
    if (want < IPC_SHM_HDR + 256) return v_err("ipc_shm_broadcast_create: size too small");
    size_t ring_cap = want - IPC_SHM_HDR - sizeof(atomic_uint) * max_r * 2;
    if (ring_cap < 64) return v_err("ipc_shm_broadcast_create: size too small for readers");
    size_t map_size = ipc_bcast_total_size(ring_cap, max_r);
    char err[512];
    err[0] = 0;
    if (!ipc_valid_shm_user_name(a[0]->s)) return v_err("ipc_shm_broadcast_create: bad name");
    char posix[256];
    if (ipc_format_posix_shm_name(posix, sizeof posix, "/shakti_ipc_", a[0]->s) != 0)
        return v_err("ipc_shm_broadcast_create: name too long");
    shm_unlink(posix);
    int fd = shm_open(posix, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        snprintf(err, sizeof err, "ipc_shm_broadcast_create: shm_open: %s", strerror(errno));
        return v_err(err);
    }
    if (ftruncate(fd, (off_t)map_size) != 0) {
        close(fd); shm_unlink(posix);
        return v_err("ipc_shm_broadcast_create: ftruncate failed");
    }
    void *ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        shm_unlink(posix);
        return v_err("ipc_shm_broadcast_create: mmap failed");
    }
    memset(ptr, 0, map_size);
    IpcShmBcastHdr *bh = (IpcShmBcastHdr *)ptr;
    bh->magic = IPC_SHM_MAGIC_BCAST;
    bh->version = IPC_SHM_VERSION;
    bh->flags = IPC_SHM_FLAG_BCAST;
    bh->capacity = (uint32_t)ring_cap;
    bh->total_size = (uint32_t)map_size;
    bh->max_readers = max_r;
    atomic_init(&bh->head, 0);
    atomic_init(&bh->gen, 0);
    atomic_uint *actives = (atomic_uint *)((uint8_t *)ptr + IPC_SHM_HDR + ring_cap);
    atomic_uint *tails = actives + max_r;
    for (uint32_t i = 0; i < max_r; i++) {
        atomic_init(&actives[i], 0);
        atomic_init(&tails[i], 0);
    }
    int h = ipc_alloc();
    if (h < 0) {
        munmap(ptr, map_size);
        shm_unlink(posix);
        return v_err("ipc_shm_broadcast_create: no handles");
    }
    g_handles[h].kind = IPC_KIND_SHM_CHAN;
    g_handles[h].shm_ptr = ptr;
    g_handles[h].shm_size = map_size;
    g_handles[h].shm_owner = 1;
    g_handles[h].shm_side = -1;
    g_handles[h].shm_bcast = 1;
    ipc_copy_cstr(g_handles[h].shm_name, sizeof g_handles[h].shm_name, posix);
    return v_int(h);
#endif
}

V *bi_ipc_shm_broadcast_attach(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("ipc_shm_broadcast_attach(name)"))
#if defined(SHAKTI_WASM) || (!defined(__linux__) && !defined(__APPLE__))
    return v_err("ipc_shm_broadcast_attach: not supported");
#else
    if (!ipc_valid_shm_user_name(a[0]->s)) return v_err("ipc_shm_broadcast_attach: bad name");
    char posix[256];
    if (ipc_format_posix_shm_name(posix, sizeof posix, "/shakti_ipc_", a[0]->s) != 0)
        return v_err("ipc_shm_broadcast_attach: name too long");
    int fd = shm_open(posix, O_RDWR, 0600);
    if (fd < 0) return v_err("ipc_shm_broadcast_attach: not found");
    struct stat sb;
    if (fstat(fd, &sb) != 0) { close(fd); return v_err("ipc_shm_broadcast_attach: fstat"); }
    size_t map_size = (size_t)sb.st_size;
    void *ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) return v_err("ipc_shm_broadcast_attach: mmap failed");
    IpcShmBcastHdr *bh = (IpcShmBcastHdr *)ptr;
    if (map_size < IPC_SHM_HDR || bh->magic != IPC_SHM_MAGIC_BCAST) {
        munmap(ptr, map_size);
        return v_err("ipc_shm_broadcast_attach: not a broadcast segment");
    }
    {
        size_t need = IPC_SHM_HDR + (size_t)bh->capacity +
                      sizeof(atomic_uint) * (size_t)bh->max_readers * 2;
        if (!(need <= map_size && bh->capacity > 0 &&
              bh->max_readers > 0 && bh->max_readers <= IPC_BCAST_MAX_READERS &&
              bh->version == IPC_SHM_VERSION)) {
            munmap(ptr, map_size);
            return v_err("ipc_shm_broadcast_attach: bad layout");
        }
    }
    int slot = -1;
    atomic_uint *actives = (atomic_uint *)((uint8_t *)ptr + IPC_SHM_HDR + bh->capacity);
    atomic_uint *tails = actives + bh->max_readers;
    for (uint32_t i = 0; i < bh->max_readers; i++) {
        uint32_t expect = 0;
        if (atomic_compare_exchange_strong(&actives[i], &expect, 1)) {
            slot = (int)i;
            atomic_store_explicit(&tails[i], atomic_load_explicit(&bh->head, memory_order_acquire),
                                  memory_order_release);
            break;
        }
    }
    if (slot < 0) {
        munmap(ptr, map_size);
        return v_err("ipc_shm_broadcast_attach: no reader slots");
    }
    int h = ipc_alloc();
    if (h < 0) {
        atomic_store(&actives[slot], 0);
        munmap(ptr, map_size);
        return v_err("ipc_shm_broadcast_attach: no handles");
    }
    g_handles[h].kind = IPC_KIND_SHM_CHAN;
    g_handles[h].shm_ptr = ptr;
    g_handles[h].shm_size = map_size;
    g_handles[h].shm_owner = 0;
    g_handles[h].shm_side = slot;
    g_handles[h].shm_bcast = 1;
    ipc_copy_cstr(g_handles[h].shm_name, sizeof g_handles[h].shm_name, posix);
    return v_int(h);
#endif
}

V *bi_ipc_shm_view(V **a, int n) {
    P(n < 3 || a[0]->t != T_INT || a[1]->t != T_INT || a[2]->t != T_INT,
      v_err("ipc_shm_view(h, offset, len)"))
    IpcHandle *s = ipc_slot((int)a[0]->j);
    P(!s, v_err("ipc_shm_view: bad handle"))
    P(s->kind != IPC_KIND_SHM_CHAN || !s->shm_ptr, v_err("ipc_shm_view: not a shm handle"))
    int64_t off = a[1]->j;
    int64_t len = a[2]->j;
    if (off < (int64_t)IPC_SHM_HDR || len < 0 ||
        (size_t)off + (size_t)len > s->shm_size)
        return v_err("ipc_shm_view: out of range (ring region only)");
    return ipc_u8vec_from_raw((const unsigned char *)s->shm_ptr + (size_t)off, (size_t)len);
}
