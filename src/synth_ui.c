#include "synth_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef SHAKTI_HAVE_ISOLDE
extern int isolde_device;
extern void kd_synth_rasterize(const UiCmd *cmds, int n, uint32_t *fb, int w, int h);
#endif

static UiCmd g_cmds[SYNTH_UI_MAX_CMDS];
static int g_ncmds;
static SynthVizMode g_viz_mode = SYNTH_VIZ_SPECTRUM;
static float g_waveform[SYNTH_UI_WAVEFORM_LEN];
static int g_wave_pos;
static float g_spectrum[SYNTH_UI_SPECTRUM_BINS];
static float g_vu_level;

#define COL_LABEL 0x646870u
#define COL_TEXT 0xf4f5f8u
#define COL_AMBER 0xff9228u
#define COL_LED_ON 0xffa840u
#define COL_LED_OFF 0x141418u
#define COL_HOT 0xff5038u
#define COL_PLAY 0xffa030u
#define COL_PANEL 0x08080au
#define COL_PAD_FACE 0x1a1a1eu
#define COL_PAD_PRESS 0x28282eu

static uint32_t rgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static int rgb_r(uint32_t x) { return (int)((x >> 16) & 255u); }
static int rgb_g(uint32_t x) { return (int)((x >> 8) & 255u); }
static int rgb_b(uint32_t x) { return (int)(x & 255u); }
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

typedef struct UiCtx {
    uint32_t *fb;
    int w, h;
} UiCtx;

