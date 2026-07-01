#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef SHAKTI_HAVE_SYNTH
#include "synth.h"
#endif

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

enum {
    EV_KEY_DOWN = 1,
    EV_KEY_UP = 2,
    EV_CHAR = 3,
};

typedef struct {
    int kind;
    int code;
    int modifiers;
    char utf8[8];
} InputEvent;

#define INPUT_QCAP 512

typedef struct {
    InputEvent q[INPUT_QCAP];
    int qh, qt;
    int raw;
    int wait_first;
    int own_gui;
    int hz;
    double x, y, wheel;
    int qwerty[65536];
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    struct termios orig;
    int has_orig;
#endif
    int active_streams;
    int mouse_buttons;
    int stop_latch;
} InputHub;

static InputHub g_in;

static int q_empty(void) { return g_in.qh == g_in.qt; }

static int q_full(void) { return ((g_in.qt + 1) % INPUT_QCAP) == g_in.qh; }

static void q_push(const InputEvent *ev) {
    if (q_full()) g_in.qh = (g_in.qh + 1) % INPUT_QCAP;
    g_in.q[g_in.qt] = *ev;
    g_in.qt = (g_in.qt + 1) % INPUT_QCAP;
}

static int q_pop(InputEvent *ev) {
    if (q_empty()) return 0;
    *ev = g_in.q[g_in.qh];
    g_in.qh = (g_in.qh + 1) % INPUT_QCAP;
    return 1;
}

static int is_tty(void) {
#if defined(_WIN32)
    return _isatty(_fileno(stdin));
#else
    return isatty(STDIN_FILENO);
#endif
}

static void qwerty_build(void) {
    static const char *rows[] = {
        "1234567890",
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm",
        NULL
    };
    int i;
    for (i = 0; i < 65536; i++) g_in.qwerty[i] = -1;
    for (i = 0; rows[i]; i++) {
        const char *r = rows[i];
        int j = 0;
        for (; r[j]; j++) {
            int c = (unsigned char)r[j];
            g_in.qwerty[c] = i * 20 + j;
            g_in.qwerty[toupper(c)] = i * 20 + j;
        }
    }
    g_in.qwerty[0xff51] = 100;
    g_in.qwerty[0xff53] = 101;
    g_in.qwerty[0xff52] = 102;
    g_in.qwerty[0xff54] = 103;
}

void input_hub_init(void) {
    static int done;
    if (done) return;
    done = 1;
    memset(&g_in, 0, sizeof g_in);
    g_in.hz = 60;
    qwerty_build();
}

void input_hub_shutdown(void) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (g_in.raw && g_in.has_orig) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_in.orig);
        g_in.raw = 0;
    }
#endif
}

V *input_event_dict(int code, int modifiers, const char *utf8, const char *kind) {
    V *keys = v_list(4);
    V *vals = v_list(4);
    keys->L[0] = v_str("code");
    vals->L[0] = v_int(code);
    keys->L[1] = v_str("modifiers");
    vals->L[1] = v_int(modifiers);
    keys->L[2] = v_str("utf8");
    vals->L[2] = v_str(utf8 ? utf8 : "");
    keys->L[3] = v_str("kind");
    vals->L[3] = v_str(kind ? kind : "down");
    return v_dict(keys, vals);
}

static V *event_to_v(const InputEvent *ev) {
    if (ev->kind == EV_CHAR)
        return v_str(ev->utf8);
    const char *kind = ev->kind == EV_KEY_UP ? "up" : "down";
    return input_event_dict(ev->code, ev->modifiers, ev->utf8, kind);
}

static void input_note_stop_key(int code, int down, const char *utf8) {
    if (!down) return;
    if (utf8 && (utf8[0] == '\r' || utf8[0] == '\n')) {
        g_in.stop_latch = 1;
        return;
    }
    if (code == '\r' || code == '\n' || code == 13 || code == 10 || code == 0xff0d || code == 0xff8d)
        g_in.stop_latch = 1;
}

