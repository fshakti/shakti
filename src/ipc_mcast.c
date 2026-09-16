#include "ipc_internal.h"

#if defined(SHAKTI_WASM) || defined(_WIN32)

int ipc_mcast_make_reply_sock(int af, uint16_t *port_out, char *err, size_t err_cap) {
    (void)af; (void)port_out;
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

int ipc_mcast_make_reply_sock(int af, uint16_t *port_out, char *err, size_t err_cap) {
    int fd = socket(af, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_cap, "ipc: reply socket: %s", strerror(errno));
        return -1;
    }
    if (af == AF_INET6) {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
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

int ipc_mcast_open(const char *group, int port, const char *iface, const char *source,
                   int *af_out, struct sockaddr_storage *grp_out, socklen_t *grp_len_out,
                   char *err, size_t err_cap) {
    if (!group || !group[0] || port <= 0 || port > 65535) {
        snprintf(err, err_cap, "ipc: bad mcast group/port");
        return -1;
    }
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
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif

    if (af == AF_INET6) {
        struct sockaddr_in6 bind_addr;
        memset(&bind_addr, 0, sizeof bind_addr);
        bind_addr.sin6_family = AF_INET6;
        bind_addr.sin6_port = htons((uint16_t)port);
        bind_addr.sin6_addr = in6addr_any;
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
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
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
    if (!s->mcast_addr_ok) {
        snprintf(err, err_cap, "ipc: mcast address unset");
        return -1;
    }
    size_t max = IPC_UDP_MAX < IPC_MAX_MSG ? IPC_UDP_MAX : IPC_MAX_MSG;
    if (len > max) {
        snprintf(err, err_cap, "ipc: mcast message too large (%zu)", len);
        return -1;
    }
    ssize_t n = sendto(fd, data, len, 0, (struct sockaddr *)&s->mcast_addr, s->mcast_addrlen);
    if (n < 0 || (size_t)n != len) {
        snprintf(err, err_cap, "ipc: mcast sendto: %s", strerror(errno));
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
    char *buf = malloc(max + 1);
    if (!buf) {
        snprintf(err, err_cap, "ipc: oom");
        return -1;
    }
    if (!block) {
        if (sock_set_block(s->fd, 0) != 0) {
            free(buf);
            snprintf(err, err_cap, "ipc: set nonblock failed");
            return -1;
        }
    }
    struct sockaddr_storage from;
    socklen_t fromlen = sizeof from;
    memset(&from, 0, sizeof from);
    ssize_t n = recvfrom(s->fd, buf, max, 0, (struct sockaddr *)&from, &fromlen);
    int saved = errno;
    if (!block) sock_set_block(s->fd, !s->nonblock);
    if (n < 0) {
        free(buf);
        if (!block && (saved == EAGAIN || saved == EWOULDBLOCK)) return -2;
        snprintf(err, err_cap, "ipc: mcast recvfrom: %s", strerror(saved));
        return -1;
    }
    buf[n] = 0;
    *out = buf;
    *out_len = (size_t)n;
    if (peer && peer_len) {
        *peer = from;
        *peer_len = fromlen;
    }
    return 0;
}

int ipc_reply_to_store(IpcHandle *s, uint32_t corr, int af, uint16_t port,
                       const uint8_t addr[16], const uint8_t cookie[8]) {
    for (int i = 0; i < IPC_REPLY_TO_MAX; i++) {
        if (!s->reply_to[i].in_use) {
            s->reply_to[i].in_use = 1;
            s->reply_to[i].corr = corr;
            s->reply_to[i].af = af;
            s->reply_to[i].port = port;
            memcpy(s->reply_to[i].addr, addr, 16);
            if (cookie) memcpy(s->reply_to[i].cookie, cookie, IPC_REPLY_COOKIE);
            else memset(s->reply_to[i].cookie, 0, IPC_REPLY_COOKIE);
            return 0;
        }
    }
    return -1;
}

int ipc_reply_to_store_peer(IpcHandle *s, uint32_t corr, const struct sockaddr_storage *peer,
                            const uint8_t cookie[8]) {
    uint8_t addr[16];
    memset(addr, 0, sizeof addr);
    if (peer->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)peer;
        memcpy(addr, &a->sin6_addr, 16);
        return ipc_reply_to_store(s, corr, AF_INET6, ntohs(a->sin6_port), addr, cookie);
    }
    if (peer->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)peer;
        memcpy(addr, &a->sin_addr, 4);
        return ipc_reply_to_store(s, corr, AF_INET, ntohs(a->sin_port), addr, cookie);
    }
    return -1;
}

int ipc_reply_to_take(IpcHandle *s, uint32_t corr, int *af, uint16_t *port, uint8_t addr[16],
                      uint8_t cookie[8]) {
    for (int i = 0; i < IPC_REPLY_TO_MAX; i++) {
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
