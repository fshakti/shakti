#include "ipc_internal.h"

#if defined(SHAKTI_WASM) || defined(_WIN32)

int ipc_mcast_make_reply_sock(int af, const char *iface, uint16_t *port_out, char *err, size_t err_cap) {
    (void)af; (void)iface; (void)port_out;
    snprintf(err, err_cap, "ipc: mcast not supported on this platform");
    return -1;
}
int ipc_mcast_open(const char *group, int port, const char *iface, const char *source,
                   int *af_out, struct sockaddr_storage *grp_out, socklen_t *grp_len_out,
                   char *err, size_t err_cap) {
    (void)group; (void)port; (void)iface; (void)source;
    (void)af_out; (void)grp_out; (void)grp_len_out;
    snprintf(err, err_cap, "ipc: mcast not supported on this platform");
    return -1;
}
int ipc_mcast_publish_on(IpcHandle *s, int fd, const unsigned char *data, size_t len,
                         char *err, size_t err_cap) {
    (void)s; (void)fd; (void)data; (void)len;
    snprintf(err, err_cap, "ipc: mcast not supported on this platform");
    return -1;
}
int ipc_mcast_publish(IpcHandle *s, const unsigned char *data, size_t len, char *err, size_t err_cap) {
    return ipc_mcast_publish_on(s, s ? s->fd : -1, data, len, err, err_cap);
}
int ipc_mcast_recv(IpcHandle *s, int block, char **out, size_t *out_len,
                   struct sockaddr_storage *peer, socklen_t *peer_len,
                   char *err, size_t err_cap) {
    (void)s; (void)block; (void)out; (void)out_len; (void)peer; (void)peer_len;
    snprintf(err, err_cap, "ipc: mcast not supported on this platform");
    return -1;
}
int ipc_mcast_send_reply(IpcHandle *s, int af, uint16_t port, const uint8_t addr[16],
                         const unsigned char *data, size_t len, char *err, size_t err_cap) {
    (void)s; (void)af; (void)port; (void)addr; (void)data; (void)len;
    snprintf(err, err_cap, "ipc: mcast not supported on this platform");
    return -1;
}
int ipc_reply_to_store(IpcHandle *s, uint32_t corr, int af, uint16_t port,
                       const uint8_t addr[16], const uint8_t cookie[8]) {
    (void)s; (void)corr; (void)af; (void)port; (void)addr; (void)cookie;
    return -1;
}
int ipc_reply_to_store_peer(IpcHandle *s, uint32_t corr, const struct sockaddr_storage *peer,
                            const uint8_t cookie[8]) {
    (void)s; (void)corr; (void)peer; (void)cookie;
    return -1;
}
int ipc_reply_to_take(IpcHandle *s, uint32_t corr, int *af, uint16_t *port, uint8_t addr[16],
                      uint8_t cookie[8]) {
    (void)s; (void)corr; (void)af; (void)port; (void)addr; (void)cookie;
    return -1;
}
void ipc_pack_reply_info(unsigned char *dst, int af, uint16_t port, const void *addr) {
    (void)dst; (void)af; (void)port; (void)addr;
}
int ipc_parse_reply_info(const unsigned char *src, int *af, uint16_t *port, uint8_t addr[16]) {
    (void)src; (void)af; (void)port; (void)addr;
    return -1;
}

#else

static int ipc_mcast_detect_af(const char *group) {
    unsigned char buf[16];
    if (inet_pton(AF_INET6, group, buf) == 1) return AF_INET6;
    if (inet_pton(AF_INET, group, buf) == 1) return AF_INET;
    return -1;
}

static int ipv6_wants_any(const char *iface) {
    return iface && iface[0] == '*' && iface[1] == 0;
}

static void ipv6_bind_addr(const char *iface, struct in6_addr *out) {
    if (ipv6_wants_any(iface)) {
        *out = in6addr_any;
        return;
    }
    if (iface && inet_pton(AF_INET6, iface, out) == 1) return;
    *out = in6addr_loopback;
}