void input_hub_inject_key(int code, int modifiers, const char *utf8, int down) {
    input_note_stop_key(code, down, utf8);
    InputEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = down ? EV_KEY_DOWN : EV_KEY_UP;
    ev.code = code;
    ev.modifiers = modifiers;
    if (utf8 && utf8[0]) {
        strncpy(ev.utf8, utf8, sizeof ev.utf8 - 1);
    } else if (code >= 32 && code < 127) {
        ev.utf8[0] = (char)code;
    }
    q_push(&ev);
    if (down && ev.utf8[0]) {
        InputEvent up = ev;
        up.kind = EV_KEY_UP;
        q_push(&up);
    }
}

void input_hub_inject_mouse(int x, int y, int buttons) {
    int left = buttons & 1;
    if (left && !(g_in.mouse_buttons & 1))
        g_in.stop_latch = 1;
    g_in.mouse_buttons = left;
    g_in.x = (double)x;
    g_in.y = (double)y;
}

void input_hub_inject_wheel(int delta) {
    g_in.wheel += (double)delta;
}

int input_own_gui(void) { return g_in.own_gui; }
void input_set_own_gui(int on) { g_in.own_gui = on ? 1 : 0; }

double input_get_x(void) { return g_in.x; }
double input_get_y(void) { return g_in.y; }
double input_get_wheel(void) { return g_in.wheel; }
void input_set_x(double x) { g_in.x = x; }
void input_set_y(double y) { g_in.y = y; }
void input_set_wheel(double w) { g_in.wheel = w; }

int input_get_hz(void) { return g_in.hz; }
void input_set_hz(int hz) { if (hz > 0) g_in.hz = hz; }

V *input_get_qwerty(void) {
    V *keys = v_list(128);
    V *vals = v_list(128);
    int n = 0;
    int i;
    for (i = 0; i < 65536 && n < 128; i++) {
        if (g_in.qwerty[i] < 0) continue;
        char kbuf[16];
        snprintf(kbuf, sizeof kbuf, "%d", i);
        keys->L[n] = v_str(kbuf);
        vals->L[n] = v_int(g_in.qwerty[i]);
        n++;
    }
    keys->n = vals->n = n;
    return v_dict(keys, vals);
}

void input_qwerty_reload(void) { qwerty_build(); }

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static void raw_on(void) {
    if (!is_tty() || g_in.raw) return;
    tcgetattr(STDIN_FILENO, &g_in.orig);
    g_in.has_orig = 1;
    struct termios r = g_in.orig;
    r.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    r.c_oflag |= OPOST;
    r.c_cflag |= CS8;
    r.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    r.c_cc[VMIN] = 0;
    r.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
    g_in.raw = 1;
}

static void raw_off(void) {
    if (!g_in.raw || !g_in.has_orig) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_in.orig);
    g_in.raw = 0;
}

static void raw_wait_first(void) {
    if (!is_tty()) return;
    if (!g_in.raw) raw_on();
    struct termios r = g_in.orig;
    r.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    r.c_oflag |= OPOST;
    r.c_cflag |= CS8;
    r.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    r.c_cc[VMIN] = 1;
    r.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
    g_in.wait_first = 1;
}

static void raw_after_first(void) {
    if (!g_in.wait_first) return;
    g_in.wait_first = 0;
    raw_on();
}
#endif

static double now_ms(void) {
    return (double)clock() / (double)(CLOCKS_PER_SEC / 1000);
}

static void pump_synth(void) {
#ifdef SHAKTI_HAVE_SYNTH
    if (synth_alive()) {
        char err[64];
        err[0] = 0;
        synth_tick(err, sizeof err);
    }
#endif
}

