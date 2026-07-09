#include "gfx.h"
#include "gfx_platform.h"
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
            for (x = 0; x < g.win_w; x++) dst[x] = 0x0a0a12;
            continue;
        }
        {
            const uint32_t *src = g.fb + (size_t)dy * (size_t)GFX_DESIGN_W;
            for (x = 0; x < g.win_w; x++) {
                dx = (int)((x - g.off_x) / g.ui_scale);
                if (dx < 0 || dx >= GFX_DESIGN_W) dst[x] = 0x0a0a12;
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
    if (w <= 0 || h <= 0) return -1;
    np = (uint32_t *)realloc(g.present, (size_t)w * (size_t)h * sizeof(uint32_t));
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
    if (down && !g.mouse_down) {
        g.click_pending = 1;
        g.click_x = dx;
        g.click_y = dy;
    }
    g.mouse_down = down ? 1 : 0;
}

int gfx_available(void) { return 1; }

int gfx_open(const char *title, char *err, size_t err_cap) {
    (void)title;
    if (g.fb) gfx_close();
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
        free(g.fb);
        g.fb = NULL;
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
    if (g.dirty) {
        gfx_letterbox();
        gfx_platform_present();
        g.dirty = 0;
    }
    return 0;
}

void gfx_clear(uint32_t color) {
    size_t n;
    size_t i;
    if (!g.fb) return;
    n = (size_t)g.design_w * (size_t)g.design_h;
    if (color == 0) {
        memset(g.fb, 0, n * sizeof(uint32_t));
    } else {
        for (i = 0; i < n; i++) g.fb[i] = color;
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
    int y, x;
    if (!g.fb || r <= 0) return;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                gfx_put(cx + x, cy + y, color);
    g.dirty = 1;
}

int gfx_click_pending(void) { return g.click_pending; }
int gfx_click_x(void) { return g.click_x; }
int gfx_click_y(void) { return g.click_y; }
void gfx_consume_click(void) { g.click_pending = 0; }

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
int gfx_click_pending(void) { return 0; }
int gfx_click_x(void) { return 0; }
int gfx_click_y(void) { return 0; }
void gfx_consume_click(void) {}
uint32_t *gfx_core_present_pixels(void) { return NULL; }
int gfx_core_present_width(void) { return 0; }
int gfx_core_present_height(void) { return 0; }
void gfx_core_set_alive(int a) { (void)a; }
int gfx_core_is_alive(void) { return 0; }
void gfx_core_mark_dirty(void) {}
int gfx_core_fb_resize(int w, int h) { (void)w;(void)h; return -1; }
void gfx_core_mouse_design(int wx, int wy, int down) { (void)wx;(void)wy;(void)down; }

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
