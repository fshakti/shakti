/*
 * IE file store format: portable little-endian V serialization.
 */
#include "iefs_format.h"
#include "iefs_io.h"
#include "iefs_map.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SHAKTI_HAVE_ZSTD)
#include <zstd.h>
#endif

static char g_iefs_err[512];

void iefs_set_last_error(const char *msg) {
    if (!msg) {
        g_iefs_err[0] = 0;
        return;
    }
    snprintf(g_iefs_err, sizeof g_iefs_err, "%s", msg);
}

const char *iefs_last_error(void) { return g_iefs_err[0] ? g_iefs_err : ""; }

static void set_err(char *err, size_t err_cap, const char *msg) {
    iefs_set_last_error(msg);
    if (err && err_cap)
        snprintf(err, err_cap, "%s", msg ? msg : "iefs error");
}

/* IEEE CRC32 (poly 0xEDB88320), table-driven. */
static uint32_t crc32_table[256];
static int crc32_ready;

static void crc32_init(void) {
    if (crc32_ready)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n) {
    crc32_init();
    crc = ~crc;
    for (size_t i = 0; i < n; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

static uint32_t crc32_buf(const unsigned char *p, size_t n) { return crc32_update(0, p, n); }

/* ---- little-endian helpers ---- */
static void put_u16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}
static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}
static void put_u64(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++)
        p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}
static void put_f64(unsigned char *p, double f) {
    uint64_t u;
    memcpy(&u, &f, 8);
    put_u64(p, u);
}

static uint16_t get_u16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_u64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}
static int64_t get_i64(const unsigned char *p) { return (int64_t)get_u64(p); }
static double get_f64(const unsigned char *p) {
    uint64_t u = get_u64(p);
    double f;
    memcpy(&f, &u, 8);
    return f;
}

/* ---- growable buffer ---- */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} IefsBuf;

static int buf_reserve(IefsBuf *b, size_t need) {
    if (need <= b->cap)
        return 0;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2))
            return -1;
        ncap *= 2;
    }
    unsigned char *nd = realloc(b->data, ncap);
    if (!nd)
        return -1;
    b->data = nd;
    b->cap = ncap;
    return 0;
}