static int read_byte_timed(int timeout_ms, unsigned char *out) {
#if defined(_WIN32)
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD avail = 0;
    if (timeout_ms <= 0) {
        if (!PeekConsoleInputA(hin, NULL, 0, &avail) && GetNumberOfConsoleInputEvents(hin, &avail) && avail) {
            INPUT_RECORD rec;
            DWORD nread = 0;
            while (ReadConsoleInputA(hin, &rec, 1, &nread) && nread) {
                if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                    char c = (char)rec.Event.KeyEvent.uChar.AsciiChar;
                    if (c) {
                        *out = (unsigned char)c;
                        return 1;
                    }
                }
            }
        }
        return 0;
    }
    double deadline = now_ms() + (double)timeout_ms;
    for (;;) {
        pump_synth();
        if (GetNumberOfConsoleInputEvents(hin, &avail) && avail) {
            INPUT_RECORD rec;
            DWORD nread = 0;
            if (ReadConsoleInputA(hin, &rec, 1, &nread) && nread &&
                rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
                char c = (char)rec.Event.KeyEvent.uChar.AsciiChar;
                if (c) {
                    *out = (unsigned char)c;
                    return 1;
                }
            }
        }
        if (now_ms() >= deadline) return 0;
        Sleep(1);
    }
#else
    if (!is_tty()) {
        fd_set fds;
        struct timeval tv = {0, 0};
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (timeout_ms > 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
        }
        pump_synth();
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, timeout_ms < 0 ? NULL : &tv) > 0 &&
            FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t n = read(STDIN_FILENO, out, 1);
            return n > 0;
        }
        return 0;
    }
    double deadline = timeout_ms < 0 ? -1.0 : now_ms() + (double)timeout_ms;
    for (;;) {
        pump_synth();
        fd_set fds;
        struct timeval tv = {0, 16000};
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        int rc = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (rc > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t n = read(STDIN_FILENO, out, 1);
            if (n > 0) return 1;
        }
        if (rc < 0) return 0;
        if (timeout_ms == 0) return 0;
        if (deadline >= 0 && now_ms() >= deadline) return 0;
    }
#endif
}

static void emit_key(int code, const char *utf8, int down) {
    InputEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = down ? EV_KEY_DOWN : EV_KEY_UP;
    ev.code = code;
    if (utf8 && utf8[0]) strncpy(ev.utf8, utf8, sizeof ev.utf8 - 1);
    else if (code >= 32 && code < 127) ev.utf8[0] = (char)code;
    q_push(&ev);
}

static int decode_vt_byte(unsigned char c, unsigned char *pending, int *pn) {
    enum { PENDING_MAX = 8 };
    if (*pn == 0 && c == 27) {
        pending[(*pn)++] = c;
        return 0;
    }
    if (*pn > 0) {
        if (*pn >= PENDING_MAX) {
            *pn = 0;
            return 0;
        }
        pending[(*pn)++] = c;
        if (*pn >= 3 && pending[0] == 27 && pending[1] == '[') {
            int code = 0;
            switch (pending[2]) {
            case 'A': code = 0xff52; break;
            case 'B': code = 0xff54; break;
            case 'C': code = 0xff53; break;
            case 'D': code = 0xff51; break;
            case 'H': code = 0xff50; break;
            case 'F': code = 0xff57; break;
            case '3':
                if (*pn >= 4 && pending[3] == '~') {
                    code = 0xffff;
                    emit_key(code, "", 1);
                    emit_key(code, "", 0);
                    *pn = 0;
                    return 1;
                }
                if (*pn >= 4) { *pn = 0; return 0; }
                return 0;
            default:
                if (*pn >= 8) *pn = 0;
                return 0;
            }
            if (code) {
                emit_key(code, "", 1);
                emit_key(code, "", 0);
                *pn = 0;
                return 1;
            }
        }
        if (*pn >= 8) *pn = 0;
        return 0;
    }
    if (c == '\r' || c == '\n') {
        char u[2] = {(char)c, 0};
        input_note_stop_key(c == '\r' ? 0xff0d : c, 1, u);
        if (g_in.active_streams > 0) {
            InputEvent ev;
            memset(&ev, 0, sizeof ev);
            ev.kind = EV_CHAR;
            ev.code = c;
            strncpy(ev.utf8, u, sizeof ev.utf8 - 1);
            q_push(&ev);
            emit_key(c == '\r' ? 0xff0d : c, u, 1);
            emit_key(c == '\r' ? 0xff0d : c, u, 0);
            return 1;
        }
        return 0;
    }
    if (c >= 32 && c < 127) {
        if (g_in.active_streams > 0) {
            char u[2] = {(char)c, 0};
            InputEvent ev;
            memset(&ev, 0, sizeof ev);
            ev.kind = EV_CHAR;
            ev.code = c;
            strncpy(ev.utf8, u, sizeof ev.utf8 - 1);
            q_push(&ev);
            emit_key((int)c, u, 1);
            emit_key((int)c, u, 0);
            return 1;
        }
        return 0;
    }
    return 0;
}

