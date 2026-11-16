// SPDX-License-Identifier: Apache-2.0
#ifndef SHAKTI_HTTP_LINE_H
#define SHAKTI_HTTP_LINE_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

/* Split one HTTP request line. Returns 0, or -1 if a token is empty or does not fit. */
static inline int http_split_request_line(const char *line,
                                           char *method, size_t mcap,
                                           char *path, size_t pcap,
                                           char *ver, size_t vcap) {
    const char *p;
    const char *m0;
    const char *u0;
    size_t ml, ul;
    if (!line || !method || !path || mcap == 0 || pcap == 0) return -1;
    p = line;
    while (*p == ' ' || *p == '\t') p++;
    m0 = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    ml = (size_t)(p - m0);
    if (ml == 0 || ml >= mcap) return -1;
    memcpy(method, m0, ml);
    method[ml] = 0;
    while (*p == ' ' || *p == '\t') p++;
    u0 = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    ul = (size_t)(p - u0);
    if (ul == 0 || ul >= pcap) return -1;
    memcpy(path, u0, ul);
    path[ul] = 0;
    if (ver && vcap) {
        const char *v0;
        size_t vl;
        while (*p == ' ' || *p == '\t') p++;
        v0 = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        vl = (size_t)(p - v0);
        if (vl >= vcap) return -1;
        if (vl) memcpy(ver, v0, vl);
        ver[vl] = 0;
    }
    return 0;
}

static inline int http_ipv4_is_loopback(const char *s, size_t n) {
    size_t i = 0;
    unsigned first = 0;
    for (int k = 0; k < 4; k++) {
        unsigned v = 0;
        size_t d = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (unsigned)(s[i] - '0');
            if (v > 255 || ++d > 3) return 0;
            i++;
        }
        if (d == 0) return 0;
        if (k == 0) first = v;
        if (k < 3) {
            if (i >= n || s[i] != '.') return 0;
            i++;
        }
    }
    return i == n && first == 127;
}

static inline int http_hostname_is_local(const char *h, size_t n) {
    if (n == 9 && !strncasecmp(h, "localhost", 9)) return 1;
    if (n == 3 && !strncmp(h, "::1", 3)) return 1;
    return http_ipv4_is_loopback(h, n);
}

/* Split "host[:port]" or "[v6][:port]" (trailing blanks allowed). Returns 0 and the bare host span. */
static inline int http_split_hostport(const char *s, const char **host, size_t *hlen) {
    if (!s) return -1;
    if (*s == '[') {
        const char *rb = strchr(s, ']');
        if (!rb) return -1;
        *host = s + 1;
        *hlen = (size_t)(rb - s - 1);
        s = rb + 1;
    } else {
        const char *e = s;
        while (*e && *e != ':' && *e != '/' && *e != ' ' && *e != '\t') e++;
        *host = s;
        *hlen = (size_t)(e - s);
        s = e;
    }
    if (*hlen == 0) return -1;
    if (*s == ':') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        while (*s >= '0' && *s <= '9') s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    return *s ? -1 : 0;
}

static inline int http_host_is_local(const char *hostport) {
    const char *h;
    size_t n;
    if (http_split_hostport(hostport, &h, &n) != 0) return 0;
    return http_hostname_is_local(h, n);
}

static inline const char *http_origin_hostport(const char *origin, char *buf, size_t cap) {
    const char *p = origin;
    if (!p || !cap) return NULL;
    if (!strncasecmp(p, "http://", 7)) p += 7;
    else if (!strncasecmp(p, "https://", 8)) p += 8;
    else return NULL;
    size_t n = 0;
    while (p[n] && p[n] != '/') n++;
    if (p[n] == '/' && p[n + 1]) return NULL;
    if (n == 0 || n >= cap) return NULL;
    memcpy(buf, p, n);
    buf[n] = 0;
    return buf;
}

static inline int http_origin_is_local(const char *origin) {
    char hp[256];
    if (!http_origin_hostport(origin, hp, sizeof hp)) return 0;
    return http_host_is_local(hp);
}

/* Comma-separated allow-list match (case-insensitive, blanks trimmed); "*" matches anything. */
static inline int http_csv_has(const char *csv, const char *s, size_t slen) {
    if (!csv || !s) return 0;
    const char *p = csv;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *t = p;
        while (*p && *p != ',') p++;
        const char *e = p;
        while (e > t && (e[-1] == ' ' || e[-1] == '\t')) e--;
        size_t tl = (size_t)(e - t);
        if (tl == 1 && *t == '*') return 1;
        if (tl && tl == slen && !strncasecmp(t, s, tl)) return 1;
    }
    return 0;
}

static inline int http_origin_allowed(const char *origin, const char *csv) {
    return origin && http_csv_has(csv, origin, strlen(origin));
}

static inline int http_host_allowed(const char *hostport, const char *csv) {
    const char *h;
    size_t n;
    if (http_split_hostport(hostport, &h, &n) != 0) return http_csv_has(csv, "", 0);
    return http_csv_has(csv, h, n);
}

#endif
