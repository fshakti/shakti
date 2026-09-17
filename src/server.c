/* HTTP JSON-RPC daemon (POST /rpc). Loopback by default. */
#include "server.h"
#include "json_parse.h"
#ifdef SHAKTI_HAVE_TLS
#include "tls.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVE_MAX_REQ (256 * 1024)

static int g_serve_loopback = 1;
static char g_serve_token[256];
static __thread char g_cors_origin[256];
static __thread int g_cors_origin_ok;
#ifdef SHAKTI_HAVE_TLS
static __thread void *g_ssl;
static void *g_tls_ctx;
#endif

static ssize_t conn_read(int fd, void *buf, size_t n) {
#ifdef SHAKTI_HAVE_TLS
    if (g_ssl) return tls_read(g_ssl, buf, n);
#endif
    return read(fd, buf, n);
}

static ssize_t conn_write(int fd, const void *buf, size_t n) {
#ifdef SHAKTI_HAVE_TLS
    if (g_ssl) return tls_write(g_ssl, buf, n);
#endif
    return write(fd, buf, n);
}

static int token_eq(const char *a, const char *b) {
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

static int http_header_value(const char *buf, const char *name, char *out, size_t out_cap) {
    if (!buf || !name || !out || out_cap == 0) return 0;
    out[0] = 0;
    size_t nlen = strlen(name);
    const char *p = buf;
    while (*p && !(p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) break;
        if ((size_t)(eol - p) >= nlen && !strncasecmp(p, name, nlen) && p[nlen] == ':') {
            const char *val = p + nlen + 1;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            size_t n = (size_t)(eol - val);
            if (n >= out_cap) n = out_cap - 1;
            memcpy(out, val, n);
            out[n] = 0;
            return 1;
        }
        p = eol + 2;
    }
    return 0;
}

static int origin_is_localhost(const char *origin) {
    if (!origin || !origin[0]) return 0;
    const char *p = origin;
    if (!strncmp(p, "http://", 7)) p += 7;
    else if (!strncmp(p, "https://", 8)) p += 8;
    else return 0;
    if (*p == '[')
        return !strncmp(p, "[::1]", 5) && (p[5] == 0 || p[5] == ':' || p[5] == '/');
    const char *end = p;
    while (*end && *end != ':' && *end != '/') end++;
    size_t n = (size_t)(end - p);
    return (n == 9 && !strncmp(p, "localhost", 9)) || (n == 9 && !strncmp(p, "127.0.0.1", 9));
}

static int host_is_local(const char *host) {
    if (!host || !host[0]) return 0;
    char tmp[256];
    size_t n = strlen(host);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, host, n);
    tmp[n] = 0;
    char *colon = strrchr(tmp, ':');
    if (colon && strchr(tmp, ':') == colon) *colon = 0;
    return !strcmp(tmp, "127.0.0.1") || !strcmp(tmp, "localhost")
        || !strcmp(tmp, "[::1]") || !strcmp(tmp, "::1");
}

static void http_respond(int fd, int code, const char *status,
                         const char *ctype, const char *body) {
    char hdr[768];
    int blen = body ? (int)strlen(body) : 0;
    const char *cors = g_cors_origin_ok ? g_cors_origin : NULL;
    int hlen;
    if (cors) {
        hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Access-Control-Allow-Origin: %s\r\n"
            "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n", code, status, ctype ? ctype : "text/plain", cors, blen);
    } else {
        hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n", code, status, ctype ? ctype : "text/plain", blen);
    }
    if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) {
        const char *fb =
            "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n"
            "Content-Length: 21\r\nConnection: close\r\n\r\nheader too large\n";
        conn_write(fd, fb, strlen(fb));
        return;
    }
    conn_write(fd, hdr, (size_t)hlen);
    if (body && blen > 0) conn_write(fd, body, (size_t)blen);
}

