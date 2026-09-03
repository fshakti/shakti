/*
 * wasm_host.c — Shakti in the browser: boot site/main.ie, tick, gfx blit, synth PCM, REPL eval.
 */
#include "shakti.h"
#include "shakti_internal.h"
#include "gfx.h"
#include "synth_platform.h"

#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Node *parse(const char *src);
extern int shakti_parse_errors(void);
extern void gfx_wasm_set_window(int w, int h);

void midi_decode_bytes(const unsigned char *data, int len) {
    (void)data;
    (void)len;
}

#define AUDIO_FRAMES 512

static Env *g_env;
static Node *g_prog;
static Node *g_tick_ast;
static Node *g_start_ast;
static float g_audio[AUDIO_FRAMES];
static int g_booted;
static int g_try_env0;

static char *slurp_file(const char *path) {
    FILE *f;
    long sz;
    char *buf;
    size_t got;
    f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = malloc((size_t)sz + 2);
    if (!buf) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\n';
    buf[got + 1] = 0;
    return buf;
}

static int eval_named(Node *ast) {
    V *r;
    int code = 0;
    if (!g_env || !ast) return 0;
    r = eval(ast, g_env);
    if (r && r->t == T_INT) code = (int)r->j;
    if (r && r->t == T_ERR && r->s)
        fprintf(stderr, "shakti: %s\n", r->s);
    v_free(r);
    return code;
}