static int ipc_mcast_enable_dst(int fd, int af) {
    int one = 1;
    int zero = 0;
    (void)one;
    (void)zero;
    if (af == AF_INET6) {
#ifdef IPV6_MULTICAST_ALL
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_ALL, &zero, sizeof zero);
#endif
#ifdef IPV6_RECVPKTINFO
        return setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &one, sizeof one);
#else
        return 0;
#endif
    }
#ifdef IP_MULTICAST_ALL
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_ALL, &zero, sizeof zero);
#endif
#if defined(IP_PKTINFO)
    return setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &one, sizeof one);
#elif defined(IP_RECVDSTADDR)
    return setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, &one, sizeof one);
#else
    return 0;
#endif
}

/* 1 = addressed to the joined group, 0 = not, -1 = platform cannot tell. */
static int ipc_mcast_dst_matches(IpcHandle *s, struct msghdr *mh) {
    for (struct cmsghdr *c = CMSG_FIRSTHDR(mh); c; c = CMSG_NXTHDR(mh, c)) {
#ifdef IPV6_RECVPKTINFO
        if (c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_PKTINFO) {
            struct in6_pktinfo pi;
            memcpy(&pi, CMSG_DATA(c), sizeof pi);
            const struct sockaddr_in6 *g = (const struct sockaddr_in6 *)&s->mcast_addr;
            return s->mcast_af == AF_INET6 && memcmp(&pi.ipi6_addr, &g->sin6_addr, 16) == 0;
        }
#endif
#if defined(IP_PKTINFO)
        if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO) {
            struct in_pktinfo pi;
            memcpy(&pi, CMSG_DATA(c), sizeof pi);
            const struct sockaddr_in *g = (const struct sockaddr_in *)&s->mcast_addr;
            return s->mcast_af == AF_INET && pi.ipi_addr.s_addr == g->sin_addr.s_addr;
        }
#elif defined(IP_RECVDSTADDR)
        if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_RECVDSTADDR) {
            struct in_addr da;
            memcpy(&da, CMSG_DATA(c), sizeof da);
            const struct sockaddr_in *g = (const struct sockaddr_in *)&s->mcast_addr;
            return s->mcast_af == AF_INET && da.s_addr == g->sin_addr.s_addr;
        }
#endif
    }
#if defined(IP_PKTINFO) || defined(IP_RECVDSTADDR)
    if (s->mcast_af == AF_INET) return 0;
#endif
#ifdef IPV6_RECVPKTINFO
    if (s->mcast_af == AF_INET6) return 0;
#endif
    return -1;
}

#define IPC_MCAST_CTX 24u
#define IPC_MCAST_SKEW 60

static void ipc_mcast_ctx(IpcHandle *s, unsigned char out[IPC_MCAST_CTX]) {
    memset(out, 0, IPC_MCAST_CTX);
    memcpy(out, "SHKM1", 5);
    if (s->mcast_af == AF_INET6) {
        const struct sockaddr_in6 *g = (const struct sockaddr_in6 *)&s->mcast_addr;
        out[5] = 6;
        memcpy(out + 6, &g->sin6_port, 2);
        memcpy(out + 8, &g->sin6_addr, 16);
    } else {
        const struct sockaddr_in *g = (const struct sockaddr_in *)&s->mcast_addr;
        out[5] = 4;
        memcpy(out + 6, &g->sin_port, 2);
        memcpy(out + 8, &g->sin_addr, 4);
    }
}

static uint64_t ipc_load_be64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static void ipc_store_be64(unsigned char *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (unsigned char)v;
        v >>= 8;
    }
}