static int buf_append(IefsBuf *b, const void *p, size_t n) {
    if (buf_reserve(b, b->len + n) != 0)
        return -1;
    if (n)
        memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

static int buf_putc(IefsBuf *b, unsigned char c) { return buf_append(b, &c, 1); }

static int buf_put_u64(IefsBuf *b, uint64_t v) {
    unsigned char tmp[8];
    put_u64(tmp, v);
    return buf_append(b, tmp, 8);
}

static int buf_put_i64(IefsBuf *b, int64_t v) { return buf_put_u64(b, (uint64_t)v); }

static int buf_put_f64(IefsBuf *b, double f) {
    unsigned char tmp[8];
    put_f64(tmp, f);
    return buf_append(b, tmp, 8);
}

static int encode_value(IefsBuf *b, V *v);

static int encode_value(IefsBuf *b, V *v) {
    if (!v)
        return buf_putc(b, (unsigned char)T_NIL);
    switch (v->t) {
    case T_NIL:
        return buf_putc(b, (unsigned char)T_NIL);
    case T_BOOL:
        if (buf_putc(b, (unsigned char)T_BOOL) != 0)
            return -1;
        return buf_putc(b, (unsigned char)(v->b ? 1 : 0));
    case T_INT:
        if (buf_putc(b, (unsigned char)T_INT) != 0)
            return -1;
        return buf_put_i64(b, v->j);
    case T_FLOAT:
        if (buf_putc(b, (unsigned char)T_FLOAT) != 0)
            return -1;
        return buf_put_f64(b, v->f);
    case T_STR: {
        if (!v->s)
            return -1;
        size_t slen = strlen(v->s);
        if (slen > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_STR) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)slen) != 0)
            return -1;
        return buf_append(b, v->s, slen);
    }
    case T_DATE:
        if (buf_putc(b, (unsigned char)T_DATE) != 0)
            return -1;
        return buf_put_i64(b, v->j);
    case T_TIME:
        if (buf_putc(b, (unsigned char)T_TIME) != 0)
            return -1;
        return buf_put_i64(b, v->j);
    case T_DATETIME:
        if (buf_putc(b, (unsigned char)T_DATETIME) != 0)
            return -1;
        return buf_put_i64(b, v->j);
    case T_IVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_IVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        for (int64_t i = 0; i < v->n; i++)
            if (buf_put_i64(b, v->J[i]) != 0)
                return -1;
        return 0;
    }
    case T_FVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_FVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        for (int64_t i = 0; i < v->n; i++)
            if (buf_put_f64(b, v->F[i]) != 0)
                return -1;
        return 0;
    }
    case T_BVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_BVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        return buf_append(b, v->B, (size_t)v->n);
    }
    case T_IMAT:
    case T_FMAT:
    case T_BMAT: {
        int64_t cols = mat_cols(v);
        uint64_t cells = (uint64_t)v->n * (uint64_t)(cols > 0 ? cols : 0);
        if ((uint64_t)v->n > IEFS_MAX_ELEMS || (uint64_t)cols > IEFS_MAX_ELEMS || cells > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)v->t) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)cols) != 0)
            return -1;
        if (v->t == T_IMAT) {
            for (uint64_t i = 0; i < cells; i++)
                if (buf_put_i64(b, v->J[i]) != 0)
                    return -1;
        } else if (v->t == T_FMAT) {
            for (uint64_t i = 0; i < cells; i++)
                if (buf_put_f64(b, v->F[i]) != 0)
                    return -1;
        } else {
            if (buf_append(b, v->B, (size_t)cells) != 0)
                return -1;
        }
        return 0;
    }
    case T_LIST: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_LIST) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        for (int64_t i = 0; i < v->n; i++)
            if (encode_value(b, v->L[i]) != 0)
                return -1;
        return 0;
    }
    case T_DICT:
    case T_TABLE:
        if (buf_putc(b, (unsigned char)v->t) != 0)
            return -1;
        if (encode_value(b, v->keys) != 0)
            return -1;
        return encode_value(b, v->vals);
    case T_ERR:
        set_err(NULL, 0, "iefs: cannot serialize error values");
        return -1;
    case T_FN:
        set_err(NULL, 0, "iefs: cannot serialize functions");
        return -1;
    case T_INPUT:
        set_err(NULL, 0, "iefs: cannot serialize input streams");
        return -1;
    default:
        set_err(NULL, 0, "iefs: unsupported type");
        return -1;
    }
}