EMSCRIPTEN_KEEPALIVE
int shakti_boot(void) {
    char *src;
    setenv("SHAKTI_LIB", "/shakti/lib", 1);
    setenv("SHAKTI_SAFE", "1", 1);
    setenv("SHAKTI_ALLOW_EXEC", "0", 1);
    setenv("SHAKTI_SYNTH_HEADLESS", "1", 1);
    setenv("SHAKTI_GFX_NO_SLEEP", "1", 1);
    snprintf(g_lib_path, sizeof(g_lib_path), "/shakti/lib");

    g_env = env_new(NULL);
    builtin_register(g_env);

    src = slurp_file("/site/main.ie");
    if (!src) {
        fprintf(stderr, "shakti: missing /site/main.ie\n");
        return -1;
    }
    g_prog = parse(src);
    free(src);
    if (shakti_parse_errors() > 0) {
        fprintf(stderr, "shakti: parse errors in main.ie\n");
        return -1;
    }
    {
        V *r = eval(g_prog, g_env);
        if (r && r->t == T_ERR && r->s)
            fprintf(stderr, "shakti boot: %s\n", r->s);
        v_free(r);
    }
    g_tick_ast = parse("tick()");
    g_start_ast = parse("start()");
    g_try_env0 = g_env ? g_env->len : 0;
    g_booted = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int shakti_start(void) {
    if (!g_booted) return -1;
    return eval_named(g_start_ast);
}

EMSCRIPTEN_KEEPALIVE
int shakti_tick(void) {
    if (!g_booted) return 0;
    return eval_named(g_tick_ast);
}

EMSCRIPTEN_KEEPALIVE
uint32_t *shakti_fb_ptr(void) {
    uint32_t *p = gfx_core_present_pixels();
    return p ? p : gfx_core_design_pixels();
}

EMSCRIPTEN_KEEPALIVE
int shakti_fb_w(void) {
    int w = gfx_core_present_width();
    return w > 0 ? w : gfx_core_design_width();
}

EMSCRIPTEN_KEEPALIVE
int shakti_fb_h(void) {
    int h = gfx_core_present_height();
    return h > 0 ? h : gfx_core_design_height();
}

EMSCRIPTEN_KEEPALIVE
void shakti_mouse(int x, int y, int down) {
    if (down)
        gfx_core_mouse_design(x, y, 1);
    else
        gfx_core_mouse_design(x, y, 0);
}

EMSCRIPTEN_KEEPALIVE
void shakti_mouse_move(int x, int y) { gfx_core_mouse_move(x, y, 0); }

EMSCRIPTEN_KEEPALIVE
void shakti_resize(int w, int h) {
    gfx_wasm_set_window(w, h);
    gfx_core_fb_resize(w, h);
}

EMSCRIPTEN_KEEPALIVE
float *shakti_audio_buf(void) { return g_audio; }

EMSCRIPTEN_KEEPALIVE
int shakti_audio_frames(void) { return AUDIO_FRAMES; }

EMSCRIPTEN_KEEPALIVE
void shakti_render(int frames) {
    if (frames < 1) return;
    if (frames > AUDIO_FRAMES) frames = AUDIO_FRAMES;
    synth_core_render(g_audio, frames);
}

static char *trim_dup(const char *src) {
    const char *s;
    size_t n;
    char *out;
    if (!src) return strdup("");
    s = src;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    out = malloc(n + 1);
    if (!out) return strdup("");
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

static char *wasm_repl_vars(int names_only) {
    char *buf = NULL;
    size_t sz = 0;
    FILE *fp;
    int i;
    fp = open_memstream(&buf, &sz);
    if (!fp) return strdup("");
    if (g_env) {
        int start = g_try_env0;
        if (start < 0) start = 0;
        if (start > g_env->len) start = g_env->len;
        for (i = start; i < g_env->len; i++) {
            if (names_only) {
                fprintf(fp, "%s\n", g_env->names[i] ? g_env->names[i] : "");
            } else {
                fprintf(fp, "%-15s", g_env->names[i] ? g_env->names[i] : "");
                print_val(g_env->vals[i], fp, 1);
                fputc('\n', fp);
            }
        }
    }
    fclose(fp);
    return buf ? buf : strdup("");
}

static char *wasm_repl_cmd(const char *line) {
    if (!line || !line[0]) return NULL;
    if (strcmp(line, "\\v") == 0) return wasm_repl_vars(0);
    if (strcmp(line, "\\w") == 0) return wasm_repl_vars(1);
    if (strcmp(line, "\\d") == 0 || strcmp(line, "\\help") == 0 || strcmp(line, "help") == 0) {
        char *doc = slurp_file("/shakti/IE.txt");
        if (doc) return doc;
        return strdup("Cannot open IE.txt");
    }
    if (line[0] == '\\') {
        char *msg;
        size_t n = strlen(line) + 48;
        msg = malloc(n);
        if (!msg) return strdup("Unknown REPL command");
        snprintf(msg, n, "Unknown REPL command: %s (try \\d for help)", line);
        return msg;
    }
    return NULL;
}

/* REPL eval: malloc'd UTF-8 result (caller frees). Empty string if silent. */
EMSCRIPTEN_KEEPALIVE
char *shakti_eval(const char *src) {
    Node *prog;
    V *result;
    char *out = NULL;
    char *line;
    char *cmd;
    if (!g_booted || !g_env) return strdup("shakti: not booted");
    if (!src) return strdup("");
    line = trim_dup(src);
    cmd = wasm_repl_cmd(line);
    free(line);
    if (cmd) return cmd;
    prog = parse(src);
    if (shakti_parse_errors() > 0) {
        node_free(prog);
        return strdup("parse error");
    }
    if (!prog) return strdup("");
    result = eval(prog, g_env);
    if (g_error) {
        const char *msg = (g_error_val && g_error_val->s) ? g_error_val->s : "error";
        out = strdup(msg);
        if (g_error_val) { v_free(g_error_val); g_error_val = NULL; }
        g_error = 0;
    } else if (result && result->t == T_ERR && result->s) {
        out = strdup(result->s);
    } else if (!shakti_prog_silent_last(prog) && result && result->t != T_NIL && result->t != T_ERR) {
        char *repr = v_repr(result);
        out = repr ? repr : strdup("");
    } else {
        out = strdup("");
    }
    v_free(result);
    node_free(prog);
    return out ? out : strdup("");
}

int main(void) { return 0; }
