/* gfx_wasm.c — Emscripten present: CPU framebuffer, JS blits to canvas. */
#include "gfx_platform.h"
#include "gfx.h"

#include <stdio.h>
#include <string.h>

static int g_w = 960;
static int g_h = 720;

int gfx_platform_init(const char *title, char *err, size_t cap) {
    (void)title;
    (void)err;
    (void)cap;
    if (gfx_core_fb_resize(g_w, g_h) != 0) {
        if (err && cap) snprintf(err, cap, "gfx: wasm fb resize failed");
        return -1;
    }
    gfx_core_set_alive(1);
    gfx_core_mark_dirty();
    return 0;
}

void gfx_platform_shutdown(void) {}

int gfx_platform_poll(void) { return gfx_core_is_alive() ? 0 : -1; }

void gfx_platform_present(void) {}

void gfx_platform_sync_keys(void) {}

void gfx_wasm_set_window(int w, int h) {
    if (w > 64 && h > 64) {
        g_w = w;
        g_h = h;
    }
}
