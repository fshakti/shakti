#include "rest.h"
#include "json_parse.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#define REST_MAX_HANDLES 128
#define REST_MAX_HDR 65536
#define REST_MAX_BODY (1024 * 1024)

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

#ifndef SHAKTI_WASM
static RestHandle g_rest_handles[REST_MAX_HANDLES];
#endif

static void rest_init(void) {
    if (g_rest_inited) return;
    g_rest_inited = 1;
    g_rest_token[0] = 0;
    const char *env = getenv("SHAKTI_REST_TOKEN");
    if (env && env[0]) strncpy(g_rest_token, env, sizeof g_rest_token - 1);
}

static int shell_unsafe(const char *s) { return s && strchr(s, '\''); }

static char *read_all_file(const char *path, size_t *out_len) {
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
    V *resp = v_dict(v_list(0), v_list(0));
    v_dict_set(resp, "status", v_int(status));
    V *body = body_value(raw_body, body_len);
    if (body->t == T_ERR) {
        v_free(resp);
        return body;
    }
    v_dict_set(resp, "body", body);
    v_free(body);
    char *raw_copy = malloc(body_len + 1);
    if (!raw_copy) {
        v_free(resp);
        return v_err("rest: out of memory");
    }
    memcpy(raw_copy, raw_body ? raw_body : "", body_len);
    raw_copy[body_len] = 0;
    V *raw = v_str(raw_copy);
    free(raw_copy);
    v_dict_set(resp, "raw", raw);
    v_free(raw);
    if (!headers) headers = v_dict(v_list(0), v_list(0));
    v_dict_set(resp, "headers", v_ref(headers));
    v_free(headers);
    return resp;
}

static V *parse_curl_headers(const char *path) {
    V *hdrs = v_dict(v_list(0), v_list(0));
    char *text = read_all_file(path, NULL);
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
            v_dict_set(hdrs, key, v_str(val));
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
    return (ssize_t)i;
}