int iefs_encode(V *v, unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!out || !out_len) {
        set_err(err, err_cap, "iefs_encode: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    IefsBuf payload = {0};
    if (encode_value(&payload, v) != 0) {
        free(payload.data);
        if (!g_iefs_err[0])
            set_err(err, err_cap, "iefs_encode: encode failed");
        else if (err && err_cap)
            snprintf(err, err_cap, "%s", g_iefs_err);
        return -1;
    }
    if (payload.len > IEFS_MAX_PAYLOAD) {
        free(payload.data);
        set_err(err, err_cap, "iefs_encode: payload too large");
        return -1;
    }
    uint32_t crc = crc32_buf(payload.data, payload.len);
    size_t total = IEFS_HEADER_SIZE + payload.len;
    unsigned char *file = malloc(total ? total : 1);
    if (!file) {
        free(payload.data);
        set_err(err, err_cap, "iefs_encode: out of memory");
        return -1;
    }
    memcpy(file, IEFS_MAGIC, 4);
    put_u16(file + 4, (uint16_t)IEFS_VERSION);
    put_u16(file + 6, 0); /* flags */
    put_u64(file + 8, (uint64_t)payload.len);
    put_u32(file + 16, crc);
    put_u32(file + 20, 0); /* reserved */
    if (payload.len)
        memcpy(file + IEFS_HEADER_SIZE, payload.data, payload.len);
    free(payload.data);
    *out = file;
    *out_len = total;
    iefs_set_last_error(NULL);
    return 0;
}

/* ---- decode ---- */
#define IEFS_MAX_DEPTH 64

typedef struct {
    const unsigned char *p;
    size_t n;
    size_t off;
    int depth;
    char err[256];
    IefsMapRegion *map_reg; /* non-NULL → alias contiguous payloads */
} IefsR;

static int need(IefsR *r, size_t nbytes) {
    if (r->off > r->n || nbytes > r->n - r->off) {
        snprintf(r->err, sizeof r->err, "iefs: truncated payload");
        return -1;
    }
    return 0;
}

static V *decode_value(IefsR *r);

/* which: 0=J, 1=F, 2=B; cols < 0 means vector. */
static V *alias_payload(IefsR *r, int t, int64_t n, int64_t cols, size_t nbytes, int which) {
    if (need(r, nbytes) != 0)
        return v_err(r->err);
    V *v;
    if (t == T_IVEC)
        v = v_ivec(n);
    else if (t == T_FVEC)
        v = v_fvec(n);
    else if (t == T_BVEC)
        v = v_bvec(n);
    else if (t == T_IMAT)
        v = v_imat(n, cols);
    else if (t == T_FMAT)
        v = v_fmat(n, cols);
    else if (t == T_BMAT)
        v = v_bmat(n, cols);
    else
        return v_err("iefs: alias type");
    /* Drop malloc'd payload; point into map when naturally aligned. */
    free(v->J); v->J = NULL;
    free(v->F); v->F = NULL;
    free(v->B); v->B = NULL;
    {
        const unsigned char *src = r->p + r->off;
        uintptr_t align = (which == 0) ? sizeof(int64_t)
                         : (which == 1) ? sizeof(double)
                         : 1;
        if (align > 1 && ((uintptr_t)src % align) != 0) {
            /* Misaligned mmap alias is UB on strict targets — copy instead. */
            if (which == 0) {
                v->J = (int64_t *)malloc(nbytes);
                if (!v->J) { v_free(v); return v_err("iefs: out of memory"); }
                memcpy(v->J, src, nbytes);
            } else if (which == 1) {
                v->F = (double *)malloc(nbytes);
                if (!v->F) { v_free(v); return v_err("iefs: out of memory"); }
                memcpy(v->F, src, nbytes);
            } else {
                v->B = (unsigned char *)malloc(nbytes);
                if (!v->B) { v_free(v); return v_err("iefs: out of memory"); }
                memcpy(v->B, src, nbytes);
            }
        } else {
            if (which == 0)
                v->J = (int64_t *)src;
            else if (which == 1)
                v->F = (double *)src;
            else
                v->B = (unsigned char *)src;
            iefs_v_set_map_alias(v, r->map_reg);
        }
    }
    r->off += nbytes;
    return v;
}

static V *decode_value(IefsR *r) {
    if (r->depth >= IEFS_MAX_DEPTH)
        return v_err("iefs: nesting too deep");
    if (need(r, 1) != 0)
        return v_err(r->err);
    unsigned char t = r->p[r->off++];
    switch (t) {
    case T_NIL:
        return v_nil();
    case T_BOOL: {
        if (need(r, 1) != 0)
            return v_err(r->err);
        return v_bool(r->p[r->off++] != 0);
    }
    case T_INT: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        int64_t j = get_i64(r->p + r->off);
        r->off += 8;
        return v_int(j);
    }
    case T_FLOAT: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        double f = get_f64(r->p + r->off);
        r->off += 8;
        return v_float(f);
    }
    case T_STR: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t slen = get_u64(r->p + r->off);
        r->off += 8;
        if (slen > IEFS_MAX_ELEMS) {
            snprintf(r->err, sizeof r->err, "iefs: string too large");
            return v_err(r->err);
        }
        if (need(r, (size_t)slen) != 0)
            return v_err(r->err);
        char *s = malloc((size_t)slen + 1);
        if (!s)
            return v_err("iefs: out of memory");
        if (slen)
            memcpy(s, r->p + r->off, (size_t)slen);
        s[slen] = 0;
        r->off += (size_t)slen;
        return v_str_take(s);
    }
    case T_DATE: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        int64_t j = get_i64(r->p + r->off);
        r->off += 8;
        return v_date(j);
    }
    case T_TIME: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        int64_t j = get_i64(r->p + r->off);
        r->off += 8;
        return v_time(j);
    }
    case T_DATETIME: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        int64_t j = get_i64(r->p + r->off);
        r->off += 8;
        return v_datetime(j);
    }
    case T_IVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: ivec too large");
        size_t nbytes = (size_t)n * 8;
        if (r->map_reg)
            return alias_payload(r, T_IVEC, (int64_t)n, -1, nbytes, 0);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_ivec((int64_t)n);
        for (uint64_t i = 0; i < n; i++) {
            v->J[i] = get_i64(r->p + r->off);
            r->off += 8;
        }
        return v;
    }
    case T_FVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: fvec too large");
        size_t nbytes = (size_t)n * 8;
        if (r->map_reg)
            return alias_payload(r, T_FVEC, (int64_t)n, -1, nbytes, 1);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_fvec((int64_t)n);
        for (uint64_t i = 0; i < n; i++) {
            v->F[i] = get_f64(r->p + r->off);
            r->off += 8;
        }
        return v;
    }
    case T_BVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: bvec too large");
        size_t nbytes = (size_t)n;
        if (r->map_reg)
            return alias_payload(r, T_BVEC, (int64_t)n, -1, nbytes, 2);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_bvec((int64_t)n);
        if (n)
            memcpy(v->B, r->p + r->off, nbytes);
        r->off += nbytes;
        return v;
    }
    case T_IMAT:
    case T_FMAT:
    case T_BMAT: {
        if (need(r, 16) != 0)
            return v_err(r->err);
        uint64_t rows = get_u64(r->p + r->off);
        uint64_t cols = get_u64(r->p + r->off + 8);
        r->off += 16;
        uint64_t cells = 0;
        if (__builtin_mul_overflow(rows, cols, &cells) ||
            rows > IEFS_MAX_ELEMS || cols > IEFS_MAX_ELEMS || cells > IEFS_MAX_ELEMS)
            return v_err("iefs: matrix too large");
        size_t esz = (t == T_BMAT) ? 1 : 8;
        size_t nbytes = (size_t)cells * esz;
        if (r->map_reg) {
            int which = (t == T_IMAT) ? 0 : (t == T_FMAT) ? 1 : 2;
            return alias_payload(r, (int)t, (int64_t)rows, (int64_t)cols, nbytes, which);
        }
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v;
        if (t == T_IMAT) {
            v = v_imat((int64_t)rows, (int64_t)cols);
            for (uint64_t i = 0; i < cells; i++) {
                v->J[i] = get_i64(r->p + r->off);
                r->off += 8;
            }
        } else if (t == T_FMAT) {
            v = v_fmat((int64_t)rows, (int64_t)cols);
            for (uint64_t i = 0; i < cells; i++) {
                v->F[i] = get_f64(r->p + r->off);
                r->off += 8;
            }
        } else {
            v = v_bmat((int64_t)rows, (int64_t)cols);
            if (cells)
                memcpy(v->B, r->p + r->off, (size_t)cells);
            r->off += (size_t)cells;
        }
        return v;
    }
    case T_LIST: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: list too large");
        r->depth++;
        V *v = v_list((int64_t)n);
        for (uint64_t i = 0; i < n; i++) {
            V *item = decode_value(r);
            if (item->t == T_ERR) {
                v_free(v);
                r->depth--;
                return item;
            }
            v->L[i] = item; /* transfer ownership */
        }
        r->depth--;
        return v;
    }
    case T_DICT:
    case T_TABLE: {
        r->depth++;
        V *keys = decode_value(r);
        if (keys->t == T_ERR) {
            r->depth--;
            return keys;
        }
        V *vals = decode_value(r);
        if (vals->t == T_ERR) {
            v_free(keys);
            r->depth--;
            return vals;
        }
        V *out = (t == T_DICT) ? v_dict(keys, vals) : v_table(keys, vals);
        v_free(keys);
        v_free(vals);
        r->depth--;
        return out;
    }
    default:
        snprintf(r->err, sizeof r->err, "iefs: unknown type tag %u", (unsigned)t);
        return v_err(r->err);
    }
}

