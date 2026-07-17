#ifdef __linux__
#include "gfx_platform.h"
#include "gfx.h"
#include "input.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    Display *dpy;
    Window win;
    GC gc;
    XImage *img;
    int scr;
} g;

static void gfx_x11_blit(void) {
    uint32_t *px;
    int w, h, x, y;
    if (!g.dpy || !g.img) return;
    px = gfx_core_present_pixels();
    w = gfx_core_present_width();
    h = gfx_core_present_height();
    if (!px || w <= 0 || h <= 0) return;
    /* Fast path: 32bpp LSB host-order matches our 0x00RRGGBB packing. */
    if (g.img->bits_per_pixel == 32 && g.img->byte_order == LSBFirst &&
        g.img->bitmap_pad >= 32 && g.img->data) {
        for (y = 0; y < h; y++) {
            const uint32_t *src = px + (size_t)y * (size_t)w;
            uint32_t *dst = (uint32_t *)(g.img->data + (size_t)y * (size_t)g.img->bytes_per_line);
            for (x = 0; x < w; x++) {
                uint32_t c = src[x];
                dst[x] = ((c >> 16) & 255u) << 16 | ((c >> 8) & 255u) << 8 | (c & 255u);
            }
        }
    } else {
        for (y = 0; y < h; y++) {
            const uint32_t *src = px + (size_t)y * (size_t)w;
            for (x = 0; x < w; x++) {
                uint32_t c = src[x];
                unsigned long rgb = ((c >> 16) & 255u) << 16 | ((c >> 8) & 255u) << 8 | (c & 255u);
                XPutPixel(g.img, x, y, rgb);
            }
        }
    }
    XPutImage(g.dpy, g.win, g.gc, g.img, 0, 0, 0, 0, w, h);
}

int gfx_platform_init(const char *title, char *err, size_t cap) {
    XSetWindowAttributes swa;
    Atom wm_delete;
  (void)title;
    memset(&g, 0, sizeof g);
    g.dpy = XOpenDisplay(NULL);
    if (!g.dpy) {
        snprintf(err, cap, "gfx_open: cannot open X display");
        return -1;
    }
    g.scr = DefaultScreen(g.dpy);
    /* Present at 2× design (960×540 → 1920×1080); letterbox scales the design buffer. */
    g.win = XCreateSimpleWindow(g.dpy, RootWindow(g.dpy, g.scr), 100, 100, 1920, 1080, 1,
                                BlackPixel(g.dpy, g.scr), BlackPixel(g.dpy, g.scr));
    g.gc = XCreateGC(g.dpy, g.win, 0, NULL);
    if (title) XStoreName(g.dpy, g.win, title);
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                     ButtonReleaseMask | StructureNotifyMask;
    XSelectInput(g.dpy, g.win, swa.event_mask);
    if (gfx_core_fb_resize(1920, 1080) != 0) {
        snprintf(err, cap, "gfx_open: framebuffer init failed");
        return -1;
    }
    g.img = XCreateImage(g.dpy, DefaultVisual(g.dpy, g.scr), 24, ZPixmap, 0, NULL, 1920, 1080, 32, 0);
    if (!g.img) {
        snprintf(err, cap, "gfx_open: XCreateImage failed");
        return -1;
    }
    g.img->data = (char *)malloc((size_t)g.img->bytes_per_line * (size_t)g.img->height);
    if (!g.img->data) {
        XDestroyImage(g.img);
        g.img = NULL;
        snprintf(err, cap, "gfx_open: out of memory");
        return -1;
    }
    XMapWindow(g.dpy, g.win);
    wm_delete = XInternAtom(g.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g.dpy, g.win, &wm_delete, 1);
    XFlush(g.dpy);
    return 0;
}

void gfx_platform_shutdown(void) {
    if (g.dpy && g.win) XDestroyWindow(g.dpy, g.win);
    if (g.gc && g.dpy) XFreeGC(g.dpy, g.gc);
    if (g.img) XDestroyImage(g.img);
    if (g.dpy) XCloseDisplay(g.dpy);
    memset(&g, 0, sizeof g);
}

void gfx_platform_present(void) { gfx_x11_blit(); }

int gfx_platform_poll(void) {
    XEvent ev;
    char utf8[8];
    if (!gfx_core_is_alive()) return -1;
    while (XPending(g.dpy)) {
        XNextEvent(g.dpy, &ev);
        switch (ev.type) {
        case ClientMessage:
            gfx_core_set_alive(0);
            return -1;
        case ConfigureNotify:
            if (gfx_core_fb_resize(ev.xconfigure.width, ev.xconfigure.height) != 0) break;
            XImage *next = XCreateImage(g.dpy, DefaultVisual(g.dpy, g.scr), 24, ZPixmap, 0, NULL,
                                        ev.xconfigure.width, ev.xconfigure.height, 32, 0);
            if (!next) break;
            next->data = (char *)malloc((size_t)next->bytes_per_line * (size_t)next->height);
            if (!next->data) {
                XDestroyImage(next);
                break;
            }
            if (g.img) XDestroyImage(g.img);
            g.img = next;
            gfx_core_mark_dirty();
            break;
        case ButtonPress:
            input_hub_inject_mouse(ev.xbutton.x, ev.xbutton.y, 1);
            gfx_core_mouse_design(ev.xbutton.x, ev.xbutton.y, 1);
            gfx_core_mark_dirty();
            break;
        case ButtonRelease:
            input_hub_inject_mouse(ev.xbutton.x, ev.xbutton.y, 0);
            gfx_core_mouse_design(ev.xbutton.x, ev.xbutton.y, 0);
            gfx_core_mark_dirty();
            break;
        case KeyPress:
        case KeyRelease: {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            utf8[0] = 0;
            if (ks >= 32 && ks < 127) { utf8[0] = (char)ks; utf8[1] = 0; }
            input_hub_inject_key((int)ks, (int)ev.xkey.state, utf8, ev.type == KeyPress);
            break;
        }
        default:
            break;
        }
    }
    return gfx_core_is_alive() ? 0 : -1;
}

void gfx_platform_sync_keys(void) {}

#else

int gfx_platform_init(const char *title, char *err, size_t cap) {
    (void)title;
    if (err && cap) snprintf(err, cap, "gfx: no platform backend");
    return -1;
}
void gfx_platform_shutdown(void) {}
void gfx_platform_present(void) {}
int gfx_platform_poll(void) { return -1; }
void gfx_platform_sync_keys(void) {}

#endif
