/* Bit-packed lane helpers for narrow numerics (bool + int/uint/float/decimal). */
#ifndef SHAKTI_PACK_H
#define SHAKTI_PACK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline size_t pack_nbytes(int64_t n, int bits) {
    if (n <= 0 || bits <= 0) return 1;
    return (size_t)((n * (int64_t)bits + 7) / 8);
}

/* Little-endian bit order within the buffer: lane i occupies bits [i*bits, i*bits+bits).
 * Word load/store — the per-bit loop made i24 widen/bin ~100× slower than i64. */
static inline uint64_t pack_get_u(const unsigned char *B, int64_t i, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 64) {
        uint64_t v = 0;
        memcpy(&v, B + (size_t)i * 8, 8);
        return v;
    }
    int64_t bit = i * (int64_t)bits;
    size_t byte = (size_t)(bit >> 3);
    unsigned sh = (unsigned)(bit & 7);
    unsigned need = (sh + (unsigned)bits + 7u) / 8u;
    unsigned char tmp[16] = {0};
    memcpy(tmp, B + byte, need);
    uint64_t v = 0;
    memcpy(&v, tmp, 8);
    uint64_t mask = ((uint64_t)1 << bits) - 1;
    if (sh + (unsigned)bits <= 64)
        return (v >> sh) & mask;
    return ((v >> sh) | ((uint64_t)tmp[8] << (64u - sh))) & mask;
}

static inline void pack_set_u(unsigned char *B, int64_t i, int bits, uint64_t val) {
    if (bits <= 0) return;
    if (bits >= 64) {
        memcpy(B + (size_t)i * 8, &val, 8);
        return;
    }
    uint64_t mask = ((uint64_t)1 << bits) - 1;
    val &= mask;
    int64_t bit = i * (int64_t)bits;
    size_t byte = (size_t)(bit >> 3);
    unsigned sh = (unsigned)(bit & 7);
    unsigned need = (sh + (unsigned)bits + 7u) / 8u;
    unsigned char tmp[16] = {0};
    memcpy(tmp, B + byte, need);
    uint64_t v = 0, hi = 0;
    memcpy(&v, tmp, 8);
    if (need > 8)
        hi = tmp[8];
    v = (v & ~(mask << sh)) | (val << sh);
    if (sh + (unsigned)bits > 64) {
        unsigned hibits = sh + (unsigned)bits - 64u;
        uint64_t himask = ((uint64_t)1 << hibits) - 1;
        hi = (hi & ~himask) | (val >> (64u - sh));
        tmp[8] = (unsigned char)hi;
    }
    memcpy(tmp, &v, 8);
    memcpy(B + byte, tmp, need);
}

static inline int64_t pack_get_i(const unsigned char *B, int64_t i, int bits) {
    uint64_t u = pack_get_u(B, i, bits);
    if (bits <= 0 || bits >= 64) return (int64_t)u;
    uint64_t sign = (uint64_t)1 << (bits - 1);
    if (u & sign) {
        uint64_t ext = ~(((uint64_t)1 << bits) - 1);
        return (int64_t)(u | ext);
    }
    return (int64_t)u;
}

static inline void pack_set_i(unsigned char *B, int64_t i, int bits, int64_t val) {
    pack_set_u(B, i, bits, (uint64_t)val);
}

static inline int bbit_get(const unsigned char *B, int64_t i) {
    return (B[i >> 3] >> (i & 7)) & 1;
}

static inline void bbit_set(unsigned char *B, int64_t i, int bit) {
    unsigned char m = (unsigned char)(1u << (i & 7));
    if (bit) B[i >> 3] |= m;
    else B[i >> 3] &= (unsigned char)~m;
}

static inline size_t bbit_nbytes(int64_t n) { return pack_nbytes(n, 1); }

/* Copy 4/8/16-byte lane (decimal / f32 patterns). */
static inline void pack_get_bytes(const unsigned char *B, int64_t i, int nbytes, unsigned char *out) {
    memcpy(out, B + (size_t)i * (size_t)nbytes, (size_t)nbytes);
}

static inline void pack_set_bytes(unsigned char *B, int64_t i, int nbytes, const unsigned char *src) {
    memcpy(B + (size_t)i * (size_t)nbytes, src, (size_t)nbytes);
}

static inline int64_t int_mask(int bits) {
    if (bits <= 0) return 0;
    if (bits >= 64) return (int64_t)~0ull;
    return (int64_t)(((uint64_t)1 << bits) - 1);
}

static inline int64_t int_wrap_signed(int64_t v, int bits) {
    if (bits >= 64) return v;
    if (bits <= 0) return 0;
    uint64_t mask = ((uint64_t)1 << bits) - 1;
    uint64_t u = (uint64_t)v & mask;
    uint64_t sign = (uint64_t)1 << (bits - 1);
    if (u & sign) return (int64_t)(u | ~mask);
    return (int64_t)u;
}

static inline uint64_t int_wrap_unsigned(uint64_t v, int bits) {
    if (bits >= 64) return v;
    if (bits <= 0) return 0;
    return v & (((uint64_t)1 << bits) - 1);
}

#endif /* SHAKTI_PACK_H */