/* ---- IEFS v3 (TOC + 2MiB extents; Isolde Basic / STAC day shards) ---- */

static int iefs_zstd_decompress(const unsigned char *src, size_t src_len,
                                unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    *out = NULL;
    *out_len = 0;
#if !defined(SHAKTI_HAVE_ZSTD)
    (void)src;
    (void)src_len;
    snprintf(err, err_cap, "iefs: zstd not built (rebuild with libzstd)");
    return -1;
#else
    unsigned long long need = ZSTD_getFrameContentSize(src, src_len);
    if (need == ZSTD_CONTENTSIZE_ERROR) {
        snprintf(err, err_cap, "iefs: zstd invalid frame");
        return -1;
    }
    size_t cap;
    if (need == ZSTD_CONTENTSIZE_UNKNOWN) {
        cap = src_len ? src_len * 4 : 64;
        if (cap > (size_t)IEFS_MAX_PAYLOAD)
            cap = (size_t)IEFS_MAX_PAYLOAD;
    } else {
        if (need > IEFS_MAX_PAYLOAD) {
            snprintf(err, err_cap, "iefs: zstd payload too large");
            return -1;
        }
        cap = (size_t)need;
    }
    unsigned char *buf = (unsigned char *)malloc(cap ? cap : 1);
    if (!buf) {
        snprintf(err, err_cap, "iefs: out of memory");
        return -1;
    }
    size_t nn = ZSTD_decompress(buf, cap, src, src_len);
    if (ZSTD_isError(nn)) {
        free(buf);
        snprintf(err, err_cap, "iefs: zstd decompress: %s", ZSTD_getErrorName(nn));
        return -1;
    }
    *out = buf;
    *out_len = nn;
    return 0;
#endif
}

