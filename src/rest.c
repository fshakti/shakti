#include "rest.h"
#include "http_line.h"
#include "json_parse.h"
#include "shakti_internal.h"
#ifdef SHAKTI_HAVE_IEFS
#include "iefs_format.h"
#ifdef SHAKTI_HAVE_HLD
#include "codec.h"
#include "hld.h"
#endif
#endif
#ifdef SHAKTI_HAVE_TLS
#include "tls.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#define REST_MAX_HANDLES 128
#define REST_MAX_HDR 65536
#define REST_MAX_BODY (1024 * 1024)
#define REST_TEMP_PATH_CAP 512
#define REST_READ_TIMEOUT_MS 10000
#define REST_TLS_HANDSHAKE_MS 5000
#define REST_MAX_DECODE_DEFAULT (256ull * 1024 * 1024)

typedef enum {
    REST_KIND_NONE = 0,
    REST_KIND_LISTEN,
    REST_KIND_CONN,
} RestKind;

typedef struct {
    int in_use;
    int closed;
    RestKind kind;
    int fd;
    void *ssl;
    void *ssl_ctx;
    char *ws_key;
    char *ws_protocol;
    char *ws_origin;
    int loopback;
    long deadline;
} RestHandle;

static char g_rest_server_token[4096];
static char g_rest_client_token[4096];
static int g_rest_tokens_inited;
static int g_rest_inited;

/* Process-lifetime temp paths for curl I/O — created once, truncated per request. */
static char g_rest_hdr_path[REST_TEMP_PATH_CAP];
static char g_rest_body_path[REST_TEMP_PATH_CAP];
static char g_rest_code_path[REST_TEMP_PATH_CAP];
static char g_rest_data_path[REST_TEMP_PATH_CAP];
static int g_rest_temps_ok;

#ifndef SHAKTI_WASM
static RestHandle g_rest_handles[REST_MAX_HANDLES];
#endif

static int rest_make_temp(char *tmpl) {
    int fd = mkstemp(tmpl);
    if (fd < 0) return -1;
#if !defined(_WIN32)
    (void)fchmod(fd, 0600);
#endif
    close(fd);
    return 0;
}

static int rest_fill_temp_tmpl(char *out, size_t cap, const char *dir, const char *leaf) {
    int n = snprintf(out, cap, "%s/%s", dir, leaf);
    return n > 0 && (size_t)n < cap;
}

static void rest_unlink_temps(void) {
    if (!g_rest_temps_ok) return;
    unlink(g_rest_hdr_path);
    unlink(g_rest_body_path);
    unlink(g_rest_code_path);
    unlink(g_rest_data_path);
}

static void rest_init_tokens(void) {
    if (g_rest_tokens_inited) return;
    g_rest_tokens_inited = 1;
    const char *legacy = getenv("SHAKTI_REST_TOKEN");
    const char *srv = getenv("SHAKTI_REST_SERVER_TOKEN");
    const char *cli = getenv("SHAKTI_REST_CLIENT_TOKEN");
    int used_legacy = 0;
    if (!srv || !srv[0]) {
        srv = legacy;
        used_legacy |= srv && srv[0];
    }
    if (!cli || !cli[0]) {
        cli = legacy;
        used_legacy |= cli && cli[0];
    }
    if (srv && srv[0]) snprintf(g_rest_server_token, sizeof g_rest_server_token, "%s", srv);
    if (cli && cli[0]) snprintf(g_rest_client_token, sizeof g_rest_client_token, "%s", cli);
    if (used_legacy)
        fprintf(stderr, "rest: SHAKTI_REST_TOKEN is deprecated; use SHAKTI_REST_SERVER_TOKEN "
                        "(inbound) and SHAKTI_REST_CLIENT_TOKEN (outbound)\n");
}

static int rest_token_eq(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t na = strlen(a), nb = strlen(b);
    size_t n = na > nb ? na : nb;
    unsigned diff = (unsigned)(na != nb);
    for (size_t i = 0; i < n; i++) {
        unsigned ca = i < na ? (unsigned char)a[i] : 0;
        unsigned cb = i < nb ? (unsigned char)b[i] : 0;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

static long rest_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long rest_env_long(const char *name, long def, long lo, long hi) {
    const char *e = getenv(name);
    if (!e || !e[0]) return def;
    char *end = NULL;
    long v = strtol(e, &end, 10);
    if (!end || *end || v < lo || v > hi) return def;
    return v;
}

static size_t rest_max_decode(void) {
    const char *e = getenv("SHAKTI_REST_MAX_DECODE");
    if (e && e[0]) {
        char *end = NULL;
        unsigned long long v = strtoull(e, &end, 10);
        if (end && !*end && v > 0 && v <= SIZE_MAX) return (size_t)v;
    }
    return (size_t)REST_MAX_DECODE_DEFAULT;
}

static void rest_init(void) {
    rest_init_tokens();
    if (g_rest_inited) return;
    g_rest_inited = 1;
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !dir[0]) dir = getenv("TMPDIR");
    if (!dir || !dir[0]) dir = "/tmp";
    if (!rest_fill_temp_tmpl(g_rest_hdr_path, sizeof g_rest_hdr_path, dir, "shakti-rest-hdr-XXXXXX") ||
        !rest_fill_temp_tmpl(g_rest_body_path, sizeof g_rest_body_path, dir, "shakti-rest-body-XXXXXX") ||
        !rest_fill_temp_tmpl(g_rest_code_path, sizeof g_rest_code_path, dir, "shakti-rest-code-XXXXXX") ||
        !rest_fill_temp_tmpl(g_rest_data_path, sizeof g_rest_data_path, dir, "shakti-rest-data-XXXXXX")) {
        return;
    }
    if (rest_make_temp(g_rest_hdr_path) == 0 &&
        rest_make_temp(g_rest_body_path) == 0 &&
        rest_make_temp(g_rest_code_path) == 0 &&
        rest_make_temp(g_rest_data_path) == 0) {
        g_rest_temps_ok = 1;
        atexit(rest_unlink_temps);
    }
}

/* Reject request-splitting control characters (CR/LF) in values that end up in
 * an HTTP request line or header. */
static int rest_has_ctl(const char *s) {
    if (!s) return 0;
    for (; *s; s++)
        if (*s == '\r' || *s == '\n') return 1;
    return 0;
}

/* Only allow real HTTP(S) URLs. This also prevents a leading '-' from being
 * interpreted by curl as an option, and blocks file://, etc.
 * Loopback (127/8, ::1) stays open so the in-process mini-server can be reached.
 */
int rest_private_allowed(void) {
    const char *e = getenv("SHAKTI_NET_ALLOW_PRIVATE");
    return e && e[0] == '1' && e[1] == '\0';
}

static int rest_host_is_blocked_ip(const struct sockaddr *sa) {
    if (!sa) return 1;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(sin4->sin_addr.s_addr);
        if ((a & 0xff000000u) == 0x0a000000u) return 1; /* 10/8 */
        if ((a & 0xfff00000u) == 0xac100000u) return 1; /* 172.16/12 */
        if ((a & 0xffff0000u) == 0xc0a80000u) return 1; /* 192.168/16 */
        if ((a & 0xffff0000u) == 0xa9fe0000u) return 1; /* 169.254/16 */
        if ((a & 0xff000000u) == 0x00000000u) return 1; /* 0/8 */
        if ((a & 0xffc00000u) == 0x64400000u) return 1; /* 100.64/10 CGNAT */
        if ((a & 0xffffff00u) == 0xc0000000u) return 1; /* 192.0.0.0/24 */
        if ((a & 0xfffe0000u) == 0xc6120000u) return 1; /* 198.18/15 */
        if ((a & 0xf0000000u) == 0xf0000000u) return 1; /* 240/4 + 255.255.255.255 */
        if ((a & 0xf0000000u) == 0xe0000000u) return 1; /* 224/4 multicast */
        return 0;
    }
#if defined(AF_INET6)
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *b = in6->sin6_addr.s6_addr;
        int zero = 1;
        for (int i = 0; i < 15; i++) if (b[i]) { zero = 0; break; }
        if (zero && b[15] == 0) return 1; /* :: */
        if (zero && b[15] == 1) return 0; /* ::1 loopback */
        if (b[0] == 0xff) return 1; /* ff00::/8 */
        if (b[0] == 0x20 && b[1] == 0x02) return 1; /* 2002::/16 6to4 */
        if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00) return 1; /* 2001:0000::/32 Teredo */
        if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b) return 1; /* 64:ff9b::/96 */
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0xc0) return 1; /* fec0::/10 */
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return 1; /* fe80::/10 */
        if ((b[0] & 0xfe) == 0xfc) return 1; /* fc00::/7 */
        /* IPv4-mapped ::ffff:a.b.c.d and IPv4-compatible ::a.b.c.d */
        {
            int mapped = 1, compat = 1;
            for (int i = 0; i < 10; i++) if (b[i]) { mapped = 0; compat = 0; break; }
            if (mapped && b[10] == 0xff && b[11] == 0xff) {
                struct sockaddr_in v4;
                memset(&v4, 0, sizeof v4);
                v4.sin_family = AF_INET;
                memcpy(&v4.sin_addr, b + 12, 4);
                return rest_host_is_blocked_ip((struct sockaddr *)&v4);
            }
            if (compat && b[10] == 0 && b[11] == 0) {
                struct sockaddr_in v4;
                memset(&v4, 0, sizeof v4);
                v4.sin_family = AF_INET;
                memcpy(&v4.sin_addr, b + 12, 4);
                return rest_host_is_blocked_ip((struct sockaddr *)&v4);
            }
        }
    }
