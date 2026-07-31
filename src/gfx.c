#include "gfx.h"
#include "gfx_platform.h"
#include "input.h"
#include "shakti.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SHAKTI_HAVE_GFX) && ( \
    (defined(__linux__) && __has_include(<X11/Xlib.h>)) || \
    (defined(__APPLE__) && !defined(__IOS__)) \
)

#define GFX_DESIGN_W 960
#define GFX_DESIGN_H 540
#define GFX_MAX_WINDOW_DIM 16384
#define GFX_LETTERBOX 0x0a0a12u

typedef struct GfxState {
    uint32_t *fb;
    uint32_t *present;
    int design_w;
    int design_h;
    int win_w;
    int win_h;
    float ui_scale;
    int off_x;
    int off_y;
    int alive;
    int dirty;
    int click_pending;
    int click_x;
    int click_y;
    int mouse_down;
    int mouse_x;
    int mouse_y;
} GfxState;

static GfxState g;

static void gfx_put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= g.design_w || y >= g.design_h) return;
    g.fb[y * g.design_w + x] = c;
}

static void gfx_letterbox(void) {
    int ww = g.win_w > 0 ? g.win_w : GFX_DESIGN_W;
    int wh = g.win_h > 0 ? g.win_h : GFX_DESIGN_H;
    float sx = (float)ww / (float)GFX_DESIGN_W;
    float sy = (float)wh / (float)GFX_DESIGN_H;
    int x, y, dx, dy;
    g.ui_scale = sx < sy ? sx : sy;
    g.off_x = (ww - (int)(GFX_DESIGN_W * g.ui_scale)) / 2;
    g.off_y = (wh - (int)(GFX_DESIGN_H * g.ui_scale)) / 2;
    if (!g.present || g.win_w <= 0 || g.win_h <= 0) return;
    /* Fast path: 1:1 copy when window matches design buffer. */
    if (g.ui_scale == 1.0f && g.off_x == 0 && g.off_y == 0 &&
        ww == GFX_DESIGN_W && wh == GFX_DESIGN_H) {
        memcpy(g.present, g.fb, (size_t)GFX_DESIGN_W * (size_t)GFX_DESIGN_H * sizeof(uint32_t));
        return;
    }
    for (y = 0; y < g.win_h; y++) {
        uint32_t *dst = g.present + (size_t)y * (size_t)g.win_w;
        dy = (int)((y - g.off_y) / g.ui_scale);
        if (dy < 0 || dy >= GFX_DESIGN_H) {
            for (x = 0; x < g.win_w; x++) dst[x] = GFX_LETTERBOX;
            continue;
        }
        {
            const uint32_t *src = g.fb + (size_t)dy * (size_t)GFX_DESIGN_W;
            for (x = 0; x < g.win_w; x++) {
                dx = (int)((x - g.off_x) / g.ui_scale);
                if (dx < 0 || dx >= GFX_DESIGN_W) dst[x] = GFX_LETTERBOX;
                else dst[x] = src[dx];
            }
        }
    }
}

void gfx_core_mark_dirty(void) { g.dirty = 1; }
int gfx_core_is_alive(void) { return g.alive; }
void gfx_core_set_alive(int a) { g.alive = a ? 1 : 0; }
uint32_t *gfx_core_present_pixels(void) { return g.present; }
int gfx_core_present_width(void) { return g.win_w; }
int gfx_core_present_height(void) { return g.win_h; }

int gfx_core_fb_resize(int w, int h) {
    uint32_t *np;
    if (w <= 0 || h <= 0 || w > GFX_MAX_WINDOW_DIM || h > GFX_MAX_WINDOW_DIM) return -1;
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > SIZE_MAX / sizeof(uint32_t)) return -1;
    np = (uint32_t *)realloc(g.present, pixels * sizeof(uint32_t));
    if (!np) return -1;
    g.present = np;
    g.win_w = w;
    g.win_h = h;
    gfx_letterbox();
    g.dirty = 1;
    return 0;
}

void gfx_core_mouse_design(int wx, int wy, int down) {
    int dx = (int)((wx - g.off_x) / g.ui_scale);
    int dy = (int)((wy - g.off_y) / g.ui_scale);
    g.mouse_x = dx;
    g.mouse_y = dy;
    if (down && !g.mouse_down) {
        g.click_pending = 1;
        g.click_x = dx;
        g.click_y = dy;
    }
    g.mouse_down = down ? 1 : 0;
}

