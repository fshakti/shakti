#include "lissen.h"
#include "json_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SHAKTI_LISSEN_API_DEFAULT
#define SHAKTI_LISSEN_API_DEFAULT "https://api.lissenprod.lissen.live"
#endif

#ifndef SHAKTI_LISSEN_APP_DEFAULT
#define SHAKTI_LISSEN_APP_DEFAULT "https://app.lissen.com"
#endif

#ifndef SHAKTI_LISSEN_WEB_DEFAULT
#define SHAKTI_LISSEN_WEB_DEFAULT "https://www.lissen.com"
#endif

static char g_lissen_token[4096];
static char g_lissen_api_base[512];
static int g_lissen_inited;

static void lissen_init(void) {
    if (g_lissen_inited) return;
    g_lissen_inited = 1;
    g_lissen_token[0] = 0;
    strncpy(g_lissen_api_base, SHAKTI_LISSEN_API_DEFAULT, sizeof g_lissen_api_base - 1);
    const char *env = getenv("SHAKTI_LISSEN_TOKEN");
    if (env && env[0]) strncpy(g_lissen_token, env, sizeof g_lissen_token - 1);
    env = getenv("SHAKTI_LISSEN_API");
    if (env && env[0]) strncpy(g_lissen_api_base, env, sizeof g_lissen_api_base - 1);
}

static void url_encode(const char *src, char *out, size_t out_sz) {
    size_t o = 0;
    if (!src) src = "";
    for (; *src && o + 4 < out_sz; src++) {
        unsigned char c = (unsigned char)*src;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            o += (size_t)snprintf(out + o, out_sz - o, "%%%02X", c);
        }
    }
    out[o] = 0;
}

static int shell_unsafe(const char *s) { return s && strchr(s, '\''); }

static char *read_all_pipe(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len + 4096 >= cap) {
            cap *= 2;
            char *n = realloc(buf, cap);
            if (!n) { free(buf); return NULL; }
            buf = n;
        }
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        len += n;
        if (n == 0) break;
    }
    buf[len] = 0;
    return buf;
}

static V *lissen_http(const char *method, const char *url, const char *body) {
    lissen_init();
    if (shell_unsafe(url) || shell_unsafe(g_lissen_token))
        return v_err("lissen: unsafe character in URL or token");
    char auth[4200];
    auth[0] = 0;
    if (g_lissen_token[0])
        snprintf(auth, sizeof auth, "-H 'Authorization: Bearer %s'", g_lissen_token);

    char tmpl[] = "/tmp/shakti-lissen-XXXXXX";
    char data_opt[128];
    data_opt[0] = 0;
    int tmp_fd = -1;
    if (body && body[0]) {
        tmp_fd = mkstemp(tmpl);
        if (tmp_fd < 0) return v_err("lissen: temp file failed");
        size_t blen = strlen(body);
        if (write(tmp_fd, body, blen) != (ssize_t)blen) {
            close(tmp_fd);
            unlink(tmpl);
            return v_err("lissen: write failed");
        }
        close(tmp_fd);
        snprintf(data_opt, sizeof data_opt, "-H 'Content-Type: application/json' --data-binary @%s", tmpl);
    }

    char cmd[16384];
    snprintf(cmd, sizeof cmd, "curl -sS -m 60 -X %s %s %s '%s' 2>/dev/null",
             method, auth, data_opt, url);
    FILE *p = popen(cmd, "r");
    if (!p) {
        if (data_opt[0]) unlink(tmpl);
        return v_err("lissen: curl failed");
    }
    char *resp = read_all_pipe(p);
    int rc = pclose(p);
    if (data_opt[0]) unlink(tmpl);
    if (!resp) return v_err("lissen: out of memory");
    if (rc != 0) {
        free(resp);
        return v_err("lissen: curl request failed");
    }
    V *out = shakti_json_parse(resp, NULL);
    free(resp);
    return out ? out : v_err("lissen: invalid JSON response");
}