#endif
    return 0;
}

int rest_addr_blocked(const struct sockaddr *sa) {
    if (!sa) return 1;
    if (rest_private_allowed()) return 0;
    return rest_host_is_blocked_ip(sa);
}

static int rest_extract_host(const char *url, char *host, size_t host_cap) {
    if (!url || !host || host_cap < 2) return 0;
    const char *p = url;
    if (!strncmp(p, "http://", 7)) p += 7;
    else if (!strncmp(p, "https://", 8)) p += 8;
    else return 0;
    /* Reject userinfo (user:pass@host) — curl connects to the post-@ host while
     * naive extraction used to validate/pin the pre-@ token (SSRF bypass). */
    {
        const char *q = p;
        while (*q && *q != '/' && *q != '?' && *q != '#') {
            if (*q == '@') return 0;
            q++;
        }
    }
    if (*p == '[') {
        size_t i = 0;
        p++;
        while (p[i] && p[i] != ']' && i + 1 < host_cap) {
            host[i] = p[i];
            i++;
        }
        if (p[i] != ']') return 0;
        host[i] = 0;
        return i > 0;
    }
    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && p[i] != '#' && i + 1 < host_cap) {
        host[i] = p[i];
        i++;
    }
    host[i] = 0;
    return i > 0;
}

/* Port from URL (explicit, or 443/80 by scheme). Returns 0 on failure. */
static int rest_extract_port(const char *url) {
    if (!url) return 0;
    int https = 0;
    const char *p = url;
    if (!strncmp(p, "https://", 8)) { p += 8; https = 1; }
    else if (!strncmp(p, "http://", 7)) p += 7;
    else return 0;
    if (*p == '[') {
        const char *rb = strchr(p, ']');
        if (!rb) return 0;
        p = rb + 1;
    }
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') p++;
    if (*p == ':') {
        p++;
        long port = 0;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            if (port > 65535) return 0;
            p++;
        }
        return (int)port;
    }
    return https ? 443 : 80;
}

/* Resolve once, reject private/blocked addrs, and build curl --resolve pin
 * "host:port:ip[,ip...]" so curl cannot rebind via a second DNS lookup. */
static int rest_url_resolve_pin(const char *url, char *resolve_out, size_t resolve_cap) {
    char host[256];
    if (!rest_extract_host(url, host, sizeof host)) return 0;
    int port = rest_extract_port(url);
    if (port <= 0) return 0;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;

    char ips[8][INET6_ADDRSTRLEN];
    int nip = 0;
    int allow_private = rest_private_allowed();
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (!allow_private && rest_host_is_blocked_ip(ai->ai_addr)) {
            freeaddrinfo(res);
            return 0;
        }
        if (nip >= 8) continue;
        const void *addr = NULL;
        if (ai->ai_family == AF_INET)
            addr = &((struct sockaddr_in *)ai->ai_addr)->sin_addr;
#if defined(AF_INET6)
        else if (ai->ai_family == AF_INET6)
            addr = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
#endif
        else continue;
        if (!inet_ntop(ai->ai_family, addr, ips[nip], sizeof ips[nip])) continue;
        /* de-dup */
        int dup = 0;
        for (int j = 0; j < nip; j++)
            if (!strcmp(ips[j], ips[nip])) { dup = 1; break; }
        if (!dup) nip++;
    }
    freeaddrinfo(res);
    if (nip == 0) return 0;

    size_t used = 0;
    int n = snprintf(resolve_out, resolve_cap, "%s:%d:", host, port);
    if (n < 0 || (size_t)n >= resolve_cap) return 0;
    used = (size_t)n;
    for (int i = 0; i < nip; i++) {
        int need_br = (strchr(ips[i], ':') != NULL);
        n = snprintf(resolve_out + used, resolve_cap - used, "%s%s%s%s",
                     i ? "," : "", need_br ? "[" : "", ips[i], need_br ? "]" : "");
        if (n < 0 || (size_t)n >= resolve_cap - used) return 0;
        used += (size_t)n;
    }
    return 1;
}

static int rest_token_host_ok(const char *url) {
    const char *hosts = getenv("SHAKTI_REST_TOKEN_HOSTS");
    if (!hosts || !hosts[0]) return 0; /* no allowlist → never auto-attach */
    if (!url || strncmp(url, "https://", 8) != 0) return 0;
    char host[256];
    if (!rest_extract_host(url, host, sizeof host)) return 0;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", hosts);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (!strcmp(tok, host)) return 1;
    }
    return 0;
}

/* Read a file fully, but never more than max_len bytes (0 = unlimited). Returns
 * NULL on error or if the cap is exceeded, so untrusted curl output cannot grow
 * memory without bound. */
