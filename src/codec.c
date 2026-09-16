/*
 * Host compression: zstd + snappy + residual column codecs (delta/FIRE/Gorilla).
 * Build with SHAKTI_HAVE_ZSTD / SHAKTI_HAVE_SNAPPY when libs are present.
 */
#include "codec.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SHAKTI_HAVE_ZSTD)
#include <zstd.h>
#include <zstd_errors.h>
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
#include <snappy-c.h>
#endif

#define CODEC_BLOCK_N 64u
#define CODEC_MAX_ELEMS (1ull << 32)
#define CODEC_MAGIC_DELTA 0x36444C44u /* 'DLD6' LE: delta i64 v1 */
#define CODEC_MAGIC_FIRE  0x36455246u /* 'FRE6' LE */
#define CODEC_MAGIC_GOR   0x36475247u /* 'GRG6' LE */
#define CODEC_MAGIC_DATE  0x34594144u /* 'DAY4' LE: midnight-ms as day δδ */
#define CODEC_MAGIC_TIME  0x34534D54u /* 'TMS4' LE: 27-bit ms-in-day */
#define CODEC_MAGIC_DT    0x384D5444u /* 'DTM8' LE: day δδ + 27-bit tod */
#define MS_PER_DAY        86400000LL
#define TOD_BITS          27

static void set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap)
        snprintf(err, err_cap, "%s", msg ? msg : "codec error");
}

static int parse_base_name(const char *name, size_t n) {
    if (n == 4 && !memcmp(name, "zstd", 4))
        return SHAKTI_CODEC_ZSTD;
    if (n == 4 && !memcmp(name, "ZSTD", 4))
        return SHAKTI_CODEC_ZSTD;
    if (n == 6 && !memcmp(name, "snappy", 6))
        return SHAKTI_CODEC_SNAPPY;
    if (n == 6 && !memcmp(name, "SNAPPY", 6))
        return SHAKTI_CODEC_SNAPPY;
    if (n == 9 && !memcmp(name, "delta_i64", 9))
        return SHAKTI_CODEC_DELTA_I64;
    if (n == 8 && !memcmp(name, "fire_i64", 8))
        return SHAKTI_CODEC_FIRE_I64;
    if (n == 11 && !memcmp(name, "gorilla_f64", 11))
        return SHAKTI_CODEC_GORILLA_F64;
    if (n == 4 && !memcmp(name, "date", 4))
        return SHAKTI_CODEC_DATE;
    if (n == 4 && !memcmp(name, "time", 4))
        return SHAKTI_CODEC_TIME;
    if (n == 8 && !memcmp(name, "datetime", 8))
        return SHAKTI_CODEC_DATETIME;
    return -1;
}

int shakti_codec_parse2(const char *name, int *outer) {
    if (outer)
        *outer = SHAKTI_CODEC_NONE;
    if (!name || !name[0])
        return SHAKTI_CODEC_NONE;
    const char *plus = strchr(name, '+');
    size_t base_n = plus ? (size_t)(plus - name) : strlen(name);
    int base = parse_base_name(name, base_n);
    if (base < 0)
        return -1;
    if (plus) {
        int o = parse_base_name(plus + 1, strlen(plus + 1));
        if (o != SHAKTI_CODEC_ZSTD && o != SHAKTI_CODEC_SNAPPY)
            return -1;
        if (!shakti_codec_is_residual(base))
            return -1; /* only residual+lz compounds */
        if (outer)
            *outer = o;
    }
    return base;
}

int shakti_codec_parse(const char *name) {
    return shakti_codec_parse2(name, NULL);
}

int shakti_codec_is_residual(int codec) {
    return codec == SHAKTI_CODEC_DELTA_I64 || codec == SHAKTI_CODEC_FIRE_I64 ||
           codec == SHAKTI_CODEC_GORILLA_F64 || codec == SHAKTI_CODEC_DATE ||
           codec == SHAKTI_CODEC_TIME || codec == SHAKTI_CODEC_DATETIME;
}

int shakti_codec_available(int codec) {
    if (codec == SHAKTI_CODEC_NONE)
        return 1;
    if (shakti_codec_is_residual(codec))
        return 1;
#if defined(SHAKTI_HAVE_ZSTD)
    if (codec == SHAKTI_CODEC_ZSTD)
        return 1;
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
    if (codec == SHAKTI_CODEC_SNAPPY)
        return 1;
#endif
    return 0;
}

/* ---- bit writer / reader ---- */
typedef struct {
    unsigned char *data;
    size_t cap;
    size_t byte_i;
    int bit_i; /* next bit 0..7 within data[byte_i] */
} BitW;

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t byte_i;
    int bit_i;
} BitR;

static int bitw_ensure(BitW *w, size_t extra_bytes) {
    size_t need = w->byte_i + extra_bytes + 8;
    if (need <= w->cap)
        return 0;
    size_t ncap = w->cap ? w->cap : 64;
    while (ncap < need)
        ncap *= 2;
    unsigned char *p = realloc(w->data, ncap);
    if (!p)
        return -1;
    memset(p + w->cap, 0, ncap - w->cap);
    w->data = p;
    w->cap = ncap;
    return 0;
}

static int bitw_put(BitW *w, uint64_t v, int nbits) {
    if (nbits <= 0)
        return 0;
    if (nbits > 64)
        return -1;
    if (bitw_ensure(w, (size_t)((nbits + 7) / 8) + 2) != 0)
        return -1;
    uint64_t mask = nbits == 64 ? ~0ull : ((1ull << nbits) - 1);
    v &= mask;
    unsigned sh = (unsigned)w->bit_i;
    unsigned need = (sh + (unsigned)nbits + 7u) / 8u;
    unsigned char tmp[16] = {0};
    memcpy(tmp, w->data + w->byte_i, need);
    uint64_t cur = 0, hi = 0;
    memcpy(&cur, tmp, 8);
    if (need > 8)
        hi = tmp[8];
    cur = (cur & ~(mask << sh)) | (v << sh);
    if (sh + (unsigned)nbits > 64) {
        unsigned hibits = sh + (unsigned)nbits - 64u;
        uint64_t himask = ((uint64_t)1 << hibits) - 1;
        hi = (hi & ~himask) | (v >> (64u - sh));
        tmp[8] = (unsigned char)hi;
    }
    memcpy(tmp, &cur, 8);
    memcpy(w->data + w->byte_i, tmp, need);
    size_t bits = (size_t)w->bit_i + (size_t)nbits;
    w->byte_i += bits / 8u;
    w->bit_i = (int)(bits % 8u);
    return 0;
}