static V *server_invoke_fn(V *fn, V **args, int nargs) {
    if (!fn || fn->t != T_FN)
        return v_err("not callable");
    Env *call_env = env_new(fn->closure);
    V *params = fn->params;
    for (int i = 0; i < params->n; i++) {
        if (i < nargs)
            env_set(call_env, params->L[i]->s, args[i]);
        else if (fn->defaults && i < fn->defaults->n && fn->defaults->L[i]->t != T_NIL)
            env_set(call_env, params->L[i]->s, fn->defaults->L[i]);
    }
    Node *body = fn_ast[(int)fn->j];
    V *result = eval_fn(body, call_env);
    if (g_returning) {
        g_returning = 0;
        v_free(result);
        result = g_retval ? g_retval : v_nil();
        g_retval = NULL;
    }
    env_free(call_env);
    return result;
}

typedef struct {
    char *p;
    size_t n;
    size_t cap;
} JsonBuf;

static int jb_grow(JsonBuf *b, size_t need) {
    if (b->n + need + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->n + need + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) return -1;
    b->p = p;
    b->cap = cap;
    return 0;
}

static int jb_put(JsonBuf *b, const char *s, size_t n) {
    if (jb_grow(b, n) != 0) return -1;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
    return 0;
}

static int jb_cstr(JsonBuf *b, const char *s) {
    return jb_put(b, s, s ? strlen(s) : 0);
}

static int jb_quoted(JsonBuf *b, const char *s) {
    if (jb_cstr(b, "\"") != 0) return -1;
    if (!s) s = "";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char tmp[8];
        const char *esc = NULL;
        size_t n = 0;
        if (c == '"') esc = "\\\"", n = 2;
        else if (c == '\\') esc = "\\\\", n = 2;
        else if (c == '\n') esc = "\\n", n = 2;
        else if (c == '\r') esc = "\\r", n = 2;
        else if (c == '\t') esc = "\\t", n = 2;
        else if (c < 0x20) {
            snprintf(tmp, sizeof tmp, "\\u%04x", c);
            esc = tmp;
            n = 6;
        }
        if (esc) {
            if (jb_put(b, esc, n) != 0) return -1;
        } else if (jb_put(b, (const char *)&c, 1) != 0) return -1;
    }
    return jb_cstr(b, "\"");
}

static int json_encode(JsonBuf *b, V *v);

static int json_encode(JsonBuf *b, V *v) {
    if (!v || v->t == T_NIL) return jb_cstr(b, "null");
    if (v->t == T_BOOL) return jb_cstr(b, v->j ? "true" : "false");
    if (v->t == T_INT) {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "%lld", (long long)v->j);
        return jb_cstr(b, tmp);
    }
    if (v->t == T_FLOAT) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%.17g", v->f);
        return jb_cstr(b, tmp);
    }
    if (v->t == T_STR) return jb_quoted(b, v->s);
    if (v->t == T_LIST) {
        if (jb_cstr(b, "[") != 0) return -1;
        for (int64_t i = 0; i < v->n; i++) {
            if (i && jb_cstr(b, ",") != 0) return -1;
            if (json_encode(b, v->L[i]) != 0) return -1;
        }
        return jb_cstr(b, "]");
    }
    if (v->t == T_DICT) {
        if (jb_cstr(b, "{") != 0) return -1;
        int first = 1;
        for (int64_t i = 0; i < v->keys->n; i++) {
            V *k = v->keys->L[i];
            if (!k || k->t != T_STR) continue;
            if (!first && jb_cstr(b, ",") != 0) return -1;
            first = 0;
            if (jb_quoted(b, k->s) != 0) return -1;
            if (jb_cstr(b, ":") != 0) return -1;
            if (json_encode(b, v->vals->L[i]) != 0) return -1;
        }
        return jb_cstr(b, "}");
    }
    return jb_quoted(b, type_name(v->t));
}

static char *json_dumps_val(V *v) {
    JsonBuf b = {0};
    if (json_encode(&b, v) != 0) {
        free(b.p);
        return NULL;
    }
    return b.p;
}