static char *read_all_file(const char *path, size_t *out_len, size_t max_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (len + 4096 >= cap) {
            cap *= 2;
            char *n = realloc(buf, cap);
            if (!n) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = n;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (max_len && len > max_len) {
            free(buf);
            fclose(f);
            return NULL;
        }
        if (n == 0) break;
    }
    fclose(f);
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}


/* Content-Type match: type/subtype, optional parameters after ';' / whitespace. */
static int rest_ct_is(const char *ct, const char *want) {
    if (!ct || !want || !want[0]) return 0;
    size_t n = strlen(want);
    if (strncasecmp(ct, want, n) != 0) return 0;
    char c = ct[n];
    return c == 0 || c == ';' || c == ' ' || c == '\t';
}

static int rest_ct_is_iefs(const char *ct) {
    return rest_ct_is(ct, "application/iefs") || rest_ct_is(ct, "application/x-iefs");
}

static int rest_ct_is_hld(const char *ct) {
    return rest_ct_is(ct, "application/x-hld") || rest_ct_is(ct, "application/hld");
}

static const char *rest_header_get(V *headers, const char *name) {
    if (!headers || headers->t != T_DICT || !name) return NULL;
    for (int64_t i = 0; i < headers->keys->n; i++) {
        V *k = headers->keys->L[i];
        if (k->t != T_STR || strcasecmp(k->s, name) != 0) continue;
        V *v = headers->vals->L[i];
        if (v->t == T_STR) return v->s;
        return NULL;
    }
    return NULL;
}

static V *cvec_from_raw(const unsigned char *p, size_t n) {
    if (n > (size_t)INT64_MAX) return v_err("rest: body too large");
    V *v = v_cvec((int64_t)n);
    if (n && p && v->B) memcpy(v->B, p, n);
    return v;
}

static int rest_body_looks_binary(const char *ct) {
    return rest_ct_is_hld(ct) || rest_ct_is_iefs(ct);
}

#ifdef SHAKTI_HAVE_HLD
static V *rest_hld_decode_max(const unsigned char *buf, size_t len, size_t max_plain) {
    if (!buf || len < HLD_HEADER_SIZE)
        return v_err("hld_decode: truncated header");
    if (memcmp(buf, HLD_MAGIC, 4) != 0)
        return v_err("hld_decode: bad magic");
    int codec = (int)buf[4];
    if (buf[5] != 0)
        return v_err("hld_decode: reserved byte nonzero");
    if (codec != SHAKTI_CODEC_NONE && codec != SHAKTI_CODEC_ZSTD && codec != SHAKTI_CODEC_SNAPPY)
        return v_err("hld_decode: unknown codec");
    uint32_t raw_len = (uint32_t)buf[6] | ((uint32_t)buf[7] << 8) | ((uint32_t)buf[8] << 16) |
                       ((uint32_t)buf[9] << 24);
    uint32_t wire_len = (uint32_t)buf[10] | ((uint32_t)buf[11] << 8) | ((uint32_t)buf[12] << 16) |
                        ((uint32_t)buf[13] << 24);
    if (len < (size_t)HLD_HEADER_SIZE + (size_t)wire_len)
        return v_err("hld_decode: truncated payload");
    if ((size_t)raw_len > max_plain)
        return v_err("rest: decoded body exceeds SHAKTI_REST_MAX_DECODE");
    const unsigned char *payload = buf + HLD_HEADER_SIZE;
    if (codec == SHAKTI_CODEC_NONE) {
        if (wire_len != raw_len)
            return v_err("hld_decode: raw_len mismatch");
        return iefs_decode_max(payload, wire_len, max_plain);
    }
    unsigned char *plain = NULL;
    size_t plain_len = 0;
    char err[256];
    err[0] = 0;
    if (shakti_decompress_max(codec, payload, wire_len, &plain, &plain_len, (size_t)raw_len,
                              err, sizeof err) != 0)
        return v_err(err[0] ? err : "hld_decode: decompress failed");
    if (plain_len != (size_t)raw_len) {
        free(plain);
        return v_err("hld_decode: raw_len mismatch after decompress");
    }
    V *v = iefs_decode_max(plain, plain_len, max_plain);
    free(plain);
    return v;
}
#endif

/* Extract HTTP body bytes from T_STR or list[char]. Returns 0 on success. */
static int rest_body_bytes(V *v, const unsigned char **out, size_t *out_len) {
    if (!out || !out_len) return -1;
    *out = (const unsigned char *)"";
    *out_len = 0;
    if (!v) return 0;
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

static V *body_value(const unsigned char *raw, size_t len, const char *content_type) {
    if (!raw || len == 0) return v_str("");
#ifdef SHAKTI_HAVE_HLD
    if (rest_ct_is_hld(content_type))
        return rest_hld_decode_max(raw, len, rest_max_decode());
#endif
#ifdef SHAKTI_HAVE_IEFS
    if (rest_ct_is_iefs(content_type))
        return iefs_decode_max(raw, len, rest_max_decode());
#endif
    while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r')) len--;
    if (len == 0) return v_str("");
    char *tmp = malloc(len + 1);
    if (!tmp) return v_err("rest: out of memory");
    memcpy(tmp, raw, len);
    tmp[len] = 0;
    /* A declared type other than JSON stays text, even when the bytes parse. */
    int json_ct = !content_type || !content_type[0] ||
                  rest_ct_is(content_type, "application/json") ||
                  rest_ct_is(content_type, "application/json-rpc") ||
                  rest_ct_is(content_type, "text/json");
    if (json_ct && (tmp[0] == '{' || tmp[0] == '[')) {
        V *parsed = shakti_json_parse(tmp, NULL);
        if (parsed && parsed->t != T_ERR) {
            free(tmp);
            return parsed;
        }
        if (parsed) v_free(parsed);
    }
    V *out = v_str(tmp);
    free(tmp);
    return out;
}

static V *make_response(int status, const unsigned char *raw_body, size_t body_len, V *headers) {
    V *resp = v_dict_empty();
    v_dict_put(resp, "status", v_int(status));
    const char *ct = rest_header_get(headers, "Content-Type");
    size_t copy_len = raw_body ? body_len : 0;
    const unsigned char *src = raw_body ? raw_body : (const unsigned char *)"";
    V *body = body_value(src, copy_len, ct);
    if (body->t == T_ERR) {
        v_free(resp);
        if (headers) v_free(headers);
        return body;
    }
    v_dict_put(resp, "body", body);
    V *raw;
    if (rest_body_looks_binary(ct)) {
        raw = cvec_from_raw(src, copy_len);
        if (raw->t == T_ERR) {
            v_free(resp);
            if (headers) v_free(headers);
            return raw;
        }
    } else {
        char *raw_copy = malloc(copy_len + 1);
        if (!raw_copy) {
            v_free(resp);
            if (headers) v_free(headers);
            return v_err("rest: out of memory");
        }
        memcpy(raw_copy, src, copy_len);
        raw_copy[copy_len] = 0;
        raw = v_str(raw_copy);
        free(raw_copy);
    }
    v_dict_put(resp, "raw", raw);
    if (!headers) headers = v_dict_empty();
    v_dict_put(resp, "headers", headers);
    return resp;
}

static V *parse_curl_headers(const char *path) {
    V *hdrs = v_dict_empty();
    char *text = read_all_file(path, NULL, REST_MAX_HDR);
    if (!text) return hdrs;
    char *line = text;
    int past_status = 0;
    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\r' || line[ll - 1] == '\n')) line[--ll] = 0;
        if (*line == 0) {
            past_status = 1;
            line = eol ? eol + 1 : NULL;
            continue;
        }
        if (!past_status && !strncmp(line, "HTTP/", 5)) {
            past_status = 1;
            line = eol ? eol + 1 : NULL;
            continue;
        }
        char *colon = strchr(line, ':');
        if (colon && past_status) {
            *colon = 0;
            char *key = line;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            v_dict_put(hdrs, key, v_str(val));
        }
        line = eol ? eol + 1 : NULL;
    }
    free(text);
    return hdrs;
}

