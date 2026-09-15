#ifndef SHAKTI_GFX_PLATFORM_H
#define SHAKTI_GFX_PLATFORM_H

#include <stddef.h>

int gfx_platform_init(const char *title, char *err, size_t cap);
void gfx_platform_shutdown(void);
int gfx_platform_poll(void);
void gfx_platform_present(void);
/* Return 1 if the host skipped a CPU framebuffer download. */
int gfx_platform_present_gpu(void);
void gfx_platform_sync_keys(void);
int gfx_platform_screen_size(int *w, int *h);
void gfx_platform_set_fullscreen(int on);

#endif