static void tty_drain(int timeout_ms) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (!g_in.raw && is_tty()) raw_on();
#endif
    unsigned char pending[8];
    int pn = 0;
    double deadline = timeout_ms <= 0 ? now_ms() : now_ms() + (double)timeout_ms;
    for (;;) {
        unsigned char c;
        int wait = timeout_ms < 0 ? -1 : (int)(deadline - now_ms());
        if (timeout_ms == 0) wait = 0;
        if (wait < 0 && timeout_ms >= 0 && timeout_ms != 0 && now_ms() >= deadline) break;
        if (!read_byte_timed(wait, &c)) {
            if (timeout_ms == 0) break;
            if (timeout_ms > 0 && now_ms() >= deadline) break;
            if (timeout_ms < 0) continue;
            break;
        }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        raw_after_first();
#endif
        if (!decode_vt_byte(c, pending, &pn)) {
#ifdef _WIN32
            if (c == '\r' || c == '\n') {
                char u[2] = {(char)c, 0};
                input_note_stop_key(c == '\r' ? 0xff0d : c, 1, u);
            } else if (c >= 32) {
                char u[2] = {(char)c, 0};
                InputEvent ev;
                memset(&ev, 0, sizeof ev);
                ev.kind = EV_CHAR;
                ev.code = c;
                strncpy(ev.utf8, u, sizeof ev.utf8 - 1);
                q_push(&ev);
                emit_key((int)c, u, 1);
                emit_key((int)c, u, 0);
            }
#endif
        }
        if (timeout_ms == 0) continue;
    }
}

void input_hub_pump(int timeout_ms) {
    input_hub_init();
    tty_drain(timeout_ms);
}

int input_hub_take_stop(void) {
    input_hub_init();
    if (!g_in.stop_latch) return 0;
    g_in.stop_latch = 0;
    return 1;
}

V *input_poll_ms(int ms) {
    input_hub_init();
    if (ms > 0) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        if (is_tty()) raw_on();
#endif
        double end = now_ms() + (double)ms;
        while (now_ms() < end) {
            tty_drain(0);
            pump_synth();
        }
    } else {
        tty_drain(0);
    }
    V *out = v_list(0);
    InputEvent ev;
    while (q_pop(&ev)) {
        v_list_append(out, event_to_v(&ev));
    }
    return out;
}

V *input_wait_ms(int64_t ms) {
    input_hub_init();
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (is_tty() && (ms < 0 || ms == INPUT_WAIT_FOREVER)) raw_wait_first();
    else if (is_tty()) raw_on();
#endif
    if (ms < 0 || ms == INPUT_WAIT_FOREVER) {
        for (;;) {
            tty_drain(-1);
            if (!q_empty()) break;
        }
    } else if (ms == 0) {
        tty_drain(0);
    } else {
        tty_drain((int)ms);
    }
    return input_poll_ms(0);
}

V *v_input_stream(int mode, const char *prompt) {
    input_hub_init();
    V *v = (V *)calloc(1, sizeof(V));
    if (!v) return v_err("input: oom");
    v->t = T_INPUT;
    v->rc = 1;
    v->j = mode;
    if (prompt && prompt[0]) v->s = strdup(prompt);
    g_in.active_streams++;
    if (mode == INPUT_STREAM_RAW || mode == INPUT_STREAM_KEY)
        input_set_own_gui(1);
    return v;
}