/* Basic STAC columns are T_IVEC / T_FVEC with bits=64. */
static V *iefs_v3_import_vec(int type, int bits, uint64_t nelem, const unsigned char *p, size_t nbytes,
                             IefsMapRegion *reg, int alias_ok) {
    if (nelem > IEFS_MAX_ELEMS)
        return v_err("iefs: extent too large");
    if (bits != 0 && bits != 64)
        return v_errf("iefs: unsupported extent bits %d (need 64)", bits);
    size_t needb = (size_t)nelem * 8u;
    if (needb != nbytes)
        return v_err("iefs: extent length mismatch");
    if (type != T_IVEC && type != T_FVEC)
        return v_errf("iefs: unsupported extent type %d (need ivec/fvec)", type);
    if (alias_ok && reg) {
        V *v = (type == T_FVEC) ? v_fvec((int64_t)nelem) : v_ivec((int64_t)nelem);
        free(v->J);
        v->J = NULL;
        free(v->F);
        v->F = NULL;
        if (type == T_FVEC)
            v->F = (double *)(uintptr_t)p;
        else
            v->J = (int64_t *)(uintptr_t)p;
        iefs_v_set_map_alias(v, reg);
        return v;
    }
    V *v = (type == T_FVEC) ? v_fvec((int64_t)nelem) : v_ivec((int64_t)nelem);
    if (nelem && nbytes) {
        if (type == T_FVEC)
            memcpy(v->F, p, nbytes);
        else
            memcpy(v->J, p, nbytes);
    }
    return v;
}

static void iefs_v3_cleanup(V **cols, V **keys, uint32_t n) {
    for (uint32_t j = 0; j < n; j++) {
        if (cols && cols[j])
            v_free(cols[j]);
        if (keys && keys[j])
            v_free(keys[j]);
    }
    free(cols);
    free(keys);
}