static V *http_result(int status, const char *ctype, const char *body) {
    V *d = v_dict_empty();
    v_dict_put(d, "status", v_int(status));
    v_dict_put(d, "content_type", v_str(ctype ? ctype : "text/plain"));
    v_dict_put(d, "body", v_str(body ? body : ""));
    return d;
}

static V *rpc_error_http(int code, const char *msg, V *id) {
    V *env = v_dict_empty();
    v_dict_put(env, "jsonrpc", v_str("2.0"));
    if (id) v_dict_set(env, "id", id);
    else v_dict_put(env, "id", v_nil());
    V *errd = v_dict_empty();
    v_dict_put(errd, "code", v_int(code));
    v_dict_put(errd, "message", v_str(msg ? msg : "error"));
    v_dict_put(env, "error", errd);
    char *js = json_dumps_val(env);
    v_free(env);
    V *http = http_result(200, "application/json",
        js ? js : "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"dumps\"},\"id\":null}");
    free(js);
    return http;
}

static V *rpc_ok_http(V *id, V *result) {
    V *env = v_dict_empty();
    v_dict_put(env, "jsonrpc", v_str("2.0"));
    if (id) v_dict_set(env, "id", id);
    else v_dict_put(env, "id", v_nil());
    if (result) v_dict_set(env, "result", result);
    else v_dict_put(env, "result", v_nil());
    char *js = json_dumps_val(env);
    v_free(env);
    V *http = http_result(200, "application/json",
        js ? js : "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"dumps\"},\"id\":null}");
    free(js);
    return http;
}

static int is_rpc_path(const char *path) {
    return path && (!strcmp(path, "/rpc") || !strcmp(path, "/jsonrpc"));
}

static V *dispatch_rpc(Env *global, const char *body) {
    V *req = shakti_json_parse(body ? body : "", NULL);
    if (!req || req->t == T_ERR) {
        if (req) v_free(req);
        return rpc_error_http(-32700, "parse error", NULL);
    }
    if (req->t != T_DICT) {
        v_free(req);
        return rpc_error_http(-32600, "invalid request", NULL);
    }
    V *method = v_dict_get(req, "method");
    V *params = v_dict_get(req, "params");
    V *id = v_dict_get(req, "id");
    int notification = !id;
    if (!method || method->t != T_STR) {
        V *http = rpc_error_http(-32600, "invalid request", notification ? NULL : id);
        v_free(req);
        return http;
    }

    V *handler = global ? env_get(global, "handle_rpc") : NULL;
    if (handler && handler->t == T_FN) {
        V *nilp = NULL, *nili = NULL;
        V *a1 = params;
        if (!a1) { nilp = v_nil(); a1 = nilp; }
        V *a2 = id;
        if (!a2) { nili = v_nil(); a2 = nili; }
        V *args[3] = {method, a1, a2};
        V *r = server_invoke_fn(handler, args, 3);
        if (nilp) v_free(nilp);
        if (nili) v_free(nili);
        if (notification) {
            v_free(r);
            v_free(req);
            return http_result(204, "text/plain", "");
        }
        if (!r || r->t == T_ERR) {
            const char *msg = (r && r->t == T_ERR && r->s) ? r->s : "internal error";
            V *http = rpc_error_http(-32603, msg, id);
            v_free(r);
            v_free(req);
            return http;
        }
        if (r->t == T_DICT) {
            char *js = json_dumps_val(r);
            v_free(r);
            V *http = http_result(200, "application/json",
                js ? js : "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"dumps\"},\"id\":null}");
            free(js);
            v_free(req);
            return http;
        }
        V *http = rpc_ok_http(id, r);
        v_free(r);
        v_free(req);
        return http;
    }

    if (notification) {
        v_free(req);
        return http_result(204, "text/plain", "");
    }
    if (!strcmp(method->s, "ping")) {
        V *pong = v_str("pong");
        V *http = rpc_ok_http(id, pong);
        v_free(pong);
        v_free(req);
        return http;
    }
    V *http = rpc_error_http(-32601, "method not found", id);
    v_free(req);
    return http;
}