static V *rest_http_request(const char *method, const char *url, const char *body,
                            const char *content_type, V *extra_hdrs) {
    rest_init();
    if (!method || !method[0] || !url || !url[0])
        return v_err("rest: empty method or url");
    if (shell_unsafe(url) || shell_unsafe(g_rest_token) || shell_unsafe(method))
        return v_err("rest: unsafe character in method, url, or token");
    if (body && shell_unsafe(body)) return v_err("rest: unsafe character in body");
    if (content_type && shell_unsafe(content_type))
        return v_err("rest: unsafe character in content_type");

    char hdr_tmpl[] = "/tmp/shakti-rest-hdr-XXXXXX";
    char body_tmpl[] = "/tmp/shakti-rest-body-XXXXXX";
    char code_tmpl[] = "/tmp/shakti-rest-code-XXXXXX";
    int hdr_fd = mkstemp(hdr_tmpl);
    int body_fd = mkstemp(body_tmpl);
    int code_fd = mkstemp(code_tmpl);
    if (hdr_fd < 0 || body_fd < 0 || code_fd < 0) {
        if (hdr_fd >= 0) { close(hdr_fd); unlink(hdr_tmpl); }
        if (body_fd >= 0) { close(body_fd); unlink(body_tmpl); }
        if (code_fd >= 0) { close(code_fd); unlink(code_tmpl); }
        return v_err("rest: temp file failed");
    }
    close(hdr_fd);
    close(body_fd);
    close(code_fd);

    char data_tmpl[] = "/tmp/shakti-rest-data-XXXXXX";
    char data_opt[256];
    data_opt[0] = 0;
    int data_fd = -1;
    if (body && body[0]) {
        data_fd = mkstemp(data_tmpl);
        if (data_fd < 0) {
            unlink(hdr_tmpl);
            unlink(body_tmpl);
            unlink(code_tmpl);
            return v_err("rest: temp file failed");
        }
        size_t blen = strlen(body);
        if (write(data_fd, body, blen) != (ssize_t)blen) {
            close(data_fd);
            unlink(data_tmpl);
            unlink(hdr_tmpl);
            unlink(body_tmpl);
            unlink(code_tmpl);
            return v_err("rest: write failed");
        }
        close(data_fd);
        const char *ct = (content_type && content_type[0]) ? content_type : "application/json";
        snprintf(data_opt, sizeof data_opt,
                 "-H 'Content-Type: %s' --data-binary @%s", ct, data_tmpl);
    }

    char auth[4200];
    auth[0] = 0;
    if (g_rest_token[0])
        snprintf(auth, sizeof auth, "-H 'Authorization: Bearer %s'", g_rest_token);

    char extra[8192];
    extra[0] = 0;
    if (extra_hdrs && extra_hdrs->t == T_DICT) {
        size_t pos = 0;
        for (int64_t i = 0; i < extra_hdrs->keys->n && pos + 64 < sizeof extra; i++) {
            V *k = extra_hdrs->keys->L[i];
            V *v = extra_hdrs->vals->L[i];
            if (k->t != T_STR || v->t != T_STR) continue;
            if (shell_unsafe(k->s) || shell_unsafe(v->s))
                return v_err("rest: unsafe character in headers");
            int n = snprintf(extra + pos, sizeof extra - pos, " -H '%s: %s'", k->s, v->s);
            if (n < 0) break;
            pos += (size_t)n;
        }
    }

    char cmd[24576];
    snprintf(cmd, sizeof cmd,
             "curl -sS -m 60 -X %s %s%s %s -D '%s' -o '%s' -w '%%{http_code}' '%s' > '%s' 2>/dev/null",
             method, auth, extra, data_opt[0] ? data_opt : "", hdr_tmpl, body_tmpl, url, code_tmpl);

    int rc = system(cmd);
    if (data_opt[0]) unlink(data_tmpl);
    if (rc != 0) {
        unlink(hdr_tmpl);
        unlink(body_tmpl);
        unlink(code_tmpl);
        return v_err("rest: curl request failed");
    }

    char *code_s = read_all_file(code_tmpl, NULL);
    int status = code_s ? atoi(code_s) : 0;
    free(code_s);

    size_t blen = 0;
    char *raw_body = read_all_file(body_tmpl, &blen);
    V *hdrs = parse_curl_headers(hdr_tmpl);
    unlink(hdr_tmpl);
    unlink(body_tmpl);
    unlink(code_tmpl);

    if (!raw_body) {
        v_free(hdrs);
        return v_err("rest: read body failed");
    }
    V *out = make_response(status, raw_body, blen, hdrs);
    free(raw_body);
    return out;
}

static V *rest_do_listen(int port, const char *host) {
    if (port < 1 || port > 65535) return v_err("rest_listen: invalid port");
    if (!host || !host[0]) host = "127.0.0.1";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return v_err("rest_listen: socket failed");

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

    struct sockaddr_in peer;
    socklen_t plen = sizeof peer;
    int cfd = accept(srv->fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) return v_err("rest_accept: accept failed");

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

    V *hdrs = v_dict(v_list(0), v_list(0));
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
            strcat(hdr_block, "\n");
            hdr_len++;
        }
        strcat(hdr_block, line);
        hdr_len += ll;
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = 0;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            v_dict_set(hdrs, line, v_str(val));
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

    V *req = v_dict(v_list(0), v_list(0));
    v_dict_set(req, "method", v_str(method));
    v_dict_set(req, "path", v_str(path));
    v_dict_set(req, "body", v_str(body));
    v_dict_set(req, "headers", v_ref(hdrs));
    free(body);
    v_free(hdrs);
    return req;
}

static V *rest_do_write(int conn_h, int status, const char *body, const char *content_type) {
    RestHandle *conn = rest_slot(conn_h);
    if (!conn || conn->kind != REST_KIND_CONN) return v_err("rest_write: invalid connection handle");
    if (!body) body = "";
    if (!content_type || !content_type[0]) content_type = "text/plain";

    const char *reason = "OK";
    if (status == 201) reason = "Created";
    else if (status == 204) reason = "No Content";
    else if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status >= 500) reason = "Internal Server Error";

    char resp[REST_MAX_BODY + 4096];
    size_t blen = strlen(body);
    int n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     status, reason, content_type, blen, body);
    if (n < 0 || (size_t)n >= sizeof resp) return v_err("rest_write: response too large");

    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t w = write(conn->fd, resp + sent, (size_t)n - sent);
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
