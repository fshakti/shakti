#include "rest.h"
#include "json_parse.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
} RestHandle;

static char g_rest_token[4096];
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

static void rest_init(void) {
    if (g_rest_inited) return;
    g_rest_inited = 1;
    g_rest_token[0] = 0;
    const char *env = getenv("SHAKTI_REST_TOKEN");
    if (env && env[0]) strncpy(g_rest_token, env, sizeof g_rest_token - 1);
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
 * interpreted by curl as an option, and blocks file://, etc. */
static int rest_host_is_blocked_ip(const struct sockaddr *sa) {
    if (!sa) return 1;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(sin4->sin_addr.s_addr);
        if ((a & 0xff000000u) == 0x7f000000u) return 1; /* 127/8 */
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
        if (zero && (b[15] == 0 || b[15] == 1)) return 1; /* :: / ::1 */
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
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (rest_host_is_blocked_ip(ai->ai_addr)) {
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

static V *body_value(const char *raw, size_t len) {
    if (!raw) return v_str("");
    while (len > 0 && (raw[len - 1] == '\n' || raw[len - 1] == '\r')) len--;
    if (len == 0) return v_str("");
    char *tmp = malloc(len + 1);
    if (!tmp) return v_err("rest: out of memory");
    memcpy(tmp, raw, len);
    tmp[len] = 0;
    if (tmp[0] == '{' || tmp[0] == '[') {
        V *parsed = shakti_json_parse(tmp, NULL);
        if (parsed) {
            free(tmp);
            return parsed;
        }
    }
    V *out = v_str(tmp);
    free(tmp);
    return out;
}

static V *make_response(int status, const char *raw_body, size_t body_len, V *headers) {
    V *resp = v_dict_empty();
    v_dict_put(resp, "status", v_int(status));
    V *body = body_value(raw_body, body_len);
    if (body->t == T_ERR) {
        v_free(resp);
        return body;
    }
    v_dict_put(resp, "body", body);
    /* Never copy from a NULL source: if raw_body is NULL, force length 0 so a
     * nonzero body_len can't over-read the "" literal. */
    size_t copy_len = raw_body ? body_len : 0;
    char *raw_copy = malloc(copy_len + 1);
    if (!raw_copy) {
        v_free(resp);
        return v_err("rest: out of memory");
    }
    memcpy(raw_copy, raw_body ? raw_body : "", copy_len);
    raw_copy[copy_len] = 0;
    V *raw = v_str(raw_copy);
    free(raw_copy);
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
            return i;
        }
    }
    return -1;
}

static ssize_t rest_read_full(int fd, char *buf, size_t want) {
    size_t got = 0;
    while (got < want) {
        ssize_t n = read(fd, buf + got, want - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static ssize_t rest_read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    if (i + 1 >= cap && (i == 0 || buf[i - 1] != '\n'))
        return -1;
    return (ssize_t)i;
}

/* Run curl with an explicit argv (no shell) and capture its stdout (the
 * -w http_code) into code_path. Returns curl's exit status, or -1 on error.
 * posix_spawn avoids a full address-space copy from fork(). */
static int rest_run_curl(char *const argv[], const char *code_path) {
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
    int rc = posix_spawnp(&pid, "curl", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(ofd);
    if (dn >= 0) close(dn);
    if (rc != 0) return -1;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static V *rest_http_request(const char *method, const char *url, const char *body,
                            const char *content_type, V *extra_hdrs) {
    rest_init();
    if (!method || !method[0] || !url || !url[0])
        return v_err("rest: empty method or url");
    if (rest_has_ctl(method) || rest_has_ctl(url) || rest_has_ctl(g_rest_token))
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
    if (body && body[0]) {
        int data_fd = open(g_rest_data_path, O_WRONLY | O_TRUNC | O_NOFOLLOW);
        if (data_fd < 0)
            return v_err("rest: temp file failed");
        size_t blen = strlen(body);
        if (write(data_fd, body, blen) != (ssize_t)blen) {
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
    if (g_rest_token[0] && rest_token_host_ok(url))
        snprintf(auth_hdr, sizeof auth_hdr, "Authorization: Bearer %s", g_rest_token);

    /* Assemble a curl argv (no shell). Each value is passed to execvp() as a
     * literal argument, so URL/header/token contents cannot inject a command. */
    int nextra = (extra_hdrs && extra_hdrs->t == T_DICT) ? (int)extra_hdrs->keys->n : 0;
    int max_args = 36 + nextra * 2;
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
    V *out = make_response(status, raw_body, blen, hdrs);
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
    if (port < 1 || port > 65535) return v_err("rest_listen: invalid port");
    if (!host || !host[0]) host = "127.0.0.1";
    if (!strcmp(host, "localhost")) host = "127.0.0.1";
    int loopback = !strcmp(host, "127.0.0.1") || !strcmp(host, "::1") || !strcmp(host, "localhost");
    {
        const char *allow = getenv("SHAKTI_REST_ALLOW_PUBLIC");
        if (!loopback && !(allow && allow[0] == '1' && allow[1] == '\0'))
            return v_err("rest_listen: non-loopback bind requires SHAKTI_REST_ALLOW_PUBLIC=1");
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
    return v_int(h);
}

static V *rest_do_accept(int listen_h) {
    RestHandle *srv = rest_slot(listen_h);
    if (!srv || srv->kind != REST_KIND_LISTEN) return v_err("rest_accept: invalid listen handle");

    struct sockaddr_storage peer;
    socklen_t plen = sizeof peer;
    int cfd = accept(srv->fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) return v_err("rest_accept: accept failed");
    rest_set_cloexec(cfd);

    int h = rest_alloc(REST_KIND_CONN, cfd);
    if (h < 0) {
        close(cfd);
        return v_err("rest_accept: too many handles");
    }
    return v_int(h);
}

static V *rest_do_read(int conn_h) {
    RestHandle *conn = rest_slot(conn_h);
    if (!conn || conn->kind != REST_KIND_CONN) return v_err("rest_read: invalid connection handle");

    char line[8192];
    if (rest_read_line(conn->fd, line, sizeof line) <= 0)
        return v_err("rest_read: read failed");

    char method[32], path[4096], version[32];
    if (sscanf(line, "%31s %4095s %31s", method, path, version) < 2)
        return v_err("rest_read: bad request line");

    V *hdrs = v_dict_empty();
    size_t hdr_len = 0;
    char hdr_block[REST_MAX_HDR];
    hdr_block[0] = 0;

    for (;;) {
        if (rest_read_line(conn->fd, line, sizeof line) < 0) {
            v_free(hdrs);
            return v_err("rest_read: header read failed");
        }
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\r' || line[ll - 1] == '\n')) line[--ll] = 0;
        if (line[0] == 0) break;
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
            *colon = 0;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            v_dict_put(hdrs, line, v_str(val));
        }
    }

    size_t content_len = 0;
    for (int64_t i = 0; i < hdrs->keys->n; i++) {
        V *k = hdrs->keys->L[i];
        if (k->t == T_STR && !strcasecmp(k->s, "Content-Length")) {
            V *v = hdrs->vals->L[i];
            if (v->t == T_STR) content_len = (size_t)strtoull(v->s, NULL, 10);
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
        if (rest_read_full(conn->fd, body, content_len) != (ssize_t)content_len) {
            free(body);
            v_free(hdrs);
            return v_err("rest_read: body read failed");
        }
    }
    body[content_len] = 0;

    V *req = v_dict_empty();
    v_dict_put(req, "method", v_str(method));
    v_dict_put(req, "path", v_str(path));
    v_dict_put(req, "body", v_str(body));
    v_dict_put(req, "headers", hdrs);
    free(body);
    return req;
}

static V *rest_do_write(int conn_h, int status, const char *body, const char *content_type) {
    RestHandle *conn = rest_slot(conn_h);
    if (!conn || conn->kind != REST_KIND_CONN) return v_err("rest_write: invalid connection handle");
    if (!body) body = "";
    if (!content_type || !content_type[0]) content_type = "text/plain";
    if (rest_has_ctl(content_type))
        return v_err("rest_write: content_type has control characters");

    const char *reason = "OK";
    if (status == 201) reason = "Created";
    else if (status == 204) reason = "No Content";
    else if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status >= 500) reason = "Internal Server Error";

    size_t blen = strlen(body);
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
        ssize_t w = write(conn->fd, hdr + sent, (size_t)hn - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return v_err("rest_write: write failed");
        }
        sent += (size_t)w;
    }
    sent = 0;
    while (sent < blen) {
        ssize_t w = write(conn->fd, body + sent, blen - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return v_err("rest_write: write failed");
        }
        sent += (size_t)w;
    }
    return v_nil();
}

static V *rest_do_close(int h) {
    RestHandle *s = rest_slot(h);
    if (!s) return v_err("rest_close: invalid handle");
    if (s->fd >= 0) close(s->fd);
    s->closed = 1;
    s->in_use = 0;
    s->fd = -1;
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
    const char *body = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    const char *ctype = (n > 3 && a[3]->t == T_STR) ? a[3]->s : "";
    V *hdrs = (n > 4 && a[4]->t == T_DICT) ? a[4] : NULL;
    return rest_http_request(a[0]->s, a[1]->s, body, ctype, hdrs);
#endif
}

V *bi_rest_get(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_get(url)"))
    return rest_http_request("GET", a[0]->s, "", "", NULL);
#endif
}

V *bi_rest_post(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_post(url[, body, content_type])"))
    const char *body = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "";
    const char *ctype = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    return rest_http_request("POST", a[0]->s, body, ctype, NULL);
#endif
}

V *bi_rest_put(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_put(url[, body, content_type])"))
    const char *body = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "";
    const char *ctype = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    return rest_http_request("PUT", a[0]->s, body, ctype, NULL);
#endif
}

V *bi_rest_delete(V **a, int n) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    return v_err("rest: not available in WASM");
#else
    P(n < 1 || a[0]->t != T_STR, v_err("rest_delete(url)"))
    return rest_http_request("DELETE", a[0]->s, "", "", NULL);
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
    const char *body = (n > 2 && a[2]->t == T_STR) ? a[2]->s : "";
    const char *ctype = (n > 3 && a[3]->t == T_STR) ? a[3]->s : "";
    return rest_do_write((int)a[0]->j, (int)a[1]->j, body, ctype);
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