/* Returns a heap buffer: data || ts || nonce || HMAC(ctx || data || ts || nonce). */
static unsigned char *ipc_mcast_seal(IpcHandle *s, const char *key, const unsigned char *data, size_t len,
                                     size_t *out_len, char *err, size_t err_cap) {
    unsigned char *buf = malloc(IPC_MCAST_CTX + len + IPC_MCAST_TRAILER);
    if (!buf) {
        snprintf(err, err_cap, "ipc: oom");
        return NULL;
    }
    ipc_mcast_ctx(s, buf);
    unsigned char *body = buf + IPC_MCAST_CTX;
    if (len) memcpy(body, data, len);
    ipc_store_be64(body + len, (uint64_t)time(NULL));
    if (ipc_random_bytes(body + len + 8, 8) != 0 ||
        ipc_hmac(key, buf, IPC_MCAST_CTX + len + 16, body + len + 16) != 0) {
        free(buf);
        snprintf(err, err_cap, "ipc: mcast hmac failed");
        return NULL;
    }
    memmove(buf, body, len + IPC_MCAST_TRAILER);
    *out_len = len + IPC_MCAST_TRAILER;
    return buf;
}

/* buf has IPC_MCAST_CTX bytes of headroom before the datagram (n bytes). */
static int ipc_mcast_open_seal(IpcHandle *s, const char *key, unsigned char *buf, size_t n, size_t *plen) {
    if (n < IPC_MCAST_TRAILER) return -1;
    size_t len = n - IPC_MCAST_TRAILER;
    unsigned char *body = buf + IPC_MCAST_CTX;
    unsigned char mac[IPC_MAC_LEN];
    ipc_mcast_ctx(s, buf);
    if (ipc_hmac(key, buf, IPC_MCAST_CTX + len + 16, mac) != 0) return -1;
    if (!ipc_mac_equal(mac, body + len + 16, IPC_MAC_LEN)) return -1;
    int64_t ts = (int64_t)ipc_load_be64(body + len);
    int64_t now = (int64_t)time(NULL);
    if (ts < now - IPC_MCAST_SKEW || ts > now + IPC_MCAST_SKEW) return -1;
    for (int i = 0; i < IPC_MCAST_SEEN; i++)
        if (memcmp(s->mcast_seen[i], mac, 16) == 0) return -1;
    memcpy(s->mcast_seen[s->mcast_seen_i], mac, 16);
    s->mcast_seen_i = (s->mcast_seen_i + 1) % IPC_MCAST_SEEN;
    *plen = len;
    return 0;
}

int ipc_mcast_make_reply_sock(int af, const char *iface, uint16_t *port_out, char *err, size_t err_cap) {
    int fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_cap, "ipc: reply socket: %s", strerror(errno));
        return -1;
    }
    { int fl = fcntl(fd, F_GETFD); if (fl >= 0) (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC); }
    if (af == AF_INET6) {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        ipv6_bind_addr(iface, &a.sin6_addr);
        a.sin6_port = 0;
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
            snprintf(err, err_cap, "ipc: reply bind6: %s", strerror(errno));
            close(fd);
            return -1;
        }
        socklen_t alen = sizeof a;
        if (getsockname(fd, (struct sockaddr *)&a, &alen) < 0) {
            snprintf(err, err_cap, "ipc: reply getsockname6: %s", strerror(errno));
            close(fd);
            return -1;
        }
        *port_out = ntohs(a.sin6_port);
        int hops = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
        int loop = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof loop);
    } else {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = 0;
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
            snprintf(err, err_cap, "ipc: reply bind: %s", strerror(errno));
            close(fd);
            return -1;
        }
        socklen_t alen = sizeof a;
        if (getsockname(fd, (struct sockaddr *)&a, &alen) < 0) {
            snprintf(err, err_cap, "ipc: reply getsockname: %s", strerror(errno));
            close(fd);
            return -1;
        }
        *port_out = ntohs(a.sin_port);
        int ttl = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
        int loop = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    }
    return fd;
}

/* Without SHAKTI_IPC_MCAST_KEY, multicast traffic is unauthenticated:
 * anyone on the segment can read and inject messages. Warn once. */
