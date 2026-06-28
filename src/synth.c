#include "shakti.h"
#include "synth.h"
#include "synth_ui.h"
#include "synth_render.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SHAKTI_HAVE_SYNTH) && ( \
    (defined(__linux__) && __has_include(<alsa/asoundlib.h>) && __has_include(<X11/Xlib.h>)) || \
    (defined(__APPLE__) && !defined(__IOS__)) \
)
#include "synth_platform.h"
#include <pthread.h>
#include <unistd.h>
#ifdef __linux__
#undef in
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <alsa/asoundlib.h>
#include "input.h"
#endif
#define DESIGN_W 960
#define DESIGN_H 660
#define SYNTH_MIN_W 640
#define SYNTH_MIN_H 400
#define SYNTH_SR 48000
#define SYNTH_VOICES 16
#define SYNTH_BUF 512
#define SYNTH_SAMPLE_MAX (SYNTH_SR * 8)
#define SYNTH_LOOP_MAX (SYNTH_SR * 32)
#define SYNTH_SAMPLE_NAME_MAX 48
#define SYNTH_METRO_VOICE (SYNTH_VOICES - 1)
#define SYNTH_TONE_DEFAULT 0
#define SYNTH_TONE_BASS 1
#define SYNTH_PI 3.14159265358979323846
typedef struct SynthVoice {
    int on;
    int note;
    float freq;
    float phase;
    float sub_phase;
    float env;
    int stage;
    float vel;
    float lp;
    int gate; /* samples until auto-release (0 = held until note_off) */
    int tone; /* SYNTH_TONE_* */
} SynthVoice;
typedef enum {
    HIT_NONE = 0,
    HIT_PLAY,
    HIT_METRO,
    HIT_METRO_SOUND,
    HIT_MUTE,
    HIT_LEN_M,
    HIT_LEN_P,
    HIT_KNOB,
    HIT_STEP,
    HIT_PAD,
    HIT_RIBBON,
    HIT_KEY,
    HIT_LOOP_REC,
    HIT_LOOP_PLAY,
    HIT_LOOP_CLR
} HitKind;
typedef struct HitResult {
    HitKind kind;
    int a;
    int b;
} HitResult;
typedef struct MetroVoice {
    int active;
    int kind;
    float t;
    float phase;
    float env;
} MetroVoice;
typedef struct SynthState {
    int open;
    int alive;
#ifdef __linux__
    Display *dpy;
    Window win;
    GC gc;
    XImage *img;
#endif
    uint32_t *fb;
    uint32_t *present;
    int render_design;
    int dirty;
    int scr;
    int win_w;
    int win_h;
    int off_x;
    int off_y;
    float ui_scale;
    float knobs[8];
    float ribbon;
    int drag_ribbon;
    int ribbon_key_left;
    int ribbon_key_right;
    int step_len;
    int step_pos;
    int playing;
    int metro_on;
    int metro_sound;
    int metro_beat;
    int metro_flash;
    int mute;
    uint64_t seq[SYNTH_ROWS];
    int row_midi[SYNTH_ROWS];
    float sample_buf[SYNTH_SAMPLE_MAX];
    int sample_n;
    int sample_pos;
    int sample_playing;
    float sample_gain;
    char sample_name[SYNTH_SAMPLE_NAME_MAX];
    float loop_buf[SYNTH_LOOP_MAX];
    int loop_len;
    int loop_pos;
    int loop_wpos;
    int loop_target;
    int loop_recording;
    int loop_arm;
    int loop_playing;
    int loop_overdub;
    int loop_flash;
    float loop_gain;
    SynthVoice voices[SYNTH_VOICES];
    MetroVoice metro;
#ifdef __linux__
    pthread_t audio_tid;
#endif
    pthread_mutex_t mu;
    int audio_run;
#ifdef __linux__
    snd_pcm_t *pcm;
#endif
    double phase_samples;
    double metro_phase_samples;
    double samples_per_step;
    double samples_per_beat;
    int drag_knob;
    int drag_knob_y;
    int mouse_x;
    int mouse_y;
    int mouse_down;
    int synth_keys;
    int base_note;
    int key_down[SYNTH_MAX_KEYS];
    int pad_down[SYNTH_PADS];
    int want_maximize;
    int maximize_tries;
    SynthLayout layout;
} SynthState;
static SynthState g;
static int g_mu_inited;
#ifdef SYNTH_HAVE_GL
static int g_use_gl;
#endif
static void synth_request_maximize(void);
static float knob_val(int i);
static float synth_bpm(void);
static void synth_recalc_timing(void);
static void synth_trigger_note(int note, float vel);
static void synth_release_note(int note);
static void synth_voice_trigger(int note, float vel);
static void synth_voice_trigger_gated(int note, float vel, int gate_samples);
static void synth_voice_trigger_bass(int note, float vel, int gate_samples);
static void metro_reset_unlocked(int on);
static void metro_reset(int on);
static void metro_reset(int on);
static int synth_pad_note(int pad);
static const char *synth_pad_lbl(int pad);
static void synth_sample_trigger(void);
static float synth_sample_tick(void);
static void synth_loop_sync_target(void);
#define COL_CHASSIS rgb(22, 23, 26)
#define COL_CHASSIS_H rgb(36, 38, 42)
#define COL_PANEL rgb(14, 15, 18)
#define COL_BEZEL_D rgb(8, 9, 11)
#define COL_BEZEL_L rgb(72, 76, 84)
#define COL_LABEL rgb(140, 144, 152)
#define COL_TEXT rgb(232, 234, 240)
#define COL_AMBER rgb(255, 176, 48)
#define COL_LED_ON rgb(255, 148, 32)
#define COL_LED_OFF rgb(28, 30, 34)
#define COL_PLAY rgb(48, 180, 82)
#define COL_HOT rgb(255, 108, 42)
#define COL_PAD rgb(48, 49, 52)
#define COL_PAD_ON rgb(34, 35, 38)
#define COL_KEY_WHITE rgb(250, 250, 250)
#define COL_KEY_WHITE_HI rgb(255, 255, 255)
#define COL_KEY_WHITE_LO rgb(220, 220, 220)
#define COL_KEY_BLACK rgb(36, 36, 36)
#define COL_KEY_BLACK_HI rgb(50, 50, 50)
#define COL_KEY_BLACK_LO rgb(20, 20, 20)
#define COL_KEY_MIDDLE_C rgb(245, 245, 240)
#define COL_KEY_MIDDLE_C_LO rgb(220, 220, 212)
#define COL_METAL_HI rgb(118, 122, 130)
#define COL_METAL_LO rgb(52, 55, 62)
f(rgb_r,(int)((x>>16)&255u))
f(rgb_g,(int)((x>>8)&255u))
f(rgb_b,(int)(x&255u))
static uint32_t rgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t rgb_mul(uint32_t c, float f) {
    return rgb((int)(rgb_r(c) * f), (int)(rgb_g(c) * f), (int)(rgb_b(c) * f));
}
static uint32_t rgb_lerp(uint32_t a, uint32_t b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return rgb((int)(rgb_r(a) * (1.f - t) + rgb_r(b) * t), (int)(rgb_g(a) * (1.f - t) + rgb_g(b) * t),
             (int)(rgb_b(a) * (1.f - t) + rgb_b(b) * t));
}
static float hash2f(int x, int y) {
    float v = sinf((float)(x * 127.1f + y * 311.7f)) * 43758.5453f;
    return v - floorf(v);
}
static void pix_s(int x, int y, uint32_t c) {
    if (g.render_design) {
        if (x >= 0 && x < DESIGN_W && y >= 0 && y < DESIGN_H) g.fb[y * DESIGN_W + x] = c;
    } else {
        if (x >= 0 && x < g.win_w && y >= 0 && y < g.win_h) g.present[y * g.win_w + x] = c;
    }
}
static void pix_blend_s(int x, int y, uint32_t c, float a) {
    uint32_t dst;
    if (g.render_design) {
        Pv(x < 0 || x >= DESIGN_W || y < 0 || y >= DESIGN_H)
        Pv(a <= 0.f)
        if (a >= 1.f) {
            pix_s(x, y, c);
            return;
        }
        dst = g.fb[y * DESIGN_W + x];
        pix_s(x, y, rgb_lerp(dst, c, a));
    } else {
        Pv(x < 0 || x >= g.win_w || y < 0 || y >= g.win_h)
        Pv(a <= 0.f)
        if (a >= 1.f) {
            pix_s(x, y, c);
            return;
        }
        dst = g.present[y * g.win_w + x];
        pix_s(x, y, rgb_lerp(dst, c, a));
    }
}
static int ui_sx(int dx){P(g.render_design,dx)return g.off_x+(int)(dx*g.ui_scale+0.5f);}
static int ui_sy(int dy){P(g.render_design,dy)return g.off_y+(int)(dy*g.ui_scale+0.5f);}
static int ui_sw(int dw){P(g.render_design,dw<1?1:dw){int w=(int)(dw*g.ui_scale+0.5f);return w<1?1:w;}}
static int screen_to_design(int sx, int sy, int *dx, int *dy) {
    float fx, fy;
    P(g.ui_scale <= 0.f,0)
    fx = (sx - g.off_x) / g.ui_scale;
    fy = (sy - g.off_y) / g.ui_scale;
    *dx = (int)fx;
    *dy = (int)fy;
    return fx >= 0.f && fx < (float)DESIGN_W && fy >= 0.f && fy < (float)DESIGN_H;
}
static int uir_contains(UiRect r, int dx, int dy) {
    return dx >= r.x && dx < r.x + r.w && dy >= r.y && dy < r.y + r.h;
}
static void drect_grad_v(UiRect r, uint32_t top, uint32_t bot) {
    int x0 = ui_sx(r.x), y0 = ui_sy(r.y), x1 = ui_sx(r.x + r.w), y1 = ui_sy(r.y + r.h);
    int x, y, h = y1 - y0;
    if (h < 1) h = 1;
    for (y = y0; y < y1; y++) {
        float t = (float)(y - y0) / (float)h;
        uint32_t c = rgb_lerp(top, bot, t);
        for (x = x0; x < x1; x++) {
            float n = hash2f(x, y) * 0.07f - 0.035f;
            pix_s(x, y, rgb_lerp(c, rgb(255, 255, 255), n > 0.f ? n : -n));
        }
    }
}
static void drect_fill(UiRect r, uint32_t c) {
    int x0 = ui_sx(r.x), y0 = ui_sy(r.y);
    int x1 = ui_sx(r.x + r.w), y1 = ui_sy(r.y + r.h);
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) pix_s(x, y, c);
}
static void drect_bevel(UiRect r, int raised) {
    uint32_t hi = raised ? COL_BEZEL_L : COL_BEZEL_D;
    uint32_t lo = raised ? COL_BEZEL_D : COL_BEZEL_L;
    UiRect t = {r.x, r.y, r.w, 1};
    UiRect l = {r.x, r.y, 1, r.h};
    UiRect b = {r.x, r.y + r.h - 1, r.w, 1};
    UiRect ri = {r.x + r.w - 1, r.y, 1, r.h};
    drect_fill(t, hi);
    drect_fill(l, hi);
    drect_fill(b, lo);
    drect_fill(ri, lo);
}
static void draw_panel_recessed(UiRect r) {
    drect_grad_v(r, rgb(16, 17, 20), rgb(26, 28, 32));
    drect_fill((UiRect){r.x + 1, r.y + 1, r.w - 2, 1}, rgb(6, 7, 9));
    drect_fill((UiRect){r.x + 1, r.y + 1, 1, r.h - 2}, rgb(6, 7, 9));
    drect_fill((UiRect){r.x, r.y, r.w, 1}, rgb(58, 62, 70));
    drect_fill((UiRect){r.x, r.y, 1, r.h}, rgb(58, 62, 70));
    drect_fill((UiRect){r.x, r.y + r.h - 1, r.w, 1}, rgb(8, 9, 11));
    drect_fill((UiRect){r.x + r.w - 1, r.y, 1, r.h}, rgb(8, 9, 11));
}
static void panel_inset(UiRect r) { draw_panel_recessed(r); }
static void fill_chassis(void) {
    int x, y;
    int w = g.render_design ? DESIGN_W : g.win_w;
    int h = g.render_design ? DESIGN_H : g.win_h;
    for (y = 0; y < h; y++) {
        float vy = (float)y / (float)h;
        for (x = 0; x < w; x++) {
            float vx = (float)x / (float)w;
            float brush = sinf(vx * 220.f) * 0.012f + hash2f(x, y) * 0.045f - 0.022f;
            uint32_t base = rgb_lerp(rgb(16, 17, 20), rgb(34, 36, 40), vy);
            float lit = 1.f + brush + (1.f - vy) * 0.06f;
            pix_s(x, y, rgb_mul(base, lit));
        }
    }
}
static void dcircle_fill(int cx, int cy, int rad, uint32_t c) {
    int x, y, scx = ui_sx(cx), scy = ui_sy(cy), sr = ui_sw(rad);
    for (y = -sr; y <= sr; y++)
        for (x = -sr; x <= sr; x++)
            if (x * x + y * y <= sr * sr) pix_s(scx + x, scy + y, c);
}
static void dcircle_radial(int cx, int cy, int rad, uint32_t inner, uint32_t outer) {
    int x, y, scx = ui_sx(cx), scy = ui_sy(cy), sr = ui_sw(rad);
    float inv = sr > 0 ? 1.f / (float)sr : 1.f;
    for (y = -sr; y <= sr; y++)
        for (x = -sr; x <= sr; x++) {
            float d = sqrtf((float)(x * x + y * y));
            if (d > (float)sr) continue;
            pix_s(scx + x, scy + y, rgb_lerp(inner, outer, d * inv));
        }
}
static void dcircle_glow(int cx, int cy, int rad, uint32_t col) {
    int i;
    for (i = 4; i >= 1; i--)
        dcircle_radial(cx, cy, rad + i, rgb_mul(col, 0.08f * (float)i), rgb_mul(col, 0.f));
    dcircle_radial(cx, cy, rad, col, rgb_mul(col, 0.55f));
}
static void draw_header_deck(UiRect r) {
    draw_panel_recessed(r);
    drect_grad_v((UiRect){r.x + 3, r.y + 3, r.w - 6, r.h / 2}, rgb(42, 44, 48), rgb(28, 30, 34));
}
static int glyph_idx(char ch){
 P(ch>='0'&&ch<='9',ch-'0')
 P(ch=='-',10)
 P(ch>='A'&&ch<='Z',11+(ch-'A'))
 P(ch=='+',37)
 P(ch==' ',38)
 P(ch>='a'&&ch<='z',11+(ch-'a'))
 return-1;}
