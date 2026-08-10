/*
 * Shared software present helpers: nearest-neighbor letterbox and
 * 0x00RRGGBB → RGBA8 conversion (optional Y-flip for Cocoa bitmaps).
 */
#ifndef SHAKTI_FB_PRESENT_H
#define SHAKTI_FB_PRESENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scale src[sw*sh] into dst[dw*dh] with uniform scale and letterbox offsets.
 * pad fills bars outside the scaled content. Fast paths: 1:1 memcpy, exact 2×. */
void fb_letterbox_nn(uint32_t *dst, int dw, int dh,
                     const uint32_t *src, int sw, int sh,
                     float scale, int off_x, int off_y, uint32_t pad);

/* Pack xRGB pixels to RGBA8 bytes. If flip_y, dst row 0 is src row h-1.
 * dst must hold w*h*4 bytes (row stride = w*4). */
void fb_pack_rgba8(unsigned char *dst, const uint32_t *src, int w, int h, int flip_y);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_FB_PRESENT_H */