static void pix(UiCtx *c, int x, int y, uint32_t col) {
    if (x >= 0 && x < c->w && y >= 0 && y < c->h) c->fb[y * c->w + x] = col;
}
static void drect_fill(UiCtx *c, UiRect r, uint32_t col) {
    int x, y;
    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++) pix(c, x, y, col);
}
static void drect_grad_v(UiCtx *c, UiRect r, uint32_t top, uint32_t bot) {
    int x, y, h = r.h;
    if (h < 1) h = 1;
    for (y = r.y; y < r.y + r.h; y++) {
        float t = (float)(y - r.y) / (float)h;
        uint32_t base = rgb_lerp(top, bot, t);
        for (x = r.x; x < r.x + r.w; x++) {
            float n = hash2f(x, y) * 0.07f - 0.035f;
            pix(c, x, y, rgb_lerp(base, rgb(255, 255, 255), n > 0.f ? n : -n));
        }
    }
}
static void drect_glow_border(UiCtx *c, UiRect r, uint32_t col, int thick) {
    int t;
    for (t = 0; t < thick; t++) {
        float f = 1.f - (float)t / (float)(thick + 1);
        uint32_t edge = rgb_mul(col, 0.35f * f);
        drect_fill(c, (UiRect){r.x + t, r.y + t, r.w - 2 * t, 1}, edge);
        drect_fill(c, (UiRect){r.x + t, r.y + t, 1, r.h - 2 * t}, edge);
        drect_fill(c, (UiRect){r.x + t, r.y + r.h - 1 - t, r.w - 2 * t, 1}, edge);
        drect_fill(c, (UiRect){r.x + r.w - 1 - t, r.y + t, 1, r.h - 2 * t}, edge);
    }
}
static void draw_panel_recessed(UiCtx *c, UiRect r) {
    drect_fill(c, r, COL_PANEL);
    drect_fill(c, (UiRect){r.x, r.y, r.w, 1}, rgb(56, 58, 64));
    drect_fill(c, (UiRect){r.x, r.y, 1, r.h}, rgb(56, 58, 64));
    drect_fill(c, (UiRect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, rgb(6, 6, 8));
    drect_fill(c, (UiRect){r.x + 1, r.y + 1, r.w - 2, 1}, rgb(14, 14, 18));
    drect_fill(c, (UiRect){r.x + 1, r.y + 1, 1, r.h - 2}, rgb(14, 14, 18));
}
static void fill_chassis(UiCtx *c) {
    int x, y;
    for (y = 0; y < c->h; y++) {
        float vy = (float)y / (float)c->h;
        float vignette = 1.f - fabsf(vy - 0.5f) * 0.18f;
        for (x = 0; x < c->w; x++) {
            float vx = (float)x / (float)c->w;
            float edge = (vx < 0.03f || vx > 0.97f) ? 0.92f : 1.f;
            uint32_t base = rgb_lerp(rgb(10, 10, 12), rgb(18, 18, 22), vy);
            pix(c, x, y, rgb_mul(base, vignette * edge));
        }
    }
    drect_fill(c, (UiRect){0, 0, c->w, 1}, rgb(72, 74, 80));
    drect_fill(c, (UiRect){0, 1, c->w, 1}, rgb(24, 24, 28));
}
static void dcircle_fill(UiCtx *c, int cx, int cy, int rad, uint32_t col) {
    int x, y;
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++)
            if (x * x + y * y <= rad * rad) pix(c, cx + x, cy + y, col);
}
static void dcircle_radial(UiCtx *c, int cx, int cy, int rad, uint32_t inner, uint32_t outer) {
    int x, y;
    float inv = rad > 0 ? 1.f / (float)rad : 1.f;
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++) {
            float d = sqrtf((float)(x * x + y * y));
            if (d > (float)rad) continue;
            pix(c, cx + x, cy + y, rgb_lerp(inner, outer, d * inv));
        }
}
static int glyph_idx(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == '-') return 10;
    if (ch >= 'A' && ch <= 'Z') return 11 + (ch - 'A');
    if (ch == '+') return 37;
    if (ch == ' ') return 38;
    if (ch >= 'a' && ch <= 'z') return 11 + (ch - 'a');
    return -1;
}
static void glyph5x7(UiCtx *c, int x, int y, char ch, uint32_t color, int scale) {
    static const unsigned char font[39][7] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f}, {0x1f, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
        {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
        {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}, {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
        {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}, {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
        {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
        {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}, {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e},
        {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, {0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
        {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}, {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
        {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
        {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
        {0x00, 0x00, 0x04, 0x0e, 0x04, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    int idx = glyph_idx(ch), row, col, px, py;
    if (idx < 0) return;
    if (scale < 1) scale = 1;
    for (row = 0; row < 7; row++)
        for (col = 0; col < 5; col++)
            if (font[idx][row] & (1 << (4 - col)))
                for (py = 0; py < scale; py++)
                    for (px = 0; px < scale; px++)
                        pix(c, x + col * scale + px, y + row * scale + py, color);
}
static void text_label(UiCtx *c, int x, int y, const char *s, uint32_t col) {
    int i, adv = 6;
    for (i = 0; s[i]; i++) glyph5x7(c, x + i * adv, y, s[i], col, 1);
}
static void draw_btn(UiCtx *c, UiRect r, uint32_t hi, uint32_t lo, int lit, int pressed, const char *txt) {
    uint32_t face = lit ? hi : rgb(26, 27, 30);
    uint32_t face_bot = lit ? lo : rgb(16, 17, 20);
    int tx;
    if (pressed) {
        drect_fill(c, r, rgb_mul(face_bot, 0.88f));
        drect_fill(c, (UiRect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, rgb_mul(face, 0.82f));
    } else {
        drect_grad_v(c, r, face, face_bot);
        if (lit) drect_glow_border(c, r, hi, 1);
        else {
            drect_fill(c, (UiRect){r.x, r.y, r.w, 1}, rgb(48, 50, 56));
            drect_fill(c, (UiRect){r.x, r.y + r.h - 1, r.w, 1}, rgb(8, 8, 10));
        }
    }
    tx = r.x + (r.w - (int)strlen(txt) * 6) / 2;
    if (tx < r.x + 2) tx = r.x + 2;
    text_label(c, tx, r.y + (r.h - 7) / 2, txt, lit ? COL_TEXT : COL_LABEL);
}
static void draw_knob(UiCtx *c, UiRect r, float val, const char *label) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 5, rad = r.w / 4, sx, sy, kx, ky, lx, i;
    float ang;
    if (rad < 6) rad = 6;
    if (cy + rad + 2 > r.y + r.h - 12) rad = (r.y + r.h - 12 - cy);
    if (rad < 6) rad = 6;
    dcircle_radial(c, cx, cy, rad + 2, rgb(10, 10, 12), rgb(4, 4, 6));
    dcircle_radial(c, cx, cy, rad, rgb(28, 28, 32), rgb(14, 14, 18));
    dcircle_fill(c, cx, cy, rad - 2, rgb(18, 18, 22));
    sx = cx - rad / 4;
    sy = cy - rad / 4;
    dcircle_fill(c, sx, sy, rad / 5, rgb_mul(rgb(255, 255, 255), 0.06f));
    ang = val * 4.18879f + 2.61799f;
    kx = cx + (int)(cosf(ang) * (rad - 4));
    ky = cy + (int)(sinf(ang) * (rad - 4));
    for (i = 2; i >= 0; i--)
        dcircle_fill(c, kx, ky, i + 1, i ? rgb_mul(COL_AMBER, 0.35f) : COL_AMBER);
    lx = r.x + (r.w - (int)strlen(label) * 6) / 2;
    if (lx < r.x) lx = r.x;
    text_label(c, lx, r.y + r.h - 10, label, COL_LABEL);
}
static void draw_led_step(UiCtx *c, UiRect r, int on, int playhead) {
    UiRect pad = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    if (pad.w < 3) pad = r;
    if (pad.h < 3) pad.h = 3;
    drect_fill(c, pad, on ? rgb(40, 32, 20) : COL_LED_OFF);
    if (on) {
        drect_grad_v(c, pad, rgb(255, 168, 56), rgb(200, 110, 28));
        drect_glow_border(c, pad, COL_LED_ON, 1);
    } else {
        drect_fill(c, (UiRect){pad.x, pad.y, pad.w, 1}, rgb(28, 28, 32));
        drect_fill(c, (UiRect){pad.x, pad.y + pad.h - 1, pad.w, 1}, rgb(8, 8, 10));
    }
    if (playhead) drect_glow_border(c, (UiRect){pad.x - 1, pad.y - 1, pad.w + 2, pad.h + 2}, COL_TEXT, 1);
}
static void draw_pad(UiCtx *c, UiRect r, int pressed, const char *lbl) {
    UiRect face = r;
    uint32_t top, bot;
    if (pressed) face.y += 1;
    top = pressed ? COL_PAD_PRESS : COL_PAD_FACE;
    bot = pressed ? rgb(16, 16, 20) : rgb(12, 12, 16);
    drect_fill(c, face, bot);
    drect_grad_v(c, face, top, bot);
    if (pressed) {
        drect_glow_border(c, face, COL_AMBER, 2);
        drect_fill(c, (UiRect){face.x + 2, face.y + 2, face.w - 4, face.h - 4},
                   rgb_mul(COL_AMBER, 0.12f));
    } else {
        drect_fill(c, (UiRect){face.x, face.y, face.w, 1}, rgb(36, 38, 44));
        drect_fill(c, (UiRect){face.x, face.y + face.h - 1, face.w, 1}, rgb(6, 6, 8));
    }
    if (lbl && lbl[0]) {
        int w = (int)strlen(lbl) * 6;
        text_label(c, face.x + (face.w - w) / 2, face.y + (face.h - 7) / 2, lbl,
                   pressed ? COL_TEXT : rgb(90, 94, 102));
    }
}
static void draw_piano_key(UiCtx *c, UiRect r, int down, int style) {
    UiRect face = r;
    uint32_t top, bot, edge_r, edge_b;
    if (down) face.y += 1;
    if (style == 1) {
        top = down ? rgb(14, 14, 16) : rgb(24, 24, 28);
        bot = down ? rgb(8, 8, 10) : rgb(14, 14, 18);
        edge_r = rgb(18, 18, 22);
        edge_b = rgb(6, 6, 8);
    } else if (style == 2) {
        top = down ? rgb(180, 180, 176) : rgb(236, 236, 232);
        bot = down ? rgb(180, 180, 176) : rgb(220, 220, 214);
        edge_r = rgb(160, 160, 156);
        edge_b = rgb(140, 140, 136);
    } else {
        top = down ? rgb(200, 200, 200) : rgb(244, 244, 242);
        bot = down ? rgb(190, 190, 188) : rgb(228, 228, 224);
        edge_r = rgb(170, 170, 168);
        edge_b = rgb(150, 150, 148);
    }
    drect_grad_v(c, face, top, bot);
    drect_fill(c, (UiRect){face.x, face.y, face.w, 1},
               rgb_lerp(rgb(255, 255, 255), rgb(0, 0, 0), down ? 0.04f : (style == 1 ? 0.12f : 0.18f)));
    drect_fill(c, (UiRect){face.x + face.w - 1, face.y, 1, face.h}, edge_r);
    drect_fill(c, (UiRect){face.x, face.y + face.h - 1, face.w, 1}, edge_b);
    if (style == 2 && !down) text_label(c, face.x + (face.w - 6) / 2, face.y + face.h - 14, "C", rgb(100, 100, 96));
}
static void draw_ribbon_track(UiCtx *c, UiRect track, float val) {
    UiRect cap;
    int cap_x;
    drect_fill(c, track, rgb(8, 8, 10));
    drect_fill(c, (UiRect){track.x, track.y, track.w, 1}, rgb(40, 42, 48));
    drect_fill(c, (UiRect){track.x, track.y + track.h - 1, track.w, 1}, rgb(4, 4, 6));
    cap_x = track.x + (int)((val * 0.5f + 0.5f) * (float)(track.w - 12));
    cap = (UiRect){cap_x, track.y - 4, 12, track.h + 8};
    drect_grad_v(c, cap, rgb(255, 180, 80), rgb(210, 110, 32));
    drect_glow_border(c, cap, COL_AMBER, 1);
}
static void draw_spectrum(UiCtx *c, UiRect r, const float *mags, int n) {
    int i, bar_w, x0, h, bh;
    UiRect inner = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    if (n < 1) return;
    draw_panel_recessed(c, r);
    text_label(c, r.x + 8, r.y + 4, "SPECTRUM", COL_LABEL);
    drect_fill(c, inner, rgb(4, 4, 6));
    bar_w = (inner.w - 12) / n;
    if (bar_w < 2) bar_w = 2;
    x0 = inner.x + 6;
    h = inner.h - 8;
    for (i = 0; i < n; i++) {
        float m = mags[i];
        if (m < 0.f) m = 0.f;
        if (m > 1.f) m = 1.f;
        bh = (int)(m * (float)h);
        if (bh < 1 && m > 0.01f) bh = 1;
        drect_grad_v(c, (UiRect){x0 + i * bar_w, inner.y + inner.h - 4 - bh, bar_w - 1, bh},
                     rgb(255, 200, 120), COL_AMBER);
    }
}
static void draw_waveform(UiCtx *c, UiRect r, const float *samples, int n) {
    int i, mid, py, last_y = -1;
    UiRect inner = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    if (n < 2) return;
    draw_panel_recessed(c, r);
    text_label(c, r.x + 8, r.y + 4, "WAVEFORM", COL_LABEL);
    drect_fill(c, inner, rgb(4, 4, 6));
    mid = inner.y + inner.h / 2;
    for (i = 0; i < n; i++) {
        float s = samples[i];
        if (s < -1.f) s = -1.f;
        if (s > 1.f) s = 1.f;
        py = mid - (int)(s * (float)(inner.h / 2 - 6));
        if (last_y >= 0) {
            int x = inner.x + 6 + (i * (inner.w - 12)) / n;
            int y0 = last_y, y1 = py;
            int ymin = y0 < y1 ? y0 : y1, ymax = y0 > y1 ? y0 : y1;
            int y;
            for (y = ymin; y <= ymax; y++) pix(c, x, y, COL_AMBER);
        }
        last_y = py;
    }
}
static void draw_vu(UiCtx *c, UiRect r, float level) {
    UiRect fill, inner = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    int fw;
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;
    drect_fill(c, r, rgb(10, 10, 12));
    drect_fill(c, (UiRect){r.x, r.y, r.w, 1}, rgb(44, 46, 52));
    drect_fill(c, (UiRect){r.x, r.y + r.h - 1, r.w, 1}, rgb(4, 4, 6));
    fw = (int)(level * (float)(inner.w - 2));
    if (fw > 0) {
        fill = (UiRect){inner.x + 1, inner.y + 1, fw, inner.h - 2};
        drect_grad_v(c, fill, rgb(255, 200, 120), COL_AMBER);
    }
}

static void ui_emit(UiCmdKind kind) {
    if (g_ncmds >= SYNTH_UI_MAX_CMDS) return;
    memset(&g_cmds[g_ncmds], 0, sizeof(g_cmds[g_ncmds]));
    g_cmds[g_ncmds].kind = kind;
    g_ncmds++;
}
static void ui_set_rect(UiRect r) { g_cmds[g_ncmds - 1].r = r; }

void synth_ui_begin(void) { g_ncmds = 0; }
const UiCmd *synth_ui_cmds(int *n) {
    if (n) *n = g_ncmds;
    return g_cmds;
}
void synth_ui_emit_chassis(void) { ui_emit(UI_CHASSIS); }
void synth_ui_emit_header_deck(UiRect r) { ui_emit(UI_HEADER_DECK); ui_set_rect(r); }
void synth_ui_emit_panel(UiRect r) { ui_emit(UI_PANEL_RECESSED); ui_set_rect(r); }
void synth_ui_emit_btn(UiRect r, uint32_t hi, uint32_t lo, int lit, int pressed, const char *txt) {
    ui_emit(UI_BTN);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].c0 = hi;
    g_cmds[g_ncmds - 1].c1 = lo;
    g_cmds[g_ncmds - 1].idx0 = lit;
    g_cmds[g_ncmds - 1].idx1 = pressed;
    if (txt) snprintf(g_cmds[g_ncmds - 1].text, SYNTH_UI_TEXT_MAX, "%s", txt);
}
void synth_ui_emit_knob(UiRect r, float val, const char *label) {
    ui_emit(UI_KNOB);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].f0 = val;
    if (label) snprintf(g_cmds[g_ncmds - 1].text, SYNTH_UI_TEXT_MAX, "%s", label);
}
void synth_ui_emit_led_step(UiRect r, int on, int playhead) {
    ui_emit(UI_LED_STEP);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].idx0 = on;
    g_cmds[g_ncmds - 1].idx1 = playhead;
}
void synth_ui_emit_pad(UiRect r, int pressed, const char *lbl) {
    ui_emit(UI_PAD);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].idx0 = pressed;
    if (lbl) snprintf(g_cmds[g_ncmds - 1].text, SYNTH_UI_TEXT_MAX, "%s", lbl);
}
void synth_ui_emit_piano_key(UiRect r, int down, int style) {
    ui_emit(UI_PIANO_KEY);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].idx0 = down;
    g_cmds[g_ncmds - 1].idx1 = style;
}
void synth_ui_emit_ribbon(UiRect track, float val) {
    ui_emit(UI_RIBBON);
    g_cmds[g_ncmds - 1].r = track;
    g_cmds[g_ncmds - 1].f0 = val;
}
void synth_ui_emit_label(int x, int y, const char *s, uint32_t c) {
    ui_emit(UI_LABEL);
    g_cmds[g_ncmds - 1].r = (UiRect){x, y, 0, 0};
    g_cmds[g_ncmds - 1].c0 = c;
    if (s) snprintf(g_cmds[g_ncmds - 1].text, SYNTH_UI_TEXT_MAX, "%s", s);
}
void synth_ui_emit_num(int x, int y, int n, uint32_t c) {
    ui_emit(UI_NUM);
    g_cmds[g_ncmds - 1].r = (UiRect){x, y, 0, 0};
    g_cmds[g_ncmds - 1].c0 = c;
    g_cmds[g_ncmds - 1].idx0 = n;
}
void synth_ui_emit_spectrum(UiRect r, const float *mags, int n) {
    int lim = n < SYNTH_UI_SPECTRUM_BINS ? n : SYNTH_UI_SPECTRUM_BINS;
    ui_emit(UI_SPECTRUM);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].idx0 = lim;
    memcpy(g_spectrum, mags, (size_t)lim * sizeof(float));
}
void synth_ui_emit_waveform(UiRect r, const float *samples, int n) {
    int lim = n < SYNTH_UI_WAVEFORM_LEN ? n : SYNTH_UI_WAVEFORM_LEN;
    ui_emit(UI_WAVEFORM);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].idx0 = lim;
    memcpy(g_waveform, samples, (size_t)lim * sizeof(float));
}
void synth_ui_emit_vu(UiRect r, float level) {
    ui_emit(UI_VU_METER);
    g_cmds[g_ncmds - 1].r = r;
    g_cmds[g_ncmds - 1].f0 = level;
}

static void replay_cmd(UiCtx *c, const UiCmd *cmd) {
    char numbuf[16];
    switch (cmd->kind) {
    case UI_CHASSIS: fill_chassis(c); break;
    case UI_HEADER_DECK:
        draw_panel_recessed(c, cmd->r);
        drect_fill(c, (UiRect){cmd->r.x + 2, cmd->r.y + 2, cmd->r.w - 4, cmd->r.h - 4}, rgb(10, 10, 12));
        drect_fill(c, (UiRect){cmd->r.x + 2, cmd->r.y + 2, cmd->r.w - 4, 1}, rgb(32, 34, 40));
        break;
    case UI_PANEL_RECESSED: draw_panel_recessed(c, cmd->r); break;
    case UI_BTN: draw_btn(c, cmd->r, cmd->c0, cmd->c1, cmd->idx0, cmd->idx1, cmd->text); break;
    case UI_KNOB: draw_knob(c, cmd->r, cmd->f0, cmd->text); break;
    case UI_LED_STEP: draw_led_step(c, cmd->r, cmd->idx0, cmd->idx1); break;
    case UI_PAD: draw_pad(c, cmd->r, cmd->idx0, cmd->text); break;
    case UI_PIANO_KEY: draw_piano_key(c, cmd->r, cmd->idx0, cmd->idx1); break;
    case UI_RIBBON: draw_ribbon_track(c, cmd->r, cmd->f0); break;
    case UI_LABEL: text_label(c, cmd->r.x, cmd->r.y, cmd->text, cmd->c0); break;
    case UI_NUM:
        snprintf(numbuf, sizeof numbuf, "%d", cmd->idx0);
        text_label(c, cmd->r.x, cmd->r.y, numbuf, cmd->c0);
        break;
    case UI_SPECTRUM: draw_spectrum(c, cmd->r, g_spectrum, cmd->idx0); break;
    case UI_WAVEFORM: draw_waveform(c, cmd->r, g_waveform, cmd->idx0); break;
    case UI_VU_METER: draw_vu(c, cmd->r, cmd->f0); break;
    default: break;
    }
}

void synth_ui_flush_cpu(const UiCmd *cmds, int n, uint32_t *fb, int w, int h) {
    UiCtx c = {fb, w, h};
    int i;
    for (i = 0; i < n; i++) replay_cmd(&c, &cmds[i]);
}

void synth_ui_flush(const UiCmd *cmds, int n, uint32_t *fb, int w, int h) {
#ifdef SHAKTI_HAVE_ISOLDE
    extern int isolde_device;
    if (isolde_device) {
        kd_synth_rasterize(cmds, n, fb, w, h);
        return;
    }
#endif
    synth_ui_flush_cpu(cmds, n, fb, w, h);
}

void synth_ui_flush_text_overlay(const UiCmd *cmds, int n, uint32_t *fb, int w, int h) {
    UiCtx c = {fb, w, h};
    int i;
    for (i = 0; i < n; i++) {
        if (cmds[i].kind == UI_LABEL || cmds[i].kind == UI_NUM || cmds[i].kind == UI_SPECTRUM ||
            cmds[i].kind == UI_WAVEFORM)
            replay_cmd(&c, &cmds[i]);
    }
}

void synth_ui_push_audio_samples(const float *mono, int n) {
    int i;
    float sum = 0.f;
    if (!mono || n <= 0) return;
    for (i = 0; i < n; i++) {
        g_waveform[g_wave_pos] = mono[i];
        g_wave_pos = (g_wave_pos + 1) % SYNTH_UI_WAVEFORM_LEN;
        sum += mono[i] * mono[i];
        {
            int bin = (i * SYNTH_UI_SPECTRUM_BINS) / n;
            if (bin >= SYNTH_UI_SPECTRUM_BINS) bin = SYNTH_UI_SPECTRUM_BINS - 1;
            float a = mono[i] >= 0.f ? mono[i] : -mono[i];
            if (a > g_spectrum[bin]) g_spectrum[bin] = a;
        }
    }
    g_vu_level = sqrtf(sum / (float)n) * 4.f;
    if (g_vu_level > 1.f) g_vu_level = 1.f;
    for (i = 0; i < SYNTH_UI_SPECTRUM_BINS; i++) g_spectrum[i] *= 0.92f;
}

void synth_ui_set_viz_mode(int mode) {
    if (mode < SYNTH_VIZ_NONE) mode = SYNTH_VIZ_NONE;
    if (mode > SYNTH_VIZ_BOTH) mode = SYNTH_VIZ_BOTH;
    g_viz_mode = (SynthVizMode)mode;
}
int synth_ui_viz_mode(void) { return (int)g_viz_mode; }

void synth_ui_get_spectrum(float *out, int *n) {
    int i;
    if (n) *n = SYNTH_UI_SPECTRUM_BINS;
    if (!out) return;
    for (i = 0; i < SYNTH_UI_SPECTRUM_BINS; i++) out[i] = g_spectrum[i];
}

void synth_ui_get_waveform(float *out, int *n) {
    int i, lim = SYNTH_UI_WAVEFORM_LEN;
    if (n) *n = lim;
    if (!out) return;
    for (i = 0; i < lim; i++) {
        int idx = (g_wave_pos + i) % SYNTH_UI_WAVEFORM_LEN;
        out[i] = g_waveform[idx];
    }
}

float synth_ui_vu_level(void) { return g_vu_level; }