static void ipc_mcast_warn_no_key(void) {
    static int warned;
    if (warned) return;
    warned = 1;
    if (!ipc_mcast_key())
        fprintf(stderr,
                "shakti: warning: multicast IPC without SHAKTI_IPC_MCAST_KEY is "
                "unauthenticated (any host on the segment can read/inject)\n");
}

int ipc_mcast_open(const char *group, int port, const char *iface, const char *source,
                   int *af_out, struct sockaddr_storage *grp_out, socklen_t *grp_len_out,
                   char *err, size_t err_cap) {
    if (!group || !group[0] || port <= 0 || port > 65535) {
        snprintf(err, err_cap, "ipc: bad mcast group/port");
        return -1;
    }
    ipc_mcast_warn_no_key();
    int af = ipc_mcast_detect_af(group);
    if (af < 0) {
        snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
        return -1;
    }
    int fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_cap, "ipc: mcast socket: %s", strerror(errno));
        return -1;
    }
    { int fl = fcntl(fd, F_GETFD); if (fl >= 0) (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC); }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    if (ipc_mcast_enable_dst(fd, af) != 0) {
        snprintf(err, err_cap, "ipc: mcast pktinfo: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (af == AF_INET6) {
        struct sockaddr_in6 bind_addr;
        memset(&bind_addr, 0, sizeof bind_addr);
        bind_addr.sin6_family = AF_INET6;
        bind_addr.sin6_port = htons((uint16_t)port);
        if (ipv6_wants_any(iface))
            bind_addr.sin6_addr = in6addr_any;
        else if (inet_pton(AF_INET6, group, &bind_addr.sin6_addr) != 1) {
            snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
            close(fd);
            return -1;
        }
        if (bind(fd, (struct sockaddr *)&bind_addr, sizeof bind_addr) < 0) {
            snprintf(err, err_cap, "ipc: mcast bind6: %s", strerror(errno));
            close(fd);
            return -1;
        }
        unsigned int ifindex = 0;
        if (iface && iface[0]) {
            ifindex = if_nametoindex(iface);
            if (!ifindex) {
                snprintf(err, err_cap, "ipc: bad iface '%s'", iface);
                close(fd);
                return -1;
            }
        }
        if (source && source[0]) {
#if defined(MCAST_JOIN_SOURCE_GROUP)
            struct group_source_req gsr;
            memset(&gsr, 0, sizeof gsr);
            gsr.gsr_interface = ifindex;
            struct sockaddr_in6 *g = (struct sockaddr_in6 *)&gsr.gsr_group;
            g->sin6_family = AF_INET6;
            inet_pton(AF_INET6, group, &g->sin6_addr);
            struct sockaddr_in6 *src = (struct sockaddr_in6 *)&gsr.gsr_source;
            src->sin6_family = AF_INET6;
            if (inet_pton(AF_INET6, source, &src->sin6_addr) != 1) {
                snprintf(err, err_cap, "ipc: bad SSM source '%s'", source);
                close(fd);
                return -1;
            }
            if (setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, &gsr, sizeof gsr) < 0) {
                snprintf(err, err_cap, "ipc: MCAST_JOIN_SOURCE_GROUP: %s", strerror(errno));
                close(fd);
                return -1;
            }
#else
            snprintf(err, err_cap, "ipc: IPv6 SSM not supported on this platform");
            close(fd);
            return -1;
#endif
        } else {
            struct ipv6_mreq mreq;
            memset(&mreq, 0, sizeof mreq);
            if (inet_pton(AF_INET6, group, &mreq.ipv6mr_multiaddr) != 1) {
                snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
                close(fd);
                return -1;
            }
            mreq.ipv6mr_interface = ifindex;
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof mreq) < 0) {
                snprintf(err, err_cap, "ipc: IPV6_JOIN_GROUP: %s", strerror(errno));
                close(fd);
                return -1;
            }
        }
        int hops = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
        int loop = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof loop);
        struct sockaddr_in6 *gout = (struct sockaddr_in6 *)grp_out;
        memset(gout, 0, sizeof *gout);
        gout->sin6_family = AF_INET6;
        gout->sin6_port = htons((uint16_t)port);
        inet_pton(AF_INET6, group, &gout->sin6_addr);
        *grp_len_out = sizeof *gout;
    } else {
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof bind_addr);
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons((uint16_t)port);
#if defined(__linux__)
        if (inet_pton(AF_INET, group, &bind_addr.sin_addr) != 1) {
            snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
            close(fd);
            return -1;
        }
