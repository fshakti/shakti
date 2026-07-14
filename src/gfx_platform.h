#ifndef SHAKTI_GFX_PLATFORM_H
#define SHAKTI_GFX_PLATFORM_H

#include <stddef.h>

int gfx_platform_init(const char *title, char *err, size_t cap);
void gfx_platform_shutdown(void);
int gfx_platform_poll(void);
void gfx_platform_present(void);
void gfx_platform_sync_keys(void);

#endif