static V *iefs_decode_v3(const unsigned char *buf, size_t len, IefsMapRegion *reg, int verify_crc) {
    uint64_t payload_len = get_u64(buf + 8);
    uint32_t expect_crc = get_u32(buf + 16);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *body = buf + IEFS_HEADER_SIZE;
    if (verify_crc) {
        uint32_t got = crc32_buf(body, (size_t)payload_len);
        if (got != expect_crc)
            return v_err("iefs: checksum mismatch");
    }
    if (payload_len < 8)
        return v_err("iefs: truncated v3 TOC");
    uint32_t n_ext = get_u32(body + 0);
    uint32_t names_len = get_u32(body + 4);
    if (n_ext > 1000000u)
        return v_err("iefs: too many extents");
    size_t toc_need = 8u + (size_t)n_ext * IEFS_V3_EXTENT_SIZE + (size_t)names_len;
    if (payload_len < toc_need)
        return v_err("iefs: truncated v3 TOC");
    const unsigned char *names = body + 8 + (size_t)n_ext * IEFS_V3_EXTENT_SIZE;
    V **cols = (V **)calloc(n_ext ? n_ext : 1, sizeof(V *));
    V **keys = (V **)calloc(n_ext ? n_ext : 1, sizeof(V *));
    if (!cols || !keys) {
        free(cols);
        free(keys);
        return v_err("iefs: out of memory");
    }
    int named = 0;
    for (uint32_t i = 0; i < n_ext; i++) {
        const unsigned char *er = body + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
        int type = er[0];
        int bits = er[1];
        int codec = er[2];
        int outer = er[3];
        uint64_t nelem = get_u64(er + 4);
        uint64_t file_off = get_u64(er + 20);
        uint64_t elen = get_u64(er + 28);
        uint32_t ecrc = get_u32(er + 36);
        uint32_t name_off = get_u32(er + 40);
        if (file_off < IEFS_HEADER_SIZE || elen > len || file_off > len - elen) {
            iefs_v3_cleanup(cols, keys, i);
            return v_err("iefs: bad extent offset");
        }
        const unsigned char *ep = buf + file_off;
        if (verify_crc && crc32_buf(ep, (size_t)elen) != ecrc) {
            iefs_v3_cleanup(cols, keys, i);
            return v_err("iefs: extent checksum mismatch");
        }
        unsigned char *plain = NULL;
        size_t plain_len = 0;
        const unsigned char *pay = ep;
        size_t pay_len = (size_t)elen;
        int alias_ok = (reg != NULL) && (codec == IEFS_CODEC_NONE) && (outer == IEFS_CODEC_NONE) &&
                       (type != (int)IEFS_EXT_TLV);
        if (outer != IEFS_CODEC_NONE) {
            iefs_v3_cleanup(cols, keys, i);
            return v_err("iefs: outer residual codecs not supported in Shakti v3 reader");
        }
        if (codec != IEFS_CODEC_NONE) {
            char err[256];
            if (codec != IEFS_CODEC_ZSTD) {
                iefs_v3_cleanup(cols, keys, i);
                return v_errf("iefs: unsupported extent codec %d (need none/zstd)", codec);
            }
            if (iefs_zstd_decompress(ep, (size_t)elen, &plain, &plain_len, err, sizeof err) != 0) {
                iefs_v3_cleanup(cols, keys, i);
                return v_err(err[0] ? err : "iefs: extent decompress failed");
            }
            pay = plain;
            pay_len = plain_len;
            alias_ok = 0;
        }
        V *col;
        if (type == (int)IEFS_EXT_TLV) {
            IefsR r = {.p = pay, .n = pay_len, .off = 0, .err = {0}, .map_reg = NULL};
            col = decode_value(&r);
            free(plain);
            if (!col || col->t == T_ERR) {
                iefs_v3_cleanup(cols, keys, i);
                return col ? col : v_err("iefs: TLV extent decode failed");
            }
        } else {
            col = iefs_v3_import_vec(type, bits ? bits : 64, nelem, pay, pay_len, reg, alias_ok);
            free(plain);
            if (!col || col->t == T_ERR) {
                iefs_v3_cleanup(cols, keys, i);
                return col ? col : v_err("iefs: extent import failed");
            }
        }
        cols[i] = col;
        if (name_off != 0xffffffffu && name_off < names_len) {
            size_t rem = (size_t)names_len - (size_t)name_off;
            int has_nul = 0;
            for (size_t k = 0; k < rem; k++) {
                if (names[name_off + k] == 0) {
                    has_nul = 1;
                    break;
                }
            }
            if (!has_nul) {
                iefs_v3_cleanup(cols, keys, i + 1);
                return v_err("iefs: column name not NUL-terminated");
            }
            keys[i] = v_str((char *)(names + name_off));
            named++;
        } else {
            keys[i] = NULL;
        }
    }
    if (n_ext == 1 && !named) {
        V *out = cols[0];
        free(cols);
        free(keys);
        return out;
    }
    V *klist = v_list((int64_t)n_ext);
    V *vlist = v_list((int64_t)n_ext);
    for (uint32_t i = 0; i < n_ext; i++) {
        klist->L[i] = keys[i] ? keys[i] : v_str("");
        vlist->L[i] = cols[i];
    }
    free(cols);
    free(keys);
    return v_table_own(klist, vlist);
}