static size_t bitw_finish(BitW *w) {
    if (w->bit_i)
        return w->byte_i + 1;
    return w->byte_i;
}

static int bitr_get(BitR *r, int nbits, uint64_t *out) {
    if (nbits < 0 || nbits > 64)
        return -1;
    if (nbits == 0) {
        *out = 0;
        return 0;
    }
    size_t end_bit = r->byte_i * 8u + (size_t)r->bit_i + (size_t)nbits;
    size_t end_byte = (end_bit + 7u) / 8u;
    if (end_byte > r->len)
        return -1;
    unsigned sh = (unsigned)r->bit_i;
    unsigned need = (sh + (unsigned)nbits + 7u) / 8u;
    unsigned char tmp[16] = {0};
    memcpy(tmp, r->data + r->byte_i, need);
    uint64_t v = 0;
    memcpy(&v, tmp, 8);
    uint64_t mask = nbits == 64 ? ~0ull : ((1ull << nbits) - 1);
    uint64_t val = v >> sh;
    if (sh + (unsigned)nbits > 64)
        val |= (uint64_t)tmp[8] << (64u - sh);
    *out = val & mask;
    r->byte_i += ((size_t)r->bit_i + (size_t)nbits) / 8u;
    r->bit_i = (int)(((size_t)r->bit_i + (size_t)nbits) % 8u);
    return 0;
}

static uint64_t zigzag_encode(int64_t v) {
    return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
}

static int64_t zigzag_decode(uint64_t v) {
    return (int64_t)((v >> 1) ^ (~(v & 1ull) + 1ull));
}

static int bit_width_u64(uint64_t v) {
    if (v == 0)
        return 0;
    int w = 0;
    while (v) {
        w++;
        v >>= 1;
    }
    return w;
}

static void put_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_u64_le(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++)
        p[i] = (unsigned char)(v >> (8 * i));
}