#else
        /* Bind the port only. Binding the multicast group itself makes sendto
         * fail with EADDRNOTAVAIL on macOS (the group is not a local source);
         * ipc_mcast_recv drops datagrams not addressed to the group. */
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
        if (bind(fd, (struct sockaddr *)&bind_addr, sizeof bind_addr) < 0) {
            snprintf(err, err_cap, "ipc: mcast bind: %s", strerror(errno));
            close(fd);
            return -1;
        }
        struct in_addr ifaddr;
        ifaddr.s_addr = htonl(INADDR_ANY);
        if (iface && iface[0]) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof ifr);
            strncpy(ifr.ifr_name, iface, sizeof ifr.ifr_name - 1);
            if (ioctl(fd, SIOCGIFADDR, &ifr) == 0)
                ifaddr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
            else {
                snprintf(err, err_cap, "ipc: bad iface '%s'", iface);
                close(fd);
                return -1;
            }
        }
        if (source && source[0]) {
            struct ip_mreq_source mreqs;
            memset(&mreqs, 0, sizeof mreqs);
            if (inet_pton(AF_INET, group, &mreqs.imr_multiaddr) != 1) {
                snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
                close(fd);
                return -1;
            }
            if (inet_pton(AF_INET, source, &mreqs.imr_sourceaddr) != 1) {
                snprintf(err, err_cap, "ipc: bad SSM source '%s'", source);
                close(fd);
                return -1;
            }
            mreqs.imr_interface = ifaddr;
            if (setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreqs, sizeof mreqs) < 0) {
                snprintf(err, err_cap, "ipc: IP_ADD_SOURCE_MEMBERSHIP: %s", strerror(errno));
                close(fd);
                return -1;
            }
        } else {
            struct ip_mreq mreq;
            memset(&mreq, 0, sizeof mreq);
            if (inet_pton(AF_INET, group, &mreq.imr_multiaddr) != 1) {
                snprintf(err, err_cap, "ipc: bad mcast group '%s'", group);
                close(fd);
                return -1;
            }
            mreq.imr_interface = ifaddr;
            if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0) {
                snprintf(err, err_cap, "ipc: IP_ADD_MEMBERSHIP: %s", strerror(errno));
                close(fd);
                return -1;
            }
        }
        int ttl = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
        int loop = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
        struct sockaddr_in *gout = (struct sockaddr_in *)grp_out;
        memset(gout, 0, sizeof *gout);
        gout->sin_family = AF_INET;
        gout->sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, group, &gout->sin_addr);
        *grp_len_out = sizeof *gout;
    }
    *af_out = af;
    return fd;
}

int ipc_mcast_publish_on(IpcHandle *s, int fd, const unsigned char *data, size_t len,
                         char *err, size_t err_cap) {
    ipc_mcast_warn_no_key();
    if (!s->mcast_addr_ok) {
        snprintf(err, err_cap, "ipc: mcast address unset");
        return -1;
    }
    size_t max = IPC_UDP_MAX < IPC_MAX_MSG ? IPC_UDP_MAX : IPC_MAX_MSG;
    const char *key = ipc_mcast_key();
    if (key) max -= IPC_MCAST_TRAILER;
    if (len > max) {
        snprintf(err, err_cap, "ipc: mcast message too large (%zu)", len);
        return -1;
    }
    unsigned char *sealed = NULL;
    if (key) {
        sealed = ipc_mcast_seal(s, key, data, len, &len, err, err_cap);
        if (!sealed) return -1;
        data = sealed;
    }
    ssize_t n = sendto(fd, data, len, 0, (struct sockaddr *)&s->mcast_addr, s->mcast_addrlen);
    int saved = errno;
    free(sealed);
    if (n < 0 || (size_t)n != len) {
        snprintf(err, err_cap, "ipc: mcast sendto: %s", strerror(saved));
        return -1;
    }
    return 0;
}