static V *dispatch_request(Env *global, const char *method, const char *path,
                           const char *body, const char *content_type) {
    if (!global) return v_err("server: no runtime env");
    V *handler = env_get(global, "handle_request");
    if (!handler || handler->t != T_FN)
        return v_err("server: define handle_request or POST /rpc");
    V *a0 = v_str(method ? method : "");
    V *a1 = v_str(path ? path : "");
    V *a2 = v_str(body ? body : "");
    V *a3 = v_str(content_type ? content_type : "");
    V *args[4] = {a0, a1, a2, a3};
    V *r = server_invoke_fn(handler, args, 4);
    v_free(a0);
    v_free(a1);
    v_free(a2);
    v_free(a3);
    return r;
}

static void client_done(int cfd) {
#ifdef SHAKTI_HAVE_TLS
    if (g_ssl) {
        tls_free(g_ssl);
        g_ssl = NULL;
    }
#endif
    close(cfd);
}

static char *http_body(char *buf) {
    char *p = strstr(buf, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

static void http_parse_method(const char *buf, char *method, char *path) {
    method[0] = 0;
    path[0] = 0;
    sscanf(buf, "%15s %255s", method, path);
}

static int dict_int(V *d, const char *key, int def) {
    V *v = v_dict_get(d, key);
    if (!v || v->t != T_INT) return def;
    return (int)v->j;
}

static const char *dict_str(V *d, const char *key, const char *def) {
    V *v = v_dict_get(d, key);
    if (!v || v->t != T_STR) return def;
    return v->s;
}

static int http_content_length(const char *buf) {
    const char *p = buf;
    while (p && *p) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) break;
        if (eol == p) break;
        if ((eol - p) >= 15 && !strncasecmp(p, "Content-Length:", 15)) {
            const char *num = p + 15;
            while (num < eol && (*num == ' ' || *num == '\t')) num++;
            if (num >= eol) return -2;
            char *end = NULL;
            long v = strtol(num, &end, 10);
            if (end == num || v < 0 || v > (long)SERVE_MAX_REQ) return -2;
            while (end < eol && (*end == ' ' || *end == '\t')) end++;
            if (end != eol) return -2;
            return (int)v;
        }
        p = eol + 2;
    }
    return -1;
}

static ssize_t http_read_request(int fd, char *buf, size_t cap) {
    size_t nread = 0;
    while (nread + 1 < cap) {
        ssize_t r = conn_read(fd, buf + nread, cap - 1 - nread);
        if (r < 0) {
            if (errno == EINTR) continue;
            return nread ? (ssize_t)nread : -1;
        }
        if (r == 0) break;
        nread += (size_t)r;
        buf[nread] = 0;
        char *body = http_body(buf);
        if (!body) continue;
        int cl = http_content_length(buf);
        if (cl == -2) return -1;
        if (cl < 0) break;
        size_t have = (size_t)(nread - (size_t)(body - buf));
        if (have >= (size_t)cl) {
            body[cl] = 0;
            break;
        }
    }
    if (nread > 0) buf[nread] = 0;
    if (nread > 0) {
        char *body = http_body(buf);
        int cl = body ? http_content_length(buf) : -1;
        if (cl == -2) return -1;
        if (cl >= 0 && body) {
            size_t have = (size_t)(nread - (size_t)(body - buf));
            if (have < (size_t)cl) return -1;
            body[cl] = 0;
        }
    }
    return (ssize_t)nread;
}

