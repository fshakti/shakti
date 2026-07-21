#include "sonicpi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum {
    SP_ARG_NONE = 0,
    SP_ARG_INT,
    SP_ARG_FLOAT,
    SP_ARG_STR,
} SpArgKind;

typedef struct {
    SpArgKind kind;
    int32_t i;
    float f;
    const char *s;
} SpArg;

static char g_host[256] = "127.0.0.1";
static int g_port = 4560;
static int g_fd = -1;
static struct sockaddr_in g_addr;
static int g_addr_ok = 0;

static size_t sp_pad4(size_t n) { return (n + 3u) & ~3u; }

static uint32_t sp_htobe32(uint32_t v) {
    return htonl(v);
}

static void sp_write_be32(char *p, uint32_t v) {
    uint32_t be = sp_htobe32(v);
    memcpy(p, &be, 4);
}

static void sp_write_be_float(char *p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    sp_write_be32(p, u);
}

static int sp_env_defaults(void) {
    const char *h = getenv("SONICPI_HOST");
    const char *p = getenv("SONICPI_PORT");
    if (h && h[0]) {
        strncpy(g_host, h, sizeof g_host - 1);
        g_host[sizeof g_host - 1] = 0;
    }
    if (p && p[0]) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end != p && v > 0 && v < 65536) g_port = (int)v;
    }
    return 0;
}

static int sp_resolve_addr(char *err, size_t err_cap) {
    memset(&g_addr, 0, sizeof g_addr);
    g_addr.sin_family = AF_INET;
    g_addr.sin_port = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, g_host, &g_addr.sin_addr) != 1) {
        snprintf(err, err_cap, "sonicpi: bad host %s", g_host);
        return -1;
    }
    g_addr_ok = 1;
    return 0;
}

static int sp_open_socket(char *err, size_t err_cap) {
    if (g_fd >= 0) return 0;
    sp_env_defaults();
    if (sp_resolve_addr(err, err_cap) != 0) return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_cap, "sonicpi: socket failed: %s", strerror(errno));
        return -1;
    }
    g_fd = fd;
    return 0;
}

static int sp_close_socket(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_addr_ok = 0;
    return 0;
}

static int sp_write_path(char *buf, size_t cap, size_t *pos, const char *path) {
    size_t len = strlen(path) + 1;
    size_t need = sp_pad4(len);
    if (*pos + need > cap) return -1;
    memcpy(buf + *pos, path, len);
    if (need > len) memset(buf + *pos + len, 0, need - len);
    *pos += need;
    return 0;
}

static int sp_write_string(char *buf, size_t cap, size_t *pos, const char *s) {
    size_t len = strlen(s) + 1;
    size_t need = sp_pad4(len);
    if (*pos + need > cap) return -1;
    memcpy(buf + *pos, s, len);
    if (need > len) memset(buf + *pos + len, 0, need - len);
    *pos += need;
    return 0;
}

/* OSC wire format: address, type-tag string, then args. */
static int sp_encode_message(const char *path, const SpArg *args, int narg, char *buf, size_t cap,
                             size_t *out_len, char *err, size_t err_cap) {
    size_t pos = 0;
    char tags[SONICPI_MAX_ARGS + 2];
    int ti = 0;

    tags[ti++] = ',';
    for (int i = 0; i < narg; i++) {
        const SpArg *a = &args[i];
        if (a->kind == SP_ARG_INT)
            tags[ti++] = 'i';
        else if (a->kind == SP_ARG_FLOAT)
            tags[ti++] = 'f';
        else if (a->kind == SP_ARG_STR)
            tags[ti++] = 's';
        else {
            snprintf(err, err_cap, "sonicpi: bad arg kind");
            return -1;
        }
    }
    tags[ti] = 0;

    if (sp_write_path(buf, cap, &pos, path) != 0) goto too_large;

    {
        size_t tlen = (size_t)ti + 1;
        size_t tpad = sp_pad4(tlen);
        if (pos + tpad > cap) goto too_large;
        memcpy(buf + pos, tags, tlen);
        if (tpad > tlen) memset(buf + pos + tlen, 0, tpad - tlen);
        pos += tpad;
    }

    for (int i = 0; i < narg; i++) {
        const SpArg *a = &args[i];
        if (a->kind == SP_ARG_INT) {
            if (pos + 4 > cap) goto too_large;
            sp_write_be32(buf + pos, (uint32_t)a->i);
            pos += 4;
        } else if (a->kind == SP_ARG_FLOAT) {
            if (pos + 4 > cap) goto too_large;
            sp_write_be_float(buf + pos, a->f);
            pos += 4;
        } else if (a->kind == SP_ARG_STR) {
            if (sp_write_string(buf, cap, &pos, a->s) != 0) goto too_large;
        }
    }

    *out_len = pos;
    return 0;

too_large:
    snprintf(err, err_cap, "sonicpi: message too large");
    return -1;
}

static int sp_send_encoded(const char *buf, size_t len, char *err, size_t err_cap) {
    if (sp_open_socket(err, err_cap) != 0) return -1;
    ssize_t n = sendto(g_fd, buf, len, 0, (struct sockaddr *)&g_addr, sizeof g_addr);
    if (n < 0) {
        snprintf(err, err_cap, "sonicpi: send failed: %s", strerror(errno));
        return -1;
    }
    if ((size_t)n != len) {
        snprintf(err, err_cap, "sonicpi: short send");
        return -1;
    }
    return 0;
}