#ifndef SHAKTI_WASM

static RestHandle *rest_slot(int h) {
    if (h < 1 || h >= REST_MAX_HANDLES) return NULL;
    RestHandle *s = &g_rest_handles[h];
    return (s->in_use && !s->closed) ? s : NULL;
}

static int rest_alloc(RestKind kind, int fd) {
    for (int i = 1; i < REST_MAX_HANDLES; i++) {
        if (!g_rest_handles[i].in_use) {
            g_rest_handles[i].in_use = 1;
            g_rest_handles[i].closed = 0;
            g_rest_handles[i].kind = kind;
            g_rest_handles[i].fd = fd;
            g_rest_handles[i].ssl = NULL;
            g_rest_handles[i].ssl_ctx = NULL;
            g_rest_handles[i].ws_key = NULL;
            g_rest_handles[i].ws_protocol = NULL;
            g_rest_handles[i].ws_origin = NULL;
            g_rest_handles[i].loopback = 0;
            g_rest_handles[i].deadline = 0;
            return i;
        }
    }
    return -1;
}

static int rest_wait_readable(RestHandle *conn) {
    long left = conn->deadline - rest_now_ms();
    if (left <= 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    struct pollfd pfd = {.fd = conn->fd, .events = POLLIN, .revents = 0};
    for (;;) {
        int pr = poll(&pfd, 1, (int)left);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) {
            if (pr == 0) errno = ETIMEDOUT;
            return -1;
        }
        return 0;
    }
}

static ssize_t rest_io_read(RestHandle *conn, void *buf, size_t n) {
    if (!conn) return -1;
    for (;;) {
        ssize_t r;
#ifdef SHAKTI_HAVE_TLS
        if (conn->ssl)
            r = shakti_tls_read(conn->ssl, buf, n);
        else
#endif
            r = read(conn->fd, buf, n);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        if (conn->deadline && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (rest_wait_readable(conn) != 0) return -1;
            continue;
        }
        return -1;
    }
}

/* A write to a client that has closed fails with EPIPE instead of raising SIGPIPE, which ends the
 * process: SO_NOSIGPIPE on the socket where the system has it (macOS; it stays on the socket when
 * ws.accept takes it over), and MSG_NOSIGNAL on each send where the system has that (Linux, recent
 * macOS). tls.c guards OpenSSL's own writes. */
static void rest_set_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    (void)fd;
#endif
}

#ifdef MSG_NOSIGNAL
#define REST_SEND_FLAGS MSG_NOSIGNAL
#else
#define REST_SEND_FLAGS 0
#endif

static ssize_t rest_io_write(RestHandle *conn, const void *buf, size_t n) {
    if (!conn) return -1;
#ifdef SHAKTI_HAVE_TLS
    if (conn->ssl) return shakti_tls_write(conn->ssl, buf, n);
#endif
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = send(conn->fd, (const char *)buf + sent, n - sent, REST_SEND_FLAGS);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)w;
    }
    return (ssize_t)sent;
}