int ipc_mcast_publish(IpcHandle *s, const unsigned char *data, size_t len, char *err, size_t err_cap) {
    return ipc_mcast_publish_on(s, s->fd, data, len, err, err_cap);
}

int ipc_mcast_recv(IpcHandle *s, int block, char **out, size_t *out_len,
                   struct sockaddr_storage *peer, socklen_t *peer_len,
                   char *err, size_t err_cap) {
    size_t max = IPC_UDP_MAX < IPC_MAX_MSG ? IPC_UDP_MAX : IPC_MAX_MSG;
    const char *key = ipc_mcast_key();
    unsigned char *raw = malloc(IPC_MCAST_CTX + max + 1);
    if (!raw) {
        snprintf(err, err_cap, "ipc: oom");
        return -1;
    }
    unsigned char *buf = raw + IPC_MCAST_CTX;
    if (!block) {
        if (sock_set_block(s->fd, 0) != 0) {
            free(raw);
            snprintf(err, err_cap, "ipc: set nonblock failed");
            return -1;
        }
    }
    struct sockaddr_storage from;
    socklen_t fromlen = 0;
    ssize_t n;
    size_t plen = 0;
    int saved = 0;
    for (;;) {
        union {
            struct cmsghdr align;
            unsigned char b[256];
        } cbuf;
        struct iovec iov = {.iov_base = buf, .iov_len = max};
        struct msghdr mh;
        memset(&mh, 0, sizeof mh);
        memset(&from, 0, sizeof from);
        mh.msg_name = &from;
        mh.msg_namelen = sizeof from;
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf.b;
        mh.msg_controllen = sizeof cbuf.b;
        n = recvmsg(s->fd, &mh, 0);
        saved = errno;
        if (n < 0) {
            if (saved == EINTR) continue;
            break;
        }
        fromlen = mh.msg_namelen;
        if ((mh.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || ipc_mcast_dst_matches(s, &mh) == 0)
            continue;
        plen = (size_t)n;
        if (key && ipc_mcast_open_seal(s, key, raw, (size_t)n, &plen) != 0)
            continue;
        break;
    }
    if (!block) sock_set_block(s->fd, !s->nonblock);
    if (n < 0) {
        free(raw);
        if (!block && (saved == EAGAIN || saved == EWOULDBLOCK)) return -2;
        snprintf(err, err_cap, "ipc: mcast recvfrom: %s", strerror(saved));
        return -1;
    }
    memmove(raw, buf, plen);
    raw[plen] = 0;
    *out = (char *)raw;
    *out_len = plen;
    if (peer && peer_len) {
        *peer = from;
        *peer_len = fromlen;
    }
    return 0;
}

#define IPC_REPLY_TTL 30

static void ipc_reply_expire(IpcHandle *s) {
    time_t now = time(NULL);
    for (int i = 0; i < IPC_REPLY_TO_MAX; i++) {
        if (s->reply_to[i].in_use && now - s->reply_to[i].at > IPC_REPLY_TTL)
            s->reply_to[i].in_use = 0;
    }
}

int ipc_reply_to_store(IpcHandle *s, uint32_t corr, int af, uint16_t port,
                       const uint8_t addr[16], const uint8_t cookie[8]) {
    uint8_t zcookie[IPC_REPLY_COOKIE];
    memset(zcookie, 0, sizeof zcookie);
    const uint8_t *ck = cookie ? cookie : zcookie;
    ipc_reply_expire(s);
    int free_i = -1;
    int old_i = 0;
    for (int i = 0; i < IPC_REPLY_TO_MAX; i++) {
        IpcReplyTo *r = &s->reply_to[i];
        if (!r->in_use) {
            if (free_i < 0) free_i = i;
            continue;
        }
        if (r->seq < s->reply_to[old_i].seq || !s->reply_to[old_i].in_use) old_i = i;
        if (r->corr == corr && memcmp(r->cookie, ck, IPC_REPLY_COOKIE) == 0) {
            if (r->af == af && r->port == port && memcmp(r->addr, addr, 16) == 0) {
                r->at = time(NULL);
                r->seq = ++s->reply_seq;
                return 0;
            }
            return -1;
        }
    }
    int i = free_i >= 0 ? free_i : old_i;
    s->reply_to[i].in_use = 1;
    s->reply_to[i].corr = corr;
    s->reply_to[i].af = af;
    s->reply_to[i].port = port;
    memcpy(s->reply_to[i].addr, addr, 16);
    memcpy(s->reply_to[i].cookie, ck, IPC_REPLY_COOKIE);
    s->reply_to[i].at = time(NULL);
    s->reply_to[i].seq = ++s->reply_seq;
    return 0;
}

static int ipc_reply_dest_ok(int af, uint16_t port, const uint8_t addr[16]) {
    static const uint8_t zero[16];
    if (port == 0) return 0;
    if (af == AF_INET6) {
        if (memcmp(addr, zero, 16) == 0 || addr[0] == 0xff) return 0;
        return 1;
    }
    if (af != AF_INET) return 0;
    if ((addr[0] & 0xf0) == 0xe0 || addr[0] == 0 || (addr[0] == 255 && addr[1] == 255 && addr[2] == 255 && addr[3] == 255))
        return 0;
    return 1;
}

int ipc_reply_to_store_peer(IpcHandle *s, uint32_t corr, const struct sockaddr_storage *peer,
                            const uint8_t cookie[8]) {
    uint8_t addr[16];
    memset(addr, 0, sizeof addr);
    if (peer->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)peer;
        memcpy(addr, &a->sin6_addr, 16);
        if (!ipc_reply_dest_ok(AF_INET6, ntohs(a->sin6_port), addr)) return -1;
        return ipc_reply_to_store(s, corr, AF_INET6, ntohs(a->sin6_port), addr, cookie);
    }
    if (peer->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)peer;
        memcpy(addr, &a->sin_addr, 4);
        if (!ipc_reply_dest_ok(AF_INET, ntohs(a->sin_port), addr)) return -1;
        return ipc_reply_to_store(s, corr, AF_INET, ntohs(a->sin_port), addr, cookie);
    }
    return -1;
}