V *iefs_decode(const unsigned char *buf, size_t len) {
    if (!buf || len < IEFS_HEADER_SIZE)
        return v_err("iefs: truncated header");
    if (memcmp(buf, IEFS_MAGIC, 4) != 0)
        return v_err("iefs: bad magic");
    uint16_t ver = get_u16(buf + 4);
    if (ver == 3)
        return iefs_decode_v3(buf, len, NULL, 1);
    if (ver != IEFS_VERSION)
        return v_errf("iefs: unsupported version %u", (unsigned)ver);
    uint64_t payload_len = get_u64(buf + 8);
    uint32_t expect_crc = get_u32(buf + 16);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *payload = buf + IEFS_HEADER_SIZE;
    uint32_t got_crc = crc32_buf(payload, (size_t)payload_len);
    if (got_crc != expect_crc)
        return v_err("iefs: checksum mismatch");
    IefsR r = {.p = payload, .n = (size_t)payload_len, .off = 0, .err = {0}, .map_reg = NULL};
    V *v = decode_value(&r);
    if (v->t == T_ERR)
        return v;
    if (r.off != r.n) {
        v_free(v);
        return v_err("iefs: trailing payload bytes");
    }
    /* Tolerate trailing padding (O_DIRECT padded files truncated by ftruncate,
     * but readers may still see exact payload_len via header). */
    return v;
}

V *iefs_decode_mapped(const unsigned char *buf, size_t len, IefsMapRegion *reg) {
    if (!buf || len < IEFS_HEADER_SIZE)
        return v_err("iefs: truncated header");
    if (memcmp(buf, IEFS_MAGIC, 4) != 0)
        return v_err("iefs: bad magic");
    uint16_t ver = get_u16(buf + 4);
    if (ver == 3)
        return iefs_decode_v3(buf, len, reg, 0);
    if (ver != IEFS_VERSION)
        return v_errf("iefs: unsupported version %u (iefs.map requires v1 or v3)", (unsigned)ver);
    uint64_t payload_len = get_u64(buf + 8);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *payload = buf + IEFS_HEADER_SIZE;
    /* Skip CRC on map path (lazy open). */
    IefsR r = {.p = payload, .n = (size_t)payload_len, .off = 0, .err = {0}, .map_reg = reg};
    V *v = decode_value(&r);
    if (v->t == T_ERR)
        return v;
    if (r.off != r.n) {
        v_free(v);
        return v_err("iefs: trailing payload bytes");
    }
    return v;
}

int iefs_store_write(V *v, const char *path, int io_mode, char *err, size_t err_cap) {
    unsigned char *buf = NULL;
    size_t len = 0;
    if (iefs_encode(v, &buf, &len, err, err_cap) != 0)
        return -1;
    int rc = iefs_io_write_atomic(path, buf, len, io_mode, err, err_cap);
    free(buf);
    if (rc != 0) {
        if (err && err_cap && err[0])
            iefs_set_last_error(err);
        return -1;
    }
    iefs_set_last_error(NULL);
    return 0;
}