static ssize_t rest_read_full(RestHandle *conn, char *buf, size_t want) {
    size_t got = 0;
    while (got < want) {
        ssize_t n = rest_io_read(conn, buf + got, want - got);
        if (n < 0) return -1;
        if (n == 0) break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static ssize_t rest_read_line(RestHandle *conn, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = rest_io_read(conn, &c, 1);
        if (n < 0) return -1;
        if (n == 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    if (i + 1 >= cap && (i == 0 || buf[i - 1] != '\n'))
        return -1;
    return (ssize_t)i;
}

static void rest_clear_ws_meta(RestHandle *s) {
    if (!s) return;
    free(s->ws_key);
    free(s->ws_protocol);
    free(s->ws_origin);
    s->ws_key = NULL;
    s->ws_protocol = NULL;
    s->ws_origin = NULL;
}

int rest_peek_ws_key(int h, char *out, size_t out_cap) {
    RestHandle *s = rest_slot(h);
    if (!s || !s->ws_key || !out || out_cap == 0) return -1;
    if (snprintf(out, out_cap, "%s", s->ws_key) >= (int)out_cap) return -1;
    return 0;
}

int rest_peek_ws_protocol(int h, char *out, size_t out_cap) {
    RestHandle *s = rest_slot(h);
    if (!s || !out || out_cap == 0) return -1;
    if (!s->ws_protocol) {
        out[0] = 0;
        return 0;
    }
    if (snprintf(out, out_cap, "%s", s->ws_protocol) >= (int)out_cap) return -1;
    return 0;
}

int rest_peek_ws_origin(int h, char *out, size_t out_cap, int *loopback) {
    RestHandle *s = rest_slot(h);
    if (!s || !out || out_cap == 0) return -1;
    if (loopback) *loopback = s->loopback;
    if (!s->ws_origin) {
        out[0] = 0;
        return 0;
    }
    if (snprintf(out, out_cap, "%s", s->ws_origin) >= (int)out_cap) return -1;
    return 0;
}

int rest_take_conn(int h, int *out_fd, void **out_ssl) {
    RestHandle *s = rest_slot(h);
    if (!s || s->kind != REST_KIND_CONN || !out_fd || !out_ssl) return -1;
    *out_fd = s->fd;
    *out_ssl = s->ssl;
    rest_clear_ws_meta(s);
    s->fd = -1;
    s->ssl = NULL;
    s->closed = 1;
    s->in_use = 0;
    return 0;
}

/* Run curl with an explicit argv (no shell) and capture its stdout (the
 * -w http_code) into code_path. Returns curl's exit status, or -1 on error.
 * posix_spawn avoids a full address-space copy from fork(). */
static const char *rest_curl_path(void) {
    const char *env = getenv("SHAKTI_CURL");
    if (env && env[0]) {
        if (env[0] == '/' && access(env, X_OK) == 0) return env;
        return NULL;
    }
    static const char *const cands[] = {"/usr/bin/curl", "/usr/local/bin/curl",
                                        "/opt/homebrew/bin/curl", "/bin/curl"};
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++)
        if (access(cands[i], X_OK) == 0) return cands[i];
    return NULL;
}

static int rest_env_keep(const char *kv) {
    static const char *const keep[] = {"LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "TZ",
                                       "SSL_CERT_FILE", "SSL_CERT_DIR", "CURL_CA_BUNDLE",
                                       "LD_LIBRARY_PATH", "SYSTEMROOT"};
    const char *eq = strchr(kv, '=');
    if (!eq) return 0;
    size_t n = (size_t)(eq - kv);
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++)
        if (strlen(keep[i]) == n && !strncmp(kv, keep[i], n)) return 1;
    return 0;
}

static int rest_run_curl(char *const argv[], const char *code_path) {
    const char *curl = rest_curl_path();
    if (!curl) return -2;
    int ofd = open(code_path, O_WRONLY | O_TRUNC | O_NOFOLLOW, 0600);
    if (ofd < 0) return -1;
    int dn = open("/dev/null", O_WRONLY);
    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) {
        close(ofd);
        if (dn >= 0) close(dn);
        return -1;
    }
    posix_spawn_file_actions_adddup2(&fa, ofd, STDOUT_FILENO);
    if (dn >= 0)
        posix_spawn_file_actions_adddup2(&fa, dn, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, ofd);
    if (dn >= 0)
        posix_spawn_file_actions_addclose(&fa, dn);

    pid_t pid = 0;
    extern char **environ;
    size_t nenv = 0;
    for (char **e = environ; e && *e; e++) nenv++;
    char **envp = calloc(nenv + 1, sizeof(char *));
    if (!envp) {
        posix_spawn_file_actions_destroy(&fa);
        close(ofd);
        if (dn >= 0) close(dn);
        return -1;
    }
    size_t ne = 0;
    for (char **e = environ; e && *e; e++)
        if (rest_env_keep(*e)) envp[ne++] = *e;
    envp[ne] = NULL;
    int rc = posix_spawn(&pid, curl, &fa, NULL, argv, envp);
    free(envp);
    posix_spawn_file_actions_destroy(&fa);
    close(ofd);
    if (dn >= 0) close(dn);
    if (rc != 0) return -1;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static V *rest_http_request(const char *method, const char *url, const unsigned char *body,
                            size_t body_len, const char *content_type, V *extra_hdrs) {
    if (!shakti_net_allowed())
        return v_err("rest: network disabled (SHAKTI_SAFE=1; set SHAKTI_ALLOW_NET=1)");
    rest_init();
    if (!method || !method[0] || !url || !url[0])
        return v_err("rest: empty method or url");
    if (rest_has_ctl(method) || rest_has_ctl(url) || rest_has_ctl(g_rest_client_token))
        return v_err("rest: invalid control character in method, url, or token");
    if (content_type && rest_has_ctl(content_type))
        return v_err("rest: invalid control character in content_type");
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0))
        return v_err("rest: url must be http(s) to a non-private host");
    if (!g_rest_temps_ok)
        return v_err("rest: temp file failed");

    char resolve_pin[1024];
    if (!rest_url_resolve_pin(url, resolve_pin, sizeof resolve_pin))
        return v_err("rest: url must be http(s) to a non-private host");

    int have_data = 0;
    char ct_hdr[512];
    char data_at[sizeof g_rest_data_path + 1];
    ct_hdr[0] = 0;
    data_at[0] = 0;
    if (body_len > 0) {
        if (!body)
            return v_err("rest: null body");
        int data_fd = open(g_rest_data_path, O_WRONLY | O_TRUNC | O_NOFOLLOW);
        if (data_fd < 0)
            return v_err("rest: temp file failed");
        if (write(data_fd, body, body_len) != (ssize_t)body_len) {
            close(data_fd);
            return v_err("rest: write failed");
        }
        close(data_fd);
        const char *ct = (content_type && content_type[0]) ? content_type : "application/json";
        snprintf(ct_hdr, sizeof ct_hdr, "Content-Type: %s", ct);
        snprintf(data_at, sizeof data_at, "@%s", g_rest_data_path);
        have_data = 1;
    }

    char auth_hdr[4200];
    auth_hdr[0] = 0;
    if (g_rest_client_token[0] && rest_token_host_ok(url))
        snprintf(auth_hdr, sizeof auth_hdr, "Authorization: Bearer %s", g_rest_client_token);

    /* Assemble a curl argv (no shell). Each value is passed to execvp() as a
     * literal argument, so URL/header/token contents cannot inject a command. */
    int nextra = (extra_hdrs && extra_hdrs->t == T_DICT) ? (int)extra_hdrs->keys->n : 0;
    int max_args = 38 + nextra * 2;
    char **argv = calloc((size_t)max_args, sizeof(char *));
    char **hdr_lines = calloc((size_t)(nextra > 0 ? nextra : 1), sizeof(char *));
    if (!argv || !hdr_lines) {
        free(argv);
        free(hdr_lines);
        return v_err("rest: out of memory");
    }
    int nh = 0;
    int ac = 0;
    argv[ac++] = "curl";
    argv[ac++] = "-q";
    argv[ac++] = "-sS";
    argv[ac++] = "-m";
    argv[ac++] = "60";
    argv[ac++] = "--proto";
    argv[ac++] = "=http,https";
    argv[ac++] = "--noproxy";
    argv[ac++] = "*";
    argv[ac++] = "--resolve";
    argv[ac++] = resolve_pin;
    argv[ac++] = "-X";
    argv[ac++] = (char *)method;
    if (auth_hdr[0]) {
        argv[ac++] = "-H";
        argv[ac++] = auth_hdr;
    }
    if (extra_hdrs && extra_hdrs->t == T_DICT) {
        for (int64_t i = 0; i < extra_hdrs->keys->n; i++) {
            V *k = extra_hdrs->keys->L[i];
            V *v = extra_hdrs->vals->L[i];
            if (k->t != T_STR || v->t != T_STR) continue;
            if (rest_has_ctl(k->s) || rest_has_ctl(v->s)) {
                for (int j = 0; j < nh; j++) free(hdr_lines[j]);
                free(hdr_lines);
                free(argv);
                return v_err("rest: invalid control character in headers");
            }
            size_t ln = strlen(k->s) + strlen(v->s) + 3;
            char *line = malloc(ln);
            if (!line) continue;
            snprintf(line, ln, "%s: %s", k->s, v->s);
            hdr_lines[nh++] = line;
            argv[ac++] = "-H";
            argv[ac++] = line;
        }
    }
    if (have_data) {
        argv[ac++] = "-H";
        argv[ac++] = ct_hdr;
        argv[ac++] = "--data-binary";
        argv[ac++] = data_at;
    }
    char max_fs[32];
    snprintf(max_fs, sizeof max_fs, "%d", REST_MAX_BODY);
    argv[ac++] = "--max-filesize";
    argv[ac++] = max_fs;
    argv[ac++] = "-D";
    argv[ac++] = g_rest_hdr_path;
    argv[ac++] = "-o";
    argv[ac++] = g_rest_body_path;
    argv[ac++] = "-w";
    argv[ac++] = "%{http_code}";
    argv[ac++] = (char *)url;
    argv[ac] = NULL;

    int rc = rest_run_curl(argv, g_rest_code_path);

    for (int j = 0; j < nh; j++) free(hdr_lines[j]);
    free(hdr_lines);
    free(argv);
    if (rc == -2)
        return v_err("rest: curl not found (install curl or set SHAKTI_CURL to an absolute path)");
    if (rc != 0)
        return v_err("rest: curl request failed");

    char *code_s = read_all_file(g_rest_code_path, NULL, 64);
    int status = code_s ? atoi(code_s) : 0;
    free(code_s);

    size_t blen = 0;
    char *raw_body = read_all_file(g_rest_body_path, &blen, REST_MAX_BODY);
    V *hdrs = parse_curl_headers(g_rest_hdr_path);

    if (!raw_body) {
        v_free(hdrs);
        return v_err("rest: read body failed");
    }
    V *out = make_response(status, (const unsigned char *)raw_body, blen, hdrs);
    free(raw_body);
    return out;
}