static void handle_client(int cfd, Env *global) {
    g_cors_origin[0] = 0;
    g_cors_origin_ok = 0;
#ifdef SHAKTI_HAVE_TLS
    g_ssl = NULL;
    if (g_tls_ctx) {
        g_ssl = tls_accept(cfd, g_tls_ctx);
        if (!g_ssl) {
            close(cfd);
            return;
        }
    }
#endif
    char *buf = calloc(1, SERVE_MAX_REQ);
    if (!buf) {
        client_done(cfd);
        return;
    }
    ssize_t nread = http_read_request(cfd, buf, SERVE_MAX_REQ);
    if (nread <= 0) {
        free(buf);
        client_done(cfd);
        return;
    }
    char method[16] = {0}, path[256] = {0};
    http_parse_method(buf, method, path);
    char *q = strchr(path, '?');
    if (q) *q = 0;

    char origin[256] = {0}, hosthdr[256] = {0}, auth[512] = {0};
    http_header_value(buf, "Origin", origin, sizeof origin);
    http_header_value(buf, "Host", hosthdr, sizeof hosthdr);
    http_header_value(buf, "Authorization", auth, sizeof auth);
    if (origin_is_localhost(origin)) {
        snprintf(g_cors_origin, sizeof g_cors_origin, "%s", origin);
        g_cors_origin_ok = 1;
    }

    if (g_serve_loopback && hosthdr[0] && !host_is_local(hosthdr)) {
        http_respond(cfd, 400, "Bad Request", "application/json",
                     "{\"error\":\"invalid Host\"}");
        free(buf);
        client_done(cfd);
        return;
    }

    if (g_serve_token[0]) {
        const char *bearer = auth;
        if (!strncasecmp(bearer, "Bearer ", 7)) bearer += 7;
        else bearer = NULL;
        if (!bearer || !token_eq(bearer, g_serve_token)) {
            if (strcmp(method, "OPTIONS") != 0) {
                http_respond(cfd, 401, "Unauthorized", "application/json",
                             "{\"error\":\"unauthorized\"}");
                free(buf);
                client_done(cfd);
                return;
            }
        }
    }
    {
        int mutating = !strcmp(method, "POST") || !strcmp(method, "PUT")
            || !strcmp(method, "PATCH") || !strcmp(method, "DELETE");
        if (mutating && origin[0] && !origin_is_localhost(origin)) {
            http_respond(cfd, 403, "Forbidden", "application/json",
                         "{\"error\":\"cross-origin request blocked\"}");
            free(buf);
            client_done(cfd);
            return;
        }
    }

    if (!strcmp(method, "OPTIONS")) {
        http_respond(cfd, 204, "No Content", "text/plain", "");
        free(buf);
        client_done(cfd);
        return;
    }

    char req_ctype[256] = {0};
    http_header_value(buf, "Content-Type", req_ctype, sizeof req_ctype);
    char *body = http_body(buf);

    V *resp;
    if (!strcmp(method, "POST") && is_rpc_path(path)) {
        const char *ct = req_ctype;
        int bin_ct = 0;
        if (!strncasecmp(ct, "application/iefs", 16)) {
            char c = ct[16];
            bin_ct = (c == 0 || c == ';' || c == ' ' || c == '\t');
        } else if (!strncasecmp(ct, "application/x-iefs", 18)) {
            char c = ct[18];
            bin_ct = (c == 0 || c == ';' || c == ' ' || c == '\t');
        } else if (!strncasecmp(ct, "application/x-hld", 17)) {
            char c = ct[17];
            bin_ct = (c == 0 || c == ';' || c == ' ' || c == '\t');
        } else if (!strncasecmp(ct, "application/hld", 15)) {
            char c = ct[15];
            bin_ct = (c == 0 || c == ';' || c == ' ' || c == '\t');
        }
        if (bin_ct) {
            http_respond(cfd, 415, "Unsupported Media Type", "application/json",
                         "{\"error\":\"unsupported media type\"}");
            free(buf);
            client_done(cfd);
            return;
        }
        resp = dispatch_rpc(global, body ? body : "");
    } else
        resp = dispatch_request(global, method, path, body ? body : "", req_ctype);

    if (!resp || resp->t == T_ERR) {
        const char *msg = (resp && resp->t == T_ERR && resp->s) ? resp->s : "internal error";
        char jbuf[512];
        snprintf(jbuf, sizeof jbuf, "{\"error\":\"%s\"}", msg);
        http_respond(cfd, 500, "Internal Server Error", "application/json", jbuf);
        v_free(resp);
        free(buf);
        client_done(cfd);
        return;
    }
    if (resp->t != T_DICT) {
        http_respond(cfd, 500, "Internal Server Error", "application/json",
                     "{\"error\":\"handler must return dict\"}");
        v_free(resp);
        free(buf);
        client_done(cfd);
        return;
    }

    int status = dict_int(resp, "status", 200);
    const char *ctype = dict_str(resp, "content_type", "application/json");
    const char *status_txt = status >= 400 ? "Error" : "OK";
    const char *b = dict_str(resp, "body", "{}");
    http_respond(cfd, status, status_txt, ctype, b);
    v_free(resp);
    free(buf);
    client_done(cfd);
}