V *iefs_store_read(const char *path) {
    unsigned char *buf = NULL;
    size_t len = 0;
    char err[256];
    if (iefs_io_read_all(path, &buf, &len, err, sizeof err) != 0) {
        iefs_set_last_error(err);
        return v_err(err);
    }
    V *v = iefs_decode(buf, len);
    free(buf);
    if (v->t == T_ERR)
        iefs_set_last_error(v->s);
    else
        iefs_set_last_error(NULL);
    return v;
}

static int iefs_mode_from_arg(V *v, int *out_mode) {
    if (!v) {
        *out_mode = IEFS_IO_AUTO;
        return 0;
    }
    if (v->t == T_BOOL) {
        *out_mode = v->b ? IEFS_IO_DIRECT : IEFS_IO_BUF;
        return 0;
    }
    if (v->t == T_INT) {
        *out_mode = v->j ? IEFS_IO_DIRECT : IEFS_IO_BUF;
        return 0;
    }
    if (v->t == T_STR) {
        if (!strcmp(v->s, "direct") || !strcmp(v->s, "dio")) {
            *out_mode = IEFS_IO_DIRECT;
            return 0;
        }
        if (!strcmp(v->s, "buf") || !strcmp(v->s, "buffered")) {
            *out_mode = IEFS_IO_BUF;
            return 0;
        }
        if (!strcmp(v->s, "auto")) {
            *out_mode = IEFS_IO_AUTO;
            return 0;
        }
    }
    return -1;
}

static int iefs_mode_from_env(void) {
    const char *e = getenv("SHAKTI_IEFS_DIRECT");
    if (!e || !*e)
        e = getenv("ISOLDE_IEFS_DIRECT");
    if (e && (*e == '1' || *e == 'y' || *e == 'Y'))
        return IEFS_IO_DIRECT;
    if (e && (*e == '0' || *e == 'n' || *e == 'N'))
        return IEFS_IO_BUF;
    return IEFS_IO_AUTO;
}

V *bi_iefs_save(V **a, int n) {
    if (n < 2 || !a[1] || a[1]->t != T_STR)
        return v_err("iefs_save(value, path[, direct])");
    int mode = iefs_mode_from_env();
    if (n > 2) {
        if (iefs_mode_from_arg(a[2], &mode) != 0)
            return v_err("iefs_save: direct must be bool/int or \"auto\"/\"direct\"/\"buf\"");
    }
    char err[256];
    if (iefs_store_write(a[0], a[1]->s, mode, err, sizeof err) != 0) {
        const char *e = iefs_last_error();
        return v_err(e && e[0] ? e : (err[0] ? err : "iefs_save failed"));
    }
    return v_nil();
}

V *bi_iefs_load(V **a, int n) {
    if (n < 1 || !a[0] || a[0]->t != T_STR)
        return v_err("iefs_load(path)");
    return iefs_store_read(a[0]->s);
}

V *bi_iefs_map(V **a, int n) {
    if (n < 1 || !a[0] || a[0]->t != T_STR)
        return v_err("iefs_map(path[, pages])");
    int pages = IEFS_MAP_PAGES_THP;
    if (n > 1 && a[1] && a[1]->t == T_STR) {
        if (!strcmp(a[1]->s, "1g") || !strcmp(a[1]->s, "1G"))
            pages = IEFS_MAP_PAGES_1G;
        else if (!strcmp(a[1]->s, "2m") || !strcmp(a[1]->s, "2M"))
            pages = IEFS_MAP_PAGES_2M;
        else if (!strcmp(a[1]->s, "thp") || !a[1]->s[0])
            pages = IEFS_MAP_PAGES_THP;
        else
            return v_err("iefs_map: pages must be \"thp\", \"2m\", or \"1g\"");
    }
    return iefs_store_map(a[0]->s, pages);
}

V *bi_iefs_direct_available(V **a, int n) {
    (void)a;
    (void)n;
    return v_bool(iefs_io_direct_available());
}