#ifndef SHAKTI_WASM
static void rest_set_cloexec(int fd) {
    int fl;
    if (fd < 0) return;
    fl = fcntl(fd, F_GETFD);
    if (fl >= 0)
        (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}
#endif

static V *rest_do_listen(int port, const char *host) {
    if (!shakti_net_allowed())
        return v_err("rest_listen: network disabled (SHAKTI_SAFE=1; set SHAKTI_ALLOW_NET=1)");
    rest_init_tokens();
    if (port < 1 || port > 65535) return v_err("rest_listen: invalid port");
    if (!host || !host[0]) host = "127.0.0.1";
    if (!strcmp(host, "localhost")) host = "127.0.0.1";
    int loopback = !strcmp(host, "127.0.0.1") || !strcmp(host, "::1") || !strcmp(host, "localhost");
    {
        const char *allow = getenv("SHAKTI_REST_ALLOW_PUBLIC");
        if (!loopback && !(allow && allow[0] == '1' && allow[1] == '\0'))
            return v_err("rest_listen: non-loopback bind requires SHAKTI_REST_ALLOW_PUBLIC=1");
        if (!loopback && !g_rest_server_token[0])
            return v_err("rest_listen: non-loopback bind requires SHAKTI_REST_SERVER_TOKEN");
    }

#if defined(AF_INET6)
    if (!strcmp(host, "::1")) {
        int fd6 = socket(AF_INET6, SOCK_STREAM, 0);
        if (fd6 < 0) return v_err("rest_listen: socket failed");
        rest_set_cloexec(fd6);
        int on6 = 1;
        setsockopt(fd6, SOL_SOCKET, SO_REUSEADDR, &on6, sizeof on6);
#ifdef IPV6_V6ONLY
        setsockopt(fd6, IPPROTO_IPV6, IPV6_V6ONLY, &on6, sizeof on6);
#endif
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof addr6);
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons((uint16_t)port);
        if (inet_pton(AF_INET6, "::1", &addr6.sin6_addr) != 1) {
            close(fd6);
            return v_err("rest_listen: invalid host");
        }
        if (bind(fd6, (struct sockaddr *)&addr6, sizeof addr6) < 0) {
            close(fd6);
            return v_err("rest_listen: bind failed");
        }
        if (listen(fd6, 16) < 0) {
            close(fd6);
            return v_err("rest_listen: listen failed");
        }
        int h6 = rest_alloc(REST_KIND_LISTEN, fd6);
        if (h6 < 0) {
            close(fd6);
            return v_err("rest_listen: too many handles");
        }
        g_rest_handles[h6].loopback = 1;
        return v_int(h6);
    }
#endif

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return v_err("rest_listen: socket failed");
    rest_set_cloexec(fd);

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return v_err("rest_listen: invalid host");
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return v_err("rest_listen: bind failed");
    }
    if (listen(fd, 16) < 0) {
        close(fd);
        return v_err("rest_listen: listen failed");
    }

    int h = rest_alloc(REST_KIND_LISTEN, fd);
    if (h < 0) {
        close(fd);
        return v_err("rest_listen: too many handles");
    }
    g_rest_handles[h].loopback = loopback || http_ipv4_is_loopback(host, strlen(host));
    return v_int(h);
}

static V *rest_do_accept(int listen_h) {
    RestHandle *srv = rest_slot(listen_h);
    if (!srv || srv->kind != REST_KIND_LISTEN) return v_err("rest_accept: invalid listen handle");

    struct sockaddr_storage peer;
    socklen_t plen = sizeof peer;
    int cfd = accept(srv->fd, (struct sockaddr *)&peer, &plen);
    if (cfd >= 0) {
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if (cfd < 0) return v_err("rest_accept: accept failed");
    rest_set_cloexec(cfd);
    rest_set_nosigpipe(cfd);

    void *ssl = NULL;
#ifdef SHAKTI_HAVE_TLS
    if (srv->ssl_ctx) {
        ssl = shakti_tls_accept_timeout(cfd, srv->ssl_ctx,
                                 (int)rest_env_long("SHAKTI_REST_TLS_TIMEOUT_MS",
                                                    REST_TLS_HANDSHAKE_MS, 1, 600000));
        if (!ssl) {
            close(cfd);
            return v_err("rest_accept: tls handshake failed");
        }
    }
#endif

    int h = rest_alloc(REST_KIND_CONN, cfd);
    if (h < 0) {
#ifdef SHAKTI_HAVE_TLS
        if (ssl) shakti_tls_close(ssl, cfd);
        else
#endif
            close(cfd);
        return v_err("rest_accept: too many handles");
    }
    g_rest_handles[h].ssl = ssl;
    g_rest_handles[h].loopback = srv->loopback;
    return v_int(h);
}

#ifdef SHAKTI_HAVE_TLS
static V *rest_do_listen_tls(int port, const char *host, const char *cert, const char *key) {
    if (!cert || !key || !cert[0] || !key[0])
        return v_err("rest_listen_tls: cert and key required");
    void *ctx = shakti_tls_server_ctx(cert, key);
    if (!ctx) return v_err("rest_listen_tls: failed to load cert/key");
    V *h = rest_do_listen(port, host);
    if (h->t == T_ERR) {
        shakti_tls_server_ctx_free(ctx);
        return h;
    }
    RestHandle *srv = rest_slot((int)h->j);
    if (!srv) {
        shakti_tls_server_ctx_free(ctx);
        v_free(h);
        return v_err("rest_listen_tls: internal error");
    }
    srv->ssl_ctx = ctx;
    return h;
}
#endif

static void rest_reject(RestHandle *conn, int status, const char *reason) {
    char resp[192];
    int n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\n\r\n%s\n",
                     status, reason, strlen(reason) + 1, reason);
    if (n > 0 && (size_t)n < sizeof resp) (void)rest_io_write(conn, resp, (size_t)n);
}