void server_serve(int port, Env *global_env) {
    signal(SIGPIPE, SIG_IGN);
    if (port < 1 || port > 65535) {
        printf("[serve] invalid port %d (must be 1-65535)\n", port);
        return;
    }
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        printf("[serve] socket failed\n");
        return;
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    const char *allow = getenv("SHAKTI_SERVE_ALLOW_PUBLIC");
    int public_ok = allow && allow[0] == '1' && allow[1] == '\0';
    const char *host = "127.0.0.1";
    g_serve_loopback = 1;
    if (public_ok) {
        host = "0.0.0.0";
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        g_serve_loopback = 0;
    } else {
        inet_pton(AF_INET, host, &addr.sin_addr);
    }

    g_serve_token[0] = 0;
    const char *tok = getenv("SHAKTI_SERVE_TOKEN");
    if (tok && tok[0]) {
        size_t tlen = strlen(tok);
        if (tlen >= sizeof g_serve_token) {
            printf("[serve] SHAKTI_SERVE_TOKEN too long\n");
            close(srv);
            return;
        }
        memcpy(g_serve_token, tok, tlen + 1);
    }
    if (!g_serve_loopback && !g_serve_token[0]) {
        printf("[serve] non-loopback bind requires SHAKTI_SERVE_TOKEN\n");
        close(srv);
        return;
    }

#ifdef SHAKTI_HAVE_TLS
    g_tls_ctx = NULL;
    const char *cert = getenv("SHAKTI_TLS_CERT");
    const char *key = getenv("SHAKTI_TLS_KEY");
    if (cert && cert[0] && key && key[0]) {
        g_tls_ctx = tls_server_ctx(cert, key);
        if (!g_tls_ctx) {
            printf("[serve] TLS cert/key failed\n");
            close(srv);
            return;
        }
    }
#endif

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        printf("[serve] bind failed: %s\n", strerror(errno));
        close(srv);
        return;
    }
    if (listen(srv, 16) < 0) {
        printf("[serve] listen failed\n");
        close(srv);
        return;
    }
    printf("[serve] listening on %s:%d  POST /rpc\n", host, port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        int cfd = accept(srv, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(cfd, global_env);
    }
    close(srv);
#ifdef SHAKTI_HAVE_TLS
    if (g_tls_ctx) {
        tls_server_ctx_free(g_tls_ctx);
        g_tls_ctx = NULL;
    }
#endif
}

V *bi_server_serve(V **a, int n, Env *e) {
#ifdef SHAKTI_WASM
    (void)a;
    (void)n;
    (void)e;
    return v_err("server_serve: not available in WASM");
#else
    int port = 8080;
    if (n > 0 && a[0]->t == T_INT) port = (int)a[0]->j;
    server_serve(port, e);
    return v_nil();
#endif
}