static V *lissen_trpc_call(const char *path, const char *input_json, int mutation) {
    P(!path || !path[0], v_err("lissen: empty procedure path"))
    lissen_init();
    if (!input_json || !input_json[0]) input_json = "{}";

    char enc[8192];
    url_encode(input_json, enc, sizeof enc);

    char url[1024];
    if (mutation) {
        snprintf(url, sizeof url, "%s/api/trpc/%s", g_lissen_api_base, path);
        char body[8704];
        snprintf(body, sizeof body, "{\"json\":%s}", input_json);
        return lissen_http("POST", url, body);
    }
    snprintf(url, sizeof url, "%s/api/trpc/%s?input=%s", g_lissen_api_base, path, enc);
    return lissen_http("GET", url, NULL);
}

V *bi_lissen_trpc_query(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_trpc_query(path[, input_json])"))
    const char *input = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "{}";
    return lissen_trpc_call(a[0]->s, input, 0);
}

V *bi_lissen_trpc_mutation(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_trpc_mutation(path[, input_json])"))
    const char *input = (n > 1 && a[1]->t == T_STR) ? a[1]->s : "{}";
    return lissen_trpc_call(a[0]->s, input, 1);
}

V *bi_lissen_set_token(V **a, int n) {
    lissen_init();
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_set_token(token)"))
    strncpy(g_lissen_token, a[0]->s, sizeof g_lissen_token - 1);
    g_lissen_token[sizeof g_lissen_token - 1] = 0;
    return v_nil();
}

V *bi_lissen_get_token(V **a, int n) {
    (void)a;
    (void)n;
    lissen_init();
    return g_lissen_token[0] ? v_str(g_lissen_token) : v_nil();
}

V *bi_lissen_set_api_base(V **a, int n) {
    lissen_init();
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_set_api_base(url)"))
    strncpy(g_lissen_api_base, a[0]->s, sizeof g_lissen_api_base - 1);
    g_lissen_api_base[sizeof g_lissen_api_base - 1] = 0;
    size_t len = strlen(g_lissen_api_base);
    while (len > 0 && g_lissen_api_base[len - 1] == '/') {
        g_lissen_api_base[len - 1] = 0;
        len--;
    }
    return v_nil();
}

V *bi_lissen_get_api_base(V **a, int n) {
    (void)a;
    (void)n;
    lissen_init();
    return v_str(g_lissen_api_base);
}

V *bi_lissen_app_url(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_app_url(kind[, id])"))
    const char *kind = a[0]->s;
    const char *id = (n > 1 && a[1]->t == T_STR) ? a[1]->s : NULL;
    char url[1024];
    if (!id || !id[0]) {
        if (!strcmp(kind, "home") || !strcmp(kind, "app"))
            return v_str(SHAKTI_LISSEN_APP_DEFAULT);
        snprintf(url, sizeof url, "%s/%s", SHAKTI_LISSEN_APP_DEFAULT, kind);
        return v_str(url);
    }
    snprintf(url, sizeof url, "%s/%s/%s", SHAKTI_LISSEN_APP_DEFAULT, kind, id);
    return v_str(url);
}

V *bi_lissen_web_url(V **a, int n) {
    const char *path = (n > 0 && a[0]->t == T_STR) ? a[0]->s : "";
    char url[1024];
    if (!path[0]) return v_str(SHAKTI_LISSEN_WEB_DEFAULT);
    if (path[0] == '/') snprintf(url, sizeof url, "%s%s", SHAKTI_LISSEN_WEB_DEFAULT, path);
    else snprintf(url, sizeof url, "%s/%s", SHAKTI_LISSEN_WEB_DEFAULT, path);
    return v_str(url);
}

V *bi_lissen_open(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("lissen_open(url)"))
    if (shell_unsafe(a[0]->s)) return v_bool(0);
#if defined(__APPLE__)
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1", a[0]->s);
#elif defined(_WIN32)
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "start \"\" \"%s\"", a[0]->s);
#else
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1", a[0]->s);
#endif
    int rc = system(cmd);
    return rc == 0 ? v_bool(1) : v_bool(0);
}