static V *rest_do_read_inner(RestHandle *conn) {
    rest_clear_ws_meta(conn);

    char line[8192];
    if (rest_read_line(conn, line, sizeof line) <= 0)
        return v_err("rest_read: read failed");

    char method[32], path[4096], version[32];
    if (http_split_request_line(line, method, sizeof method, path, sizeof path,
                                version, sizeof version) != 0)
        return v_err("rest_read: bad request line");

    V *hdrs = v_dict_empty();
    int nhdr = 0;
    size_t hdr_len = 0;
    char hdr_block[REST_MAX_HDR];
    hdr_block[0] = 0;

    for (;;) {
        if (rest_read_line(conn, line, sizeof line) < 0) {
            v_free(hdrs);
            return v_err("rest_read: header read failed");
        }
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\r' || line[ll - 1] == '\n')) line[--ll] = 0;
        {
            const char *hn = line;
            while (*hn == ' ' || *hn == '\t') hn++;
            if (!strncasecmp(hn, "Transfer-Encoding", 17)) {
                const char *c = hn + 17;
                while (*c == ' ' || *c == '\t') c++;
                if (*c == ':' || *c == 0) {
                    v_free(hdrs);
                    return v_err("rest_read: Transfer-Encoding is not supported");
                }
            }
        }
        if (line[0] == 0) break;
        if (++nhdr > 128) {
            v_free(hdrs);
            return v_err("rest_read: too many headers");
        }
        if (hdr_len + ll + 2 >= REST_MAX_HDR) {
            v_free(hdrs);
            return v_err("rest_read: headers too large");
        }
        if (hdr_len) {
            hdr_block[hdr_len++] = '\n';
        }
        memcpy(hdr_block + hdr_len, line, ll);
        hdr_len += ll;
        hdr_block[hdr_len] = 0;
        char *colon = strchr(line, ':');
        if (colon) {
            char *name_end = colon;
            while (name_end > line && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
            *name_end = 0;
            if (!strcasecmp(line, "Content-Length") && rest_header_get(hdrs, "Content-Length")) {
                v_free(hdrs);
                return v_err("rest_read: duplicate Content-Length");
            }
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            v_dict_put(hdrs, line, v_str(val));
            if (!strcasecmp(line, "Sec-WebSocket-Key")) {
                if (strlen(val) > 128) {
                    v_free(hdrs);
                    return v_err("rest_read: websocket key too long");
                }
                free(conn->ws_key);
                conn->ws_key = strdup(val);
            } else if (!strcasecmp(line, "Origin")) {
                free(conn->ws_origin);
                conn->ws_origin = strdup(val);
            } else if (!strcasecmp(line, "Sec-WebSocket-Protocol")) {
                free(conn->ws_protocol);
                char *comma = strchr(val, ',');
                if (comma) {
                    size_t n = (size_t)(comma - val);
                    while (n > 0 && (val[n - 1] == ' ' || val[n - 1] == '\t')) n--;
                    conn->ws_protocol = malloc(n + 1);
                    if (conn->ws_protocol) {
                        memcpy(conn->ws_protocol, val, n);
                        conn->ws_protocol[n] = 0;
                    }
                } else {
                    conn->ws_protocol = strdup(val);
                }
            }
        }
    }

    if (conn->loopback) {
        const char *host = rest_header_get(hdrs, "Host");
        if (host && !http_host_is_local(host) &&
            !http_host_allowed(host, getenv("SHAKTI_REST_ALLOWED_HOSTS"))) {
            v_free(hdrs);
            rest_reject(conn, 400, "Bad Request");
            return v_err("rest_read: non-local Host on loopback listener (see SHAKTI_REST_ALLOWED_HOSTS)");
        }
        const char *origin = conn->ws_origin;
        const char *upg = rest_header_get(hdrs, "Upgrade");
        int is_ws = upg && !strcasecmp(upg, "websocket");
        int safe = !strcmp(method, "GET") || !strcmp(method, "HEAD") || !strcmp(method, "OPTIONS");
        if (!is_ws && !safe && origin && origin[0] && !http_origin_is_local(origin) &&
            !http_origin_allowed(origin, getenv("SHAKTI_REST_ALLOWED_ORIGINS"))) {
            v_free(hdrs);
            rest_reject(conn, 403, "Forbidden");
            return v_err("rest_read: cross-origin request blocked (see SHAKTI_REST_ALLOWED_ORIGINS)");
        }
    }

    if (g_rest_server_token[0]) {
        const char *auth = NULL;
        for (int64_t i = 0; i < hdrs->keys->n; i++) {
            V *k = hdrs->keys->L[i];
            if (k && k->t == T_STR && !strcasecmp(k->s, "Authorization")) {
                V *v = hdrs->vals->L[i];
                if (v && v->t == T_STR) auth = v->s;
            }
        }
        const char *bearer = auth;
        if (bearer && !strncasecmp(bearer, "Bearer ", 7)) bearer += 7;
        else bearer = NULL;
        if (!bearer || !rest_token_eq(bearer, g_rest_server_token)) {
            v_free(hdrs);
            rest_reject(conn, 401, "Unauthorized");
            return v_err("rest_read: unauthorized");
        }
    }

    size_t content_len = 0;
    for (int64_t i = 0; i < hdrs->keys->n; i++) {
        V *k = hdrs->keys->L[i];
        if (k->t == T_STR && !strcasecmp(k->s, "Content-Length")) {
            V *v = hdrs->vals->L[i];
            if (v->t == T_STR) {
                char *end = NULL;
                unsigned long long cl = strtoull(v->s, &end, 10);
                if (!v->s[0] || v->s[0] == '+' || v->s[0] == '-' || (end && *end) || cl > SIZE_MAX) {
                    v_free(hdrs);
                    return v_err("rest_read: bad Content-Length");
                }
                content_len = (size_t)cl;
            }
            break;
        }
    }
    if (content_len > REST_MAX_BODY) {
        v_free(hdrs);
        return v_err("rest_read: body too large");
    }

    char *body = malloc(content_len + 1);
    if (!body) {
        v_free(hdrs);
        return v_err("rest: out of memory");
    }
    if (content_len > 0) {
        if (rest_read_full(conn, body, content_len) != (ssize_t)content_len) {
            free(body);
            v_free(hdrs);
            return v_err("rest_read: body read failed");
        }
    }
    body[content_len] = 0;

    const char *ct = rest_header_get(hdrs, "Content-Type");
    V *req = v_dict_empty();
    v_dict_put(req, "method", v_str(method));
    v_dict_put(req, "path", v_str(path));
    if (rest_body_looks_binary(ct)) {
        V *decoded = body_value((const unsigned char *)body, content_len, ct);
        if (decoded->t == T_ERR) {
            free(body);
            v_free(hdrs);
            v_free(req);
            return decoded;
        }
        v_dict_put(req, "body", decoded);
        V *raw = cvec_from_raw((const unsigned char *)body, content_len);
        if (raw->t == T_ERR) {
            free(body);
            v_free(hdrs);
            v_free(req);
            return raw;
        }
        v_dict_put(req, "raw", raw);
    } else {
        v_dict_put(req, "body", v_str(body));
    }
    v_dict_put(req, "headers", hdrs);
    free(body);
    return req;
}

static V *rest_do_read(int conn_h) {
    RestHandle *conn = rest_slot(conn_h);
    if (!conn || conn->kind != REST_KIND_CONN) return v_err("rest_read: invalid connection handle");
    rest_init_tokens();
    int fl = fcntl(conn->fd, F_GETFL, 0);
    if (fl < 0 || fcntl(conn->fd, F_SETFL, fl | O_NONBLOCK) < 0)
        return v_err("rest_read: fcntl failed");
    conn->deadline = rest_now_ms() + rest_env_long("SHAKTI_REST_READ_TIMEOUT_MS",
                                                   REST_READ_TIMEOUT_MS, 1, 3600000);
    V *r = rest_do_read_inner(conn);
    conn->deadline = 0;
    fcntl(conn->fd, F_SETFL, fl);
    return r;
}

static V *rest_do_write(int conn_h, int status, const unsigned char *body, size_t body_len,
                        const char *content_type) {
    RestHandle *conn = rest_slot(conn_h);
    if (!conn || conn->kind != REST_KIND_CONN) return v_err("rest_write: invalid connection handle");
    if (!body) body = (const unsigned char *)"";
    if (!content_type || !content_type[0]) content_type = "text/plain";
    if (rest_has_ctl(content_type))
        return v_err("rest_write: content_type has control characters");

    const char *reason = "OK";
    if (status == 201) reason = "Created";
    else if (status == 204) reason = "No Content";
    else if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 415) reason = "Unsupported Media Type";
    else if (status >= 500) reason = "Internal Server Error";

    size_t blen = body_len;
    if (blen > REST_MAX_BODY)
        return v_err("rest_write: response too large");

    char hdr[512];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      status, reason, content_type, blen);
    if (hn < 0 || (size_t)hn >= sizeof hdr)
        return v_err("rest_write: response too large");

    size_t sent = 0;
    while (sent < (size_t)hn) {
        ssize_t w = rest_io_write(conn, hdr + sent, (size_t)hn - sent);
        if (w < 0)
            return v_errf("rest_write: write failed: %s", strerror(errno));
        sent += (size_t)w;
    }
    sent = 0;
    while (sent < blen) {
        ssize_t w = rest_io_write(conn, body + sent, blen - sent);
        if (w < 0)
            return v_errf("rest_write: write failed: %s", strerror(errno));
        sent += (size_t)w;
    }
    return v_nil();
}

