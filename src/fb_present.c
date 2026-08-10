#include "fb_present.h"

#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static void pack_row_rgba8(uint32_t *dst, const uint32_t *src, int w) {
    int x = 0;
#if defined(__aarch64__)
    for (; x + 4 <= w; x += 4) {
        uint32x4_t c = vld1q_u32(src + x);
        uint32x4_t r = vshrq_n_u32(vandq_u32(c, vdupq_n_u32(0x00ff0000u)), 16);
        uint32x4_t g = vandq_u32(c, vdupq_n_u32(0x0000ff00u));
        uint32x4_t b = vshlq_n_u32(vandq_u32(c, vdupq_n_u32(0x000000ffu)), 16);
        uint32x4_t a = vdupq_n_u32(0xff000000u);
        vst1q_u32(dst + x, vorrq_u32(vorrq_u32(r, g), vorrq_u32(b, a)));
    }
#endif
    for (; x < w; x++) {
        uint32_t c = src[x];
        dst[x] = ((c & 0xff0000u) >> 16) | (c & 0x00ff00u) | ((c & 0xffu) << 16) | 0xff000000u;
    }
}

void fb_pack_rgba8(unsigned char *dst, const uint32_t *src, int w, int h, int flip_y) {
    int y;
    if (!dst || !src || w <= 0 || h <= 0) return;
    for (y = 0; y < h; y++) {
        int dy = flip_y ? (h - 1 - y) : y;
        const uint32_t *src_row = src + (size_t)y * (size_t)w;
        uint32_t *dst_row = (uint32_t *)(dst + (size_t)dy * (size_t)w * 4u);
        pack_row_rgba8(dst_row, src_row, w);
    }
}

static void fill_row(uint32_t *dst, int n, uint32_t color) {
    int x = 0;
#if defined(__aarch64__)
    uint32x4_t v = vdupq_n_u32(color);
    for (; x + 4 <= n; x += 4)
        vst1q_u32(dst + x, v);
#endif
    for (; x < n; x++)
        dst[x] = color;
}

static void letterbox_2x(uint32_t *dst, int dw, int dh,
                         const uint32_t *src, int sw, int sh,
                         int off_x, int off_y, uint32_t pad) {
    int y, x;
    int content_h = sh * 2;
    int content_w = sw * 2;
    for (y = 0; y < dh; y++) {
        uint32_t *row = dst + (size_t)y * (size_t)dw;
        int sy = y - off_y;
        if (sy < 0 || sy >= content_h) {
            fill_row(row, dw, pad);
            continue;
        }
        {
            const uint32_t *src_row = src + (size_t)(sy >> 1) * (size_t)sw;
            if (off_x > 0)
                fill_row(row, off_x, pad);
            for (x = 0; x < sw; x++) {
                uint32_t c = src_row[x];
                int dx = off_x + x * 2;
                row[dx] = c;
                row[dx + 1] = c;
            }
            if (off_x + content_w < dw)
                fill_row(row + off_x + content_w, dw - off_x - content_w, pad);
        }
    }
}

void fb_letterbox_nn(uint32_t *dst, int dw, int dh,
                     const uint32_t *src, int sw, int sh,
                     float scale, int off_x, int off_y, uint32_t pad) {
    int x, y, dx, dy;
    if (!dst || !src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    if (scale == 1.0f && off_x == 0 && off_y == 0 && dw == sw && dh == sh) {
        memcpy(dst, src, (size_t)sw * (size_t)sh * sizeof(uint32_t));
        return;
    }

    /* Exact 2× (macOS gfx default: 960×540 → 1920×1080). */
    if (scale == 2.0f && (off_x + sw * 2) <= dw && (off_y + sh * 2) <= dh) {
        letterbox_2x(dst, dw, dh, src, sw, sh, off_x, off_y, pad);
        return;
    }

    for (y = 0; y < dh; y++) {
        uint32_t *drow = dst + (size_t)y * (size_t)dw;
        dy = (int)((y - off_y) / scale);
        if (dy < 0 || dy >= sh) {
            fill_row(drow, dw, pad);
            continue;
        }
        {
            const uint32_t *srow = src + (size_t)dy * (size_t)sw;
            for (x = 0; x < dw; x++) {
                dx = (int)((x - off_x) / scale);
                if (dx < 0 || dx >= sw) drow[x] = pad;
                else drow[x] = srow[dx];
            }
        }
    }
}