void gfx_core_mouse_move(int wx, int wy, int down) {
    int dx = (int)((wx - g.off_x) / g.ui_scale);
    int dy = (int)((wy - g.off_y) / g.ui_scale);
    g.mouse_x = dx;
    g.mouse_y = dy;
    /* Motion may clear a held button (release outside the window) but must
     * not invent a press — only ButtonPress / mouse_design may set down. */
    if (!down)
        g.mouse_down = 0;
}

int gfx_available(void) { return 1; }

int gfx_open(const char *title, char *err, size_t err_cap) {
    (void)title;
    /* Tear down any prior session, including a failed open that left
     * present/platform state without fb (issue #2 reopen path). */
    if (g.fb || g.present || g.alive) gfx_close();
    g.design_w = GFX_DESIGN_W;
    g.design_h = GFX_DESIGN_H;
    g.fb = (uint32_t *)calloc((size_t)GFX_DESIGN_W * (size_t)GFX_DESIGN_H, sizeof(uint32_t));
    if (!g.fb) {
        if (err && err_cap) snprintf(err, err_cap, "gfx_open: out of memory");
        return -1;
    }
    g.alive = 1;
    g.dirty = 1;
    if (gfx_platform_init(title, err, err_cap) != 0) {
        gfx_close();
        if (err && err_cap && !err[0])
            snprintf(err, err_cap, "gfx_open: platform init failed");
        return -1;
    }
    return 0;
}

void gfx_close(void) {
    gfx_platform_shutdown();
    free(g.fb);
    free(g.present);
    memset(&g, 0, sizeof g);
}

int gfx_alive(void) { return g.alive && g.fb != NULL; }

int gfx_tick(char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (!gfx_alive()) return 0;
    if (gfx_platform_poll() != 0) g.alive = 0;
    if (input_own_gui()) gfx_platform_sync_keys();
    if (g.dirty) {
        gfx_letterbox();
        gfx_platform_present();
        g.dirty = 0;
    }
    return 0;
}

void gfx_clear(uint32_t color) {
    size_t n;
    if (!g.fb) return;
    n = (size_t)g.design_w * (size_t)g.design_h;
    if (color == 0) {
        memset(g.fb, 0, n * sizeof(uint32_t));
    } else {
        size_t row = (size_t)g.design_w;
        size_t y;
        uint32_t *p = g.fb;
        for (y = 0; y < row; y++) p[y] = color;
        for (y = 1; y < (size_t)g.design_h; y++)
            memcpy(p + y * row, p, row * sizeof(uint32_t));
    }
    g.dirty = 1;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int j, x0, x1;
    if (!g.fb || w <= 0 || h <= 0) return;
    x0 = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    x1 = x + w;
    if (x1 > g.design_w) x1 = g.design_w;
    w = x1 - x0;
    if (w <= 0 || y >= g.design_h) return;
    if (y + h > g.design_h) h = g.design_h - y;
    for (j = 0; j < h; j++) {
        uint32_t *row = g.fb + (size_t)(y + j) * (size_t)g.design_w + (size_t)x0;
        int i;
        for (i = 0; i < w; i++) row[i] = color;
    }
    g.dirty = 1;
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!g.fb) return;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        gfx_put(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    g.dirty = 1;
}

void gfx_fill_circle(int cx, int cy, int r, uint32_t color) {
    int y;
    if (!g.fb || r <= 0) return;
    for (y = -r; y <= r; y++) {
        int dy2 = r * r - y * y;
        int dx = dy2 > 0 ? (int)sqrt((double)dy2) : 0;
        gfx_fill_rect(cx - dx, cy + y, dx * 2 + 1, 1, color);
    }
    g.dirty = 1;
}

int gfx_click_pending(void) { return g.click_pending; }
int gfx_click_x(void) { return g.click_x; }
int gfx_click_y(void) { return g.click_y; }
void gfx_consume_click(void) { g.click_pending = 0; }
int gfx_mouse_x(void) { return g.mouse_x; }
int gfx_mouse_y(void) { return g.mouse_y; }
int gfx_mouse_down(void) { return g.mouse_down; }

