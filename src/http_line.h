#ifndef SHAKTI_HTTP_LINE_H
#define SHAKTI_HTTP_LINE_H

#include <stddef.h>
#include <string.h>

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

#endif