static double ipc_reply_rate_limit(void) {
    const char *v = getenv("SHAKTI_IPC_MCAST_REPLY_RATE");
    if (!v || !v[0]) return 100.0;
    char *end = NULL;
    double r = strtod(v, &end);
    if (end == v || r < 0) return 100.0;
    return r;
}

static int ipc_rate_take(IpcReplyRate *b, double rate, const struct timespec *now) {
    if (b->last.tv_sec == 0 && b->last.tv_nsec == 0) {
        b->tokens = rate;
    } else {
        double dt = (double)(now->tv_sec - b->last.tv_sec) + (double)(now->tv_nsec - b->last.tv_nsec) / 1e9;
        if (dt > 0) b->tokens += dt * rate;
        if (b->tokens > rate) b->tokens = rate;
    }
    b->last = *now;
    if (b->tokens < 1.0) return -1;
    b->tokens -= 1.0;
    return 0;
}

static int ipc_reply_rate_ok(IpcHandle *s, int af, const uint8_t addr[16]) {
    double rate = ipc_reply_rate_limit();
    if (rate == 0) return 1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec == 0 && now.tv_nsec == 0) now.tv_nsec = 1;
    IpcReplyRate *slot = NULL;
    IpcReplyRate *oldest = &s->reply_rate[0];
    for (int i = 0; i < IPC_REPLY_RATE_SLOTS; i++) {
        IpcReplyRate *r = &s->reply_rate[i];
        if (r->af == af && memcmp(r->addr, addr, 16) == 0) {
            slot = r;
            break;
        }
        if (r->last.tv_sec < oldest->last.tv_sec ||
            (r->last.tv_sec == oldest->last.tv_sec && r->last.tv_nsec < oldest->last.tv_nsec))
            oldest = r;
    }
    if (!slot) {
        slot = oldest;
        memset(slot, 0, sizeof *slot);
        slot->af = af;
        memcpy(slot->addr, addr, 16);
    }
    if (ipc_rate_take(&s->reply_rate_all, rate * 10.0, &now) != 0) return 0;
    return ipc_rate_take(slot, rate, &now) == 0;
}