/* 5x7 monospace font: digits, A-Z, a-z, common punctuation. */
static int gfx_glyph_idx(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == '-') return 10;
    if (ch >= 'A' && ch <= 'Z') return 11 + (ch - 'A');
    if (ch == '+') return 37;
    if (ch == ' ') return 38;
    if (ch == '.') return 39;
    if (ch == ':') return 40;
    if (ch == '_') return 41;
    if (ch == '(') return 42;
    if (ch == ')') return 43;
    if (ch == '[') return 44;
    if (ch == ']') return 45;
    if (ch == '{') return 46;
    if (ch == '}') return 47;
    if (ch == ',') return 48;
    if (ch == ';') return 49;
    if (ch == '=') return 50;
    if (ch == '!') return 51;
    if (ch == '?') return 52;
    if (ch == '/') return 53;
    if (ch == '\\') return 54;
    if (ch == '"') return 55;
    if (ch == '\'') return 56;
    if (ch == '*') return 57;
    if (ch == '#') return 58;
    if (ch == '<') return 59;
    if (ch == '>') return 60;
    if (ch == '&') return 61;
    if (ch == '|') return 62;
    if (ch == '%') return 63;
    if (ch == '@') return 64;
    if (ch == '$') return 65;
    if (ch == '^') return 66;
    if (ch == '~') return 67;
    if (ch == '`') return 68;
    if (ch >= 'a' && ch <= 'z') return 69 + (ch - 'a');
    return -1;
}