static int sp_send_args(const char *path, const SpArg *args, int narg, char *err, size_t err_cap) {
    char buf[SONICPI_MAX_MSG];
    size_t len = 0;
    if (sp_encode_message(path, args, narg, buf, sizeof buf, &len, err, err_cap) != 0) return -1;
    return sp_send_encoded(buf, len, err, err_cap);
}

static float sp_as_float(V *v) {
    if (v->t == T_FLOAT) return (float)v->f;
    if (v->t == T_INT) return (float)v->j;
    return 0.f;
}

static int sp_arg_from_v(V *v, SpArg *out, char *err, size_t err_cap) {
    if (v->t == T_INT) {
        out->kind = SP_ARG_INT;
        out->i = (int32_t)v->j;
        return 0;
    }
    if (v->t == T_FLOAT) {
        out->kind = SP_ARG_FLOAT;
        out->f = (float)v->f;
        return 0;
    }
    if (v->t == T_STR) {
        out->kind = SP_ARG_STR;
        out->s = v->s;
        return 0;
    }
    snprintf(err, err_cap, "sonicpi: arg must be int, float, or str");
    return -1;
}

V *bi_sonicpi_configure(V **a, int n) {
    const char *host = g_host;
    int port = g_port;
    if (n >= 1 && a[0]->t == T_STR) host = a[0]->s;
    if (n >= 2 && a[1]->t == T_INT) port = (int)a[1]->j;
    P(n >= 1 && a[0]->t != T_STR && (n < 2 || a[1]->t != T_INT),
      v_err("sonicpi_configure([host, port])"))
    P(port <= 0 || port >= 65536, v_err("sonicpi_configure: bad port"))
    strncpy(g_host, host, sizeof g_host - 1);
    g_host[sizeof g_host - 1] = 0;
    g_port = port;
    sp_close_socket();
    char err[256];
    err[0] = 0;
    if (sp_open_socket(err, sizeof err) != 0) return v_err(err);
    return v_nil();
}

V *bi_sonicpi_send(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("sonicpi_send(path[, args...])"))
    SpArg args[SONICPI_MAX_ARGS];
    int narg = 0;
    char err[256];
    err[0] = 0;
    for (int i = 1; i < n && narg < SONICPI_MAX_ARGS; i++) {
        if (sp_arg_from_v(a[i], &args[narg], err, sizeof err) != 0) return v_err(err);
        narg++;
    }
    if (n - 1 > SONICPI_MAX_ARGS) return v_err("sonicpi_send: too many args");
    if (sp_send_args(a[0]->s, args, narg, err, sizeof err) != 0) return v_err(err);
    return v_nil();
}

V *bi_sonicpi_play(V **a, int n) {
    P(n < 1, v_err("sonicpi_play(note[, amp, sustain])"))
    SpArg args[3];
    int narg = 0;
    args[narg].kind = SP_ARG_FLOAT;
    args[narg++].f = sp_as_float(a[0]);
    if (n >= 2) {
        args[narg].kind = SP_ARG_FLOAT;
        args[narg++].f = sp_as_float(a[1]);
    } else {
        args[narg].kind = SP_ARG_FLOAT;
        args[narg++].f = 0.8f;
    }
    if (n >= 3) {
        args[narg].kind = SP_ARG_FLOAT;
        args[narg++].f = sp_as_float(a[2]);
    } else {
        args[narg].kind = SP_ARG_FLOAT;
        args[narg++].f = 0.25f;
    }
    char err[256];
    err[0] = 0;
    if (sp_send_args("/shakti/play", args, narg, err, sizeof err) != 0) return v_err(err);
    return v_nil();
}

V *bi_sonicpi_synth(V **a, int n) {
    P(n < 2 || a[0]->t != T_STR, v_err("sonicpi_synth(name, note[, amp, sustain])"))
    SpArg args[4];
    args[0].kind = SP_ARG_STR;
    args[0].s = a[0]->s;
    args[1].kind = SP_ARG_FLOAT;
    args[1].f = sp_as_float(a[1]);
    if (n >= 3) {
        args[2].kind = SP_ARG_FLOAT;
        args[2].f = sp_as_float(a[2]);
    } else {
        args[2].kind = SP_ARG_FLOAT;
        args[2].f = 0.9f;
    }
    if (n >= 4) {
        args[3].kind = SP_ARG_FLOAT;
        args[3].f = sp_as_float(a[3]);
    } else {
        args[3].kind = SP_ARG_FLOAT;
        args[3].f = 1.0f;
    }
    char err[256];
    err[0] = 0;
    if (sp_send_args("/shakti/synth", args, 4, err, sizeof err) != 0) return v_err(err);
    return v_nil();
}

V *bi_sonicpi_stop(V **a, int n) {
    (void)a;
    (void)n;
    char err[256];
    err[0] = 0;
    if (sp_send_args("/shakti/stop", NULL, 0, err, sizeof err) != 0) return v_err(err);
    return v_nil();
}

V *bi_sonicpi_bpm(V **a, int n) {
    P(n < 1, v_err("sonicpi_bpm(bpm)"))
    SpArg args[1];
    args[0].kind = SP_ARG_FLOAT;
    args[0].f = sp_as_float(a[0]);
    char err[256];
    err[0] = 0;
    if (sp_send_args("/shakti/bpm", args, 1, err, sizeof err) != 0) return v_err(err);
    return v_nil();
}