int ipc_mcast_send_reply(IpcHandle *s, int af, uint16_t port, const uint8_t addr[16],
                         const unsigned char *data, size_t len, char *err, size_t err_cap) {
    if (!ipc_reply_dest_ok(af, port, addr)) {
        snprintf(err, err_cap, "ipc_reply: refusing reply to non-unicast address");
        return -1;
    }
    if (!ipc_reply_rate_ok(s, af, addr)) {
        snprintf(err, err_cap, "ipc_reply: rate limited (SHAKTI_IPC_MCAST_REPLY_RATE)");
        return -1;
    }
    size_t max = IPC_UDP_MAX;
    const char *key = ipc_mcast_key();
    if (key) max -= IPC_MCAST_TRAILER;
    if (len > max) {
        snprintf(err, err_cap, "ipc_reply: message too large (%zu)", len);
        return -1;
    }
    unsigned char *sealed = NULL;
    if (key) {
        sealed = ipc_mcast_seal(s, key, data, len, &len, err, err_cap);
        if (!sealed) return -1;
        data = sealed;
    }
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
    ssize_t wn = sendto(s->reply_fd >= 0 ? s->reply_fd : s->fd, data, len, 0,
                        (struct sockaddr *)&dest, dlen);
    free(sealed);
    if (wn < 0 || (size_t)wn != len) {
        snprintf(err, err_cap, "ipc_reply: sendto failed");
        return -1;
    }
    return 0;
}

int ipc_reply_to_take(IpcHandle *s, uint32_t corr, int *af, uint16_t *port, uint8_t addr[16],
                      uint8_t cookie[8]) {
    time_t now = time(NULL);
    for (int i = 0; i < IPC_REPLY_TO_MAX; i++) {
        if (s->reply_to[i].in_use && now - s->reply_to[i].at > IPC_REPLY_TTL) {
            s->reply_to[i].in_use = 0;
            continue;
        }
        if (s->reply_to[i].in_use && s->reply_to[i].corr == corr) {
            *af = s->reply_to[i].af;
            *port = s->reply_to[i].port;
            memcpy(addr, s->reply_to[i].addr, 16);
            if (cookie) memcpy(cookie, s->reply_to[i].cookie, IPC_REPLY_COOKIE);
            s->reply_to[i].in_use = 0;
            return 0;
        }
    }
    return -1;
}

void ipc_pack_reply_info(unsigned char *dst, int af, uint16_t port, const void *addr) {
    dst[0] = (unsigned char)(af == AF_INET6 ? 6 : 4);
    uint16_t be = htons(port);
    memcpy(dst + 1, &be, 2);
    memset(dst + 3, 0, 16);
    if (addr) memcpy(dst + 3, addr, 16);
}

int ipc_parse_reply_info(const unsigned char *src, int *af, uint16_t *port, uint8_t addr[16]) {
    *af = (src[0] == 6) ? AF_INET6 : AF_INET;
    uint16_t be;
    memcpy(&be, src + 1, 2);
    *port = ntohs(be);
    memcpy(addr, src + 3, 16);
    return 0;
}

#endif