V *input_readline(const char *prompt) {
    input_hub_init();
    if (prompt && prompt[0]) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (!is_tty()) {
        char buf[4096];
        if (!fgets(buf, sizeof buf, stdin)) return v_str("");
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = 0;
        return v_str(buf);
    }
    char *rl = input_hub_readline_repl(prompt ? prompt : "");
    return rl ? v_str(rl) : v_str("");
}

static int stream_wait_event(InputEvent *ev) {
    for (;;) {
        if (q_pop(ev)) return 1;
        tty_drain(g_in.hz > 0 ? (1000 / g_in.hz) : 16);
        pump_synth();
    }
}

V *input_stream_next(V *stream) {
    if (!stream || stream->t != T_INPUT) return v_err("input_stream_next: bad stream");
    input_hub_init();
    if (stream->j == INPUT_STREAM_LINE) {
        return input_readline(stream->s ? stream->s : "");
    }
    InputEvent ev;
    if (!stream_wait_event(&ev)) return v_nil();
    if (stream->j == INPUT_STREAM_RAW) {
        if (ev.kind == EV_CHAR) return v_str(ev.utf8);
        if (ev.utf8[0]) return v_str(ev.utf8);
        return v_str("");
    }
    if (stream->j == INPUT_STREAM_KEY) {
        if (ev.kind == EV_CHAR) {
            const char *kind = "down";
            return input_event_dict(ev.code, ev.modifiers, ev.utf8, kind);
        }
        const char *kind = ev.kind == EV_KEY_UP ? "up" : "down";
        return input_event_dict(ev.code, ev.modifiers, ev.utf8, kind);
    }
    return v_nil();
}

int input_hub_read_char(char *out) {
    input_hub_init();
    if (g_in.active_streams == 0) {
        InputEvent ev;
        while (q_pop(&ev)) {
            if (ev.kind == EV_CHAR && ev.utf8[0]) {
                *out = ev.utf8[0];
                return 1;
            }
        }
        unsigned char c;
        if (!read_byte_timed(-1, &c)) return 0;
        *out = (char)c;
        return 1;
    }
    InputEvent ev;
    if (q_pop(&ev)) {
        *out = ev.utf8[0] ? ev.utf8[0] : (char)(ev.code & 0xff);
        return 1;
    }
    unsigned char c;
    if (!read_byte_timed(-1, &c)) return 0;
    unsigned char pending[8];
    int pn = 0;
    decode_vt_byte(c, pending, &pn);
    if (q_pop(&ev)) {
        *out = ev.utf8[0] ? ev.utf8[0] : (char)c;
        return 1;
    }
    *out = (char)c;
    return 1;
}

char *input_hub_readline_repl(const char *prompt) {
    static char buf[65536];
    int len = 0, pos = 0;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    raw_on();
#endif
    if (prompt && prompt[0]) {
        printf("\r%s", prompt);
        fflush(stdout);
    }
    for (;;) {
        char c;
        if (!input_hub_read_char(&c)) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
            raw_off();
#endif
            return NULL;
        }
        if (c == '\r' || c == '\n') {
            buf[len] = 0;
            printf("\r\n");
            fflush(stdout);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
            raw_off();
#endif
            return buf;
        }
        if (c == 127 || c == 8) {
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, (size_t)(len - pos));
                pos--;
                len--;
            }
        } else if (c >= 32 && len + 1 < (int)sizeof buf) {
            memmove(buf + pos + 1, buf + pos, (size_t)(len - pos));
            buf[pos] = c;
            pos++;
            len++;
        } else {
            continue;
        }
        buf[len] = 0;
        printf("\r\033[K%s%s", prompt ? prompt : "", buf);
        if (prompt && (int)strlen(prompt) + pos > 0)
            printf("\r\033[%dC", (int)strlen(prompt) + pos);
        else
            printf("\r");
        fflush(stdout);
    }
}
