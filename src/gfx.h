#ifndef SHAKTI_GFX_H
#define SHAKTI_GFX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gfx_open(const char *title, char *err, size_t err_cap);
void gfx_close(void);
int gfx_alive(void);
int gfx_tick(char *err, size_t err_cap);
int gfx_available(void);

void gfx_clear(uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_fill_circle(int cx, int cy, int r, uint32_t color);
void gfx_text(int x, int y, const char *s, uint32_t color, int scale);
int gfx_text_width(const char *s, int scale);
void gfx_copy_rect(int sx, int sy, int w, int h, int dx, int dy);

int gfx_click_pending(void);
int gfx_click_x(void);
int gfx_click_y(void);
void gfx_consume_click(void);

void gfx_core_mouse_design(int wx, int wy, int down);
uint32_t *gfx_core_present_pixels(void);
int gfx_core_present_width(void);
int gfx_core_present_height(void);
void gfx_core_set_alive(int alive);
int gfx_core_is_alive(void);
void gfx_core_mark_dirty(void);
int gfx_core_fb_resize(int w, int h);

#ifdef __cplusplus
}
#endif

#endif