static void glyph5x7(int scx, int scy, char ch, uint32_t c, int scale) {
    static const unsigned char font[39][7] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f},
        {0x1f, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
        {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
        {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00},
        {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
        {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
        {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
        {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
        {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
        {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e},
        {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
        {0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
        {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
        {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
        {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
        {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
        {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
        {0x00, 0x00, 0x04, 0x0e, 0x04, 0x00, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    int idx = glyph_idx(ch), row, col, px, py;
    Pv(idx < 0)
    if (scale < 1) scale = 1;
    for (row = 0; row < 7; row++)
        for (col = 0; col < 5; col++)
            if (font[idx][row] & (1 << (4 - col)))
                for (py = 0; py < scale; py++)
                    for (px = 0; px < scale; px++)
                        pix_s(scx + col * scale + px, scy + row * scale + py, c);
}
static void text_label(int dx, int dy, const char *s, uint32_t c) {
    int scale = g.render_design ? 1 : (int)(g.ui_scale + 0.5f);
    int i, adv = 6 * scale;
    int scx = ui_sx(dx), scy = ui_sy(dy);
    if (scale < 1) scale = 1;
    for (i = 0; s[i]; i++) {
        glyph5x7(scx + i * adv, scy, s[i], c, scale);
    }
}
static void text_num_d(int dx, int dy, int n, uint32_t c) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n);
    text_label(dx, dy, buf, c);
}
static void draw_btn(UiRect r, uint32_t hi, uint32_t lo, int lit, int pressed, const char *txt) {
    uint32_t face_hi = lit ? hi : rgb_lerp(hi, rgb(48, 50, 56), 0.55f);
    uint32_t face_lo = lit ? lo : rgb_lerp(lo, rgb(28, 30, 34), 0.55f);
    int tx;
    if (pressed)
        drect_grad_v(r, rgb_mul(face_lo, 0.9f), rgb_mul(face_hi, 0.82f));
    else {
        drect_grad_v(r, face_hi, face_lo);
        drect_fill((UiRect){r.x, r.y, r.w, 1}, rgb_lerp(face_hi, rgb(255, 255, 255), 0.22f));
        drect_fill((UiRect){r.x, r.y, 1, r.h}, rgb_lerp(face_hi, rgb(255, 255, 255), 0.12f));
    }
    drect_bevel(r, !pressed);
    tx = r.x + (r.w - (int)strlen(txt) * 6) / 2;
    if (tx < r.x + 2) tx = r.x + 2;
    text_label(tx, r.y + (r.h - 7) / 2, txt, lit ? COL_TEXT : COL_LABEL);
}
f(synth_key_midi,g.base_note+x)
static int synth_key_is_black(int k){int s=synth_key_midi(k)%12;return s==1||s==3||s==6||s==8||s==10;}
static void synth_key_set_note(int note, int down) {
    int k = note - g.base_note;
    if (k >= 0 && k < g.synth_keys) g.key_down[k] = down ? 1 : 0;
}
static void synth_layout_compute(SynthLayout *L) {
    const int M = 16;                  /* outer margin */
    const int CW = DESIGN_W - 2 * M;   /* content width (928) */
    int row, step, pad, k, j;
    /* ---- header band: 8..68 ---- */
    {
        int by = 22, bh = 30, bx = 98, bw = 54, bgap = 6;
        L->transport = (UiRect){bx, by, bw, bh};
        bx += bw + bgap;
        L->metro_btn = (UiRect){bx, by, bw, bh};
        bx += bw + bgap;
        L->metro_sound_btn = (UiRect){bx, by, 58, bh};
        bx += 58 + bgap;
        L->mute_btn = (UiRect){bx, by, 52, bh};
        bx += 52 + 8;
        L->loop_rec_btn = (UiRect){bx, by, 44, bh};
        bx += 44 + 4;
        L->loop_play_btn = (UiRect){bx, by, 50, bh};
        bx += 50 + 4;
        L->loop_clr_btn = (UiRect){bx, by, 40, bh};
        bx += 40 + 8;
        L->step_minus = (UiRect){bx, by, 24, bh};
        bx += 24 + 4;
        L->step_box = (UiRect){bx, by, 50, bh};
        bx += 50 + 4;
        L->step_plus = (UiRect){bx, by, 24, bh};
        L->vu_meter = (UiRect){560, 32, 220, 10};
        L->bpm_readout = (UiRect){DESIGN_W - M - 86, 18, 86, 40};
    }
    /* ---- knob band: 72..158 ---- */
    {
        int kgap = 14, ky = 80, kh = 70;
        int kw = (CW - 7 * kgap) / 8;
        for (k = 0; k < 8; k++) L->knobs[k] = (UiRect){M + k * (kw + kgap), ky, kw, kh};
    }
    /* ---- main band (sequencer | pads): 164..374 ---- */
    {
        int my = 164, mh = 210, gap = 12;
        int seq_w = 556;
        int row_label_w = 56, step_area_x, step_area_w, step_w;
        int rows_top, rows_h, rh;
        L->seq_panel = (UiRect){M, my, seq_w, mh};
        rows_top = my + 28;
        rows_h = mh - 36;
        rh = rows_h / SYNTH_ROWS;
        step_area_x = L->seq_panel.x + 12 + row_label_w + 8;
        step_area_w = L->seq_panel.w - (12 + row_label_w + 8) - 14;
        step_w = g.step_len > 0 ? step_area_w / g.step_len : step_area_w / SYNTH_DEFAULT_STEPS;
        if (step_w < 6) step_w = 6;
        for (row = 0; row < SYNTH_ROWS; row++) {
            int ry = rows_top + row * rh;
            L->seq_row[row] = (UiRect){L->seq_panel.x + 12, ry, row_label_w, rh - 4};
            for (step = 0; step < g.step_len; step++)
                L->seq_step[row][step] = (UiRect){step_area_x + step * step_w, ry + 2, step_w - 3, rh - 8};
        }
        /* drum pads */
        {
            int px = M + seq_w + gap, pw_panel = DESIGN_W - M - px;
            int pad_top = 28, pgap = 8, gx0, gy0, grid_w, grid_h, pw;
            L->pad_panel = (UiRect){px, my, pw_panel, mh};
            grid_w = pw_panel - 24;
            grid_h = mh - pad_top - 12;
            pw = (grid_h - 3 * pgap) / 4;
            if (pw > (grid_w - 3 * pgap) / 4) pw = (grid_w - 3 * pgap) / 4;
            if (pw < 16) pw = 16;
            grid_w = 4 * pw + 3 * pgap;
            gx0 = L->pad_panel.x + (L->pad_panel.w - grid_w) / 2;
            gy0 = L->pad_panel.y + pad_top;
            for (pad = 0; pad < SYNTH_PADS; pad++)
                L->pads[pad] = (UiRect){gx0 + (pad % 4) * (pw + pgap),
                                        gy0 + (pad / 4) * (pw + pgap), pw, pw};
        }
    }
    /* ---- viz band: 380..432 ---- */
    {
        int vy = 380, vh = 52, gap = 12, sw = 556;
        L->spectrum_panel = (UiRect){M, vy, sw, vh};
        L->waveform_panel = (UiRect){M + sw + gap, vy, DESIGN_W - M - (M + sw + gap), vh};
    }
    /* ---- ribbon: 438..466 ---- */
    {
        int ry = 438, rh = 28;
        L->ribbon_panel = (UiRect){M, ry, CW, rh};
        L->ribbon_track = (UiRect){L->ribbon_panel.x + 104, ry + 8, L->ribbon_panel.w - 120, 12};
    }
    /* ---- keyboard: 472..648 ---- */
    {
        int ky = 472, kh = DESIGN_H - 12 - ky;
        int kx0, ky0, kh2, white_count = 0, wi, ww, bw, bx, black_h;
        L->keys_panel = (UiRect){M, ky, CW, kh};
        kx0 = L->keys_panel.x + 6;
        ky0 = L->keys_panel.y + 24;
        kh2 = kh - 30;
        if (kh2 < 24) kh2 = 24;
        for (k = 0; k < g.synth_keys; k++)
            if (!synth_key_is_black(k)) white_count++;
        ww = white_count > 0 ? (L->keys_panel.w - 12) / white_count : 20;
        if (ww < 14) ww = 14;
        wi = 0;
        for (k = 0; k < g.synth_keys; k++) {
            if (synth_key_is_black(k)) continue;
            L->keys[k] = (UiRect){kx0 + wi * ww, ky0, ww - 2, kh2};
            wi++;
        }
        black_h = (kh2 * 60) / 100;
        if (black_h < 16) black_h = 16;
        bw = (ww * 64) / 100;
        if (bw < 8) bw = 8;
        for (k = 0; k < g.synth_keys; k++) {
            if (!synth_key_is_black(k)) continue;
            wi = 0;
            j(k, if (!synth_key_is_black(j)) wi++)
            bx = kx0 + wi * ww - bw / 2;
            if (bx < kx0) bx = kx0;
            L->keys[k] = (UiRect){bx, ky0, bw, black_h};
        }
    }
}
static void draw_knob(UiRect r, float val, const char *label) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 5;
    int rad = r.w / 4, sx, sy, kx, ky, lx, i;
    float ang;
    if (rad < 6) rad = 6;
    if (cy + rad + 2 > r.y + r.h - 12) rad = (r.y + r.h - 12 - cy);
    if (rad < 6) rad = 6;
    dcircle_radial(cx, cy, rad + 3, rgb(18, 19, 22), rgb(8, 9, 11));
    dcircle_radial(cx, cy, rad + 1, COL_METAL_HI, COL_METAL_LO);
    dcircle_radial(cx, cy, rad - 1, rgb(78, 82, 90), rgb(44, 47, 54));
    sx = cx - rad / 3;
    sy = cy - rad / 3;
    for (i = 0; i < rad / 2; i++)
        dcircle_fill(sx + i / 2, sy + i / 2, rad / 4 - i / 3, rgb_mul(rgb(255, 255, 255), 0.12f - i * 0.02f));
    ang = val * 4.18879f + 2.61799f;
    kx = cx + (int)(cosf(ang) * (rad - 3));
    ky = cy + (int)(sinf(ang) * (rad - 3));
    dcircle_fill(kx, ky, 2, rgb(240, 242, 248));
    lx = r.x + (r.w - (int)strlen(label) * 6) / 2;
    if (lx < r.x) lx = r.x;
    text_label(lx, r.y + r.h - 10, label, COL_LABEL);
}
static void draw_led_step(UiRect r, int on, int playhead) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int rad = r.w / 4;
    if (rad > r.h / 3) rad = r.h / 3;
    if (rad < 2) rad = 2;
    if (rad > 5) rad = 5;
    dcircle_radial(cx, cy, rad + 1, rgb(20, 21, 24), rgb(10, 11, 13));
    if (playhead) dcircle_glow(cx, cy, rad + 1, COL_HOT);
    if (on) dcircle_glow(cx, cy, rad, COL_LED_ON);
    else dcircle_fill(cx, cy, rad, COL_LED_OFF);
}
static void draw_pad(UiRect r, int pressed, const char *lbl) {
    UiRect face = r;
    if (pressed) face.y += 1;
    drect_grad_v(face, pressed ? rgb(36, 37, 40) : rgb(62, 64, 68), pressed ? rgb(24, 25, 28) : rgb(38, 40, 44));
    drect_fill((UiRect){face.x, face.y, face.w, 1}, rgb_lerp(rgb(255, 255, 255), face.y > r.y ? rgb(0, 0, 0) : rgb(255, 255, 255),
                                                                  pressed ? 0.05f : 0.18f));
    drect_bevel(face, !pressed);
    if (!pressed) {
        UiRect sh = {r.x + 2, r.y + r.h, r.w - 2, 2};
        drect_fill(sh, rgb(6, 7, 9));
    }
    if (lbl && lbl[0]) {
        int w = 0, i;
        for (i = 0; lbl[i]; i++) w += 6;
        text_label(face.x + (face.w - w) / 2, face.y + (face.h - 7) / 2, lbl,
                   pressed ? rgb(220, 220, 220) : rgb(140, 145, 150));
    }
}
static void draw_piano_key(UiRect r, int down, int style) {
    UiRect face = r;
    uint32_t top, bot, edge_r, edge_b;
    if (down) face.y += 1;
    if (style == 1) {
        top = down ? COL_KEY_BLACK_LO : COL_KEY_BLACK;
        bot = down ? rgb(10, 10, 10) : COL_KEY_BLACK_LO;
        edge_r = rgb(30, 30, 30);
        edge_b = rgb(15, 15, 15);
    } else if (style == 2) {
        top = down ? COL_KEY_MIDDLE_C_LO : COL_KEY_MIDDLE_C;
        bot = down ? rgb(200, 200, 192) : rgb(232, 232, 226);
        edge_r = rgb(180, 180, 172);
        edge_b = rgb(160, 160, 152);
    } else {
        top = down ? COL_KEY_WHITE_LO : COL_KEY_WHITE;
        bot = down ? rgb(200, 200, 200) : COL_KEY_WHITE_HI;
        edge_r = rgb(180, 180, 180);
        edge_b = rgb(160, 160, 160);
    }
    drect_grad_v(face, top, bot);
    drect_fill((UiRect){face.x, face.y, face.w, 1},
               rgb_lerp(rgb(255, 255, 255), rgb(0, 0, 0), down ? 0.06f : (style == 1 ? 0.18f : 0.24f)));
    drect_fill((UiRect){face.x + face.w - 1, face.y, 1, face.h}, edge_r);
    drect_fill((UiRect){face.x, face.y + face.h - 1, face.w, 1}, edge_b);
    if (style == 2 && !down)
        text_label(face.x + (face.w - 6) / 2, face.y + face.h - 14, "C", rgb(120, 120, 112));
}
static void draw_ribbon_track(UiRect track, float val) {
    UiRect cap;
    int cap_x;
    drect_grad_v(track, rgb(12, 13, 16), rgb(22, 24, 28));
    drect_fill((UiRect){track.x, track.y, track.w, 1}, rgb(48, 52, 58));
    drect_fill((UiRect){track.x, track.y + track.h - 1, track.w, 1}, rgb(6, 7, 9));
    cap_x = track.x + (int)((val * 0.5f + 0.5f) * (float)(track.w - 10));
    cap = (UiRect){cap_x, track.y - 3, 10, track.h + 6};
    drect_grad_v(cap, rgb(255, 190, 70), rgb(200, 120, 30));
    drect_fill((UiRect){cap.x, cap.y, cap.w, 1}, rgb_lerp(COL_AMBER, rgb(255, 255, 255), 0.35f));
    drect_bevel(cap, 1);
}
static HitResult synth_hit_test(int dx, int dy) {
    HitResult h = {HIT_NONE, 0, 0};
    int k, row, step, pad;
    if (uir_contains(g.layout.transport, dx, dy)) {
        h.kind = HIT_PLAY;
        return h;
    }
    if (uir_contains(g.layout.metro_btn, dx, dy)) {
        h.kind = HIT_METRO;
        return h;
    }
    if (uir_contains(g.layout.metro_sound_btn, dx, dy)) {
        h.kind = HIT_METRO_SOUND;
        return h;
    }
    if (uir_contains(g.layout.mute_btn, dx, dy)) {
        h.kind = HIT_MUTE;
        return h;
    }
    if (uir_contains(g.layout.loop_rec_btn, dx, dy)) {
        h.kind = HIT_LOOP_REC;
        return h;
    }
    if (uir_contains(g.layout.loop_play_btn, dx, dy)) {
        h.kind = HIT_LOOP_PLAY;
        return h;
    }
    if (uir_contains(g.layout.loop_clr_btn, dx, dy)) {
        h.kind = HIT_LOOP_CLR;
        return h;
    }
    if (uir_contains(g.layout.step_minus, dx, dy)) {
        h.kind = HIT_LEN_M;
        return h;
    }
    if (uir_contains(g.layout.step_plus, dx, dy)) {
        h.kind = HIT_LEN_P;
        return h;
    }
    for (k = 0; k < 8; k++)
        if (uir_contains(g.layout.knobs[k], dx, dy)) {
            h.kind = HIT_KNOB;
            h.a = k;
            return h;
        }
    for (pad = 0; pad < SYNTH_PADS; pad++)
        if (uir_contains(g.layout.pads[pad], dx, dy)) {
            h.kind = HIT_PAD;
            h.a = pad;
            return h;
        }
    for (row = 0; row < SYNTH_ROWS; row++)
        for (step = 0; step < SYNTH_MAX_STEPS; step++)
            if (step < g.step_len && uir_contains(g.layout.seq_step[row][step], dx, dy)) {
                h.kind = HIT_STEP;
                h.a = row;
                h.b = step;
                return h;
            }
    if (uir_contains(g.layout.ribbon_track, dx, dy)) {
        h.kind = HIT_RIBBON;
        return h;
    }
    for (k = 0; k < g.synth_keys; k++)
        if (synth_key_is_black(k) && uir_contains(g.layout.keys[k], dx, dy)) {
            h.kind = HIT_KEY;
            h.a = k;
            return h;
        }
    for (k = 0; k < g.synth_keys; k++)
        if (!synth_key_is_black(k) && uir_contains(g.layout.keys[k], dx, dy)) {
            h.kind = HIT_KEY;
            h.a = k;
            return h;
        }
    return h;
}
void synth_core_ui_draw(void) {
    SynthLayout *L = &g.layout;
    static const char *knob_lbl[8] = {"BPM", "LEVEL", "CUT", "RES", "ATT", "DEC", "SUS", "REL"};
    float spec[SYNTH_UI_SPECTRUM_BINS];
    float wave[SYNTH_UI_WAVEFORM_LEN];
    int spec_n = 0, wave_n = 0;
    int row, step, pad, k, on, playhead;
    int ncmds;
    const UiCmd *cmds;
    uint32_t col_play = rgb(48, 180, 82);
    uint32_t col_amber = rgb(255, 176, 48);
    uint32_t col_hot = rgb(255, 108, 42);
    uint32_t col_label = rgb(140, 144, 152);
    uint32_t col_text = rgb(232, 234, 240);
    int viz;

    synth_layout_compute(L);
    synth_ui_begin();
    synth_ui_emit_chassis();
    synth_ui_emit_header_deck((UiRect){8, 8, DESIGN_W - 16, 60});
    synth_ui_emit_btn(L->transport, col_play, rgb(28, 110, 52), g.playing, 0, g.playing ? "STOP" : "PLAY");
    synth_ui_emit_btn(L->metro_btn, col_amber, rgb(160, 100, 20), g.metro_on, g.metro_flash > 0, "METRO");
    synth_ui_emit_btn(L->metro_sound_btn, rgb(70, 74, 82), rgb(38, 40, 46), 0, 0, g.metro_sound ? "DRUM" : "CLICK");
    synth_ui_emit_btn(L->mute_btn, col_hot, rgb(140, 60, 20), g.mute, 0, "MUTE");
    synth_ui_emit_btn(L->loop_rec_btn, col_hot, rgb(120, 40, 18), g.loop_recording || g.loop_arm, g.loop_flash > 0,
                      "REC");
    synth_ui_emit_btn(L->loop_play_btn, col_play, rgb(28, 110, 52), g.loop_playing, 0, "LOOP");
    synth_ui_emit_btn(L->loop_clr_btn, rgb(58, 62, 68), rgb(32, 34, 38), 0, 0, "CLR");
    synth_ui_emit_btn(L->step_minus, rgb(58, 62, 68), rgb(32, 34, 38), 0, 0, "-");
    synth_ui_emit_panel(L->step_box);
    synth_ui_emit_label(L->step_box.x + 4, L->step_box.y + 2, "STEPS", col_label);
    synth_ui_emit_num(L->step_box.x + 4, L->step_box.y + 12, g.step_len, col_text);
    synth_ui_emit_btn(L->step_plus, rgb(58, 62, 68), rgb(32, 34, 38), 0, 0, "+");
    synth_ui_emit_vu(L->vu_meter, synth_ui_vu_level());
    i(8, synth_ui_emit_knob(L->knobs[i], knob_val(i), knob_lbl[i]);)
    synth_ui_emit_label(L->bpm_readout.x, L->bpm_readout.y, "BPM", col_label);
    synth_ui_emit_num(L->bpm_readout.x, L->bpm_readout.y + 12, (int)synth_bpm(), col_amber);
    synth_ui_emit_panel(L->seq_panel);
    synth_ui_emit_label(L->seq_panel.x + 8, L->seq_panel.y + 4, "SEQUENCER", col_label);
    for (row = 0; row < SYNTH_ROWS; row++) {
        synth_ui_emit_label(L->seq_row[row].x, L->seq_row[row].y + 2, synth_row_label(row), col_label);
        for (step = 0; step < g.step_len; step++) {
            on = (g.seq[row] & (1ULL << step));
            playhead = g.playing && step == g.step_pos;
            synth_ui_emit_led_step(L->seq_step[row][step], on, playhead);
        }
    }
    synth_ui_emit_panel(L->pad_panel);
    synth_ui_emit_label(L->pad_panel.x + 8, L->pad_panel.y + 4, "DRUMS", col_label);
    i(SYNTH_PADS, synth_ui_emit_pad(L->pads[i], g.pad_down[i], synth_pad_lbl(i));)
    synth_ui_emit_panel(L->ribbon_panel);
    synth_ui_emit_label(L->ribbon_panel.x + 8, L->ribbon_panel.y + 4, "PITCH BEND", col_label);
    synth_ui_emit_ribbon(L->ribbon_track, g.ribbon);
    synth_ui_emit_panel(L->keys_panel);
    synth_ui_emit_label(L->keys_panel.x + 8, L->keys_panel.y + 4, "KEYS", col_label);
    for (k = 0; k < g.synth_keys; k++) {
        if (synth_key_is_black(k)) continue;
        synth_ui_emit_piano_key(L->keys[k], g.key_down[k], synth_key_midi(k) == 60 ? 2 : 0);
    }
    for (k = 0; k < g.synth_keys; k++) {
        if (!synth_key_is_black(k)) continue;
        synth_ui_emit_piano_key(L->keys[k], g.key_down[k], 1);
    }
    viz = synth_ui_viz_mode();
    synth_ui_get_spectrum(spec, &spec_n);
    synth_ui_get_waveform(wave, &wave_n);
    if ((viz == SYNTH_VIZ_SPECTRUM || viz == SYNTH_VIZ_BOTH) && spec_n > 0)
        synth_ui_emit_spectrum(L->spectrum_panel, spec, spec_n);
    if ((viz == SYNTH_VIZ_WAVEFORM || viz == SYNTH_VIZ_BOTH) && wave_n > 0)
        synth_ui_emit_waveform(L->waveform_panel, wave, wave_n);
    if (!g.fb && synth_core_fb_design_init() != 0) return;
    cmds = synth_ui_cmds(&ncmds);
    synth_ui_flush(cmds, ncmds, g.fb, DESIGN_W, DESIGN_H);
    g.dirty = 1;
}
static void synth_ui_blit(void) {
    Pv(!g.dirty)
#ifdef __linux__
    Pv(!g.dpy || !g.img)
#endif
    if (g.fb && g.present) synth_core_present_scale();
    synth_platform_present();
    g.dirty = 0;
}
#ifdef SYNTH_HAVE_GL
static const char *const g_knob_lbl[8] = {"BPM", "LEVEL", "CUT", "RES", "ATT", "DEC", "SUS", "REL"};
static const char *g_pad_lbl_cache[SYNTH_PADS];
static void synth_gl_draw(void) {
    SynthRenderState s;
    int i, n = 0;
    memset(&s, 0, sizeof s);
    synth_layout_compute(&g.layout);
    s.L = &g.layout;
    s.design_w = DESIGN_W;
    s.design_h = DESIGN_H;
    s.win_w = g.win_w;
    s.win_h = g.win_h;
    s.ui_scale = g.ui_scale;
    s.off_x = g.off_x;
    s.off_y = g.off_y;
    synth_core_audio_lock();
    for (i = 0; i < 8; i++) s.knobs[i] = knob_val(i);
    s.knob_lbl = g_knob_lbl;
    for (i = 0; i < SYNTH_ROWS; i++) s.seq[i] = g.seq[i];
    s.step_len = g.step_len;
    s.step_pos = g.step_pos;
    s.playing = g.playing;
    s.metro_on = g.metro_on;
    s.metro_flash = g.metro_flash;
    s.metro_sound = g.metro_sound;
    s.mute = g.mute;
    s.loop_rec = g.loop_recording || g.loop_arm;
    s.loop_play = g.loop_playing;
    s.loop_has = g.loop_len > 0;
    s.loop_flash = g.loop_flash;
    s.sample_loaded = g.sample_n > 0;
    if (g.sample_name[0])
        snprintf(s.sample_name, sizeof s.sample_name, "%s", g.sample_name);
    for (i = 0; i < SYNTH_PADS; i++) {
        s.pad_down[i] = g.pad_down[i];
        g_pad_lbl_cache[i] = synth_pad_lbl(i);
    }
    s.pad_lbl = g_pad_lbl_cache;
    for (i = 0; i < SYNTH_MAX_KEYS; i++) s.key_down[i] = g.key_down[i];
    s.synth_keys = g.synth_keys;
    s.base_note = g.base_note;
    s.ribbon = g.ribbon;
    s.bpm = (int)synth_bpm();
    s.viz = synth_ui_viz_mode();
    synth_core_audio_unlock();
    s.vu = synth_ui_vu_level();
    synth_ui_get_spectrum(s.spectrum, &n);
    s.spectrum_n = n;
    synth_ui_get_waveform(s.waveform, &n);
    s.waveform_n = n;
    synth_gl_render(&s);
    synth_gl_swap();
}
#endif
static void synth_present_frame(void) {
#ifdef SYNTH_HAVE_GL
    if (g_use_gl) {
        synth_gl_draw();
        return;
    }
#endif
    synth_core_ui_draw();
    synth_ui_blit();
}
void synth_core_handle_click(int sx, int sy, int down) {
    int dx, dy;
    HitResult h;
    Pv(!screen_to_design(sx, sy, &dx, &dy))
    synth_core_audio_lock();
    g.dirty = 1;
    h = synth_hit_test(dx, dy);
    if (down && h.kind == HIT_PLAY) {
        g.playing = !g.playing;
        if (g.playing) g.step_pos = 0;
        goto click_done;
    }
    if (down && h.kind == HIT_METRO) {
        metro_reset_unlocked(!g.metro_on);
        goto click_done;
    }
    if (down && h.kind == HIT_METRO_SOUND) {
        g.metro_sound = !g.metro_sound;
        goto click_done;
    }
    if (down && h.kind == HIT_MUTE) {
        g.mute = !g.mute;
        goto click_done;
    }
    if (down && h.kind == HIT_LOOP_REC) {
        if (g.loop_recording || g.loop_arm) {
            g.loop_recording = 0;
            g.loop_arm = 0;
        } else if (g.playing) {
            g.loop_arm = 1;
            g.loop_flash = 12;
        } else {
            g.loop_recording = 1;
            g.loop_wpos = 0;
            synth_loop_sync_target();
            g.loop_flash = 12;
        }
        goto click_done;
    }
    if (down && h.kind == HIT_LOOP_PLAY) {
        if (g.loop_len > 0) g.loop_playing = !g.loop_playing;
        goto click_done;
    }
    if (down && h.kind == HIT_LOOP_CLR) {
        memset(g.loop_buf, 0, sizeof g.loop_buf);
        g.loop_len = 0;
        g.loop_pos = 0;
        g.loop_wpos = 0;
        g.loop_playing = 0;
        g.loop_recording = 0;
        g.loop_arm = 0;
        goto click_done;
    }
    if (down && h.kind == HIT_LEN_M) {
        if (g.step_len > 1) g.step_len--;
        goto click_done;
    }
    if (down && h.kind == HIT_LEN_P) {
        if (g.step_len < SYNTH_MAX_STEPS) g.step_len++;
        goto click_done;
    }
    if (down && h.kind == HIT_KNOB) {
        g.drag_knob = h.a + 1;
        g.drag_knob_y = sy;
        UiRect *r = &g.layout.knobs[h.a];
        int cx = r->x + r->w / 2;
        if (dx < cx - 4) {
            g.knobs[h.a] -= 0.05f;
            if (g.knobs[h.a] < 0.f) g.knobs[h.a] = 0.f;
            synth_recalc_timing();
        } else if (dx > cx + 4) {
            g.knobs[h.a] += 0.05f;
            if (g.knobs[h.a] > 1.f) g.knobs[h.a] = 1.f;
            synth_recalc_timing();
        }
        goto click_done;
    }
    if (!down) {
        g.drag_knob = 0;
        memset(g.pad_down, 0, sizeof g.pad_down);
        goto click_done;
    }
    if (h.kind == HIT_STEP) {
        g.seq[h.a] ^= (1ULL << h.b);
        goto click_done;
    }
    if (h.kind == HIT_PAD) {
        g.pad_down[h.a] = 1;
        synth_voice_trigger(synth_pad_note(h.a), 0.9f);
        goto click_done;
    }
    if (h.kind == HIT_RIBBON) {
        g.drag_ribbon = 1;
        UiRect *t = &g.layout.ribbon_track;
        g.ribbon = (float)(dx - t->x) / (float)t->w * 2.f - 1.f;
        if (g.ribbon < -1.f) g.ribbon = -1.f;
        if (g.ribbon > 1.f) g.ribbon = 1.f;
        goto click_done;
    }
    if (h.kind == HIT_KEY && !g.key_down[h.a]) {
        g.key_down[h.a] = 1;
        synth_voice_trigger(synth_key_midi(h.a), 0.85f);
    }
click_done:
    synth_core_audio_unlock();
}
void synth_core_handle_motion(int sx, int sy) {
    int dx, dy;
    UiRect *t;
    synth_core_audio_lock();
    if (g.drag_knob > 0) {
        float d = (float)(g.drag_knob_y - sy) * 0.005f / g.ui_scale;
        int i = g.drag_knob - 1;
        g.knobs[i] += d;
        if (g.knobs[i] < 0.f) g.knobs[i] = 0.f;
        if (g.knobs[i] > 1.f) g.knobs[i] = 1.f;
        g.drag_knob_y = sy;
        g.dirty = 1;
        synth_recalc_timing();
        synth_core_audio_unlock();
        return;
    }
    if (!g.mouse_down || !screen_to_design(sx, sy, &dx, &dy)) {
        synth_core_audio_unlock();
        return;
    }
    if (g.drag_ribbon) {
        t = &g.layout.ribbon_track;
        g.ribbon = (float)(dx - t->x) / (float)t->w * 2.f - 1.f;
        if (g.ribbon < -1.f) g.ribbon = -1.f;
        if (g.ribbon > 1.f) g.ribbon = 1.f;
        g.dirty = 1;
    }
    synth_core_audio_unlock();
}
void synth_core_handle_release(int sx, int sy, int down) {
    int k, pad;
    (void)sx;
    (void)sy;
    (void)down;
    synth_core_audio_lock();
    g.dirty = 1;
    g.drag_knob = 0;
    g.drag_ribbon = 0;
    for (pad = 0; pad < SYNTH_PADS; pad++) {
        if (g.pad_down[pad])
            i(SYNTH_VOICES, if (g.voices[i].on && g.voices[i].note == synth_pad_note(pad)) g.voices[i].stage = 3)
    }
    memset(g.pad_down, 0, sizeof g.pad_down);
    for (k = 0; k < g.synth_keys; k++) {
        if (g.key_down[k]) {
            g.key_down[k] = 0;
            i(SYNTH_VOICES, if (g.voices[i].on && g.voices[i].note == synth_key_midi(k)) g.voices[i].stage = 3)
        }
    }
    synth_core_audio_unlock();
}
void synth_core_handle_key(int key, int down) {
    synth_core_audio_lock();
    g.dirty = 1;
    if (key == 0xff51) {
        g.ribbon_key_left = down;
        synth_core_audio_unlock();
        return;
    }
    if (key == 0xff53) {
        g.ribbon_key_right = down;
        synth_core_audio_unlock();
        return;
    }
    if (!down) {
        synth_core_audio_unlock();
        return;
    }
    if (key >= '1' && key <= '8') {
        int i = key - '1';
        g.knobs[i] += 0.05f;
        if (g.knobs[i] > 1.f) g.knobs[i] = 1.f;
        synth_recalc_timing();
    } else {
        int idx = -1;
        if (key == 'q' || key == 'Q') idx = 0;
        else if (key == 'w' || key == 'W') idx = 1;
        else if (key == 'e' || key == 'E') idx = 2;
        else if (key == 'r' || key == 'R') idx = 3;
        else if (key == 't' || key == 'T') idx = 4;
        else if (key == 'y' || key == 'Y') idx = 5;
        else if (key == 'u' || key == 'U') idx = 6;
        else if (key == 'i' || key == 'I') idx = 7;
        if (idx >= 0) {
            g.knobs[idx] -= 0.05f;
            if (g.knobs[idx] < 0.f) g.knobs[idx] = 0.f;
            synth_recalc_timing();
        }
    }
    synth_core_audio_unlock();
}
static void synth_letterbox_update(void) {
    int ww = g.win_w, wh = g.win_h;
    float sx, sy;
    if (ww < SYNTH_MIN_W) ww = SYNTH_MIN_W;
    if (wh < SYNTH_MIN_H) wh = SYNTH_MIN_H;
    sx = (float)ww / (float)DESIGN_W;
    sy = (float)wh / (float)DESIGN_H;
    g.ui_scale = sx < sy ? sx : sy;
    g.off_x = (ww - (int)(DESIGN_W * g.ui_scale)) / 2;
    g.off_y = (wh - (int)(DESIGN_H * g.ui_scale)) / 2;
}
int synth_core_fb_design_init(void) {
    P(g.fb,0)
    g.fb = (uint32_t *)calloc((size_t)DESIGN_W * (size_t)DESIGN_H, sizeof(uint32_t));
    return g.fb ? 0 : -1;
}
static void synth_request_maximize(void) { synth_platform_request_maximize(); }
void synth_core_present_scale(void) {
    uint32_t bar = rgb(14, 15, 18);
    int x, y, dx, dy, dw, dh;
    Pv(!g.present || !g.fb)
    dw = (int)(DESIGN_W * g.ui_scale);
    dh = (int)(DESIGN_H * g.ui_scale);
    for (y = 0; y < g.win_h; y++) {
        for (x = 0; x < g.win_w; x++) {
            if (x < g.off_x || y < g.off_y || x >= g.off_x + dw || y >= g.off_y + dh) {
                g.present[y * g.win_w + x] = bar;
                continue;
            }
            dx = (int)((x - g.off_x) / g.ui_scale);
            dy = (int)((y - g.off_y) / g.ui_scale);
            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            if (dx >= DESIGN_W) dx = DESIGN_W - 1;
            if (dy >= DESIGN_H) dy = DESIGN_H - 1;
            g.present[y * g.win_w + x] = g.fb[dy * DESIGN_W + dx];
        }
    }
}
int synth_core_fb_resize(int w, int h) {
    if (w < SYNTH_MIN_W) w = SYNTH_MIN_W;
    if (h < SYNTH_MIN_H) h = SYNTH_MIN_H;
#ifdef SYNTH_HAVE_GL
    if (g_use_gl) {
        P(w == g.win_w && h == g.win_h,0)
    } else
#endif
    P(w == g.win_w && h == g.win_h && g.present,0)
    g.win_w = w;
    g.win_h = h;
    int octaves = w / 480;
    if (octaves < 1) octaves = 1;
    if (octaves > 6) octaves = 6;
    g.synth_keys = octaves * 12;
    g.base_note = 60 - (g.synth_keys / 2);

    synth_letterbox_update();
    synth_layout_compute(&g.layout);
#ifdef SYNTH_HAVE_GL
    if (g_use_gl) {
        synth_gl_resize(w, h);
        g.dirty = 1;
        return 0;
    }
#endif
#ifdef __linux__
    if (g.dpy && g.img) {
        g.img->data = NULL;
        XDestroyImage(g.img);
        g.img = NULL;
    }
#endif
    free(g.present);
    g.present = (uint32_t *)calloc((size_t)g.win_w * (size_t)g.win_h, sizeof(uint32_t));
    P(!g.present,-1)
#ifdef __linux__
    if (g.dpy) {
        g.img = XCreateImage(g.dpy, DefaultVisual(g.dpy, g.scr), 24, ZPixmap, 0, (char *)g.present, g.win_w,
                             g.win_h, 32, 0);
        P(!g.img,-1)
        g.img->byte_order = LSBFirst;
        g.img->bitmap_bit_order = LSBFirst;
    }
#endif
    g.dirty = 1;
    return 0;
}
static float knob_val(int i) {
    float v = g.knobs[i];
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    return v;
}
static float synth_bpm(void) { return 40.f + knob_val(0) * 200.f; }
static float synth_master(void) { return knob_val(1); }
static float synth_cutoff(void) { return 200.f + knob_val(2) * 7800.f; }
static float synth_reso(void) { return knob_val(3) * 0.72f; }
static void synth_recalc_timing(void) {
    float bpm = synth_bpm();
    if (bpm < 40.f) bpm = 40.f;
    g.samples_per_step = (double)SYNTH_SR * 60.0 / (bpm * 4.0);
    g.samples_per_beat = (double)SYNTH_SR * 60.0 / (double)bpm;
}
static void synth_loop_sync_target(void) {
    int t = (int)(g.step_len * g.samples_per_step);
    if (t < SYNTH_SR / 4) t = SYNTH_SR / 4;
    if (t > SYNTH_LOOP_MAX) t = SYNTH_LOOP_MAX;
    g.loop_target = t;
}
static uint32_t synth_r32le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t synth_r16le(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void synth_sample_basename(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    snprintf(out, cap, "%s", base);
}
static int synth_wav_load(const char *path, float *dst, int dst_cap, int *out_n) {
    FILE *f;
    unsigned char hdr[12], chunk[8];
    uint32_t riff_size, chunk_sz, fmt_rate = 0, data_bytes = 0;
    uint16_t fmt_ch = 0, fmt_bits = 0, fmt_tag = 0;
    long data_pos = 0;
    float *pcm = NULL;
    int i, n, ch, frames;
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f);
        return -1;
    }
    riff_size = synth_r32le(hdr + 4);
    (void)riff_size;
    while (fread(chunk, 1, 8, f) == 8) {
        chunk_sz = synth_r32le(chunk + 4);
        if (!memcmp(chunk, "fmt ", 4)) {
            unsigned char fmt[16];
            if (chunk_sz < 16 || fread(fmt, 1, 16, f) != 16) {
                fclose(f);
                return -1;
            }
            fmt_tag = synth_r16le(fmt);
            fmt_ch = synth_r16le(fmt + 2);
            fmt_rate = synth_r32le(fmt + 4);
            fmt_bits = synth_r16le(fmt + 14);
            if (chunk_sz > 16) fseek(f, (long)(chunk_sz - 16), SEEK_CUR);
        } else if (!memcmp(chunk, "data", 4)) {
            data_bytes = chunk_sz;
            data_pos = ftell(f);
            fseek(f, (long)chunk_sz, SEEK_CUR);
        } else {
            fseek(f, (long)chunk_sz + (chunk_sz & 1u), SEEK_CUR);
        }
    }
    if (fmt_tag != 1 || fmt_bits != 16 || fmt_ch < 1 || fmt_ch > 2 || !data_bytes || !fmt_rate) {
        fclose(f);
        return -1;
    }
    frames = (int)(data_bytes / ((size_t)fmt_ch * 2u));
    pcm = (float *)malloc((size_t)frames * sizeof(float));
    if (!pcm) {
        fclose(f);
        return -1;
    }
    fseek(f, data_pos, SEEK_SET);
    for (i = 0; i < frames; i++) {
        int16_t s0 = 0, s1 = 0;
        unsigned char b[4];
        if (fread(b, 1, (size_t)fmt_ch * 2u, f) != (size_t)fmt_ch * 2u) break;
        s0 = (int16_t)(b[0] | (b[1] << 8));
        if (fmt_ch == 2) s1 = (int16_t)(b[2] | (b[3] << 8));
        pcm[i] = (fmt_ch == 2) ? ((float)s0 + (float)s1) * 0.5f / 32768.f : (float)s0 / 32768.f;
    }
    fclose(f);
    n = i;
    if (fmt_rate != SYNTH_SR) {
        int out_frames = (int)((int64_t)n * SYNTH_SR / (int64_t)fmt_rate);
        float *rs = (float *)malloc((size_t)out_frames * sizeof(float));
        if (!rs) {
            free(pcm);
            return -1;
        }
        for (i = 0; i < out_frames; i++) {
            float src = (float)i * (float)fmt_rate / (float)SYNTH_SR;
            int idx0 = (int)src;
            float t = src - (float)idx0;
            float a = pcm[idx0 < n ? idx0 : n - 1];
            float b = pcm[(idx0 + 1) < n ? idx0 + 1 : n - 1];
            rs[i] = a * (1.f - t) + b * t;
        }
        free(pcm);
        pcm = rs;
        n = out_frames;
    }
    if (n > dst_cap) n = dst_cap;
    memcpy(dst, pcm, (size_t)n * sizeof(float));
    free(pcm);
    *out_n = n;
    return 0;
}
static void synth_sample_trigger(void) {
    Pv(g.sample_n <= 0)
    g.sample_pos = 0;
    g.sample_playing = 1;
}
static float synth_sample_tick(void) {
    float s;
    P(!g.sample_playing || g.sample_n <= 0, 0.f)
    s = g.sample_buf[g.sample_pos++] * g.sample_gain;
    if (g.sample_pos >= g.sample_n) {
        g.sample_playing = 0;
        g.sample_pos = 0;
    }
    return s;
}
const char *synth_row_label(int row) {
    static const char *base[SYNTH_ROWS] = {"KICK", "SNAR", "HAT", "PERC", "BASS", "SYNTH", "SAMP", "LEAD"};
    static char samp_lbl[32];
    P(row < 0 || row >= SYNTH_ROWS, "")
    if (row == 6 && g.sample_name[0]) {
        snprintf(samp_lbl, sizeof samp_lbl, "%.10s", g.sample_name);
        return samp_lbl;
    }
    return base[row];
}
static int synth_row_midi_note(int row) {
    static const int drums[SYNTH_DRUM_ROWS] = {36, 42, 44, 46};
    P(row < 0 || row >= SYNTH_ROWS, 60)
    if (row < SYNTH_DRUM_ROWS) return drums[row];
    if (row == 6) return 60;
    return g.row_midi[row] > 0 ? g.row_midi[row] : 60;
}
static const char *synth_pad_lbl(int pad) {
    /* 4x4 grid, row-major; labels match GM-ish voice (see synth_pad_note). */
    static const char *lbls[16] = {
        "CYM",  "CYM",  "TOM",  "OPEN", /* 47, 47, 46, 44 — metallic / open */
        "SNAR", "CHH",  "SNAR", "SNAR", /* 43, 42, 41, 40 — snare + closed hat */
        "CLAP", "TOM",  "CLAP", "SNAR", /* 39, 45, 39, 40 */
        "KICK", "RIM",  "SNAR", "OPEN"  /* 36, 37, 38, 44 — core kit row */
    };
    P(pad < 0 || pad >= 16,"")
    return lbls[pad];
}
static int synth_pad_note(int pad) {
    static const int map[16] = {
        47, 47, 46, 44,
        43, 42, 41, 40,
        39, 45, 39, 40,
        36, 37, 38, 44
    };
    P(pad<0||pad>=SYNTH_PADS,36)return map[pad];
}
static float note_freq(int n) {
    return 440.f * powf(2.f, (n - 69) / 12.f);
}
static SynthVoice *synth_voice_alloc(int note, float vel) {
    int i, oldest = 0;
    for (i = 0; i < SYNTH_METRO_VOICE; i++) {
        if (!g.voices[i].on) {
            oldest = i;
            break;
        }
        oldest = i;
    }
    SynthVoice *v = &g.voices[oldest];
    v->on = 1;
    v->note = note;
    v->freq = note_freq(note);
    v->phase = 0.f;
    v->sub_phase = 0.f;
    v->env = 0.f;
    v->stage = 0;
    v->vel = vel;
    v->lp = 0.f;
    v->gate = 0;
    v->tone = SYNTH_TONE_DEFAULT;
    return v;
}
static void synth_voice_trigger(int note, float vel) {
    synth_voice_alloc(note, vel);
}
static void synth_voice_trigger_gated(int note, float vel, int gate_samples) {
    SynthVoice *v = synth_voice_alloc(note, vel);
    v->gate = gate_samples > 0 ? gate_samples : 1;
}
static void synth_voice_trigger_bass(int note, float vel, int gate_samples) {
    SynthVoice *v = synth_voice_alloc(note, vel);
    v->tone = SYNTH_TONE_BASS;
    v->gate = gate_samples > 0 ? gate_samples : 1;
}
static void synth_trigger_note(int note, float vel) {
    pthread_mutex_lock(&g.mu);
    synth_voice_trigger(note, vel);
    pthread_mutex_unlock(&g.mu);
}
static void synth_release_note(int note) {
    pthread_mutex_lock(&g.mu);
    i(SYNTH_VOICES,if(g.voices[i].on&&g.voices[i].note==note)g.voices[i].stage=3)
    pthread_mutex_unlock(&g.mu);
}
static float synth_env_advance(SynthVoice *v, float a, float d, float s, float r) {
    P(!v->on,0.f)
    if (v->stage == 0) {
        if (a <= 0.f) {
            v->env = 1.f;
            v->stage = 1;
        } else {
            v->env += 1.f / (a * (float)SYNTH_SR + 1.f);
            if (v->env >= 1.f) {
                v->env = 1.f;
                v->stage = 1;
            }
        }
    } else if (v->stage == 1) {
        v->env -= (1.f - s) / (d * (float)SYNTH_SR + 1.f);
        if (v->env <= s) {
            v->env = s;
            v->stage = 2;
        }
    } else if (v->stage == 3) {
        v->env -= s / (r * (float)SYNTH_SR + 1.f);
        if (v->env <= 0.f) {
            v->env = 0.f;
            v->on = 0;
        }
    }
    return v->env;
}
static float metro_noise(float x);
static float synth_drum_sample(SynthVoice *v) {
    float t, env, out, freq, n;
    t = v->phase;
    v->phase += 1.f / (float)SYNTH_SR;
    if (t > 0.3f) {
        v->on = 0;
        v->phase = 0.f;
        return 0.f;
    }
    n = metro_noise(t * 900.f + (float)v->note * 0.41f);
    if (v->note <= 37) {
        freq = 58.f + 110.f * expf(-t * 48.f);
        env = expf(-t * 28.f);
        out = (sinf(2.f * (float)SYNTH_PI * freq * t) * 0.7f + n * 0.45f) * env * v->vel * 1.6f;
    } else if (v->note <= 43) {
        env = expf(-t * 30.f);
        out = (n * 0.9f + sinf(2.f * (float)SYNTH_PI * 200.f * t) * 0.12f) * env * v->vel * 1.35f;
    } else {
        env = expf(-t * 50.f);
        out = n * env * v->vel * 1.1f;
    }
    return out;
}
static float synth_voice_sample(SynthVoice *v, float cutoff, float reso, float bend) {
    float a, d, s, r, env, osc, out, f, saw, sin1, fc;
    P(!v->on,0.f)
    if (v->gate > 0) {
        v->gate--;
        if (v->gate == 0 && v->stage < 3)
            v->stage = 3;
    }
    P(v->note < 48 && v != &g.voices[SYNTH_METRO_VOICE],synth_drum_sample(v))
    if (v == &g.voices[SYNTH_METRO_VOICE]) {
        a = 0.f;
        d = 0.04f;
        s = 0.f;
        r = 0.05f;
    } else if (v->note < 48) {
        a = 0.f;
        d = 0.06f;
        s = 0.f;
        r = 0.08f;
    } else if (v->tone == SYNTH_TONE_BASS) {
        a = 0.002f;
        d = 0.05f + knob_val(5) * 0.1f;
        s = 0.45f + knob_val(6) * 0.3f;
        r = 0.14f + knob_val(7) * 0.4f;
    } else {
        a = knob_val(4) * 0.5f;
        d = 0.001f + knob_val(5) * 0.5f;
        s = 0.2f + knob_val(6) * 0.8f;
        r = 0.001f + knob_val(7) * 1.5f;
    }
    env = synth_env_advance(v, a, d, s, r);
    P(!v->on && env <= 0.f,0.f)
    f = v->freq * powf(2.f, bend / 12.f);
    v->phase += f / (float)SYNTH_SR;
    if (v->phase >= 1.f) v->phase -= 1.f;
    saw = 2.f * v->phase - 1.f;
    sin1 = sinf(2.f * (float)SYNTH_PI * v->phase);
    if (v->tone == SYNTH_TONE_BASS) {
        v->sub_phase += (f * 0.5f) / (float)SYNTH_SR;
        if (v->sub_phase >= 1.f) v->sub_phase -= 1.f;
        osc = 0.4f * saw + 0.25f * sin1 + 0.7f * sinf(2.f * (float)SYNTH_PI * v->sub_phase);
        out = osc * env * v->vel * 1.3f;
        fc = (cutoff * 0.16f) / (float)SYNTH_SR;
    } else {
        osc = 0.6f * saw + 0.4f * sin1;
        out = osc * env * v->vel;
        fc = cutoff / (float)SYNTH_SR;
    }
    if (fc > 0.45f) fc = 0.45f;
    v->lp += fc * (out - v->lp);
    out = v->lp + reso * (out - v->lp);
    return out;
}
static void synth_seq_tick(void) {
    int row;
    Pv(!g.playing)
    if (g.step_pos == 0 && g.loop_arm && !g.loop_recording) {
        g.loop_recording = 1;
        g.loop_wpos = 0;
        synth_loop_sync_target();
        g.loop_arm = 0;
        g.loop_flash = 12;
    }
    for (row = 0; row < SYNTH_ROWS; row++) {
        if (!(g.seq[row] & (1ULL << g.step_pos))) continue;
        if (row == 6) {
            if (g.sample_n > 0) synth_sample_trigger();
            continue;
        }
        if (row < SYNTH_DRUM_ROWS) {
            synth_voice_trigger(synth_row_midi_note(row), 1.f);
        } else if (row == 4) {
            int gate = (int)(g.samples_per_step * 0.92);
            synth_voice_trigger_bass(synth_row_midi_note(row), 1.f, gate);
        } else {
            int gate = (int)(g.samples_per_step * 0.9);
            synth_voice_trigger_gated(synth_row_midi_note(row), 0.85f, gate);
        }
    }
    g.step_pos++;
    if (g.step_pos >= g.step_len) g.step_pos = 0;
}
static float metro_noise(float x) {
    return sinf(x * 12.9898f) * 43758.5453f - floorf(sinf(x * 12.9898f) * 43758.5453f);
}
static void metro_play_beat(int accent) {
    SynthVoice *v = &g.voices[SYNTH_METRO_VOICE];
    int note = (g.metro_sound == 0) ? (accent ? 79 : 76) : (accent ? 42 : 45);
    v->on = 1;
    v->note = note;
    v->freq = note_freq(note);
    v->phase = 0.f;
    v->env = 0.f;
    v->stage = 0;
    v->vel = accent ? 0.95f : 0.8f;
    v->lp = 0.f;
}
static void metro_reset_unlocked(int on) {
    g.metro_on = on ? 1 : 0;
    g.metro_beat = 0;
    g.metro_phase_samples = 0.0;
    g.metro_flash = 0;
    g.metro.active = 0;
    if (on) {
        metro_play_beat(1);
        g.metro_beat = 1;
        g.metro_flash = 12;
    }
}
static void metro_reset(int on) {
    synth_core_audio_lock();
    metro_reset_unlocked(on);
    synth_core_audio_unlock();
}
static void metro_advance_beat(void) {
    int accent;
    Pv(!g.metro_on)
    accent = (g.metro_beat == 0);
    metro_play_beat(accent);
    g.metro_beat = (g.metro_beat + 1) % 4;
    g.metro_flash = 12;
}
void synth_core_render(float *out, int n) {
    int i;
    float master, cutoff, reso, bend, mix;
    float rb_dt = (float)n / (float)SYNTH_SR;

    synth_core_audio_lock();
    if (g.ribbon_key_left && !g.ribbon_key_right) {
        g.ribbon -= rb_dt * 2.5f;
        if (g.ribbon < -1.f) g.ribbon = -1.f;
        g.dirty = 1;
    } else if (g.ribbon_key_right && !g.ribbon_key_left) {
        g.ribbon += rb_dt * 2.5f;
        if (g.ribbon > 1.f) g.ribbon = 1.f;
        g.dirty = 1;
    } else if (!g.drag_ribbon) {
        if (g.ribbon > 0.001f) {
            g.ribbon -= rb_dt * 4.0f;
            if (g.ribbon < 0.f) g.ribbon = 0.f;
            g.dirty = 1;
        } else if (g.ribbon < -0.001f) {
            g.ribbon += rb_dt * 4.0f;
            if (g.ribbon > 0.f) g.ribbon = 0.f;
            g.dirty = 1;
        } else {
            g.ribbon = 0.f;
        }
    }

    master = synth_master();
    cutoff = synth_cutoff();
    reso = synth_reso();
    bend = g.ribbon * 2.f;
    for (i = 0; i < n; i++) {
        while (g.phase_samples >= g.samples_per_step) {
            g.phase_samples -= g.samples_per_step;
            synth_seq_tick();
        }
        g.phase_samples += 1.0;
        if (g.metro_on) {
            while (g.metro_phase_samples >= g.samples_per_beat) {
                g.metro_phase_samples -= g.samples_per_beat;
                metro_advance_beat();
            }
            g.metro_phase_samples += 1.0;
        }
        mix = 0.f;
        j(SYNTH_METRO_VOICE,mix+=synth_voice_sample(&g.voices[j],cutoff,reso,bend))
        mix += synth_sample_tick();
        {
            float metro_m = synth_voice_sample(&g.voices[SYNTH_METRO_VOICE], cutoff, reso, 0.f);
            float voice_out = g.mute ? 0.f : mix * master * 0.7f;
            float loop_s = 0.f;
            float rec_src = voice_out + metro_m * 0.95f;
            if (g.loop_len > 0 && g.loop_playing) {
                loop_s = g.loop_buf[g.loop_pos] * g.loop_gain;
                g.loop_pos++;
                if (g.loop_pos >= g.loop_len) g.loop_pos = 0;
            }
            if (g.loop_recording && g.loop_wpos < g.loop_target) {
                if (g.loop_overdub && g.loop_len > 0)
                    g.loop_buf[g.loop_wpos] += rec_src * 0.5f;
                else
                    g.loop_buf[g.loop_wpos] = rec_src;
                g.loop_wpos++;
                if (g.loop_wpos >= g.loop_target) {
                    g.loop_recording = 0;
                    g.loop_len = g.loop_target;
                    g.loop_pos = 0;
                    g.loop_playing = 1;
                    g.loop_flash = 12;
                    g.dirty = 1;
                }
            }
            out[i] = rec_src + loop_s;
        }
    }
    synth_ui_push_audio_samples(out, n);
    synth_core_audio_unlock();
}
#ifdef __linux__
static void *synth_audio_loop(void *arg) {
    float mono[SYNTH_BUF];
    int16_t stereo[SYNTH_BUF * 2];
    int i;
    snd_pcm_sframes_t wr;
    snd_pcm_uframes_t left;
    int16_t *ptr;
    (void)arg;
    while (g.audio_run) {
        synth_core_render(mono, SYNTH_BUF);
        for (i = 0; i < SYNTH_BUF; i++) {
            int s = (int)(mono[i] * 32767.f);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            stereo[i * 2] = (int16_t)s;
            stereo[i * 2 + 1] = (int16_t)s;
        }
        ptr = stereo;
        left = SYNTH_BUF;
        while (left > 0 && g.audio_run) {
            wr = snd_pcm_writei(g.pcm, ptr, left);
            if (wr == -EPIPE) {
                snd_pcm_prepare(g.pcm);
                continue;
            }
            if (wr < 0) {
                snd_pcm_prepare(g.pcm);
                break;
            }
            left -= (snd_pcm_uframes_t)wr;
            ptr += wr * 2;
        }
    }
    return NULL;
}
#endif
#ifdef __linux__
static int synth_x11_init(char *err, size_t cap) {
    int scr;
    XSetWindowAttributes swa;
    Atom wm_delete;
    g.dpy = XOpenDisplay(NULL);
    if (!g.dpy) {
        snprintf(err, cap, "synth_open: cannot open X display");
        return -1;
    }
    scr = DefaultScreen(g.dpy);
    g.scr = scr;
#ifdef SYNTH_HAVE_GL
    g_use_gl = getenv("SHAKTI_SYNTH_NOGL") ? 0 : 1;
    if (g_use_gl) {
        unsigned long win = 0;
        if (synth_gl_create_window(g.dpy, scr, "Shakti Synth", DESIGN_W, DESIGN_H, &win) == 0) {
            g.win = (Window)win;
        } else {
            g_use_gl = 0;
        }
    }
#endif
    if (!g.win) {
        g.win = XCreateSimpleWindow(g.dpy, RootWindow(g.dpy, scr), 100, 100, DESIGN_W, DESIGN_H, 1,
                                    BlackPixel(g.dpy, scr), COL_CHASSIS);
        g.gc = XCreateGC(g.dpy, g.win, 0, NULL);
    }
    XStoreName(g.dpy, g.win, "Shakti Synth");
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                     ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    XSelectInput(g.dpy, g.win, swa.event_mask);
#ifdef SYNTH_HAVE_GL
    if (g_use_gl) {
        g.win_w = DESIGN_W;
        g.win_h = DESIGN_H;
        synth_core_fb_resize(DESIGN_W, DESIGN_H);
    } else
#endif
    {
        if (synth_core_fb_design_init() != 0 || synth_core_fb_resize(DESIGN_W, DESIGN_H) != 0) {
            snprintf(err, cap, "synth_open: framebuffer init failed");
            return -1;
        }
    }
    synth_request_maximize();
    XMapWindow(g.dpy, g.win);
    wm_delete = XInternAtom(g.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g.dpy, g.win, &wm_delete, 1);
    synth_request_maximize();
    XFlush(g.dpy);
    return 0;
}
static int synth_alsa_init(char *err, size_t cap) {
    int rc;
    const char *env = getenv("SHAKTI_SYNTH_DEVICE");
    const char *dev = NULL;
    const char *fallbacks[] = {"pulse", "default", NULL};
    int fi;
    rc = -1;
    if (env && env[0]) {
        rc = snd_pcm_open(&g.pcm, env, SND_PCM_STREAM_PLAYBACK, 0);
        if (rc < 0) {
            snprintf(err, cap, "synth_open: ALSA %s (%s)", snd_strerror(rc), env);
            return -1;
        }
        dev = env;
    } else {
        for (fi = 0; fallbacks[fi]; fi++) {
            rc = snd_pcm_open(&g.pcm, fallbacks[fi], SND_PCM_STREAM_PLAYBACK, 0);
            if (rc >= 0) {
                dev = fallbacks[fi];
                break;
            }
        }
        if (rc < 0) {
            snprintf(err, cap, "synth_open: ALSA %s", snd_strerror(rc));
            return -1;
        }
    }
    rc = snd_pcm_set_params(g.pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, SYNTH_SR,
                            1, 500000);
    if (rc < 0) {
        snprintf(err, cap, "synth_open: ALSA params %s", snd_strerror(rc));
        return -1;
    }
    snd_pcm_prepare(g.pcm);
    return 0;
}
int synth_platform_init(char *err, size_t cap) {
    return synth_x11_init(err, cap);
}
void synth_platform_present(void) {
    if (g.dpy && g.img)
        XPutImage(g.dpy, g.win, g.gc, g.img, 0, 0, 0, 0, g.win_w, g.win_h);
}
void synth_platform_request_maximize(void) {
    Atom wm_state, max_h, max_v;
    XEvent xev;
    Atom states[2];
    Pv(!g.dpy || !g.win)
    wm_state = XInternAtom(g.dpy, "_NET_WM_STATE", False);
    max_h = XInternAtom(g.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    max_v = XInternAtom(g.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    states[0] = max_h;
    states[1] = max_v;
    XChangeProperty(g.dpy, g.win, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)states, 2);
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = g.win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1;
    xev.xclient.data.l[1] = (long)max_h;
    xev.xclient.data.l[2] = (long)max_v;
    XSendEvent(g.dpy, RootWindow(g.dpy, g.scr), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}
int synth_platform_poll(int *cfg_w, int *cfg_h, int *cfg_count) {
    XEvent ev;
    int count = 0;
    if (cfg_w) *cfg_w = 0;
    if (cfg_h) *cfg_h = 0;
    if (cfg_count) *cfg_count = 0;
    while (g.dpy && XPending(g.dpy)) {
        XNextEvent(g.dpy, &ev);
        if (ev.type == ClientMessage) {
            Atom proto = (Atom)ev.xclient.data.l[0];
            if (proto == XInternAtom(g.dpy, "WM_DELETE_WINDOW", True)) g.alive = 0;
        } else if (ev.type == DestroyNotify && ev.xdestroywindow.window == g.win) {
            g.alive = 0;
            break;
        } else if (ev.type == ConfigureNotify && ev.xconfigure.window == g.win) {
            count++;
            if (cfg_w) *cfg_w = ev.xconfigure.width;
            if (cfg_h) *cfg_h = ev.xconfigure.height;
            if (g.want_maximize && g.dpy) {
                int sw = DisplayWidth(g.dpy, g.scr);
                int sh = DisplayHeight(g.dpy, g.scr);
                if (ev.xconfigure.width < sw * 3 / 4 || ev.xconfigure.height < sh * 3 / 4) {
                    if (g.maximize_tries < 8) {
                        g.maximize_tries++;
                        synth_request_maximize();
                    }
                } else {
                    g.want_maximize = 0;
                }
            }
        } else if (ev.type == ButtonPress) {
            g.mouse_down = 1;
            g.mouse_x = ev.xbutton.x;
            g.mouse_y = ev.xbutton.y;
            input_hub_inject_mouse(g.mouse_x, g.mouse_y, 1);
            if (!input_own_gui())
                synth_core_handle_click(g.mouse_x, g.mouse_y, 1);
        } else if (ev.type == ButtonRelease) {
            g.mouse_down = 0;
            g.mouse_x = ev.xbutton.x;
            g.mouse_y = ev.xbutton.y;
            input_hub_inject_mouse(g.mouse_x, g.mouse_y, 0);
            if (!input_own_gui())
                synth_core_handle_release(ev.xbutton.x, ev.xbutton.y, 0);
        } else if (ev.type == MotionNotify) {
            g.mouse_x = ev.xmotion.x;
            g.mouse_y = ev.xmotion.y;
            input_hub_inject_mouse(g.mouse_x, g.mouse_y, g.mouse_down);
            if (!input_own_gui())
                synth_core_handle_motion(g.mouse_x, g.mouse_y);
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            char utf8[8] = {0};
            if (ks >= 32 && ks < 127) {
                utf8[0] = (char)ks;
            }
            input_hub_inject_key((int)ks, (int)ev.xkey.state, utf8, 1);
            if (!input_own_gui())
                synth_core_handle_key((int)ks, 1);
        } else if (ev.type == KeyRelease) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            char utf8[8] = {0};
            if (ks >= 32 && ks < 127) {
                utf8[0] = (char)ks;
            }
            input_hub_inject_key((int)ks, (int)ev.xkey.state, utf8, 0);
            if (!input_own_gui())
                synth_core_handle_key((int)ks, 0);
        }
    }
    if (cfg_count) *cfg_count = count;
    return g.alive;
}
void synth_platform_shutdown(void) {
#ifdef SYNTH_HAVE_GL
    if (g_use_gl) {
        synth_gl_shutdown();
        g.win = 0;
        g_use_gl = 0;
    }
#endif
    if (g.img) {
        g.img->data = NULL;
        XDestroyImage(g.img);
        g.img = NULL;
    }
    if (g.gc) XFreeGC(g.dpy, g.gc);
    if (g.win) XDestroyWindow(g.dpy, g.win);
    if (g.dpy) {
        XCloseDisplay(g.dpy);
        g.dpy = NULL;
    }
}
int synth_audio_start(char *err, size_t cap) {
    P(synth_alsa_init(err, cap) != 0,-1)
    g.audio_run = 1;
    if (pthread_create(&g.audio_tid, NULL, synth_audio_loop, NULL) != 0) {
        snprintf(err, cap, "synth_open: pthread_create failed");
        return -1;
    }
    return 0;
}
void synth_audio_stop(void) {
    g.audio_run = 0;
    if (g.audio_tid) pthread_join(g.audio_tid, NULL);
    g.audio_tid = 0;
    if (g.pcm) {
        snd_pcm_close(g.pcm);
        g.pcm = NULL;
    }
}
#endif
static void synth_shutdown(void) {
    synth_audio_stop();
    synth_platform_shutdown();
    if (g_mu_inited) {
        pthread_mutex_destroy(&g.mu);
        g_mu_inited = 0;
    }
    free(g.fb);
    g.fb = NULL;
    free(g.present);
    g.present = NULL;
    memset(&g, 0, sizeof g);
}
int synth_open(char *err, size_t err_cap) {
    int headless = getenv("SHAKTI_SYNTH_HEADLESS") != NULL;
    P(g.open && g.alive,0)
#ifdef __linux__
    if (!headless && !getenv("DISPLAY")) {
        snprintf(err, err_cap, "synth_open: DISPLAY not set");
        return -1;
    }
#endif
    memset(&g, 0, sizeof g);
    g.step_len = SYNTH_DEFAULT_STEPS;
    g.knobs[0] = 0.35f;
    g.knobs[1] = 0.75f;
    g.knobs[2] = 0.55f;
    g.knobs[3] = 0.2f;
    g.knobs[4] = 0.f;
    g.knobs[5] = 0.25f;
    g.knobs[6] = 0.7f;
    g.knobs[7] = 0.3f;
    g.row_midi[4] = 36;
    g.row_midi[5] = 60;
    g.row_midi[7] = 67;
    g.sample_gain = 0.9f;
    g.loop_gain = 0.85f;
    synth_recalc_timing();
    pthread_mutex_init(&g.mu, NULL);
    g_mu_inited = 1;
    g.want_maximize = 1;
    g.maximize_tries = 0;
    if (headless) {
        if (synth_core_fb_design_init() != 0) {
            if (err) snprintf(err, err_cap, "synth_open: fb init failed");
            return -1;
        }
        synth_core_fb_resize(DESIGN_W, DESIGN_H);
    } else {
        P(synth_platform_init(err, err_cap) != 0,-1)
        if (synth_audio_start(err, err_cap) != 0) {
            synth_shutdown();
            return -1;
        }
    }
    g.open = 1;
    g.alive = 1;
    for (int i = 0; i < 100 && g.alive && g.want_maximize; i++) synth_tick(err, err_cap);
    synth_present_frame();
    return 0;
}
void synth_close(void) {
    Pv(!g.open)
    g.alive = 0;
    synth_shutdown();
}
int synth_alive(void) { return g.open && g.alive; }
int synth_tick(char *err, size_t err_cap) {
    int cfg_count = 0, pending_w = 0, pending_h = 0;
    int headless = getenv("SHAKTI_SYNTH_HEADLESS") != NULL;
    (void)err;
    (void)err_cap;
    P(!g.open || !g.alive,0)
    if (g.metro_flash > 0) g.metro_flash--;
    if (g.loop_flash > 0) g.loop_flash--;
    if (!headless) {
        synth_platform_poll(&pending_w, &pending_h, &cfg_count);
        if (cfg_count > 0) synth_core_fb_resize(pending_w, pending_h);
    }
    if (headless) {
        synth_core_ui_draw();
        synth_ui_blit();
    } else {
        synth_present_frame();
    }
    if (!g.alive) synth_shutdown();
    if (!headless) {
        usleep(16000);
    }
    return g.alive;
}
int synth_set_steps(int n, char *err, size_t err_cap) {
    if (n < 1 || n > SYNTH_MAX_STEPS) {
        snprintf(err, err_cap, "synth_set_steps: length must be 1..%d", SYNTH_MAX_STEPS);
        return -1;
    }
    g.step_len = n;
    if (g.step_pos >= g.step_len) g.step_pos = 0;
    return 0;
}
int synth_steps(void) { return g.step_len; }
int synth_set_metro(int on, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    metro_reset(on ? 1 : 0);
    return 0;
}
int synth_metro_on(void) { return g.metro_on; }
int synth_set_metro_sound(int sound, char *err, size_t err_cap) {
    if (sound < 0 || sound > 1) {
        snprintf(err, err_cap, "synth_set_metro_sound: sound must be 0 (click) or 1 (drum)");
        return -1;
    }
    g.metro_sound = sound;
    return 0;
}
int synth_metro_sound(void) { return g.metro_sound; }
int synth_set_mute(int mute, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    g.mute = mute ? 1 : 0;
    return 0;
}
int synth_mute_on(void) { return g.mute; }
int synth_note_on(int note, float vel, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_note_on: synth not open");
        return -1;
    }
    if (note < 0 || note > 127) {
        if (err && err_cap) snprintf(err, err_cap, "synth_note_on: note must be 0..127");
        return -1;
    }
    if (vel < 0.f) vel = 0.f;
    if (vel > 1.f) vel = 1.f;
    synth_key_set_note(note, 1);
    synth_trigger_note(note, vel);
    return 0;
}
int synth_note_off(int note, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_note_off: synth not open");
        return -1;
    }
    if (note < 0 || note > 127) {
        if (err && err_cap) snprintf(err, err_cap, "synth_note_off: note must be 0..127");
        return -1;
    }
    synth_key_set_note(note, 0);
    synth_release_note(note);
    return 0;
}
int synth_set_bpm(float bpm, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_bpm: synth not open");
        return -1;
    }
    if (bpm < 40.f) bpm = 40.f;
    if (bpm > 240.f) bpm = 240.f;
    g.knobs[0] = (bpm - 40.f) / 200.f;
    synth_recalc_timing();
    return 0;
}
float synth_get_bpm(void) {
    P(!g.open,120.f)
    return synth_bpm();
}
int synth_set_level(float level, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_level: synth not open");
        return -1;
    }
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;
    g.knobs[1] = level;
    return 0;
}
float synth_get_level(void) {
    P(!g.open,0.75f)
    return knob_val(1);
}
int synth_set_cutoff(float cutoff, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_cutoff: synth not open");
        return -1;
    }
    if (cutoff < 0.f) cutoff = 0.f;
    if (cutoff > 1.f) cutoff = 1.f;
    g.knobs[2] = cutoff;
    return 0;
}
float synth_get_cutoff(void) {
    P(!g.open,0.55f)
    return knob_val(2);
}
int synth_set_reso(float reso, char *err, size_t err_cap) {
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_reso: synth not open");
        return -1;
    }
    if (reso < 0.f) reso = 0.f;
    if (reso > 1.f) reso = 1.f;
    g.knobs[3] = reso;
    return 0;
}
float synth_get_reso(void) {
    P(!g.open,0.2f)
    return knob_val(3);
}
int synth_set_seq_row(int row, uint64_t mask, char *err, size_t err_cap) {
    uint64_t limit;
    if (!g.open || !g.alive) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_seq_row: synth not open");
        return -1;
    }
    if (row < 0 || row >= SYNTH_ROWS) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_seq_row: row must be 0..%d", SYNTH_ROWS - 1);
        return -1;
    }
    limit = (g.step_len >= 64) ? ~0ULL : ((1ULL << g.step_len) - 1ULL);
    g.seq[row] = mask & limit;
    return 0;
}
int synth_play(int on, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    P(!g.open || !g.alive,-1)
    g.playing = on ? 1 : 0;
    if (g.playing) g.step_pos = 0;
    return 0;
}
int synth_playing(void) { return g.playing; }
int synth_set_viz(int mode, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    synth_ui_set_viz_mode(mode);
    return 0;
}
int synth_viz_mode(void) { return synth_ui_viz_mode(); }

int synth_load_sample(const char *path, char *err, size_t err_cap) {
    int n = 0;
    if (!path || !path[0]) {
        if (err && err_cap) snprintf(err, err_cap, "synth_load_sample: path required");
        return -1;
    }
    synth_core_audio_lock();
    if (synth_wav_load(path, g.sample_buf, SYNTH_SAMPLE_MAX, &n) != 0) {
        synth_core_audio_unlock();
        if (err && err_cap) snprintf(err, err_cap, "synth_load_sample: failed to read %s", path);
        return -1;
    }
    g.sample_n = n;
    g.sample_pos = 0;
    g.sample_playing = 0;
    synth_sample_basename(path, g.sample_name, sizeof g.sample_name);
    g.dirty = 1;
    synth_core_audio_unlock();
    return 0;
}
int synth_sample_loaded(void) { return g.sample_n > 0; }
const char *synth_sample_name(void) { return g.sample_name[0] ? g.sample_name : ""; }

int synth_set_row_note(int row, int midi, char *err, size_t err_cap) {
    if (row < 0 || row >= SYNTH_ROWS) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_row_note: row must be 0..%d", SYNTH_ROWS - 1);
        return -1;
    }
    if (row < SYNTH_DRUM_ROWS || row == 6) {
        if (err && err_cap)
            snprintf(err, err_cap, "synth_set_row_note: row %d is not melodic (use 4,5,7)", row);
        return -1;
    }
    if (midi < 24 || midi > 96) {
        if (err && err_cap) snprintf(err, err_cap, "synth_set_row_note: midi must be 24..96");
        return -1;
    }
    g.row_midi[row] = midi;
    g.dirty = 1;
    return 0;
}
int synth_row_note_get(int row) {
    P(row < 0 || row >= SYNTH_ROWS, 60)
    return synth_row_midi_note(row);
}

int synth_looper_rec(int on, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    synth_core_audio_lock();
    if (!on) {
        g.loop_recording = 0;
        g.loop_arm = 0;
    } else if (g.playing) {
        g.loop_arm = 1;
        g.loop_flash = 12;
    } else {
        g.loop_recording = 1;
        g.loop_wpos = 0;
        synth_loop_sync_target();
        g.loop_flash = 12;
    }
    g.dirty = 1;
    synth_core_audio_unlock();
    return 0;
}
int synth_looper_play(int on, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (on && g.loop_len <= 0) return -1;
    g.loop_playing = on ? 1 : 0;
    g.dirty = 1;
    return 0;
}
int synth_looper_clear(char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    synth_core_audio_lock();
    memset(g.loop_buf, 0, sizeof g.loop_buf);
    g.loop_len = 0;
    g.loop_pos = 0;
    g.loop_wpos = 0;
    g.loop_playing = 0;
    g.loop_recording = 0;
    g.loop_arm = 0;
    g.dirty = 1;
    synth_core_audio_unlock();
    return 0;
}
int synth_looper_overdub(int on, char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    g.loop_overdub = on ? 1 : 0;
    return 0;
}
int synth_looper_rec_on(void) { return g.loop_recording || g.loop_arm; }
int synth_looper_play_on(void) { return g.loop_playing; }
int synth_looper_has_loop(void) { return g.loop_len > 0; }

void synth_core_set_alive(int alive) { g.alive = alive ? 1 : 0; }
int synth_core_is_alive(void) { return g.alive; }
int synth_core_want_maximize(void) { return g.want_maximize; }
void synth_core_clear_want_maximize(void) { g.want_maximize = 0; }
int synth_core_bump_maximize_tries(void) { return ++g.maximize_tries; }
int synth_core_get_maximize_tries(void) { return g.maximize_tries; }
void synth_core_audio_lock(void) { pthread_mutex_lock(&g.mu); }
void synth_core_audio_unlock(void) { pthread_mutex_unlock(&g.mu); }
int synth_core_audio_running(void) { return g.audio_run; }
void synth_core_set_audio_run(int on) { g.audio_run = on ? 1 : 0; }
uint32_t *synth_core_present_pixels(void) { return g.present; }
int synth_core_present_width(void) { return g.win_w; }
int synth_core_present_height(void) { return g.win_h; }
int synth_core_dirty(void) { return g.dirty; }
void synth_core_clear_dirty(void) { g.dirty = 0; }
void synth_core_mark_dirty(void) { g.dirty = 1; }
#else
int synth_open(char *err, size_t err_cap) {
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
void synth_close(void) {}
int synth_alive(void) { return 0; }
int synth_tick(char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    return 0;
}
int synth_set_steps(int n, char *err, size_t err_cap) {
    (void)n;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_steps(void) { return SYNTH_DEFAULT_STEPS; }
int synth_set_metro(int on, char *err, size_t err_cap) {
    (void)on;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_metro_on(void) { return 0; }
int synth_set_metro_sound(int sound, char *err, size_t err_cap) {
    (void)sound;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_metro_sound(void) { return 0; }
int synth_set_mute(int mute, char *err, size_t err_cap) {
    (void)mute;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_mute_on(void) { return 0; }
int synth_note_on(int note, float vel, char *err, size_t err_cap) {
    (void)note;
    (void)vel;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_note_off(int note, char *err, size_t err_cap) {
    (void)note;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_set_bpm(float bpm, char *err, size_t err_cap) {
    (void)bpm;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
float synth_get_bpm(void) { return 120.f; }
int synth_set_level(float level, char *err, size_t err_cap) {
    (void)level;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
float synth_get_level(void) { return 0.75f; }
int synth_set_cutoff(float cutoff, char *err, size_t err_cap) {
    (void)cutoff;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
float synth_get_cutoff(void) { return 0.55f; }
int synth_set_reso(float reso, char *err, size_t err_cap) {
    (void)reso;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
float synth_get_reso(void) { return 0.2f; }
int synth_set_seq_row(int row, uint64_t mask, char *err, size_t err_cap) {
    (void)row;
    (void)mask;
    if (err && err_cap)
#if defined(__linux__) && defined(SHAKTI_HAVE_SYNTH)
        snprintf(err, err_cap,
                 "synth: install libasound2-dev and libx11-dev, rebuild with SHAKTI_SYNTH=1");
#else
        snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
#endif
    return -1;
}
int synth_play(int on, char *err, size_t err_cap) {
    (void)on;
    (void)err;
    (void)err_cap;
    return -1;
}
int synth_playing(void) { return 0; }
int synth_mouse_press(int x, int y, char *err, size_t err_cap) { (void)x; (void)y; (void)err; (void)err_cap; return -1; }
int synth_mouse_release(int x, int y, char *err, size_t err_cap) { (void)x; (void)y; (void)err; (void)err_cap; return -1; }
int synth_set_viz(int mode, char *err, size_t err_cap) { (void)mode; (void)err; (void)err_cap; return -1; }
int synth_viz_mode(void) { return 0; }
int synth_load_sample(const char *path, char *err, size_t err_cap) {
    (void)path;
    if (err && err_cap) snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
    return -1;
}
int synth_sample_loaded(void) { return 0; }
const char *synth_sample_name(void) { return ""; }
int synth_set_row_note(int row, int midi, char *err, size_t err_cap) {
    (void)row;
    (void)midi;
    if (err && err_cap) snprintf(err, err_cap, "synth: Linux desktop only (build with SHAKTI_SYNTH=1)");
    return -1;
}
int synth_row_note_get(int row) { (void)row; return 60; }
int synth_looper_rec(int on, char *err, size_t err_cap) { (void)on; (void)err; (void)err_cap; return -1; }
int synth_looper_play(int on, char *err, size_t err_cap) { (void)on; (void)err; (void)err_cap; return -1; }
int synth_looper_clear(char *err, size_t err_cap) { (void)err; (void)err_cap; return -1; }
int synth_looper_overdub(int on, char *err, size_t err_cap) { (void)on; (void)err; (void)err_cap; return -1; }
int synth_looper_rec_on(void) { return 0; }
int synth_looper_play_on(void) { return 0; }
int synth_looper_has_loop(void) { return 0; }
const char *synth_row_label(int row) {
    static const char *lbls[] = {"KICK", "SNAR", "HAT", "PERC", "BASS", "SYNTH", "SAMP", "LEAD"};
    P(row < 0 || row >= 8, "")
    return lbls[row];
}
#endif
static inline int synth_arg_int(V**a,int n,int idx,int fallback){P(n<=idx,fallback)P(a[idx]->t==T_INT,(int)a[idx]->j)P(a[idx]->t==T_FLOAT,(int)a[idx]->f)return fallback;}
static V *synth_err(char *err) {
    P(err[0],v_err(err))return v_err("synth: failed");
}
V *bi_synth_open(V **a, int n) {
    char err[512];
    (void)a;
    (void)n;
    err[0] = 0;
    P(synth_open(err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_close(V **a, int n) {
    (void)a;
    (void)n;
    synth_close();
    return v_nil();
}
V *bi_synth_alive(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_alive());
}
V *bi_synth_tick(V **a, int n) {
    char err[512];
    (void)a;
    (void)n;
    err[0] = 0;
    synth_tick(err, sizeof err);
    return v_nil();
}
V *bi_synth_set_steps(V **a, int n) {
    char err[512];
    int steps;
    err[0] = 0;
    P(n < 1,v_err("synth_set_steps(n)"))
    steps = synth_arg_int(a, n, 0, SYNTH_DEFAULT_STEPS);
    P(synth_set_steps(steps, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_steps(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_steps());
}
V *bi_synth_set_metro(V **a, int n) {
    char err[512];
    int on;
    err[0] = 0;
    P(n < 1,v_err("synth_set_metro(on)"))
    on = synth_arg_int(a, n, 0, 0);
    P(synth_set_metro(on, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_metro_on(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_metro_on());
}
V *bi_synth_set_metro_sound(V **a, int n) {
    char err[512];
    int sound;
    err[0] = 0;
    P(n < 1,v_err("synth_set_metro_sound(sound)"))
    sound = synth_arg_int(a, n, 0, 0);
    P(synth_set_metro_sound(sound, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_metro_sound(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_metro_sound());
}
V *bi_synth_set_mute(V **a, int n) {
    char err[512];
    int mute;
    err[0] = 0;
    P(n < 1,v_err("synth_set_mute(mute)"))
    mute = synth_arg_int(a, n, 0, 0);
    P(synth_set_mute(mute, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_mute_on(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_mute_on());
}
V *bi_synth_note_on(V **a, int n) {
    char err[512];
    int note;
    float vel;
    err[0] = 0;
    P(n < 1,v_err("synth_note_on(note[, vel])"))
    note = synth_arg_int(a, n, 0, 60);
    vel = 0.85f;
    if (n >= 2) {
        if (a[1]->t == T_FLOAT) vel = (float)a[1]->f;
        else if (a[1]->t == T_INT) vel = (float)a[1]->j;
    }
    P(synth_note_on(note, vel, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_note_off(V **a, int n) {
    char err[512];
    int note;
    err[0] = 0;
    P(n < 1,v_err("synth_note_off(note)"))
    note = synth_arg_int(a, n, 0, 60);
    P(synth_note_off(note, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_set_bpm(V **a, int n) {
    char err[512];
    float bpm;
    err[0] = 0;
    P(n < 1,v_err("synth_set_bpm(bpm)"))
    if (a[0]->t == T_FLOAT) bpm = (float)a[0]->f;
    else if (a[0]->t == T_INT) bpm = (float)a[0]->j;
    else return v_err("synth_set_bpm(bpm)");
    P(synth_set_bpm(bpm, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_bpm(V **a, int n) {
    (void)a;
    (void)n;
    return v_float((double)synth_get_bpm());
}
V *bi_synth_set_level(V **a, int n) {
    char err[512];
    float level;
    err[0] = 0;
    P(n < 1,v_err("synth_set_level(level)"))
    if (a[0]->t == T_FLOAT) level = (float)a[0]->f;
    else if (a[0]->t == T_INT) level = (float)a[0]->j;
    else return v_err("synth_set_level(level)");
    P(synth_set_level(level, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_level(V **a, int n) {
    (void)a;
    (void)n;
    return v_float((double)synth_get_level());
}
V *bi_synth_set_cutoff(V **a, int n) {
    char err[512];
    float cutoff;
    err[0] = 0;
    P(n < 1,v_err("synth_set_cutoff(cutoff)"))
    if (a[0]->t == T_FLOAT) cutoff = (float)a[0]->f;
    else if (a[0]->t == T_INT) cutoff = (float)a[0]->j;
    else return v_err("synth_set_cutoff(cutoff)");
    P(synth_set_cutoff(cutoff, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_cutoff(V **a, int n) {
    (void)a;
    (void)n;
    return v_float((double)synth_get_cutoff());
}
V *bi_synth_set_reso(V **a, int n) {
    char err[512];
    float reso;
    err[0] = 0;
    P(n < 1,v_err("synth_set_reso(reso)"))
    if (a[0]->t == T_FLOAT) reso = (float)a[0]->f;
    else if (a[0]->t == T_INT) reso = (float)a[0]->j;
    else return v_err("synth_set_reso(reso)");
    P(synth_set_reso(reso, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_reso(V **a, int n) {
    (void)a;
    (void)n;
    return v_float((double)synth_get_reso());
}
V *bi_synth_set_seq_row(V **a, int n) {
    char err[512];
    int row;
    uint64_t mask;
    err[0] = 0;
    P(n < 2,v_err("synth_set_seq_row(row, mask)"))
    row = synth_arg_int(a, n, 0, 0);
    if (a[1]->t == T_INT) mask = (uint64_t)a[1]->j;
    else if (a[1]->t == T_FLOAT) mask = (uint64_t)a[1]->f;
    else return v_err("synth_set_seq_row(row, mask)");
    P(synth_set_seq_row(row, mask, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_play(V **a, int n) {
    char err[512];
    int on;
    err[0] = 0;
    P(n < 1,v_err("synth_play(on)"))
    on = synth_arg_int(a, n, 0, 0);
    P(synth_play(on, err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_playing(V **a, int n) {
    (void)a; (void)n;
    return v_int(synth_playing());
}
int synth_mouse_press(int x, int y, char *err, size_t err_cap) {
    (void)err; (void)err_cap;
    P(!g.open || !g.alive,-1)
    synth_core_handle_click(x, y, 1);
    return 0;
}
int synth_mouse_release(int x, int y, char *err, size_t err_cap) {
    (void)err; (void)err_cap;
    P(!g.open || !g.alive,-1)
    synth_core_handle_release(x, y, 0);
    return 0;
}
V *bi_synth_mouse_press(V **a, int n) {
    char err[256];
    P(n != 2,v_err("synth_mouse_press: expected x, y"))
    P(synth_mouse_press(synth_arg_int(a, n, 0, 0), synth_arg_int(a, n, 1, 0), err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_mouse_release(V **a, int n) {
    char err[256];
    P(n != 2,v_err("synth_mouse_release: expected x, y"))
    P(synth_mouse_release(synth_arg_int(a, n, 0, 0), synth_arg_int(a, n, 1, 0), err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_set_viz(V **a, int n) {
    char err[256];
    P(n < 1,v_err("synth_set_viz(mode)"))
    P(synth_set_viz(synth_arg_int(a, n, 0, 0), err, sizeof err) != 0,synth_err(err))
    return v_nil();
}
V *bi_synth_viz_mode(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_viz_mode());
}
V *bi_synth_load_sample(V **a, int n) {
    char err[512];
    P(n < 1 || a[0]->t != T_STR, v_err("synth_load_sample(path)"))
    P(synth_load_sample(a[0]->s, err, sizeof err) != 0, synth_err(err))
    return v_nil();
}
V *bi_synth_sample_loaded(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_sample_loaded());
}
V *bi_synth_sample_name(V **a, int n) {
    (void)a;
    (void)n;
    return v_str(synth_sample_name());
}
V *bi_synth_set_row_note(V **a, int n) {
    char err[512];
    P(n < 2, v_err("synth_set_row_note(row, midi)"))
    P(synth_set_row_note(synth_arg_int(a, n, 0, 0), synth_arg_int(a, n, 1, 60), err, sizeof err) != 0,
      synth_err(err))
    return v_nil();
}
V *bi_synth_row_note(V **a, int n) {
    P(n < 1, v_err("synth_row_note(row)"))
    return v_int(synth_row_note_get(synth_arg_int(a, n, 0, 0)));
}
V *bi_synth_looper_rec(V **a, int n) {
    char err[256];
    P(n < 1, v_err("synth_looper_rec(on)"))
    P(synth_looper_rec(synth_arg_int(a, n, 0, 0), err, sizeof err) != 0, synth_err(err))
    return v_nil();
}
V *bi_synth_looper_play(V **a, int n) {
    char err[256];
    P(n < 1, v_err("synth_looper_play(on)"))
    P(synth_looper_play(synth_arg_int(a, n, 0, 0), err, sizeof err) != 0, synth_err(err))
    return v_nil();
}
V *bi_synth_looper_clear(V **a, int n) {
    char err[256];
    (void)a;
    (void)n;
    P(synth_looper_clear(err, sizeof err) != 0, synth_err(err))
    return v_nil();
}
V *bi_synth_looper_overdub(V **a, int n) {
    char err[256];
    P(n < 1, v_err("synth_looper_overdub(on)"))
    P(synth_looper_overdub(synth_arg_int(a, n, 0, 0), err, sizeof err) != 0, synth_err(err))
    return v_nil();
}
V *bi_synth_looper_rec_on(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_looper_rec_on());
}
V *bi_synth_looper_play_on(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_looper_play_on());
}
V *bi_synth_looper_has_loop(V **a, int n) {
    (void)a;
    (void)n;
    return v_int(synth_looper_has_loop());
}