static uint32_t get_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64_le(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

/* ---- delta_i64 / fire_i64 ---- */

static int64_t fire_predict(int64_t prev, int32_t alpha_fp) {
    /* alpha_fp: Q8 fixed-point in [0, 512) ≈ [0, 2) */
    return (int64_t)(((int64_t)alpha_fp * prev) >> 8);
}

static void fire_adapt(int32_t *alpha_fp, int64_t err, int64_t prev_err) {
    int32_t a = *alpha_fp;
    int same = (err == 0 && prev_err == 0) ||
               ((err > 0) == (prev_err > 0) && err != 0 && prev_err != 0);
    if (same) {
        if (a < 512 - 4)
            a += 4;
    } else {
        if (a > 4)
            a -= 4;
    }
    *alpha_fp = a;
}

static int compress_i64_residual(int codec, const unsigned char *src, size_t src_len,
                                 unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (src_len % 8u != 0) {
        set_err(err, err_cap, "codec: residual requires i64 byte length");
        return -1;
    }
    size_t n = src_len / 8u;
    if (n > CODEC_MAX_ELEMS) {
        set_err(err, err_cap, "codec: too many samples");
        return -1;
    }
    const int64_t *x = (const int64_t *)(const void *)src;
    uint32_t magic = CODEC_MAGIC_DELTA;
    if (codec == SHAKTI_CODEC_FIRE_I64)
        magic = CODEC_MAGIC_FIRE;
    else if (codec == SHAKTI_CODEC_DATE)
        magic = CODEC_MAGIC_DATE;

    /* header: magic u32, ver u8, flags u8, block_n u16, n u64, x0 i64  (= 24 bytes) */
    BitW w = {0};
    if (bitw_ensure(&w, 24 + n * 9 + 64) != 0) {
        set_err(err, err_cap, "compress: out of memory");
        return -1;
    }
    put_u32_le(w.data + 0, magic);
    w.data[4] = 1; /* ver */
    w.data[5] = 0; /* flags */
    w.data[6] = (unsigned char)(CODEC_BLOCK_N & 0xff);
    w.data[7] = (unsigned char)((CODEC_BLOCK_N >> 8) & 0xff);
    put_u64_le(w.data + 8, (uint64_t)n);
    int64_t x0 = n ? x[0] : 0;
    put_u64_le(w.data + 16, (uint64_t)x0);
    w.byte_i = 24;
    w.bit_i = 0;

    if (n <= 1) {
        *out = w.data;
        *out_len = 24;
        return 0;
    }

    int32_t alpha = 256; /* 1.0 in Q8 — FIRE only */
    int64_t prev_err = 0;
    int64_t prev_delta = 0;

    size_t i = 1;
    while (i < n) {
        size_t bend = i + CODEC_BLOCK_N;
        if (bend > n)
            bend = n;
        size_t bn = bend - i;
        uint64_t *zz = calloc(bn, sizeof(uint64_t));
        if (!zz) {
            free(w.data);
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        int maxw = 0;
        int all_zero = 1;
        for (size_t k = 0; k < bn; k++) {
            size_t idx = i + k;
            int64_t resid;
            if (codec == SHAKTI_CODEC_FIRE_I64) {
                int64_t pred = fire_predict(x[idx - 1], alpha);
                resid = x[idx] - pred;
                fire_adapt(&alpha, resid, prev_err);
                prev_err = resid;
            } else {
                /* δδ: first sample in stream uses simple delta; rest δδ */
                int64_t d = x[idx] - x[idx - 1];
                if (idx == 1) {
                    resid = d;
                    prev_delta = d;
                } else {
                    resid = d - prev_delta;
                    prev_delta = d;
                }
            }
            zz[k] = zigzag_encode(resid);
            if (zz[k])
                all_zero = 0;
            int bw = bit_width_u64(zz[k]);
            if (bw > maxw)
                maxw = bw;
        }
        if (all_zero) {
            if (bitw_put(&w, 0xFF, 8) != 0) {
                free(zz);
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
        } else {
            if (maxw == 0)
                maxw = 1;
            if (maxw > 64)
                maxw = 64;
            if (bitw_put(&w, (uint64_t)maxw, 8) != 0) {
                free(zz);
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            for (size_t k = 0; k < bn; k++) {
                if (bitw_put(&w, zz[k], maxw) != 0) {
                    free(zz);
                    free(w.data);
                    set_err(err, err_cap, "compress: out of memory");
                    return -1;
                }
            }
        }
        free(zz);
        i = bend;
    }

    *out_len = bitw_finish(&w);
    *out = w.data;
    return 0;
}

static int decompress_i64_residual(int codec, const unsigned char *src, size_t src_len,
                                   unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (src_len < 24) {
        set_err(err, err_cap, "codec: truncated residual header");
        return -1;
    }
    uint32_t magic = get_u32_le(src);
    uint32_t expect = CODEC_MAGIC_DELTA;
    if (codec == SHAKTI_CODEC_FIRE_I64)
        expect = CODEC_MAGIC_FIRE;
    else if (codec == SHAKTI_CODEC_DATE)
        expect = CODEC_MAGIC_DATE;
    if (magic != expect || src[4] != 1) {
        set_err(err, err_cap, "codec: bad residual magic");
        return -1;
    }
    uint16_t block_n = (uint16_t)src[6] | ((uint16_t)src[7] << 8);
    if (block_n == 0 || block_n > 4096) {
        set_err(err, err_cap, "codec: bad block size");
        return -1;
    }
    uint64_t n64 = get_u64_le(src + 8);
    if (n64 > CODEC_MAX_ELEMS || n64 > SHAKTI_CODEC_MAX_PLAIN / 8u) {
        set_err(err, err_cap, "codec: residual too large");
        return -1;
    }
    /* Each block needs ≥1 tag byte; RLE unlocks up to block_n samples per byte. */
    size_t max_n = 1u + (src_len - 24u) * (size_t)block_n;
    if (n64 > max_n) {
        set_err(err, err_cap, "codec: truncated residual body");
        return -1;
    }
    size_t n = (size_t)n64;
    int64_t x0 = (int64_t)get_u64_le(src + 16);
    unsigned char *buf = malloc(n ? n * 8u : 1);
    if (!buf) {
        set_err(err, err_cap, "decompress: out of memory");
        return -1;
    }
    int64_t *x = (int64_t *)(void *)buf;
    if (n == 0) {
        *out = buf;
        *out_len = 0;
        return 0;
    }
    x[0] = x0;
    if (n == 1) {
        *out = buf;
        *out_len = 8;
        return 0;
    }

    BitR r = {.data = src, .len = src_len, .byte_i = 24, .bit_i = 0};
    int32_t alpha = 256;
    int64_t prev_err = 0;
    int64_t prev_delta = 0;
    size_t i = 1;
    while (i < n) {
        size_t bend = i + block_n;
        if (bend > n)
            bend = n;
        size_t bn = bend - i;
        uint64_t tag = 0;
        if (bitr_get(&r, 8, &tag) != 0) {
            free(buf);
            set_err(err, err_cap, "codec: truncated residual body");
            return -1;
        }
        if (tag == 0xFF) {
            for (size_t k = 0; k < bn; k++) {
                size_t idx = i + k;
                int64_t resid = 0;
                if (codec == SHAKTI_CODEC_FIRE_I64) {
                    int64_t pred = fire_predict(x[idx - 1], alpha);
                    x[idx] = pred + resid;
                    fire_adapt(&alpha, resid, prev_err);
                    prev_err = resid;
                } else {
                    int64_t d = (idx == 1) ? resid : (prev_delta + resid);
                    x[idx] = x[idx - 1] + d;
                    prev_delta = d;
                }
            }
        } else {
            int maxw = (int)tag;
            if (maxw < 1 || maxw > 64) {
                free(buf);
                set_err(err, err_cap, "codec: bad residual bit width");
                return -1;
            }
            for (size_t k = 0; k < bn; k++) {
                uint64_t zz = 0;
                if (bitr_get(&r, maxw, &zz) != 0) {
                    free(buf);
                    set_err(err, err_cap, "codec: truncated residual body");
                    return -1;
                }
                int64_t resid = zigzag_decode(zz);
                size_t idx = i + k;
                if (codec == SHAKTI_CODEC_FIRE_I64) {
                    int64_t pred = fire_predict(x[idx - 1], alpha);
                    x[idx] = pred + resid;
                    fire_adapt(&alpha, resid, prev_err);
                    prev_err = resid;
                } else {
                    int64_t d = (idx == 1) ? resid : (prev_delta + resid);
                    x[idx] = x[idx - 1] + d;
                    prev_delta = d;
                }
            }
        }
        i = bend;
    }
    *out = buf;
    *out_len = n * 8u;
    return 0;
}

/* ---- gorilla_f64 ---- */

static int compress_gorilla_f64(const unsigned char *src, size_t src_len, unsigned char **out,
                                size_t *out_len, char *err, size_t err_cap) {
    if (src_len % 8u != 0) {
        set_err(err, err_cap, "codec: gorilla requires f64 byte length");
        return -1;
    }
    size_t n = src_len / 8u;
    if (n > CODEC_MAX_ELEMS) {
        set_err(err, err_cap, "codec: too many samples");
        return -1;
    }
    const uint64_t *bits = (const uint64_t *)(const void *)src;
    BitW w = {0};
    if (bitw_ensure(&w, 16 + n * 9 + 64) != 0) {
        set_err(err, err_cap, "compress: out of memory");
        return -1;
    }
    put_u32_le(w.data + 0, CODEC_MAGIC_GOR);
    w.data[4] = 1;
    w.data[5] = 0;
    w.data[6] = 0;
    w.data[7] = 0;
    put_u64_le(w.data + 8, (uint64_t)n);
    w.byte_i = 16;
    w.bit_i = 0;
    if (n == 0) {
        *out = w.data;
        *out_len = 16;
        return 0;
    }
    /* first value raw */
    if (bitw_put(&w, bits[0], 64) != 0) {
        free(w.data);
        set_err(err, err_cap, "compress: out of memory");
        return -1;
    }
    int prev_lz = 0, prev_tz = 0;
    int have_window = 0;
    uint64_t prev = bits[0];
    for (size_t i = 1; i < n; i++) {
        uint64_t cur = bits[i];
        uint64_t xor = prev ^ cur;
        prev = cur;
        if (xor == 0) {
            if (bitw_put(&w, 0, 1) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            continue;
        }
        if (bitw_put(&w, 1, 1) != 0) {
            free(w.data);
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        int lz = 0;
        uint64_t t = xor;
        while (lz < 63 && ((t >> 63) == 0)) {
            lz++;
            t <<= 1;
        }
        int tz = 0;
        t = xor;
        while (tz < 64 && ((t & 1ull) == 0)) {
            tz++;
            t >>= 1;
        }
        int meaningful = 64 - lz - tz;
        if (meaningful <= 0)
            meaningful = 1;
        if (have_window && lz >= prev_lz && tz >= prev_tz) {
            /* reuse window */
            if (bitw_put(&w, 0, 1) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            int use_lz = prev_lz;
            int use_tz = prev_tz;
            int use_m = 64 - use_lz - use_tz;
            uint64_t payload = (xor >> (unsigned)use_tz) & ((use_m >= 64) ? ~0ull : ((1ull << use_m) - 1ull));
            if (bitw_put(&w, payload, use_m) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
        } else {
            if (bitw_put(&w, 1, 1) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            /* 5-bit leading zeros (0..31); when true lz>31 keep real tz and widen meaningful. */
            int store_lz = lz > 31 ? 31 : lz;
            int store_tz = tz;
            int store_m = 64 - store_lz - store_tz;
            if (store_m < 1)
                store_m = 1;
            if (store_m > 64)
                store_m = 64;
            if (bitw_put(&w, (uint64_t)store_lz, 5) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            if (bitw_put(&w, (uint64_t)(store_m - 1), 6) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            uint64_t payload = (xor >> (unsigned)store_tz) & ((store_m >= 64) ? ~0ull : ((1ull << store_m) - 1ull));
            if (bitw_put(&w, payload, store_m) != 0) {
                free(w.data);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            prev_lz = store_lz;
            prev_tz = store_tz;
            have_window = 1;
        }
    }
    *out_len = bitw_finish(&w);
    *out = w.data;
    return 0;
}

static int decompress_gorilla_f64(const unsigned char *src, size_t src_len, unsigned char **out,
                                  size_t *out_len, char *err, size_t err_cap) {
    if (src_len < 16) {
        set_err(err, err_cap, "codec: truncated gorilla header");
        return -1;
    }
    if (get_u32_le(src) != CODEC_MAGIC_GOR || src[4] != 1) {
        set_err(err, err_cap, "codec: bad gorilla magic");
        return -1;
    }
    uint64_t n64 = get_u64_le(src + 8);
    if (n64 > CODEC_MAX_ELEMS || n64 > SHAKTI_CODEC_MAX_PLAIN / 8u) {
        set_err(err, err_cap, "codec: gorilla too large");
        return -1;
    }
    /* Body: first sample 64 bits + ≥1 bit per later sample → ≤ 8*(src_len-16) extras. */
    size_t max_n = 1u + (src_len - 16u) * 8u;
    if (n64 > max_n) {
        set_err(err, err_cap, "codec: truncated gorilla body");
        return -1;
    }
    size_t n = (size_t)n64;
    unsigned char *buf = malloc(n ? n * 8u : 1);
    if (!buf) {
        set_err(err, err_cap, "decompress: out of memory");
        return -1;
    }
    if (n == 0) {
        *out = buf;
        *out_len = 0;
        return 0;
    }
    BitR r = {.data = src, .len = src_len, .byte_i = 16, .bit_i = 0};
    uint64_t *bits = (uint64_t *)(void *)buf;
    uint64_t first = 0;
    if (bitr_get(&r, 64, &first) != 0) {
        free(buf);
        set_err(err, err_cap, "codec: truncated gorilla body");
        return -1;
    }
    bits[0] = first;
    int prev_lz = 0, prev_tz = 0;
    int have_window = 0;
    uint64_t prev = first;
    for (size_t i = 1; i < n; i++) {
        uint64_t ctrl = 0;
        if (bitr_get(&r, 1, &ctrl) != 0) {
            free(buf);
            set_err(err, err_cap, "codec: truncated gorilla body");
            return -1;
        }
        if (ctrl == 0) {
            bits[i] = prev;
            continue;
        }
        uint64_t reuse = 0;
        if (bitr_get(&r, 1, &reuse) != 0) {
            free(buf);
            set_err(err, err_cap, "codec: truncated gorilla body");
            return -1;
        }
        int lz, tz, meaningful;
        if (reuse == 0) {
            if (!have_window) {
                free(buf);
                set_err(err, err_cap, "codec: gorilla window missing");
                return -1;
            }
            lz = prev_lz;
            tz = prev_tz;
            meaningful = 64 - lz - tz;
        } else {
            uint64_t lz5 = 0, m6 = 0;
            if (bitr_get(&r, 5, &lz5) != 0 || bitr_get(&r, 6, &m6) != 0) {
                free(buf);
                set_err(err, err_cap, "codec: truncated gorilla body");
                return -1;
            }
            lz = (int)lz5;
            meaningful = (int)m6 + 1;
            tz = 64 - lz - meaningful;
            if (tz < 0) {
                free(buf);
                set_err(err, err_cap, "codec: bad gorilla window");
                return -1;
            }
            prev_lz = lz;
            prev_tz = tz;
            have_window = 1;
        }
        uint64_t payload = 0;
        if (bitr_get(&r, meaningful, &payload) != 0) {
            free(buf);
            set_err(err, err_cap, "codec: truncated gorilla body");
            return -1;
        }
        uint64_t xor = payload << (unsigned)tz;
        prev = prev ^ xor;
        bits[i] = prev;
    }
    *out = buf;
    *out_len = n * 8u;
    return 0;
}

static void split_ms(int64_t x, int64_t *days, int64_t *tod) {
    int64_t q = x / MS_PER_DAY;
    int64_t r = x % MS_PER_DAY;
    if (r < 0) {
        r += MS_PER_DAY;
        q--;
    }
    *days = q;
    *tod = r;
}

static int scale_days_to_ms(int64_t *x, size_t n, char *err, size_t err_cap) {
    int64_t lim = INT64_MAX / MS_PER_DAY;
    for (size_t i = 0; i < n; i++) {
        if (x[i] > lim || x[i] < -lim) {
            set_err(err, err_cap, "codec: date overflow");
            return -1;
        }
        x[i] *= MS_PER_DAY;
    }
    return 0;
}

static int write_delta_body(BitW *w, const int64_t *x, size_t n, char *err, size_t err_cap) {
    if (n <= 1)
        return 0;
    int64_t prev_delta = 0;
    size_t i = 1;
    while (i < n) {
        size_t bend = i + CODEC_BLOCK_N;
        if (bend > n)
            bend = n;
        size_t bn = bend - i;
        uint64_t *zz = calloc(bn, sizeof(uint64_t));
        if (!zz) {
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        int maxw = 0;
        int all_zero = 1;
        for (size_t k = 0; k < bn; k++) {
            size_t idx = i + k;
            int64_t d = x[idx] - x[idx - 1];
            int64_t resid = (idx == 1) ? d : (d - prev_delta);
            prev_delta = d;
            zz[k] = zigzag_encode(resid);
            if (zz[k])
                all_zero = 0;
            int wbits = bit_width_u64(zz[k]);
            if (wbits > maxw)
                maxw = wbits;
        }
        if (all_zero) {
            if (bitw_put(w, 0xFF, 8) != 0) {
                free(zz);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
        } else {
            if (maxw == 0)
                maxw = 1;
            if (maxw > 64)
                maxw = 64;
            if (bitw_put(w, (uint64_t)maxw, 8) != 0) {
                free(zz);
                set_err(err, err_cap, "compress: out of memory");
                return -1;
            }
            for (size_t k = 0; k < bn; k++) {
                if (bitw_put(w, zz[k], maxw) != 0) {
                    free(zz);
                    set_err(err, err_cap, "compress: out of memory");
                    return -1;
                }
            }
        }
        free(zz);
        i = bend;
    }
    return 0;
}

static int read_delta_body(BitR *r, int64_t *x, size_t n, uint16_t block_n, char *err,
                           size_t err_cap) {
    if (n <= 1)
        return 0;
    int64_t prev_delta = 0;
    size_t i = 1;
    while (i < n) {
        size_t bend = i + (size_t)block_n;
        if (bend > n)
            bend = n;
        size_t bn = bend - i;
        uint64_t tag = 0;
        if (bitr_get(r, 8, &tag) != 0) {
            set_err(err, err_cap, "codec: truncated residual body");
            return -1;
        }
        if (tag == 0xFF) {
            for (size_t k = 0; k < bn; k++) {
                size_t idx = i + k;
                int64_t resid = 0;
                int64_t d = (idx == 1) ? resid : (prev_delta + resid);
                x[idx] = x[idx - 1] + d;
                prev_delta = d;
            }
        } else {
            int maxw = (int)tag;
            if (maxw < 1 || maxw > 64) {
                set_err(err, err_cap, "codec: bad residual bit width");
                return -1;
            }
            for (size_t k = 0; k < bn; k++) {
                uint64_t zz = 0;
                if (bitr_get(r, maxw, &zz) != 0) {
                    set_err(err, err_cap, "codec: truncated residual body");
                    return -1;
                }
                int64_t resid = zigzag_decode(zz);
                size_t idx = i + k;
                int64_t d = (idx == 1) ? resid : (prev_delta + resid);
                x[idx] = x[idx - 1] + d;
                prev_delta = d;
            }
        }
        i = bend;
    }
    return 0;
}

static int compress_date(const unsigned char *src, size_t src_len, unsigned char **out,
                         size_t *out_len, char *err, size_t err_cap) {
    if (src_len % 8u != 0) {
        set_err(err, err_cap, "codec: date requires i64 byte length");
        return -1;
    }
    size_t n = src_len / 8u;
    int64_t *days = NULL;
    if (n) {
        days = malloc(n * sizeof(int64_t));
        if (!days) {
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        const int64_t *x = (const int64_t *)(const void *)src;
        for (size_t i = 0; i < n; i++) {
            int64_t d, tod;
            split_ms(x[i], &d, &tod);
            if (tod != 0) {
                free(days);
                set_err(err, err_cap, "codec: date requires UTC midnight-ms");
                return -1;
            }
            days[i] = d;
        }
    }
    int rc = compress_i64_residual(SHAKTI_CODEC_DATE,
                                   days ? (const unsigned char *)(const void *)days
                                        : (const unsigned char *)"",
                                   n * 8u, out, out_len, err, err_cap);
    free(days);
    return rc;
}

static int decompress_date(const unsigned char *src, size_t src_len, unsigned char **out,
                           size_t *out_len, char *err, size_t err_cap) {
    if (decompress_i64_residual(SHAKTI_CODEC_DATE, src, src_len, out, out_len, err, err_cap) != 0)
        return -1;
    size_t n = *out_len / 8u;
    int64_t *x = (int64_t *)(void *)*out;
    if (scale_days_to_ms(x, n, err, err_cap) != 0) {
        free(*out);
        *out = NULL;
        *out_len = 0;
        return -1;
    }
    return 0;
}

static int compress_time(const unsigned char *src, size_t src_len, unsigned char **out,
                         size_t *out_len, char *err, size_t err_cap) {
    if (src_len % 8u != 0) {
        set_err(err, err_cap, "codec: time requires i64 byte length");
        return -1;
    }
    size_t n = src_len / 8u;
    if (n > CODEC_MAX_ELEMS) {
        set_err(err, err_cap, "codec: too many samples");
        return -1;
    }
    const int64_t *x = (const int64_t *)(const void *)src;
    for (size_t i = 0; i < n; i++) {
        if (x[i] < 0 || x[i] >= MS_PER_DAY) {
            set_err(err, err_cap, "codec: time requires ms-in-day [0,86400000)");
            return -1;
        }
    }
    BitW w = {0};
    if (bitw_ensure(&w, 16 + (n * TOD_BITS + 7) / 8 + 8) != 0) {
        set_err(err, err_cap, "compress: out of memory");
        return -1;
    }
    put_u32_le(w.data + 0, CODEC_MAGIC_TIME);
    w.data[4] = 1;
    w.data[5] = 0;
    w.data[6] = 0;
    w.data[7] = 0;
    put_u64_le(w.data + 8, (uint64_t)n);
    w.byte_i = 16;
    w.bit_i = 0;
    for (size_t i = 0; i < n; i++) {
        if (bitw_put(&w, (uint64_t)x[i], TOD_BITS) != 0) {
            free(w.data);
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
    }
    *out_len = bitw_finish(&w);
    *out = w.data;
    return 0;
}

static int decompress_time(const unsigned char *src, size_t src_len, unsigned char **out,
                           size_t *out_len, char *err, size_t err_cap) {
    if (src_len < 16) {
        set_err(err, err_cap, "codec: truncated time header");
        return -1;
    }
    if (get_u32_le(src) != CODEC_MAGIC_TIME || src[4] != 1) {
        set_err(err, err_cap, "codec: bad time magic");
        return -1;
    }
    uint64_t n64 = get_u64_le(src + 8);
    if (n64 > CODEC_MAX_ELEMS || n64 > SHAKTI_CODEC_MAX_PLAIN / 8u) {
        set_err(err, err_cap, "codec: time too large");
        return -1;
    }
    size_t n = (size_t)n64;
    size_t need = 16u + (n * (size_t)TOD_BITS + 7u) / 8u;
    if (src_len < need) {
        set_err(err, err_cap, "codec: truncated time body");
        return -1;
    }
    unsigned char *buf = malloc(n ? n * 8u : 1);
    if (!buf) {
        set_err(err, err_cap, "decompress: out of memory");
        return -1;
    }
    int64_t *x = (int64_t *)(void *)buf;
    BitR r = {.data = src, .len = src_len, .byte_i = 16, .bit_i = 0};
    for (size_t i = 0; i < n; i++) {
        uint64_t v = 0;
        if (bitr_get(&r, TOD_BITS, &v) != 0 || v >= (uint64_t)MS_PER_DAY) {
            free(buf);
            set_err(err, err_cap, "codec: bad time sample");
            return -1;
        }
        x[i] = (int64_t)v;
    }
    *out = buf;
    *out_len = n * 8u;
    return 0;
}

static int compress_datetime(const unsigned char *src, size_t src_len, unsigned char **out,
                             size_t *out_len, char *err, size_t err_cap) {
    if (src_len % 8u != 0) {
        set_err(err, err_cap, "codec: datetime requires i64 byte length");
        return -1;
    }
    size_t n = src_len / 8u;
    if (n > CODEC_MAX_ELEMS) {
        set_err(err, err_cap, "codec: too many samples");
        return -1;
    }
    const int64_t *x = (const int64_t *)(const void *)src;
    int64_t *days = NULL;
    int64_t *tod = NULL;
    if (n) {
        days = malloc(n * sizeof(int64_t));
        tod = malloc(n * sizeof(int64_t));
        if (!days || !tod) {
            free(days);
            free(tod);
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        for (size_t i = 0; i < n; i++)
            split_ms(x[i], &days[i], &tod[i]);
    }
    BitW w = {0};
    if (bitw_ensure(&w, 24 + n * 9 + (n * TOD_BITS + 7) / 8 + 64) != 0) {
        free(days);
        free(tod);
        set_err(err, err_cap, "compress: out of memory");
        return -1;
    }
    put_u32_le(w.data + 0, CODEC_MAGIC_DT);
    w.data[4] = 1;
    w.data[5] = 0;
    w.data[6] = (unsigned char)(CODEC_BLOCK_N & 0xff);
    w.data[7] = (unsigned char)((CODEC_BLOCK_N >> 8) & 0xff);
    put_u64_le(w.data + 8, (uint64_t)n);
    put_u64_le(w.data + 16, (uint64_t)(n ? x[0] : 0));
    w.byte_i = 24;
    w.bit_i = 0;
    if (write_delta_body(&w, days ? days : (const int64_t *)(const void *)"", n, err, err_cap) != 0) {
        free(w.data);
        free(days);
        free(tod);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (bitw_put(&w, (uint64_t)tod[i], TOD_BITS) != 0) {
            free(w.data);
            free(days);
            free(tod);
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
    }
    free(days);
    free(tod);
    *out_len = bitw_finish(&w);
    *out = w.data;
    return 0;
}

static int decompress_datetime(const unsigned char *src, size_t src_len, unsigned char **out,
                               size_t *out_len, char *err, size_t err_cap) {
    if (src_len < 24) {
        set_err(err, err_cap, "codec: truncated datetime header");
        return -1;
    }
    if (get_u32_le(src) != CODEC_MAGIC_DT || src[4] != 1) {
        set_err(err, err_cap, "codec: bad datetime magic");
        return -1;
    }
    uint16_t block_n = (uint16_t)src[6] | ((uint16_t)src[7] << 8);
    if (block_n == 0 || block_n > 4096) {
        set_err(err, err_cap, "codec: bad block size");
        return -1;
    }
    uint64_t n64 = get_u64_le(src + 8);
    if (n64 > CODEC_MAX_ELEMS || n64 > SHAKTI_CODEC_MAX_PLAIN / 8u) {
        set_err(err, err_cap, "codec: datetime too large");
        return -1;
    }
    size_t n = (size_t)n64;
    /* Each delta block ≥1 tag byte; plus 27-bit tod per sample. */
    size_t max_n = 1u + (src_len > 24 ? (src_len - 24u) * (size_t)block_n : 0);
    if (n > max_n) {
        set_err(err, err_cap, "codec: truncated datetime body");
        return -1;
    }
    unsigned char *buf = malloc(n ? n * 8u : 1);
    if (!buf) {
        set_err(err, err_cap, "decompress: out of memory");
        return -1;
    }
    int64_t *x = (int64_t *)(void *)buf;
    if (n == 0) {
        *out = buf;
        *out_len = 0;
        return 0;
    }
    int64_t x0 = (int64_t)get_u64_le(src + 16);
    int64_t d0, t0;
    split_ms(x0, &d0, &t0);
    int64_t *days = malloc(n * sizeof(int64_t));
    if (!days) {
        free(buf);
        set_err(err, err_cap, "decompress: out of memory");
        return -1;
    }
    days[0] = d0;
    BitR r = {.data = src, .len = src_len, .byte_i = 24, .bit_i = 0};
    if (read_delta_body(&r, days, n, block_n, err, err_cap) != 0) {
        free(days);
        free(buf);
        return -1;
    }
    int64_t lim = INT64_MAX / MS_PER_DAY;
    for (size_t i = 0; i < n; i++) {
        uint64_t tod = 0;
        if (bitr_get(&r, TOD_BITS, &tod) != 0 || tod >= (uint64_t)MS_PER_DAY) {
            free(days);
            free(buf);
            set_err(err, err_cap, "codec: bad datetime time-of-day");
            return -1;
        }
        if (days[i] > lim || days[i] < -lim) {
            free(days);
            free(buf);
            set_err(err, err_cap, "codec: datetime overflow");
            return -1;
        }
        x[i] = days[i] * MS_PER_DAY + (int64_t)tod;
    }
    free(days);
    *out = buf;
    *out_len = n * 8u;
    return 0;
}

int shakti_compress(int codec, int level, const unsigned char *src, size_t src_len,
                    unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!out || !out_len) {
        set_err(err, err_cap, "compress: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    if (!src && src_len) {
        set_err(err, err_cap, "compress: bad args");
        return -1;
    }
    if (codec == SHAKTI_CODEC_NONE) {
        unsigned char *buf = malloc(src_len ? src_len : 1);
        if (!buf) {
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        if (src_len)
            memcpy(buf, src, src_len);
        *out = buf;
        *out_len = src_len;
        return 0;
    }
    if (codec == SHAKTI_CODEC_DELTA_I64 || codec == SHAKTI_CODEC_FIRE_I64)
        return compress_i64_residual(codec, src ? src : (const unsigned char *)"", src_len, out, out_len,
                                     err, err_cap);
    if (codec == SHAKTI_CODEC_DATE)
        return compress_date(src ? src : (const unsigned char *)"", src_len, out, out_len, err, err_cap);
    if (codec == SHAKTI_CODEC_TIME)
        return compress_time(src ? src : (const unsigned char *)"", src_len, out, out_len, err, err_cap);
    if (codec == SHAKTI_CODEC_DATETIME)
        return compress_datetime(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                                 err_cap);
    if (codec == SHAKTI_CODEC_GORILLA_F64)
        return compress_gorilla_f64(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                                    err_cap);
    if (!shakti_codec_available(codec)) {
        set_err(err, err_cap,
                codec == SHAKTI_CODEC_ZSTD ? "codec: zstd not built"
                                          : "codec: snappy not built");
        return -1;
    }
#if defined(SHAKTI_HAVE_ZSTD)
    if (codec == SHAKTI_CODEC_ZSTD) {
        if (level == 0)
            level = 3;
        if (level < 1)
            level = 1;
        if (level > 22)
            level = 22;
        size_t bound = ZSTD_compressBound(src_len);
        if (ZSTD_isError(bound)) {
            set_err(err, err_cap, ZSTD_getErrorName(bound));
            return -1;
        }
        unsigned char *buf = malloc(bound ? bound : 1);
        if (!buf) {
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        size_t nn = ZSTD_compress(buf, bound, src ? src : (const unsigned char *)"", src_len, level);
        if (ZSTD_isError(nn)) {
            free(buf);
            set_err(err, err_cap, ZSTD_getErrorName(nn));
            return -1;
        }
        *out = buf;
        *out_len = nn;
        return 0;
    }
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
    if (codec == SHAKTI_CODEC_SNAPPY) {
        size_t bound = snappy_max_compressed_length(src_len);
        unsigned char *buf = malloc(bound ? bound : 1);
        if (!buf) {
            set_err(err, err_cap, "compress: out of memory");
            return -1;
        }
        size_t nn = bound;
        int snap_rc = (int)snappy_compress((const char *)(src ? src : (const unsigned char *)""),
                                           src_len, (char *)buf, &nn);
        if (snap_rc != (int)SNAPPY_OK) {
            free(buf);
            set_err(err, err_cap, "snappy compress failed");
            return -1;
        }
        *out = buf;
        *out_len = nn;
        return 0;
    }
#endif
    set_err(err, err_cap, "compress: unknown codec");
    return -1;
}

int shakti_decompress(int codec, const unsigned char *src, size_t src_len,
                      unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    return shakti_decompress_max(codec, src, src_len, out, out_len, (size_t)SHAKTI_CODEC_MAX_PLAIN,
                                 err, err_cap);
}

int shakti_decompress_max(int codec, const unsigned char *src, size_t src_len,
                          unsigned char **out, size_t *out_len, size_t max_plain,
                          char *err, size_t err_cap) {
    if (!out || !out_len) {
        set_err(err, err_cap, "decompress: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    if (max_plain == 0) {
        set_err(err, err_cap, "decompress: max_plain=0");
        return -1;
    }
    if (max_plain > (size_t)SHAKTI_CODEC_MAX_PLAIN)
        max_plain = (size_t)SHAKTI_CODEC_MAX_PLAIN;
    if (!src && src_len) {
        set_err(err, err_cap, "decompress: bad args");
        return -1;
    }
    if (codec == SHAKTI_CODEC_NONE) {
        if (src_len > max_plain) {
            set_err(err, err_cap, "decompress: payload too large");
            return -1;
        }
        unsigned char *buf = malloc(src_len ? src_len : 1);
        if (!buf) {
            set_err(err, err_cap, "decompress: out of memory");
            return -1;
        }
        if (src_len)
            memcpy(buf, src, src_len);
        *out = buf;
        *out_len = src_len;
        return 0;
    }
    if (codec == SHAKTI_CODEC_DELTA_I64 || codec == SHAKTI_CODEC_FIRE_I64)
        return decompress_i64_residual(codec, src ? src : (const unsigned char *)"", src_len, out,
                                       out_len, err, err_cap);
    if (codec == SHAKTI_CODEC_DATE)
        return decompress_date(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                               err_cap);
    if (codec == SHAKTI_CODEC_TIME)
        return decompress_time(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                               err_cap);
    if (codec == SHAKTI_CODEC_DATETIME)
        return decompress_datetime(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                                   err_cap);
    if (codec == SHAKTI_CODEC_GORILLA_F64)
        return decompress_gorilla_f64(src ? src : (const unsigned char *)"", src_len, out, out_len, err,
                                      err_cap);
    if (!shakti_codec_available(codec)) {
        set_err(err, err_cap,
                codec == SHAKTI_CODEC_ZSTD ? "codec: zstd not built"
                                          : "codec: snappy not built");
        return -1;
    }
#if defined(SHAKTI_HAVE_ZSTD)
    if (codec == SHAKTI_CODEC_ZSTD) {
        unsigned long long need = ZSTD_getFrameContentSize(src, src_len);
        if (need == ZSTD_CONTENTSIZE_ERROR) {
            set_err(err, err_cap, "zstd: invalid frame");
            return -1;
        }
        if (need == ZSTD_CONTENTSIZE_UNKNOWN) {
            size_t cap;
            if (src_len == 0)
                cap = 64;
            else if (src_len > max_plain / 4)
                cap = max_plain;
            else {
                cap = src_len * 4;
                if (cap < 64)
                    cap = 64;
            }
            if (cap > max_plain)
                cap = max_plain;
            for (int attempt = 0; attempt < 8; attempt++) {
                unsigned char *buf = malloc(cap);
                if (!buf) {
                    set_err(err, err_cap, "decompress: out of memory");
                    return -1;
                }
                size_t nn = ZSTD_decompress(buf, cap, src, src_len);
                if (!ZSTD_isError(nn)) {
                    if (nn > max_plain) {
                        free(buf);
                        set_err(err, err_cap, "zstd: payload too large");
                        return -1;
                    }
                    *out = buf;
                    *out_len = nn;
                    return 0;
                }
                free(buf);
                if (ZSTD_getErrorCode(nn) != ZSTD_error_dstSize_tooSmall) {
                    set_err(err, err_cap, ZSTD_getErrorName(nn));
                    return -1;
                }
                if (cap >= max_plain) {
                    set_err(err, err_cap, "zstd: payload too large");
                    return -1;
                }
                if (cap > (max_plain / 2))
                    cap = max_plain;
                else
                    cap *= 2;
            }
            set_err(err, err_cap, "zstd: payload too large");
            return -1;
        }
        if (need > max_plain) {
            set_err(err, err_cap, "zstd: payload too large");
            return -1;
        }
        unsigned char *buf = malloc(need ? (size_t)need : 1);
        if (!buf) {
            set_err(err, err_cap, "decompress: out of memory");
            return -1;
        }
        size_t nn = ZSTD_decompress(buf, (size_t)need, src, src_len);
        if (ZSTD_isError(nn)) {
            free(buf);
            set_err(err, err_cap, ZSTD_getErrorName(nn));
            return -1;
        }
        *out = buf;
        *out_len = nn;
        return 0;
    }
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
    if (codec == SHAKTI_CODEC_SNAPPY) {
        size_t need = 0;
        int snap_rc = (int)snappy_uncompressed_length(
            (const char *)(src ? src : (const unsigned char *)""), src_len, &need);
        if (snap_rc != (int)SNAPPY_OK) {
            set_err(err, err_cap, "snappy: invalid input");
            return -1;
        }
        if (need > max_plain) {
            set_err(err, err_cap, "snappy: payload too large");
            return -1;
        }
        unsigned char *buf = malloc(need ? need : 1);
        if (!buf) {
            set_err(err, err_cap, "decompress: out of memory");
            return -1;
        }
        size_t nn = need;
        snap_rc = (int)snappy_uncompress((const char *)(src ? src : (const unsigned char *)""),
                                         src_len, (char *)buf, &nn);
        if (snap_rc != (int)SNAPPY_OK) {
            free(buf);
            set_err(err, err_cap, "snappy decompress failed");
            return -1;
        }
        *out = buf;
        *out_len = nn;
        return 0;
    }
#endif
    set_err(err, err_cap, "decompress: unknown codec");
    return -1;
}

/* ---- list[char] / typed-lane helpers ---- */
static int u8vec_bytes(V *v, const unsigned char **p, size_t *n, char *err, size_t err_cap) {
    if (!v || v->t != T_CVEC) {
        set_err(err, err_cap, "compress: expected list[char]");
        return -1;
    }
    if (v->n < 0) {
        set_err(err, err_cap, "compress: invalid list[char] length");
        return -1;
    }
    *n = (size_t)v->n;
    *p = (*n && v->B) ? v->B : (const unsigned char *)"";
    return 0;
}

static int codec_src_bytes(V *v, int codec, const unsigned char **p, size_t *n, char *err,
                           size_t err_cap) {
    if (shakti_codec_is_residual(codec)) {
        if (codec == SHAKTI_CODEC_GORILLA_F64) {
            if (v && v->t == T_FVEC) {
                if (v->n < 0) {
                    set_err(err, err_cap, "compress: invalid length");
                    return -1;
                }
                *n = (size_t)v->n * 8u;
                *p = (*n && v->F) ? (const unsigned char *)(const void *)v->F : (const unsigned char *)"";
                return 0;
            }
        } else {
            if (v && v->t == T_IVEC) {
                if (v->n < 0) {
                    set_err(err, err_cap, "compress: invalid length");
                    return -1;
                }
                *n = (size_t)v->n * 8u;
                *p = (*n && v->J) ? (const unsigned char *)(const void *)v->J : (const unsigned char *)"";
                return 0;
            }
        }
    }
    return u8vec_bytes(v, p, n, err, err_cap);
}

static V *u8vec_from_bytes(const unsigned char *p, size_t n) {
    if (n > (size_t)INT64_MAX)
        return v_err("codec: output too large");
    V *v = v_cvec((int64_t)n);
    if (n && p && v->B)
        memcpy(v->B, p, n);
    return v;
}

static V *typed_from_residual(int codec, const unsigned char *p, size_t n) {
    if (n % 8u != 0)
        return v_err("codec: residual length not multiple of 8");
    int64_t ne = (int64_t)(n / 8u);
    if (codec == SHAKTI_CODEC_GORILLA_F64) {
        V *v = v_fvec(ne);
        if (n && p && v->F)
            memcpy(v->F, p, n);
        return v;
    }
    V *v = v_ivec(ne);
    if (n && p && v->J)
        memcpy(v->J, p, n);
    return v;
}

V *bi_compress(V **a, int n) {
    if (n < 2 || !a[0] || !a[1] || a[1]->t != T_STR)
        return v_err("compress(list[char]|list[int]|list[float], codec[, level])");
    int outer = SHAKTI_CODEC_NONE;
    int codec = shakti_codec_parse2(a[1]->s, &outer);
    if (codec < 0)
        return v_err("compress: unknown codec");
    if (codec == SHAKTI_CODEC_NONE)
        return v_err("compress: codec required");
    int level = 3;
    if (n > 2) {
        if (a[2]->t != T_INT)
            return v_err("compress: level must be int");
        level = (int)a[2]->j;
    }
    const unsigned char *src = NULL;
    size_t src_len = 0;
    char err[256];
    if (codec_src_bytes(a[0], codec, &src, &src_len, err, sizeof err) != 0)
        return v_err(err);
    unsigned char *mid = NULL;
    size_t mid_len = 0;
    if (shakti_compress(codec, level, src, src_len, &mid, &mid_len, err, sizeof err) != 0)
        return v_err(err);
    if (outer != SHAKTI_CODEC_NONE) {
        unsigned char *out = NULL;
        size_t out_len = 0;
        int rc = shakti_compress(outer, level, mid, mid_len, &out, &out_len, err, sizeof err);
        free(mid);
        if (rc != 0)
            return v_err(err);
        V *r = u8vec_from_bytes(out, out_len);
        free(out);
        return r;
    }
    V *r = u8vec_from_bytes(mid, mid_len);
    free(mid);
    return r;
}

V *bi_decompress(V **a, int n) {
    if (n < 2 || !a[0] || !a[1] || a[1]->t != T_STR)
        return v_err("decompress(list[char], codec)");
    int outer = SHAKTI_CODEC_NONE;
    int codec = shakti_codec_parse2(a[1]->s, &outer);
    if (codec < 0 || codec == SHAKTI_CODEC_NONE)
        return v_err("decompress: unknown codec");
    const unsigned char *src = NULL;
    size_t src_len = 0;
    char err[256];
    if (u8vec_bytes(a[0], &src, &src_len, err, sizeof err) != 0) {
        set_err(err, sizeof err, "decompress: expected list[char]");
        return v_err(err);
    }
    unsigned char *cur = NULL;
    size_t cur_len = 0;
    const unsigned char *pay = src;
    size_t pay_len = src_len;
    if (outer != SHAKTI_CODEC_NONE) {
        if (shakti_decompress(outer, src, src_len, &cur, &cur_len, err, sizeof err) != 0)
            return v_err(err);
        pay = cur;
        pay_len = cur_len;
    }
    unsigned char *out = NULL;
    size_t out_len = 0;
    int rc = shakti_decompress(codec, pay, pay_len, &out, &out_len, err, sizeof err);
    free(cur);
    if (rc != 0)
        return v_err(err);
    if (shakti_codec_is_residual(codec)) {
        V *r = typed_from_residual(codec, out, out_len);
        free(out);
        return r;
    }
    V *r = u8vec_from_bytes(out, out_len);
    free(out);
    return r;
}