static const unsigned char GFX_FONT[][7] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, /* 0 */
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f},
    {0x1f, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, /* 9 */
    {0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x00}, /* - */
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* A */
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
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}, /* Z */
    {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}, /* . */
    {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}, /* : */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}, /* _ */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, /* ( */
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, /* ) */
    {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e}, /* [ */
    {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e}, /* ] */
    {0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06}, /* { */
    {0x0c, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0c}, /* } */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04}, /* , */
    {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08}, /* ; */
    {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00}, /* = */
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04}, /* ! */
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, /* / */
    {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00}, /* \ */
    {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x00, 0x0a, 0x04, 0x1f, 0x04, 0x0a, 0x00}, /* * */
    {0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a}, /* # */
    {0x03, 0x04, 0x08, 0x10, 0x08, 0x04, 0x03}, /* < */
    {0x18, 0x04, 0x02, 0x01, 0x02, 0x04, 0x18}, /* > */
    {0x0a, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x0a}, /* & */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* | */
    {0x00, 0x13, 0x14, 0x08, 0x05, 0x19, 0x00}, /* % */
    {0x0e, 0x11, 0x15, 0x17, 0x16, 0x10, 0x0f}, /* @ */
    {0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04}, /* $ */
    {0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00}, /* ^ */
    {0x00, 0x00, 0x00, 0x0d, 0x12, 0x00, 0x00}, /* ~ */
    {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ` */
    /* a-z (distinct lowercase forms) */
    {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}, /* a */
    {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x1e}, /* b */
    {0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e}, /* c */
    {0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f}, /* d */
    {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}, /* e */
    {0x06, 0x08, 0x08, 0x1c, 0x08, 0x08, 0x08}, /* f */
    {0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x0e}, /* g */
    {0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11}, /* h */
    {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}, /* i */
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c}, /* j */
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, /* k */
    {0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* l */
    {0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15}, /* m */
    {0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11}, /* n */
    {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}, /* o */
    {0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10}, /* p */
    {0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x01}, /* q */
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, /* r */
    {0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e}, /* s */
    {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}, /* t */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}, /* u */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04}, /* v */
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a}, /* w */
    {0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11}, /* x */
    {0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e}, /* y */
    {0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f}, /* z */
};

static void gfx_glyph5x7(int x, int y, char ch, uint32_t color, int scale) {
    int idx = gfx_glyph_idx(ch), row, col, px, py;
    if (scale < 1) scale = 1;
    if (idx < 0) {
        /* Unknown glyph: hollow box */
        gfx_fill_rect(x, y, 5 * scale, scale, color);
        gfx_fill_rect(x, y + 6 * scale, 5 * scale, scale, color);
        gfx_fill_rect(x, y, scale, 7 * scale, color);
        gfx_fill_rect(x + 4 * scale, y, scale, 7 * scale, color);
        return;
    }
    for (row = 0; row < 7; row++)
        for (col = 0; col < 5; col++)
            if (GFX_FONT[idx][row] & (1 << (4 - col)))
                for (py = 0; py < scale; py++)
                    for (px = 0; px < scale; px++)
                        gfx_put(x + col * scale + px, y + row * scale + py, color);
}

void gfx_text(int x, int y, const char *s, uint32_t color, int scale) {
    int i, adv;
    if (!g.fb || !s) return;
    if (scale < 1) scale = 1;
    adv = 6 * scale;
    for (i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            x = 0;
            y += 8 * scale;
            continue;
        }
        gfx_glyph5x7(x, y, s[i], color, scale);
        x += adv;
    }
    g.dirty = 1;
}

int gfx_text_width(const char *s, int scale) {
    int n = 0, i;
    if (!s) return 0;
    if (scale < 1) scale = 1;
    for (i = 0; s[i] && s[i] != '\n'; i++) n++;
    return n * 6 * scale;
}

void gfx_copy_rect(int sx, int sy, int w, int h, int dx, int dy) {
    int x, y, x0, y0, x1, y1;
    uint32_t *tmp;
    size_t n;
    if (!g.fb || w <= 0 || h <= 0) return;
    if (sx == dx && sy == dy) return;
    /* Clip source to design buffer */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > g.design_w) w = g.design_w - sx;
    if (sy + h > g.design_h) h = g.design_h - sy;
    if (w <= 0 || h <= 0) return;
    /* Clip dest */
    x0 = dx < 0 ? 0 : dx;
    y0 = dy < 0 ? 0 : dy;
    x1 = dx + w;
    y1 = dy + h;
    if (x1 > g.design_w) x1 = g.design_w;
    if (y1 > g.design_h) y1 = g.design_h;
    if (x0 >= x1 || y0 >= y1) return;
    n = (size_t)w * (size_t)h;
    tmp = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!tmp) return;
    for (y = 0; y < h; y++)
        memcpy(tmp + (size_t)y * (size_t)w,
               g.fb + (size_t)(sy + y) * (size_t)g.design_w + (size_t)sx,
               (size_t)w * sizeof(uint32_t));
    for (y = y0; y < y1; y++) {
        int src_y = y - dy;
        for (x = x0; x < x1; x++) {
            int src_x = x - dx;
            if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h)
                g.fb[(size_t)y * (size_t)g.design_w + (size_t)x] =
                    tmp[(size_t)src_y * (size_t)w + (size_t)src_x];
        }
    }
    free(tmp);
    g.dirty = 1;
}

#else /* stubs */

int gfx_available(void) { return 0; }
int gfx_open(const char *title, char *err, size_t err_cap) {
    (void)title;
    if (err && err_cap) snprintf(err, err_cap, "gfx: not available on this build");
    return -1;
}
void gfx_close(void) {}
int gfx_alive(void) { return 0; }
int gfx_tick(char *err, size_t err_cap) { (void)err; (void)err_cap; return 0; }
void gfx_clear(uint32_t c) { (void)c; }
void gfx_fill_rect(int x, int y, int w, int h, uint32_t c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void gfx_line(int x0, int y0, int x1, int y1, uint32_t c) { (void)x0;(void)y0;(void)x1;(void)y1;(void)c; }
void gfx_fill_circle(int cx, int cy, int r, uint32_t c) { (void)cx;(void)cy;(void)r;(void)c; }
void gfx_text(int x, int y, const char *s, uint32_t c, int scale) { (void)x;(void)y;(void)s;(void)c;(void)scale; }
int gfx_text_width(const char *s, int scale) { (void)s;(void)scale; return 0; }
void gfx_copy_rect(int sx, int sy, int w, int h, int dx, int dy) { (void)sx;(void)sy;(void)w;(void)h;(void)dx;(void)dy; }
int gfx_click_pending(void) { return 0; }
int gfx_click_x(void) { return 0; }
int gfx_click_y(void) { return 0; }
void gfx_consume_click(void) {}
int gfx_mouse_x(void) { return 0; }
int gfx_mouse_y(void) { return 0; }
int gfx_mouse_down(void) { return 0; }
uint32_t *gfx_core_present_pixels(void) { return NULL; }
int gfx_core_present_width(void) { return 0; }
int gfx_core_present_height(void) { return 0; }
void gfx_core_set_alive(int a) { (void)a; }
int gfx_core_is_alive(void) { return 0; }
void gfx_core_mark_dirty(void) {}
int gfx_core_fb_resize(int w, int h) { (void)w;(void)h; return -1; }
void gfx_core_mouse_design(int wx, int wy, int down) { (void)wx;(void)wy;(void)down; }
void gfx_core_mouse_move(int wx, int wy, int down) { (void)wx;(void)wy;(void)down; }

#endif

/* ---- Shakti builtins ---- */
static inline int gfx_arg_int(V **a, int n, int idx, int fb) {
    if (n <= idx) return fb;
    if (a[idx]->t == T_INT) return (int)a[idx]->j;
    if (a[idx]->t == T_FLOAT) return (int)a[idx]->f;
    return fb;
}
static V *gfx_err(char *err) { P(!err[0], v_err("gfx: failed")); return v_err(err); }

V *bi_gfx_open(V **a, int n) {
    char err[512];
    const char *title = "Shakti GFX";
    err[0] = 0;
    if (n >= 1 && a[0]->t == T_STR) title = a[0]->s;
    P(gfx_open(title, err, sizeof err) != 0, gfx_err(err));
    return v_nil();
}
V *bi_gfx_close(V **a, int n) { (void)a;(void)n; gfx_close(); return v_nil(); }
V *bi_gfx_alive(V **a, int n) { (void)a;(void)n; return v_int(gfx_alive()); }
V *bi_gfx_available(V **a, int n) { (void)a;(void)n; return v_int(gfx_available()); }
V *bi_gfx_tick(V **a, int n) { char err[512]; (void)a;(void)n; err[0]=0; gfx_tick(err,sizeof err); return v_nil(); }
V *bi_gfx_sync_keys(V **a, int n) {
    (void)a;(void)n;
#if defined(SHAKTI_HAVE_GFX) && ( \
    (defined(__linux__) && __has_include(<X11/Xlib.h>)) || \
    (defined(__APPLE__) && !defined(__IOS__)) \
)
    if (input_own_gui()) gfx_platform_sync_keys();
#endif
    return v_nil();
}
V *bi_gfx_clear(V **a, int n) {
    P(n<1,v_err("gfx_clear(color)"));
    gfx_clear((uint32_t)gfx_arg_int(a,n,0,0));
    return v_nil();
}
V *bi_gfx_fill_rect(V **a, int n) {
    P(n<5,v_err("gfx_fill_rect(x,y,w,h,color)"));
    gfx_fill_rect(gfx_arg_int(a,n,0,0), gfx_arg_int(a,n,1,0), gfx_arg_int(a,n,2,0),
                  gfx_arg_int(a,n,3,0), (uint32_t)gfx_arg_int(a,n,4,0));
    return v_nil();
}
V *bi_gfx_line(V **a, int n) {
    P(n<5,v_err("gfx_line(x0,y0,x1,y1,color)"));
    gfx_line(gfx_arg_int(a,n,0,0), gfx_arg_int(a,n,1,0), gfx_arg_int(a,n,2,0),
             gfx_arg_int(a,n,3,0), (uint32_t)gfx_arg_int(a,n,4,0));
    return v_nil();
}
V *bi_gfx_fill_circle(V **a, int n) {
    P(n<4,v_err("gfx_fill_circle(cx,cy,r,color)"));
    gfx_fill_circle(gfx_arg_int(a,n,0,0), gfx_arg_int(a,n,1,0), gfx_arg_int(a,n,2,0),
                    (uint32_t)gfx_arg_int(a,n,3,0));
    return v_nil();
}
V *bi_gfx_click_pending(V **a, int n) { (void)a;(void)n; return v_int(gfx_click_pending()); }
V *bi_gfx_click_x(V **a, int n) { (void)a;(void)n; return v_int(gfx_click_x()); }
V *bi_gfx_click_y(V **a, int n) { (void)a;(void)n; return v_int(gfx_click_y()); }
V *bi_gfx_consume_click(V **a, int n) { (void)a;(void)n; gfx_consume_click(); return v_nil(); }
V *bi_gfx_mouse_x(V **a, int n) { (void)a;(void)n; return v_int(gfx_mouse_x()); }
V *bi_gfx_mouse_y(V **a, int n) { (void)a;(void)n; return v_int(gfx_mouse_y()); }
V *bi_gfx_mouse_down(V **a, int n) { (void)a;(void)n; return v_int(gfx_mouse_down()); }
V *bi_gfx_text(V **a, int n) {
    const char *s;
    int scale;
    P(n < 4, v_err("gfx_text(x,y,s,color[,scale])"));
    P(a[2]->t != T_STR, v_err("gfx_text: s must be string"));
    s = a[2]->s;
    scale = n >= 5 ? gfx_arg_int(a, n, 4, 1) : 1;
    gfx_text(gfx_arg_int(a, n, 0, 0), gfx_arg_int(a, n, 1, 0), s,
             (uint32_t)gfx_arg_int(a, n, 3, 0), scale);
    return v_nil();
}
V *bi_gfx_text_width(V **a, int n) {
    int scale;
    P(n < 1 || a[0]->t != T_STR, v_err("gfx_text_width(s[,scale])"));
    scale = n >= 2 ? gfx_arg_int(a, n, 1, 1) : 1;
    return v_int(gfx_text_width(a[0]->s, scale));
}
V *bi_gfx_copy_rect(V **a, int n) {
    P(n < 6, v_err("gfx_copy_rect(sx,sy,w,h,dx,dy)"));
    gfx_copy_rect(gfx_arg_int(a, n, 0, 0), gfx_arg_int(a, n, 1, 0),
                  gfx_arg_int(a, n, 2, 0), gfx_arg_int(a, n, 3, 0),
                  gfx_arg_int(a, n, 4, 0), gfx_arg_int(a, n, 5, 0));
    return v_nil();
}