static V *rest_do_close(int h) {
    RestHandle *s = rest_slot(h);
    if (!s) return v_err("rest_close: invalid handle");
    rest_clear_ws_meta(s);
#ifdef SHAKTI_HAVE_TLS
    if (s->ssl) {
        shakti_tls_close(s->ssl, s->fd);
        s->ssl = NULL;
        s->fd = -1;
    } else
#endif
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
#ifdef SHAKTI_HAVE_TLS
    if (s->ssl_ctx) {
        shakti_tls_server_ctx_free(s->ssl_ctx);
        s->ssl_ctx = NULL;
    }
#endif
    s->closed = 1;
    s->in_use = 0;
    return v_nil();
}

static V *rest_do_set_nonblock(int h, int enabled) {
    RestHandle *s = rest_slot(h);
    if (!s) return v_err("rest_set_nonblock: invalid handle");
    if (s->fd < 0) return v_err("rest_set_nonblock: no fd");
    int flags = fcntl(s->fd, F_GETFL, 0);
    if (flags < 0) return v_err("rest_set_nonblock: fcntl failed");
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    if (fcntl(s->fd, F_SETFL, flags) < 0) return v_err("rest_set_nonblock: fcntl failed");
    return v_nil();
}

#endif /* !SHAKTI_WASM */

V *bi_rest_request(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR, v_err("rest_request(method, url[, body, content_type, headers])"))
    const unsigned char *body = (const unsigned char *)"";
    size_t body_len = 0;
    if (n > 2) {
        if (rest_body_bytes(a[2], &body, &body_len) != 0)
            return v_err("rest_request: body must be str or list[char]");
    }
    const char *ctype = (n > 3 && a[3]->t == T_STR) ? a[3]->s : "";
    V *hdrs = (n > 4 && a[4]->t == T_DICT) ? a[4] : NULL;
    return rest_http_request(a[0]->s, a[1]->s, body, body_len, ctype, hdrs);
#endif
}

V *bi_rest_get(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_get(url)"))
    return rest_http_request("GET", a[0]->s, (const unsigned char *)"", 0, "", NULL);
#endif
}

V *bi_rest_post(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_post(url[, body, content_type])"))
    const unsigned char *body = (const unsigned char *)"";
    size_t body_len = 0;
    if (n > 1) {
        if (rest_body_bytes(a[1], &body, &body_len) != 0)
            return v_err("rest_post: body must be str or list[char]");
    }
    const char *ctype = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    return rest_http_request("POST", a[0]->s, body, body_len, ctype, NULL);
#endif
}

V *bi_rest_put(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_put(url[, body, content_type])"))
    const unsigned char *body = (const unsigned char *)"";
    size_t body_len = 0;
    if (n > 1) {
        if (rest_body_bytes(a[1], &body, &body_len) != 0)
            return v_err("rest_put: body must be str or list[char]");
    }
    const char *ctype = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    return rest_http_request("PUT", a[0]->s, body, body_len, ctype, NULL);
#endif
}

V *bi_rest_delete(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_delete(url)"))
    return rest_http_request("DELETE", a[0]->s, (const unsigned char *)"", 0, "", NULL);
#endif
}

V *bi_rest_listen(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("rest_listen(port[, host])"))
    const char *host = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "127.0.0.1";
    return rest_do_listen((int)a[0]->j, host);
#endif
}

V *bi_rest_listen_tls(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
#ifndef SHAKTI_HAVE_TLS
    (void)a;
    (void)n;
    return v_err("rest_listen_tls: TLS not built");
#else
    P(n < 3 || a[0]->t != T_INT || a[1]->t != T_STR || a[2]->t != T_STR,
      v_err("rest_listen_tls(port, cert, key[, host])"))
    const char *host = (n > 3 && a[3]->t == T_STR) ? a[3]->s : "127.0.0.1";
    return rest_do_listen_tls((int)a[0]->j, host, a[1]->s, a[2]->s);
#endif
#endif
}

V *bi_rest_accept(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("rest_accept(listen_h)"))
    return rest_do_accept((int)a[0]->j);
#endif
}

V *bi_rest_read(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("rest_read(conn)"))
    return rest_do_read((int)a[0]->j);
#endif
}

V *bi_rest_write(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_INT, v_err("rest_write(conn, status[, body, content_type])"))
    const unsigned char *body = (const unsigned char *)"";
    size_t body_len = 0;
    if (n > 2) {
        if (rest_body_bytes(a[2], &body, &body_len) != 0)
            return v_err("rest_write: body must be str or list[char]");
    }
    const char *ctype = (n > 3 && a[3]->t == T_STR) ? a[3]->s : "";
    return rest_do_write((int)a[0]->j, (int)a[1]->j, body, body_len, ctype);
#endif
}

V *bi_rest_close(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_INT, v_err("rest_close(h)"))
    return rest_do_close((int)a[0]->j);
#endif
}

V *bi_rest_set_nonblock(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 2 || a[0]->t != T_INT, v_err("rest_set_nonblock(h, enabled)"))
    int en = 0;
    if (a[1]->t == T_INT) en = a[1]->j != 0;
    else if (a[1]->t == T_FLOAT) en = a[1]->f != 0;
    else return v_err("rest_set_nonblock: enabled must be int");
    return rest_do_set_nonblock((int)a[0]->j, en);
#endif
}

#ifdef SHAKTI_WASM
int rest_take_conn(int h, int *out_fd, void **out_ssl) {
    (void)h;
    (void)out_fd;
    (void)out_ssl;
    return -1;
}
int rest_peek_ws_key(int h, char *out, size_t out_cap) {
    (void)h;
    (void)out;
    (void)out_cap;
    return -1;
}
int rest_peek_ws_protocol(int h, char *out, size_t out_cap) {
    (void)h;
    (void)out;
    (void)out_cap;
    return -1;
}
int rest_peek_ws_origin(int h, char *out, size_t out_cap, int *loopback) {
    (void)h;
    (void)out;
    (void)out_cap;
    (void)loopback;
    return -1;
}
#endif
