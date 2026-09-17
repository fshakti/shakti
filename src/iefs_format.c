/*
 * IE file store format: portable little-endian V serialization.
 */
#include "iefs_format.h"
#include "iefs_io.h"
#include "iefs_map.h"
#include "codec.h"
#include "pack.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(SHAKTI_HAVE_ZSTD)
#include <zstd.h>
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
#include <snappy-c.h>
#endif

/* Shakti has no extra numeric/media types. Stubs keep leftover switch arms compiling. */
#ifndef T_UINT
enum {
    T_UINT = 1000, T_UVEC = 1001, T_UMAT = 1002, T_DEC = 1003, T_DVEC = 1004, T_DMAT = 1005,
    T_GUID = 1006, T_GVEC = 1007, T_IMAGE = 1008, T_VIDEO = 1009, T_AUDIO = 1010,
    T_BED = 1011, T_GFF = 1012, T_FASTQ = 1013, T_VCF = 1014, T_SAM = 1015, T_BAM = 1016,
    T_SAA = 1017, T_DAT = 1018, T_MRI = 1019, T_CSF = 1020, T_GGUF = 1021, T_ISWT = 1022
};
#endif
static V *v_uvec(int64_t n, int bits) { (void)bits; return v_cvec(n); }
static V *v_int_bits(int64_t j, int bits) { (void)bits; return v_int(j); }
static V *v_float_bits(double f, int bits) { (void)bits; return v_float(f); }
static V *v_ivec_bits(int64_t n, int bits) { (void)bits; return v_ivec(n); }
static V *v_fvec_bits(int64_t n, int bits) { (void)bits; return v_fvec(n); }
static V *v_imat_bits(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_fmat_bits(int64_t r, int64_t c, int bits) { (void)bits; return v_fmat(r, c); }
static V *v_umat(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_dvec(int64_t n, int bits) { (void)bits; return v_ivec(n); }
static V *v_dmat(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_gvec(int64_t n) { return v_cvec(n); }
static V *v_bio_table(int t, int codec, V *k, V *vals) { (void)t;(void)codec;v_free(k);v_free(vals); return v_err("iefs: unsupported type"); }
static int v_is_rel(V *v) { return v && v->t == T_TABLE; }
/* Packed integer payloads (bits < 64) are widened to i64. */
static V *widen_packed_ivec(const unsigned char *p, uint64_t n, int bits) {
    V *v = v_ivec((int64_t)n);
    for (uint64_t i = 0; i < n; i++)
        v->J[i] = pack_get_i(p, (int64_t)i, bits);
    return v;
}
static void v_free_payload(V *v) {
    if (!v || v->owner_kind == V_OWNER_MAP_ALIAS) return;
    free(v->J); v->J = NULL;
    free(v->F); v->F = NULL;
    free(v->B); v->B = NULL;
}

/* Thread-local: map decodes on OpenMP workers; a process-global
 * buffer would race. Same-thread set/read (the only usage) is unchanged. */
#if defined(_WIN32)
#define IEFS_THREAD_LOCAL __declspec(thread)
#else
#define IEFS_THREAD_LOCAL __thread
#endif
static IEFS_THREAD_LOCAL char g_iefs_err[512];

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

/* IEEE CRC32 (poly 0xEDB88320), slicing-by-8 (ZIP/PNG polynomial; not CRC32C). */
static uint32_t crc32_tab[8][256];
static int crc32_ready;

static void crc32_init(void) {
    if (crc32_ready)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_tab[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = crc32_tab[0][i];
        for (int k = 1; k < 8; k++) {
            c = crc32_tab[0][c & 0xffu] ^ (c >> 8);
            crc32_tab[k][i] = c;
        }
    }
    crc32_ready = 1;
}

static uint32_t crc32_load_u32le(const unsigned char *p) {
    uint32_t v;
    memcpy(&v, p, 4);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    v = __builtin_bswap32(v);
#endif
    return v;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n) {
    crc32_init();
    crc = ~crc;
    while (n && ((uintptr_t)p & 7u)) {
        crc = crc32_tab[0][(crc ^ *p++) & 0xffu] ^ (crc >> 8);
        n--;
    }
    while (n >= 8) {
        uint32_t one = crc ^ crc32_load_u32le(p);
        uint32_t two = crc32_load_u32le(p + 4);
        crc = crc32_tab[7][one & 0xffu] ^ crc32_tab[6][(one >> 8) & 0xffu] ^
              crc32_tab[5][(one >> 16) & 0xffu] ^ crc32_tab[4][one >> 24] ^
              crc32_tab[3][two & 0xffu] ^ crc32_tab[2][(two >> 8) & 0xffu] ^
              crc32_tab[1][(two >> 16) & 0xffu] ^ crc32_tab[0][two >> 24];
        p += 8;
        n -= 8;
    }
    while (n--)
        crc = crc32_tab[0][(crc ^ *p++) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

static uint32_t crc32_buf(const unsigned char *p, size_t n) { return crc32_update(0, p, n); }

/* Bind type-layout (hdr[20:24]) into the checksum so layout cannot be forged
 * independently of payload CRC. Covers hdr[0:16] + hdr[20:24] + body. */
static uint32_t crc32_iefs_layout(const unsigned char *hdr, const unsigned char *body, size_t body_len) {
    uint32_t c = crc32_update(0, hdr, 16);
    c = crc32_update(c, hdr + 20, 4);
    if (body && body_len)
        c = crc32_update(c, body, body_len);
    return c;
}

static uint32_t crc32_update_zeros(uint32_t crc, size_t n) {
    static const unsigned char z[4096];
    while (n) {
        size_t c = n < sizeof z ? n : sizeof z;
        crc = crc32_update(crc, z, c);
        n -= c;
    }
    return crc;
}

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
    int arena; /* 1 if data came from iefs_io_alloc_buf */
} IefsBuf;

static void buf_free(IefsBuf *b) {
    if (!b || !b->data)
        return;
    if (b->arena)
        iefs_io_free_buf(b->data);
    else
        free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->arena = 0;
}

static int buf_reserve(IefsBuf *b, size_t need) {
    if (need <= b->cap)
        return 0;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2))
            return -1;
        ncap *= 2;
    }
    unsigned char *nd;
    int arena = 0;
    if (ncap >= (size_t)IEFS_IO_THP) {
        nd = (unsigned char *)iefs_io_alloc_buf(ncap);
        if (!nd)
            return -1;
        arena = 1;
    } else {
        nd = (unsigned char *)realloc(b->arena ? NULL : b->data, ncap);
        if (!nd)
            return -1;
    }
    if (b->data && (arena || b->arena)) {
        if (b->len)
            memcpy(nd, b->data, b->len);
        if (b->arena)
            iefs_io_free_buf(b->data);
        else
            free(b->data);
    }
    b->data = nd;
    b->cap = ncap;
    b->arena = arena;
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


static int pack_bool_bytes(unsigned char **out, size_t *out_len, const unsigned char *src, int64_t n) {
    size_t nbytes = bbit_nbytes(n);
    unsigned char *tmp = calloc(nbytes ? nbytes : 1, 1);
    if (!tmp) return -1;
    for (int64_t i = 0; i < n; i++)
        bbit_set(tmp, i, src && src[i] ? 1 : 0);
    *out = tmp;
    *out_len = nbytes;
    return 0;
}
static int buf_append_packed_bool(IefsBuf *b, const unsigned char *src, int64_t n) {
    unsigned char *tmp = NULL;
    size_t nbytes = 0;
    if (pack_bool_bytes(&tmp, &nbytes, src, n) != 0) return -1;
    int rc = nbytes ? buf_append(b, tmp, nbytes) : 0;
    free(tmp);
    return rc;
}
static void unpack_bool_bytes(unsigned char *dst, const unsigned char *src, int64_t n) {
    for (int64_t i = 0; i < n; i++)
        dst[i] = (unsigned char)(bbit_get(src, i) ? 1 : 0);
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

/* Native LE hosts can bulk-copy i64/f64 column payloads (IEFS is LE). */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define IEFS_NATIVE_LE 1
#else
#define IEFS_NATIVE_LE 0
#endif

static int buf_append_i64_le(IefsBuf *b, const int64_t *p, uint64_t n) {
    if (!n)
        return 0;
#if IEFS_NATIVE_LE
    return buf_append(b, p, (size_t)n * 8u);
#else
    for (uint64_t i = 0; i < n; i++)
        if (buf_put_i64(b, p[i]) != 0)
            return -1;
    return 0;
#endif
}

static int buf_append_f64_le(IefsBuf *b, const double *p, uint64_t n) {
    if (!n)
        return 0;
#if IEFS_NATIVE_LE
    return buf_append(b, p, (size_t)n * 8u);
#else
    for (uint64_t i = 0; i < n; i++)
        if (buf_put_f64(b, p[i]) != 0)
            return -1;
    return 0;
#endif
}

static void copy_i64_le(int64_t *dst, const unsigned char *src, uint64_t n) {
#if IEFS_NATIVE_LE
    if (n)
        memcpy(dst, src, (size_t)n * 8u);
#else
    for (uint64_t i = 0; i < n; i++)
        dst[i] = get_i64(src + i * 8);
#endif
}

static void copy_f64_le(double *dst, const unsigned char *src, uint64_t n) {
#if IEFS_NATIVE_LE
    if (n)
        memcpy(dst, src, (size_t)n * 8u);
#else
    for (uint64_t i = 0; i < n; i++)
        dst[i] = get_f64(src + i * 8);
#endif
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
        return buf_putc(b, (unsigned char)(v->j ? 1 : 0));
    case T_CHAR:
        if (buf_putc(b, (unsigned char)T_CHAR) != 0)
            return -1;
        return buf_putc(b, (unsigned char)v->j);
    case T_INT:
        if (buf_putc(b, (unsigned char)T_INT) != 0)
            return -1;
        if (buf_putc(b, (unsigned char)(64 > 0 ? 64 : 64)) != 0)
            return -1;
        return buf_put_i64(b, v->j);
    case T_FLOAT:
        if (buf_putc(b, (unsigned char)T_FLOAT) != 0)
            return -1;
        if (buf_putc(b, (unsigned char)(64 > 0 ? 64 : 64)) != 0)
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
        int bits = 64 > 0 ? 64 : 64;
        if (buf_putc(b, (unsigned char)T_IVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        if (buf_putc(b, (unsigned char)bits) != 0)
            return -1;
        if (bits < 64 && v->B)
            return buf_append(b, v->B, pack_nbytes(v->n, bits));
        if (v->J)
            return buf_append_i64_le(b, v->J, (uint64_t)v->n);
        for (int64_t i = 0; i < v->n; i++) {
            if (buf_put_i64(b, 0) != 0)
                return -1;
        }
        return 0;
    }
    case T_FVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        int bits = 64 > 0 ? 64 : 64;
        if (buf_putc(b, (unsigned char)T_FVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        if (buf_putc(b, (unsigned char)bits) != 0)
            return -1;
        if (bits < 64 && v->B)
            return buf_append(b, v->B, pack_nbytes(v->n, bits));
        if (v->F)
            return buf_append_f64_le(b, v->F, (uint64_t)v->n);
        for (int64_t i = 0; i < v->n; i++) {
            if (buf_put_f64(b, 0.0) != 0)
                return -1;
        }
        return 0;
    }
    case T_BVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_BVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        return buf_append_packed_bool(b, v->B, v->n);
    }
    case T_CVEC: {
        if ((uint64_t)v->n > IEFS_MAX_ELEMS)
            return -1;
        if (buf_putc(b, (unsigned char)T_CVEC) != 0)
            return -1;
        if (buf_put_u64(b, (uint64_t)v->n) != 0)
            return -1;
        return v->n > 0 ? buf_append(b, v->B, (size_t)v->n) : 0;
    }
    case T_IMAT:
    case T_FMAT:
    case T_BMAT:
    case T_CMAT: {
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
            if (v->J)
                return buf_append_i64_le(b, v->J, cells);
            for (uint64_t i = 0; i < cells; i++)
                if (buf_put_i64(b, 0) != 0)
                    return -1;
        } else if (v->t == T_FMAT) {
            if (v->F)
                return buf_append_f64_le(b, v->F, cells);
            for (uint64_t i = 0; i < cells; i++)
                if (buf_put_f64(b, 0.0) != 0)
                    return -1;
        } else if (v->t == T_CMAT) {
            return cells ? buf_append(b, v->B, (size_t)cells) : 0;
        } else {
            if (buf_append_packed_bool(b, v->B, (int64_t)cells) != 0)
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
    case T_SUBPROCESS:
        set_err(NULL, 0, "iefs: cannot serialize subprocess handles");
        return -1;
    default:
        set_err(NULL, 0, "iefs: unsupported type");
        return -1;
    }
}

static uint16_t iefs_flags_for_codec(int codec) {
    if (codec == SHAKTI_CODEC_ZSTD)
        return IEFS_FLAG_ZSTD;
    if (codec == SHAKTI_CODEC_SNAPPY)
        return IEFS_FLAG_SNAPPY;
    return 0;
}

static int iefs_codec_from_flags(uint16_t flags) {
    flags = (uint16_t)(flags & IEFS_FLAG_CODEC_MASK);
    if (flags == IEFS_FLAG_ZSTD)
        return SHAKTI_CODEC_ZSTD;
    if (flags == IEFS_FLAG_SNAPPY)
        return SHAKTI_CODEC_SNAPPY;
    if (flags == 0)
        return SHAKTI_CODEC_NONE;
    return -1;
}


/* ---- IEFS v3: TOC + 2MiB-aligned extents ---- */
static size_t iefs_align_up(size_t x, size_t a) {
    return (x + a - 1) & ~(a - 1);
}

static int col_is_extent(V *v) {
    if (!v) return 0;
    switch (v->t) {
    case T_DVEC: case T_IVEC: case T_UVEC: case T_FVEC: case T_BVEC: case T_CVEC:
    case T_IMAT: case T_FMAT: case T_BMAT: case T_CMAT: case T_UMAT: case T_DMAT:
    case T_GVEC:
        return 1;
    default:
        return 0;
    }
}

static int col_raw_export(V *v, unsigned char **out, size_t *out_len, uint64_t *nelem, uint64_t *ncols,
                          int *bits_out, int *own_out) {
    *out = NULL; *out_len = 0; *nelem = 0; *ncols = 0; *bits_out = 64;
    if (own_out)
        *own_out = 0;
    if (!col_is_extent(v)) return -1;
    int bits = (v->t == T_BVEC || v->t == T_BMAT) ? 1 : ((v->t == T_CVEC || v->t == T_CMAT) ? 8 : 64);
    *bits_out = bits;
    const void *src = NULL;
    size_t nbytes;
    if (v->t == T_CVEC) {
        *nelem = (uint64_t)v->n;
        *ncols = 0;
        nbytes = (size_t)(v->n > 0 ? v->n : 0);
        src = v->B;
    } else if (v->t == T_CMAT) {
        *nelem = (uint64_t)v->n;
        *ncols = (uint64_t)mat_cols(v);
        uint64_t cells = (*nelem) * (*ncols);
        nbytes = (size_t)cells;
        src = v->B;
    } else if (v->t == T_IMAT || v->t == T_FMAT || v->t == T_BMAT || v->t == T_UMAT || v->t == T_DMAT) {
        *nelem = (uint64_t)v->n;
        *ncols = (uint64_t)mat_cols(v);
        uint64_t cells = (*nelem) * (*ncols);
        if (v->t == T_BMAT)
            nbytes = bbit_nbytes((int64_t)cells);
        else if (v->t == T_DMAT) {
            int nb = bits / 8; if (nb < 4) nb = 4;
            nbytes = (size_t)cells * (size_t)nb;
        } else if (bits < 64 && v->B)
            nbytes = pack_nbytes((int64_t)cells, bits);
        else
            nbytes = (size_t)cells * 8;
        src = (bits < 64 && v->B) ? (void *)v->B
            : (v->t == T_FMAT ? (void *)v->F
               : (v->t == T_BMAT || v->t == T_DMAT ? (void *)v->B : (void *)v->J));
    } else {
        *nelem = (uint64_t)v->n;
        *ncols = 0;
        if (v->t == T_BVEC)
            nbytes = bbit_nbytes(v->n);
        else if (v->t == T_DVEC) {
            int nb = bits / 8; if (nb < 4) nb = 4;
            nbytes = (size_t)v->n * (size_t)nb;
        } else if (v->t == T_GVEC) {
            nbytes = (size_t)v->n * 16u;
        } else if (bits < 64 && v->B)
            nbytes = pack_nbytes(v->n, bits);
        else
            nbytes = (size_t)v->n * 8;
        src = (bits < 64 && v->B) ? (void *)v->B
            : (v->t == T_FVEC ? (void *)v->F
               : (v->t == T_BVEC || v->t == T_DVEC || v->t == T_GVEC ? (void *)v->B : (void *)v->J));
    }
    if (v->t == T_BVEC || v->t == T_BMAT) {
        int64_t nbool = (v->t == T_BMAT) ? (int64_t)((*nelem) * (*ncols)) : v->n;
        if (pack_bool_bytes(out, out_len, v->B, nbool) != 0)
            return -1;
        if (own_out)
            *own_out = 1;
        return 0;
    }
    if (!nbytes) {
        *out = NULL;
        *out_len = 0;
        return 0;
    }
    if (!src)
        return -1;
#if IEFS_NATIVE_LE
    *out = (unsigned char *)src;
    *out_len = nbytes;
    if (own_out)
        *own_out = 0;
    return 0;
#else
    unsigned char *buf = malloc(nbytes);
    if (!buf)
        return -1;
    memcpy(buf, src, nbytes);
    *out = buf;
    *out_len = nbytes;
    if (own_out)
        *own_out = 1;
    return 0;
#endif
}

static V *col_raw_import(int type, int bits, uint64_t nelem, uint64_t ncols,
                         const unsigned char *p, size_t nbytes, IefsMapRegion *reg, int alias_ok) {
    if (nelem > IEFS_MAX_ELEMS || ncols > IEFS_MAX_ELEMS)
        return v_err("iefs: extent too large");
    if (type == T_CVEC) {
        if (nbytes != (size_t)nelem) return v_err("iefs: extent length mismatch");
        if (alias_ok && reg) {
            V *v = v_cvec((int64_t)nelem);
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
            iefs_v_set_map_alias(v, reg);
            return v;
        }
        V *v = v_cvec((int64_t)nelem);
        if (nbytes) memcpy(v->B, p, nbytes);
        return v;
    }
    if (type == T_CMAT) {
        if (ncols == 0 && nelem > 0)
            return v_err("iefs: matrix ncols is zero");
        if (ncols != 0 && nelem > IEFS_MAX_ELEMS / ncols)
            return v_err("iefs: extent too large");
        uint64_t cells = nelem * ncols;
        if (nbytes != (size_t)cells) return v_err("iefs: extent length mismatch");
        if (alias_ok && reg) {
            V *v = v_cmat((int64_t)nelem, (int64_t)ncols);
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
            iefs_v_set_map_alias(v, reg);
            return v;
        }
        V *v = v_cmat((int64_t)nelem, (int64_t)ncols);
        if (nbytes) memcpy(v->B, p, nbytes);
        return v;
    }
    if (type == T_IMAT || type == T_FMAT || type == T_BMAT || type == T_UMAT || type == T_DMAT) {
        if (ncols == 0 && nelem > 0)
            return v_err("iefs: matrix ncols is zero");
        if (ncols != 0 && nelem > IEFS_MAX_ELEMS / ncols)
            return v_err("iefs: extent too large");
        uint64_t cells = nelem * ncols;
        if (cells > IEFS_MAX_ELEMS)
            return v_err("iefs: extent too large");
        size_t needb;
        if (type == T_BMAT) needb = bbit_nbytes((int64_t)cells);
        else if (type == T_DMAT) { int nb = bits / 8; if (nb < 4) nb = 4; needb = (size_t)cells * (size_t)nb; }
        else if (bits < 64) needb = pack_nbytes((int64_t)cells, bits);
        else needb = (size_t)cells * 8;
        if (needb != nbytes) return v_err("iefs: extent length mismatch");
        if (alias_ok && reg && bits >= 64 && type != T_BMAT && type != T_DMAT) {
            V *v = (type == T_FMAT) ? v_fmat((int64_t)nelem, (int64_t)ncols)
                 : (type == T_UMAT) ? v_umat((int64_t)nelem, (int64_t)ncols, bits)
                                   : v_imat((int64_t)nelem, (int64_t)ncols);
            v_free_payload(v);
            if (type == T_FMAT) v->F = (double *)(uintptr_t)p;
            else v->J = (int64_t *)(uintptr_t)p;
            iefs_v_set_map_alias(v, reg);
            return v;
        }
        if (alias_ok && reg && type != T_BMAT && (bits < 64 || type == T_DMAT)) {
            V *v = (type == T_BMAT) ? v_bmat((int64_t)nelem, (int64_t)ncols)
                 : (type == T_FMAT) ? v_fmat_bits((int64_t)nelem, (int64_t)ncols, bits)
                 : (type == T_DMAT) ? v_dmat((int64_t)nelem, (int64_t)ncols, bits)
                 : (type == T_UMAT) ? v_umat((int64_t)nelem, (int64_t)ncols, bits)
                 : v_imat_bits((int64_t)nelem, (int64_t)ncols, bits);
            /* Prefer alias into B when packed */
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
            (void)bits;
            iefs_v_set_map_alias(v, reg);
            return v;
        }
        V *v;
        if (type == T_BMAT) {
            v = v_bmat((int64_t)nelem, (int64_t)ncols);
            if (nbytes) unpack_bool_bytes(v->B, p, (int64_t)cells);
        } else if (type == T_FMAT) {
            v = bits < 64 ? v_fmat_bits((int64_t)nelem, (int64_t)ncols, bits) : v_fmat((int64_t)nelem, (int64_t)ncols);
            if (bits < 64) { if (nbytes) memcpy(v->B, p, nbytes); }
            else copy_f64_le(v->F, p, cells);
        } else if (type == T_DMAT) {
            v = v_dmat((int64_t)nelem, (int64_t)ncols, bits);
            if (nbytes) memcpy(v->B, p, nbytes);
        } else if (type == T_UMAT) {
            v = v_umat((int64_t)nelem, (int64_t)ncols, bits);
            if (bits < 64) { if (nbytes) memcpy(v->B, p, nbytes); }
            else copy_i64_le(v->J, p, cells);
        } else {
            v = bits < 64 ? v_imat_bits((int64_t)nelem, (int64_t)ncols, bits) : v_imat((int64_t)nelem, (int64_t)ncols);
            if (bits < 64) { if (nbytes) memcpy(v->B, p, nbytes); }
            else copy_i64_le(v->J, p, cells);
        }
        return v;
    }
    /* vectors */
    size_t needb;
    if (type == T_BVEC) needb = bbit_nbytes((int64_t)nelem);
    else if (type == T_DVEC) { int nb = bits / 8; if (nb < 4) nb = 4; needb = (size_t)nelem * (size_t)nb; }
    else if (type == T_GVEC) needb = (size_t)nelem * 16u;
    else if (bits < 64) needb = pack_nbytes((int64_t)nelem, bits);
    else needb = (size_t)nelem * 8;
    if (needb != nbytes) return v_err("iefs: extent length mismatch");
    if (alias_ok && reg && type != T_BVEC) {
        V *v;
        if (type == T_FVEC) {
            v = bits < 64 ? v_fvec_bits((int64_t)nelem, bits) : v_fvec((int64_t)nelem);
            v_free_payload(v);
            if (bits < 64) v->B = (unsigned char *)(uintptr_t)p;
            else v->F = (double *)(uintptr_t)p;
        } else if (type == T_BVEC) {
            v = v_bvec((int64_t)nelem);
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
        } else if (type == T_DVEC) {
            v = v_dvec((int64_t)nelem, bits);
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
        } else if (type == T_GVEC) {
            v = v_gvec((int64_t)nelem);
            v_free_payload(v);
            v->B = (unsigned char *)(uintptr_t)p;
        } else if (type == T_UVEC) {
            v = v_uvec((int64_t)nelem, bits);
            v_free_payload(v);
            if (bits < 64) v->B = (unsigned char *)(uintptr_t)p;
            else v->J = (int64_t *)(uintptr_t)p;
        } else if (type == T_IVEC && bits < 64) {
            return widen_packed_ivec(p, nelem, bits);
        } else {
            v = v_ivec((int64_t)nelem);
            v_free_payload(v);
            v->J = (int64_t *)(uintptr_t)p;
        }
        iefs_v_set_map_alias(v, reg);
        return v;
    }
    V *v;
    if (type == T_FVEC) {
        v = bits < 64 ? v_fvec_bits((int64_t)nelem, bits) : v_fvec((int64_t)nelem);
        if (bits < 64) { if (nbytes) memcpy(v->B, p, nbytes); }
        else copy_f64_le(v->F, p, nelem);
    } else if (type == T_BVEC) {
        v = v_bvec((int64_t)nelem);
        if (nbytes) unpack_bool_bytes(v->B, p, (int64_t)nelem);
    } else if (type == T_DVEC) {
        v = v_dvec((int64_t)nelem, bits);
        if (nbytes) memcpy(v->B, p, nbytes);
    } else if (type == T_GVEC) {
        v = v_gvec((int64_t)nelem);
        if (nbytes && v->B) memcpy(v->B, p, nbytes);
    } else if (type == T_UVEC) {
        v = v_uvec((int64_t)nelem, bits);
        if (bits < 64) { if (nbytes) memcpy(v->B, p, nbytes); }
        else copy_i64_le(v->J, p, nelem);
    } else if (type == T_IVEC && bits < 64) {
        return widen_packed_ivec(p, nelem, bits);
    } else {
        v = v_ivec((int64_t)nelem);
        copy_i64_le(v->J, p, nelem);
    }
    return v;
}

typedef struct {
    int type;
    int bits;
    int codec;
    int outer; /* optional LZ after residual; 0 = none */
    uint64_t nelem;
    uint64_t ncols;
    unsigned char *raw;
    size_t raw_len;
    int own_raw;
    unsigned char *stored;
    size_t stored_len;
    int own_stored;
    char *name;
} IefsExtentW;

static void extent_w_free(IefsExtentW *e) {
    if (!e) return;
    if (e->own_raw)
        free(e->raw);
    if (e->own_stored)
        free(e->stored);
    free(e->name);
    memset(e, 0, sizeof *e);
}

static int residual_ok_for_extent(int codec, int type, int bits, size_t raw_len) {
    if (!shakti_codec_is_residual(codec))
        return 1;
    if (raw_len % 8u != 0)
        return 0;
    if (bits != 0 && bits != 64)
        return 0;
    if (codec == SHAKTI_CODEC_GORILLA_F64)
        return type == T_FVEC || type == T_FMAT;
    return type == T_IVEC || type == T_IMAT;
}

static int lookup_col_codec(V *codecs, const char *name, int def_codec, int def_outer,
                            int *codec_out, int *outer_out) {
    *codec_out = def_codec;
    *outer_out = def_outer;
    if (!codecs || codecs->t != T_DICT || !codecs->keys || !codecs->vals || !name)
        return 0;
    if (codecs->keys->t != T_LIST || codecs->vals->t != T_LIST)
        return -1;
    for (int64_t i = 0; i < codecs->keys->n; i++) {
        V *k = codecs->keys->L[i];
        V *val = codecs->vals->L[i];
        if (!k || k->t != T_STR || !k->s || strcmp(k->s, name) != 0)
            continue;
        if (!val || val->t != T_STR || !val->s)
            return -1;
        int outer = SHAKTI_CODEC_NONE;
        int c = shakti_codec_parse2(val->s, &outer);
        if (c < 0)
            return -1;
        *codec_out = c;
        *outer_out = outer;
        return 0;
    }
    return 0;
}

typedef struct {
    IefsExtentW *ext;
    int n_ext;
    unsigned char *prefix;
    size_t prefix_len;
    size_t *file_offs;
    size_t total;
} IefsV3Prep;

static void iefs_v3_prep_free(IefsV3Prep *p) {
    if (!p)
        return;
    if (p->ext) {
        for (int i = 0; i < p->n_ext; i++)
            extent_w_free(&p->ext[i]);
        free(p->ext);
    }
    free(p->prefix);
    free(p->file_offs);
    memset(p, 0, sizeof *p);
}

static int iefs_v3_prepare(V *v, int codec, int outer, int level, V *codecs, IefsV3Prep *prep,
                           char *err, size_t err_cap) {
    memset(prep, 0, sizeof *prep);
    IefsExtentW *ext = NULL;
    int n_ext = 0;

    if ((v->t == T_TABLE || v->t == T_DICT) && v->keys && v->vals &&
        v->keys->t == T_LIST && v->vals->t == T_LIST && v->keys->n == v->vals->n) {
        int ok = 1;
        for (int64_t i = 0; i < v->keys->n; i++) {
            if (!v->keys->L[i] || v->keys->L[i]->t != T_STR || !col_is_extent(v->vals->L[i])) {
                ok = 0; break;
            }
        }
        if (ok && v->keys->n > 0) {
            n_ext = (int)v->keys->n;
            ext = calloc((size_t)n_ext, sizeof(*ext));
            if (!ext) { set_err(err, err_cap, "iefs: out of memory"); return -1; }
            for (int i = 0; i < n_ext; i++) {
                ext[i].type = v->vals->L[i]->t;
                ext[i].name = strdup(v->keys->L[i]->s ? v->keys->L[i]->s : "");
                if (!ext[i].name) {
                    set_err(err, err_cap, "iefs: out of memory");
                    goto fail;
                }
                if (col_raw_export(v->vals->L[i], &ext[i].raw, &ext[i].raw_len, &ext[i].nelem,
                                   &ext[i].ncols, &ext[i].bits, &ext[i].own_raw) != 0) {
                    set_err(err, err_cap, "iefs: column export failed");
                    goto fail;
                }
            }
        }
    }
    if (!ext && col_is_extent(v)) {
        n_ext = 1;
        ext = calloc(1, sizeof(*ext));
        if (!ext) { set_err(err, err_cap, "iefs: out of memory"); return -1; }
        ext[0].type = v->t;
        if (col_raw_export(v, &ext[0].raw, &ext[0].raw_len, &ext[0].nelem, &ext[0].ncols,
                           &ext[0].bits, &ext[0].own_raw) != 0) {
            set_err(err, err_cap, "iefs: column export failed");
            goto fail;
        }
    }
    if (!ext) {
        n_ext = 1;
        ext = calloc(1, sizeof(*ext));
        if (!ext) { set_err(err, err_cap, "iefs: out of memory"); return -1; }
        IefsBuf payload = {0};
        if (encode_value(&payload, v) != 0) {
            buf_free(&payload);
            set_err(err, err_cap, g_iefs_err[0] ? g_iefs_err : "iefs_encode: encode failed");
            goto fail;
        }
        ext[0].type = (int)IEFS_EXT_TLV;
        ext[0].bits = 0;
        ext[0].raw_len = payload.len;
        ext[0].nelem = payload.len;
        ext[0].ncols = 0;
        if (payload.arena) {
            unsigned char *copy = malloc(payload.len ? payload.len : 1);
            if (!copy) {
                buf_free(&payload);
                set_err(err, err_cap, "iefs: out of memory");
                goto fail;
            }
            if (payload.len)
                memcpy(copy, payload.data, payload.len);
            buf_free(&payload);
            ext[0].raw = copy;
        } else {
            ext[0].raw = payload.data;
        }
        ext[0].own_raw = 1;
    }

    for (int i = 0; i < n_ext; i++) {
        int c = codec;
        int o = outer;
        if (lookup_col_codec(codecs, ext[i].name, codec, outer, &c, &o) != 0) {
            set_err(err, err_cap, "iefs: bad codecs map entry");
            goto fail;
        }
        if (o != SHAKTI_CODEC_NONE && !shakti_codec_is_residual(c)) {
            set_err(err, err_cap, "iefs: outer LZ only valid with residual codec");
            goto fail;
        }
        if (c != SHAKTI_CODEC_NONE && !shakti_codec_available(c)) {
            set_err(err, err_cap, "iefs: codec not available");
            goto fail;
        }
        if (o != SHAKTI_CODEC_NONE && !shakti_codec_available(o)) {
            set_err(err, err_cap, "iefs: outer codec not available");
            goto fail;
        }
        if (shakti_codec_is_residual(c) &&
            !residual_ok_for_extent(c, ext[i].type, ext[i].bits, ext[i].raw_len)) {
            set_err(err, err_cap, "iefs: residual codec type mismatch");
            goto fail;
        }
        ext[i].codec = c;
        ext[i].outer = o;
        if (c == SHAKTI_CODEC_NONE && o == SHAKTI_CODEC_NONE) {
            ext[i].stored = ext[i].raw;
            ext[i].stored_len = ext[i].raw_len;
            ext[i].own_stored = 0;
        } else {
            unsigned char *mid = NULL;
            size_t mid_len = 0;
            if (shakti_compress(c, level, ext[i].raw, ext[i].raw_len, &mid, &mid_len, err, err_cap) !=
                0)
                goto fail;
            if (o != SHAKTI_CODEC_NONE) {
                unsigned char *comp = NULL;
                size_t clen = 0;
                int rc = shakti_compress(o, level, mid, mid_len, &comp, &clen, err, err_cap);
                free(mid);
                if (rc != 0)
                    goto fail;
                ext[i].stored = comp;
                ext[i].stored_len = clen;
                ext[i].own_stored = 1;
            } else {
                ext[i].stored = mid;
                ext[i].stored_len = mid_len;
                ext[i].own_stored = 1;
            }
        }
        if (ext[i].stored_len > IEFS_MAX_PAYLOAD) {
            set_err(err, err_cap, "iefs: extent too large");
            goto fail;
        }
    }

    size_t names_len = 0;
    for (int i = 0; i < n_ext; i++)
        if (ext[i].name) names_len += strlen(ext[i].name) + 1;
    unsigned char *names = calloc(names_len ? names_len : 1, 1);
    if (!names) { set_err(err, err_cap, "iefs: out of memory"); goto fail; }
    size_t noff = 0;
    uint32_t *name_offs = calloc((size_t)n_ext, sizeof(uint32_t));
    if (!name_offs) { free(names); set_err(err, err_cap, "iefs: out of memory"); goto fail; }
    for (int i = 0; i < n_ext; i++) {
        if (!ext[i].name) { name_offs[i] = 0xffffffffu; continue; }
        name_offs[i] = (uint32_t)noff;
        size_t l = strlen(ext[i].name) + 1;
        memcpy(names + noff, ext[i].name, l);
        noff += l;
    }

    size_t toc_bytes = 8u + (size_t)n_ext * IEFS_V3_EXTENT_SIZE + names_len;
    size_t cursor = IEFS_HEADER_SIZE + toc_bytes;
    size_t *file_offs = calloc((size_t)n_ext, sizeof(size_t));
    if (!file_offs) { free(names); free(name_offs); set_err(err, err_cap, "iefs: out of memory"); goto fail; }
    for (int i = 0; i < n_ext; i++) {
        cursor = iefs_align_up(cursor, IEFS_V3_ALIGN);
        file_offs[i] = cursor;
        cursor += ext[i].stored_len;
    }
    size_t total = cursor;
    if (total > IEFS_MAX_PAYLOAD + IEFS_HEADER_SIZE) {
        free(names); free(name_offs); free(file_offs);
        set_err(err, err_cap, "iefs: payload too large");
        goto fail;
    }

    size_t prefix_len = IEFS_HEADER_SIZE + toc_bytes;
    unsigned char *prefix = calloc(prefix_len ? prefix_len : 1, 1);
    if (!prefix) {
        free(names); free(name_offs); free(file_offs);
        set_err(err, err_cap, "iefs: out of memory");
        goto fail;
    }
    memcpy(prefix, IEFS_MAGIC, 4);
    put_u16(prefix + 4, 3);
    put_u16(prefix + 6, 0);
    put_u64(prefix + 8, (uint64_t)(total - IEFS_HEADER_SIZE));
    put_u32(prefix + 20, IEFS_TYPE_LAYOUT);

    unsigned char *toc = prefix + IEFS_HEADER_SIZE;
    put_u32(toc + 0, (uint32_t)n_ext);
    put_u32(toc + 4, (uint32_t)names_len);
    for (int i = 0; i < n_ext; i++) {
        unsigned char *er = toc + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
        er[0] = (unsigned char)ext[i].type;
        er[1] = (unsigned char)ext[i].bits;
        er[2] = (unsigned char)ext[i].codec;
        er[3] = (unsigned char)ext[i].outer;
        put_u64(er + 4, ext[i].nelem);
        put_u64(er + 12, ext[i].ncols);
        put_u64(er + 20, (uint64_t)file_offs[i]);
        put_u64(er + 28, (uint64_t)ext[i].stored_len);
        put_u32(er + 36, crc32_buf(ext[i].stored, ext[i].stored_len));
        put_u32(er + 40, name_offs[i]);
        put_u32(er + 44, 0);
    }
    if (names_len)
        memcpy(toc + 8 + (size_t)n_ext * IEFS_V3_EXTENT_SIZE, names, names_len);
    free(names);
    free(name_offs);

    uint32_t whole = crc32_iefs_layout(prefix, prefix + IEFS_HEADER_SIZE, prefix_len - IEFS_HEADER_SIZE);
    size_t pos = prefix_len;
    for (int i = 0; i < n_ext; i++) {
        if (file_offs[i] > pos)
            whole = crc32_update_zeros(whole, file_offs[i] - pos);
        if (ext[i].stored_len)
            whole = crc32_update(whole, ext[i].stored, ext[i].stored_len);
        pos = file_offs[i] + ext[i].stored_len;
    }
    if (total > pos)
        whole = crc32_update_zeros(whole, total - pos);
    put_u32(prefix + 16, whole);

    prep->ext = ext;
    prep->n_ext = n_ext;
    prep->prefix = prefix;
    prep->prefix_len = prefix_len;
    prep->file_offs = file_offs;
    prep->total = total;
    iefs_set_last_error(NULL);
    return 0;
fail:
    if (ext) {
        for (int i = 0; i < n_ext; i++)
            extent_w_free(&ext[i]);
        free(ext);
    }
    return -1;
}

static int iefs_encode_v3(V *v, int codec, int outer, int level, V *codecs, unsigned char **out,
                          size_t *out_len, char *err, size_t err_cap) {
    IefsV3Prep prep = {0};
    if (iefs_v3_prepare(v, codec, outer, level, codecs, &prep, err, err_cap) != 0)
        return -1;
    unsigned char *file = (unsigned char *)iefs_io_alloc_buf(prep.total);
    if (!file) {
        iefs_v3_prep_free(&prep);
        set_err(err, err_cap, "iefs: out of memory");
        return -1;
    }
    memcpy(file, prep.prefix, prep.prefix_len);
    size_t pos = prep.prefix_len;
    for (int i = 0; i < prep.n_ext; i++) {
        if (prep.file_offs[i] > pos)
            memset(file + pos, 0, prep.file_offs[i] - pos);
        if (prep.ext[i].stored_len)
            memcpy(file + prep.file_offs[i], prep.ext[i].stored, prep.ext[i].stored_len);
        pos = prep.file_offs[i] + prep.ext[i].stored_len;
    }
    if (prep.total > pos)
        memset(file + pos, 0, prep.total - pos);
    size_t total = prep.total;
    iefs_v3_prep_free(&prep);
    *out = file;
    *out_len = total;
    return 0;
}

static int iefs_encode_v2(V *v, int codec, int level, unsigned char **out, size_t *out_len,
                          char *err, size_t err_cap);

int iefs_encode_full(V *v, int codec, int outer, int level, unsigned ver, V *codecs,
                     unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!out || !out_len) {
        set_err(err, err_cap, "iefs_encode: bad args");
        return -1;
    }
    *out = NULL; *out_len = 0;
    if (codecs && codecs->t != T_DICT) {
        set_err(err, err_cap, "iefs: codecs must be a dict");
        return -1;
    }
    if (codecs && ver != 3) {
        set_err(err, err_cap, "iefs: codecs map requires format:3");
        return -1;
    }
    if ((shakti_codec_is_residual(codec) || outer != SHAKTI_CODEC_NONE) && ver != 3) {
        set_err(err, err_cap, "iefs: residual codecs require format:3");
        return -1;
    }
    if (codec < 0 || (codec != SHAKTI_CODEC_NONE && !shakti_codec_available(codec))) {
        set_err(err, err_cap,
                codec == SHAKTI_CODEC_ZSTD ? "iefs: zstd not built"
                : codec == SHAKTI_CODEC_SNAPPY ? "iefs: snappy not built"
                                              : "iefs: unknown codec");
        return -1;
    }
    if (outer < 0 || (outer != SHAKTI_CODEC_NONE && !shakti_codec_available(outer))) {
        set_err(err, err_cap, "iefs: outer codec not available");
        return -1;
    }
    if (ver == 3)
        return iefs_encode_v3(v, codec, outer, level, codecs, out, out_len, err, err_cap);
    if (ver != 1 && ver != 2) {
        set_err(err, err_cap, "iefs: unsupported write version");
        return -1;
    }
    return iefs_encode_v2(v, codec, level, out, out_len, err, err_cap);
}

int iefs_encode_ex(V *v, int codec, int level, unsigned ver, unsigned char **out, size_t *out_len,
                   char *err, size_t err_cap) {
    return iefs_encode_full(v, codec, SHAKTI_CODEC_NONE, level, ver, NULL, out, out_len, err,
                            err_cap);
}

static int iefs_encode_v2(V *v, int codec, int level, unsigned char **out, size_t *out_len,
                          char *err, size_t err_cap) {
    IefsBuf payload = {0};
    if (encode_value(&payload, v) != 0) {
        buf_free(&payload);
        if (!g_iefs_err[0])
            set_err(err, err_cap, "iefs_encode: encode failed");
        else if (err && err_cap)
            snprintf(err, err_cap, "%s", g_iefs_err);
        return -1;
    }
    if (payload.len > IEFS_MAX_PAYLOAD) {
        buf_free(&payload);
        set_err(err, err_cap, "iefs_encode: payload too large");
        return -1;
    }

    unsigned char *stored = payload.data;
    size_t stored_len = payload.len;
    int own_stored = 0;
    if (codec != SHAKTI_CODEC_NONE) {
        unsigned char *comp = NULL;
        size_t comp_len = 0;
        if (shakti_compress(codec, level, payload.data, payload.len, &comp, &comp_len, err,
                            err_cap) != 0) {
            buf_free(&payload);
            return -1;
        }
        buf_free(&payload);
        stored = comp;
        stored_len = comp_len;
        own_stored = 1;
        if (stored_len > IEFS_MAX_PAYLOAD) {
            free(stored);
            set_err(err, err_cap, "iefs_encode: compressed payload too large");
            return -1;
        }
    }

    size_t total = IEFS_HEADER_SIZE + stored_len;
    unsigned char *file = (unsigned char *)iefs_io_alloc_buf(total);
    if (!file) {
        if (own_stored)
            free(stored);
        else
            buf_free(&payload);
        set_err(err, err_cap, "iefs_encode: out of memory");
        return -1;
    }
    memcpy(file, IEFS_MAGIC, 4);
    put_u16(file + 4, (uint16_t)IEFS_VERSION);
    put_u16(file + 6, iefs_flags_for_codec(codec));
    put_u64(file + 8, (uint64_t)stored_len);
    put_u32(file + 20, IEFS_TYPE_LAYOUT);
    if (stored_len)
        memcpy(file + IEFS_HEADER_SIZE, stored, stored_len);
    uint32_t crc = crc32_iefs_layout(file, file + IEFS_HEADER_SIZE, stored_len);
    put_u32(file + 16, crc);
    if (own_stored)
        free(stored);
    else
        buf_free(&payload);
    *out = file;
    *out_len = total;
    iefs_set_last_error(NULL);
    return 0;
}

int iefs_encode_codec(V *v, int codec, int level, unsigned char **out, size_t *out_len,
                      char *err, size_t err_cap) {
    return iefs_encode_ex(v, codec, level, IEFS_VERSION, out, out_len, err, err_cap);
}

int iefs_encode(V *v, unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    return iefs_encode_codec(v, SHAKTI_CODEC_NONE, 0, out, out_len, err, err_cap);
}

/* ---- decode ---- */
typedef struct {
    const unsigned char *p;
    size_t n;
    size_t off;
    uint16_t ver;
    int depth; /* decode_value nesting; capped by IEFS_MAX_NESTING */
    char err[256];
    IefsMapRegion *map_reg; /* non-NULL → alias contiguous payloads */
} IefsR;

static int need(IefsR *r, size_t nbytes) {
    if (r->off + nbytes > r->n) {
        snprintf(r->err, sizeof r->err, "iefs: truncated payload");
        return -1;
    }
    return 0;
}

static V *decode_value(IefsR *r);

/* which: 0=J, 1=F, 2=B; cols < 0 means vector. */
static V *alias_payload(IefsR *r, int t, int64_t n, int bits, int64_t cols, size_t nbytes, int which) {
    if (need(r, nbytes) != 0)
        return v_err(r->err);
    V *v;
    if (t == T_IVEC)
        v = (bits < 64) ? v_ivec_bits(n, bits) : v_ivec(n);
    else if (t == T_UVEC)
        v = v_uvec(n, bits);
    else if (t == T_FVEC)
        v = (bits < 64) ? v_fvec_bits(n, bits) : v_fvec(n);
    else if (t == T_BVEC)
        v = v_bvec(n);
    else if (t == T_CVEC)
        v = v_cvec(n);
    else if (t == T_DVEC)
        v = v_dvec(n, bits);
    else if (t == T_IMAT)
        v = v_imat(n, cols);
    else if (t == T_FMAT)
        v = v_fmat(n, cols);
    else if (t == T_BMAT)
        v = v_bmat(n, cols);
    else if (t == T_CMAT)
        v = v_cmat(n, cols);
    else
        return v_err("iefs: alias type");
    /* Alias into the map when 8-byte payloads are aligned; otherwise copy. */
    {
        const unsigned char *src = r->p + r->off;
        int unaligned_word = (which == 0 || which == 1) && (((uintptr_t)src) & 7u);
        if (unaligned_word) {
            uint64_t ne = (cols < 0) ? (uint64_t)n : (uint64_t)n * (uint64_t)cols;
            if (which == 0)
                copy_i64_le(v->J, src, ne);
            else
                copy_f64_le(v->F, src, ne);
        } else {
            v_free_payload(v);
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

static V *decode_value_inner(IefsR *r);

static V *decode_value(IefsR *r) {
    if (r->depth >= IEFS_MAX_NESTING)
        return v_err("iefs: nesting too deep");
    r->depth++;
    V *v = decode_value_inner(r);
    r->depth--;
    return v;
}

static V *decode_value_inner(IefsR *r) {
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
    case T_CHAR: {
        if (need(r, 1) != 0)
            return v_err(r->err);
        return v_char(r->p[r->off++]);
    }
    case T_INT: {
        int bits = 64;
        if (r->ver >= 2) {
            if (need(r, 1) != 0) return v_err(r->err);
            bits = r->p[r->off++];
            if (bits <= 0) bits = 64;
        }
        if (need(r, 8) != 0)
            return v_err(r->err);
        int64_t j = get_i64(r->p + r->off);
        r->off += 8;
        return bits >= 64 ? v_int(j) : v_int_bits(j, bits);
    }
    case T_FLOAT: {
        int bits = 64;
        if (r->ver >= 2) {
            if (need(r, 1) != 0) return v_err(r->err);
            bits = r->p[r->off++];
            if (bits <= 0) bits = 64;
        }
        if (need(r, 8) != 0)
            return v_err(r->err);
        double f = get_f64(r->p + r->off);
        r->off += 8;
        return bits >= 64 ? v_float(f) : v_float_bits(f, bits);
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
        int bits = 64;
        if (r->ver >= 2) {
            if (need(r, 1) != 0)
                return v_err(r->err);
            bits = r->p[r->off++];
            if (bits <= 0) bits = 64;
        }
        if (bits < 64) {
            size_t nbytes = pack_nbytes((int64_t)n, bits);
            if (need(r, nbytes) != 0)
                return v_err(r->err);
            V *v = widen_packed_ivec(r->p + r->off, n, bits);
            r->off += nbytes;
            return v;
        }
        size_t nbytes = (size_t)n * 8;
        if (r->map_reg)
            return alias_payload(r, T_IVEC, (int64_t)n, 64, -1, nbytes, 0);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_ivec((int64_t)n);
        copy_i64_le(v->J, r->p + r->off, n);
        r->off += nbytes;
        return v;
    }
    case T_FVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: fvec too large");
        int bits = 64;
        if (r->ver >= 2) {
            if (need(r, 1) != 0)
                return v_err(r->err);
            bits = r->p[r->off++];
            if (bits <= 0) bits = 64;
        }
        if (bits < 64) {
            size_t nbytes = pack_nbytes((int64_t)n, bits);
            if (r->map_reg)
                return alias_payload(r, T_FVEC, (int64_t)n, bits, -1, nbytes, 2);
            if (need(r, nbytes) != 0)
                return v_err(r->err);
            V *v = v_fvec_bits((int64_t)n, bits);
            if (n) memcpy(v->B, r->p + r->off, nbytes);
            r->off += nbytes;
            return v;
        }
        size_t nbytes = (size_t)n * 8;
        if (r->map_reg)
            return alias_payload(r, T_FVEC, (int64_t)n, 64, -1, nbytes, 1);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_fvec((int64_t)n);
        copy_f64_le(v->F, r->p + r->off, n);
        r->off += nbytes;
        return v;
    }
    case T_BVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: bvec too large");
        size_t nbytes = (r->ver >= 2) ? bbit_nbytes((int64_t)n) : (size_t)n;
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_bvec((int64_t)n);
        if (r->ver >= 2) {
            if (n) unpack_bool_bytes(v->B, r->p + r->off, (int64_t)n);
        } else {
            if (n) memcpy(v->B, r->p + r->off, nbytes);
        }
        r->off += nbytes;
        return v;
    }
    case T_CVEC: {
        if (need(r, 8) != 0)
            return v_err(r->err);
        uint64_t n = get_u64(r->p + r->off);
        r->off += 8;
        if (n > IEFS_MAX_ELEMS)
            return v_err("iefs: cvec too large");
        size_t nbytes = (size_t)n;
        if (r->map_reg)
            return alias_payload(r, T_CVEC, (int64_t)n, 8, -1, nbytes, 2);
        if (need(r, nbytes) != 0)
            return v_err(r->err);
        V *v = v_cvec((int64_t)n);
        if (n) memcpy(v->B, r->p + r->off, nbytes);
        r->off += nbytes;
        return v;
    }
    case T_IMAT:
    case T_FMAT:
    case T_BMAT:
    case T_CMAT: {
        if (need(r, 16) != 0)
            return v_err(r->err);
        uint64_t rows = get_u64(r->p + r->off);
        uint64_t cols = get_u64(r->p + r->off + 8);
        r->off += 16;
        if (rows >= IEFS_MAX_ELEMS || cols >= IEFS_MAX_ELEMS)
            return v_err("iefs: matrix too large");
        if (cols == 0) {
            if (rows != 0) return v_err("iefs: matrix too large");
        } else if (rows > IEFS_MAX_ELEMS / cols) {
            return v_err("iefs: matrix too large");
        }
        uint64_t cells = rows * cols;
        size_t payload = (t == T_BMAT)
            ? ((r->ver >= 2) ? bbit_nbytes((int64_t)cells) : (size_t)cells)
            : (t == T_CMAT ? (size_t)cells : (size_t)cells * 8);
        if (r->map_reg && t != T_BMAT) {
            int which = (t == T_FMAT) ? 1 : (t == T_IMAT ? 0 : 2);
            int bits = t == T_BMAT ? 1 : (t == T_CMAT ? 8 : 64);
            return alias_payload(r, (int)t, (int64_t)rows, bits, (int64_t)cols, payload, which);
        }
        if (need(r, payload) != 0)
            return v_err(r->err);
        V *v;
        if (t == T_IMAT) {
            v = v_imat((int64_t)rows, (int64_t)cols);
            copy_i64_le(v->J, r->p + r->off, cells);
            r->off += payload;
        } else if (t == T_FMAT) {
            v = v_fmat((int64_t)rows, (int64_t)cols);
            copy_f64_le(v->F, r->p + r->off, cells);
            r->off += payload;
        } else if (t == T_CMAT) {
            v = v_cmat((int64_t)rows, (int64_t)cols);
            if (cells) memcpy(v->B, r->p + r->off, payload);
            r->off += payload;
        } else {
            v = v_bmat((int64_t)rows, (int64_t)cols);
            if (r->ver >= 2) {
                if (cells) unpack_bool_bytes(v->B, r->p + r->off, (int64_t)cells);
                r->off += payload;
            } else {
                if (cells) memcpy(v->B, r->p + r->off, (size_t)cells);
                r->off += (size_t)cells;
            }
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
        V *v = v_list((int64_t)n);
        for (uint64_t i = 0; i < n; i++) {
            V *item = decode_value(r);
            if (item->t == T_ERR) {
                v_free(v);
                return item;
            }
            v->L[i] = item; /* transfer ownership */
        }
        return v;
    }
    case T_DICT:
    case T_TABLE: {
        V *keys = decode_value(r);
        if (keys->t == T_ERR)
            return keys;
        V *vals = decode_value(r);
        if (vals->t == T_ERR) {
            v_free(keys);
            return vals;
        }
        V *out = (t == T_DICT) ? v_dict(keys, vals) : v_table(keys, vals);
        v_free(keys);
        v_free(vals);
        return out;
    }
    default:
        snprintf(r->err, sizeof r->err, "iefs: unknown type tag %u", (unsigned)t);
        return v_err(r->err);
    }
}


static int iefs_colnames_active(V *colnames) {
    return colnames && colnames->t == T_LIST && colnames->n > 0;
}

static int iefs_want_col(V *colnames, const char *name) {
    if (!iefs_colnames_active(colnames))
        return 1;
    if (!name || !name[0])
        return 0;
    for (int64_t i = 0; i < colnames->n; i++) {
        V *s = colnames->L[i];
        if (s && s->t == T_STR && s->s && strcmp(s->s, name) == 0)
            return 1;
    }
    return 0;
}

static V *iefs_table_select_cols(V *tbl, V *colnames);

static V *iefs_decode_v3(const unsigned char *buf, size_t len, IefsMapRegion *reg, int verify_crc,
                         V *colnames) {
    uint64_t payload_len = get_u64(buf + 8);
    uint32_t expect_crc = get_u32(buf + 16);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *body = buf + IEFS_HEADER_SIZE;
    if (verify_crc) {
        uint32_t got = crc32_iefs_layout(buf, body, (size_t)payload_len);
        if (got != expect_crc) {
            uint32_t pay_crc = crc32_buf(body, (size_t)payload_len);
            if (pay_crc != expect_crc)
                return v_err("iefs: checksum mismatch");
        }
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
    V **cols = calloc(n_ext ? n_ext : 1, sizeof(V *));
    V **keys = calloc(n_ext ? n_ext : 1, sizeof(V *));
    if (!cols || !keys) {
        free(cols); free(keys);
        return v_err("iefs: out of memory");
    }
    int named = 0;
    uint32_t n_keep = 0;
    for (uint32_t i = 0; i < n_ext; i++) {
        const unsigned char *er = body + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
        int type = er[0];
        int bits = er[1];
        int codec = er[2];
        int outer = er[3];
        uint64_t nelem = get_u64(er + 4);
        uint64_t ncols = get_u64(er + 12);
        uint64_t file_off = get_u64(er + 20);
        uint64_t elen = get_u64(er + 28);
        uint32_t ecrc = get_u32(er + 36);
        uint32_t name_off = get_u32(er + 40);
        if (file_off < IEFS_HEADER_SIZE || elen > len || file_off > len - elen)
            { /* cleanup */ for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys); return v_err("iefs: bad extent offset"); }
        const char *cname = NULL;
        if (name_off != 0xffffffffu && name_off < names_len) {
            size_t rem = (size_t)names_len - (size_t)name_off;
            int has_nul = 0;
            for (size_t k = 0; k < rem; k++) {
                if (names[name_off + k] == 0) { has_nul = 1; break; }
            }
            if (!has_nul) {
                for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);}
                free(cols); free(keys);
                return v_err("iefs: column name not NUL-terminated");
            }
            cname = (const char *)(names + name_off);
        }
        int keep_unnamed_tlv = (type == (int)IEFS_EXT_TLV && !cname);
        if (!iefs_want_col(colnames, cname) && !keep_unnamed_tlv)
            continue;
        const unsigned char *ep = buf + file_off;
        if (verify_crc && crc32_buf(ep, (size_t)elen) != ecrc) {
            for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys);
            return v_err("iefs: extent checksum mismatch");
        }
        unsigned char *plain = NULL;
        size_t plain_len = 0;
        unsigned char *mid = NULL;
        size_t mid_len = 0;
        const unsigned char *pay = ep;
        size_t pay_len = (size_t)elen;
        int alias_ok = (reg != NULL) && (codec == SHAKTI_CODEC_NONE) && (outer == SHAKTI_CODEC_NONE) &&
                       (type != (int)IEFS_EXT_TLV);
        if (outer != SHAKTI_CODEC_NONE || codec != SHAKTI_CODEC_NONE) {
            char err[256];
            const unsigned char *cur = ep;
            size_t cur_len = (size_t)elen;
            if (outer != SHAKTI_CODEC_NONE) {
                if (shakti_decompress(outer, ep, (size_t)elen, &mid, &mid_len, err, sizeof err) != 0) {
                    for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys);
                    return v_err(err[0] ? err : "iefs: extent outer decompress failed");
                }
                cur = mid;
                cur_len = mid_len;
            }
            if (codec != SHAKTI_CODEC_NONE) {
                if (shakti_decompress(codec, cur, cur_len, &plain, &plain_len, err, sizeof err) != 0) {
                    free(mid);
                    for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys);
                    return v_err(err[0] ? err : "iefs: extent decompress failed");
                }
                free(mid);
                mid = NULL;
            } else {
                plain = mid;
                plain_len = mid_len;
                mid = NULL;
            }
            pay = plain;
            pay_len = plain_len;
            alias_ok = 0;
        }
        V *col;
        if (type == (int)IEFS_EXT_TLV) {
            IefsR r = {.p = pay, .n = pay_len, .off = 0, .ver = 2, .depth = 0, .err = {0}, .map_reg = NULL};
            col = decode_value(&r);
            free(plain);
            if (!col || col->t == T_ERR) {
                for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys);
                return col ? col : v_err("iefs: TLV extent decode failed");
            }
        } else {
            col = col_raw_import(type, bits ? bits : 64, nelem, ncols, pay, pay_len, reg, alias_ok);
            if (!alias_ok)
                free(plain);
            else
                free(plain); /* plain is NULL when alias */
            if (!col || col->t == T_ERR) {
                for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);} free(cols); free(keys);
                return col ? col : v_err("iefs: extent import failed");
            }
            /* When not aliased, pay pointed at plain which we freed — import copied. */
        }
        cols[n_keep] = col;
        if (cname) {
            keys[n_keep] = v_str(cname);
            named++;
        } else {
            keys[n_keep] = NULL;
        }
        n_keep++;
    }
    if (iefs_colnames_active(colnames)) {
        int bio_blob = (n_keep == 1 && !named && cols[0] && v_is_rel(cols[0]));
        if (!bio_blob) {
        for (int64_t ci = 0; ci < colnames->n; ci++) {
            V *want = colnames->L[ci];
            const char *wn = (want && want->t == T_STR) ? want->s : NULL;
            if (!wn) {
                for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);}
                free(cols); free(keys);
                return v_err("iefs: column names must be strings");
            }
            int found = 0;
            for (uint32_t j = 0; j < n_keep; j++) {
                if (keys[j] && keys[j]->s && strcmp(keys[j]->s, wn) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                for (uint32_t j=0;j<n_keep;j++){v_free(cols[j]); if(keys[j]) v_free(keys[j]);}
                free(cols); free(keys);
                return v_errf("iefs: column '%s' not in file", wn);
            }
        }
        }
    }
    V *out;
    if (n_keep == 1 && !named) {
        out = cols[0];
        free(cols); free(keys);
        if (iefs_colnames_active(colnames) && v_is_rel(out))
            return iefs_table_select_cols(out, colnames);
        return out;
    }
    if (n_keep == 0) {
        free(cols); free(keys);
        return v_err("iefs: no columns");
    }
    V *klist = v_list((int64_t)n_keep);
    V *vlist = v_list((int64_t)n_keep);
    for (uint32_t i = 0; i < n_keep; i++) {
        klist->L[i] = keys[i] ? keys[i] : v_str("");
        vlist->L[i] = cols[i];
    }
    free(cols); free(keys);
    out = v_table_own(klist, vlist);
    return out;
}

static V *iefs_check_type_layout(const unsigned char *buf) {
    uint32_t layout = get_u32(buf + 20);
    if (layout != IEFS_TYPE_LAYOUT)
        return v_err("iefs: type layout predates char (tag 4 was str); re-save required");
    return NULL;
}

V *iefs_decode_max(const unsigned char *buf, size_t len, size_t max_plain) {
    if (!buf || len < IEFS_HEADER_SIZE)
        return v_err("iefs: truncated header");
    if (max_plain == 0)
        return v_err("iefs: max_plain=0");
    if (memcmp(buf, IEFS_MAGIC, 4) != 0)
        return v_err("iefs: bad magic");
    V *layout_err = iefs_check_type_layout(buf);
    if (layout_err)
        return layout_err;
    uint16_t ver = get_u16(buf + 4);
    if (ver != 1 && ver != 2 && ver != 3)
        return v_errf("iefs: unsupported version %u", (unsigned)ver);
    if (ver == 3) {
        uint64_t v3_payload = get_u64(buf + 8);
        /* Reject oversized TOC bodies; capped max_plain still unsupported for
         * per-extent codecs (IPC uses format 2). */
        if (v3_payload > max_plain)
            return v_err("iefs: payload too large");
        if (max_plain < IEFS_MAX_PAYLOAD)
            return v_err("iefs: max_plain not supported for format 3");
        return iefs_decode_v3(buf, len, NULL, 1, NULL);
    }
    uint16_t flags = get_u16(buf + 6);
    int codec = iefs_codec_from_flags(flags);
    if (codec < 0)
        return v_err("iefs: unknown compression flags");
    uint64_t payload_len = get_u64(buf + 8);
    uint32_t expect_crc = get_u32(buf + 16);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (codec == SHAKTI_CODEC_NONE && payload_len > max_plain)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *payload = buf + IEFS_HEADER_SIZE;
    uint32_t got_crc = crc32_iefs_layout(buf, payload, (size_t)payload_len);
    if (got_crc != expect_crc) {
        uint32_t pay_crc = crc32_buf(payload, (size_t)payload_len);
        if (pay_crc != expect_crc)
            return v_err("iefs: checksum mismatch");
    }

    unsigned char *plain = NULL;
    size_t plain_len = 0;
    const unsigned char *tlv = payload;
    size_t tlv_len = (size_t)payload_len;
    if (codec != SHAKTI_CODEC_NONE) {
        char err[256];
#if defined(SHAKTI_HAVE_ZSTD)
        if (codec == SHAKTI_CODEC_ZSTD) {
            unsigned long long need = ZSTD_getFrameContentSize(payload, (size_t)payload_len);
            if (need == ZSTD_CONTENTSIZE_ERROR)
                return v_err("iefs: zstd invalid frame");
            if (need != ZSTD_CONTENTSIZE_UNKNOWN && need > max_plain)
                return v_err("iefs: payload too large");
        }
#endif
#if defined(SHAKTI_HAVE_SNAPPY)
        if (codec == SHAKTI_CODEC_SNAPPY) {
            size_t need = 0;
            if (snappy_uncompressed_length((const char *)payload, (size_t)payload_len, &need) ==
                SNAPPY_OK) {
                if (need > max_plain)
                    return v_err("iefs: payload too large");
            }
        }
#endif
        if (shakti_decompress_max(codec, payload, (size_t)payload_len, &plain, &plain_len, max_plain,
                                  err, sizeof err) != 0)
            return v_err(err[0] ? err : "iefs: decompress failed");
        if (plain_len > IEFS_MAX_PAYLOAD || plain_len > max_plain) {
            free(plain);
            return v_err("iefs: payload too large");
        }
        tlv = plain;
        tlv_len = plain_len;
    }

    IefsR r = {.p = tlv, .n = tlv_len, .off = 0, .ver = ver, .depth = 0, .err = {0}, .map_reg = NULL};
    V *v = decode_value(&r);
    free(plain);
    if (!v)
        return v_err("iefs: decode failed");
    if (v->t == T_ERR)
        return v;
    if (r.off != r.n) {
        v_free(v);
        return v_err("iefs: trailing payload bytes");
    }
    return v;
}

V *iefs_decode(const unsigned char *buf, size_t len) {
    return iefs_decode_max(buf, len, (size_t)IEFS_MAX_PAYLOAD);
}

V *iefs_decode_mapped(const unsigned char *buf, size_t len, IefsMapRegion *reg) {
    return iefs_decode_mapped_cols(buf, len, reg, NULL);
}

static V *iefs_table_select_cols(V *tbl, V *colnames) {
    if (!iefs_colnames_active(colnames))
        return tbl;
    if (!tbl || tbl->t == T_ERR)
        return tbl;
    if (!v_is_rel(tbl) || !tbl->keys || !tbl->vals) {
        v_free(tbl);
        return v_err("iefs: column list requires a table");
    }
    V *klist = v_list(colnames->n);
    V *vlist = v_list(colnames->n);
    for (int64_t i = 0; i < colnames->n; i++) {
        V *want = colnames->L[i];
        if (!want || want->t != T_STR || !want->s) {
            v_free(klist);
            v_free(vlist);
            v_free(tbl);
            return v_err("iefs: column names must be strings");
        }
        int found = 0;
        for (int64_t j = 0; j < tbl->keys->n; j++) {
            V *kn = tbl->keys->L[j];
            if (kn && kn->t == T_STR && kn->s && strcmp(kn->s, want->s) == 0) {
                klist->L[i] = v_ref(kn);
                vlist->L[i] = v_ref(tbl->vals->L[j]);
                found = 1;
                break;
            }
        }
        if (!found) {
            v_free(klist);
            v_free(vlist);
            v_free(tbl);
            return v_errf("iefs: column '%s' not in file", want->s);
        }
    }
    {
        int t = tbl->t, codec = (int)64;
        v_free(tbl);
        if (t != T_TABLE) {
            V *out = v_bio_table(t, codec, klist, vlist);
            v_free(klist);
            v_free(vlist);
            return out;
        }
    }
    return v_table_own(klist, vlist);
}

V *iefs_map_v3_cols_pread(int fd, size_t file_len, V *colnames) {
    if (fd < 0 || file_len < IEFS_HEADER_SIZE || !iefs_colnames_active(colnames))
        return NULL;
    unsigned char hdr[IEFS_HEADER_SIZE];
    char err[256];
    if (iefs_io_pread(fd, hdr, IEFS_HEADER_SIZE, 0, err, sizeof err) != 0)
        return NULL;
    if (memcmp(hdr, IEFS_MAGIC, 4) != 0 || get_u16(hdr + 4) != 3)
        return NULL;
    uint64_t payload_len = get_u64(hdr + 8);
    if (payload_len < 8 || payload_len > IEFS_MAX_PAYLOAD ||
        IEFS_HEADER_SIZE + (size_t)payload_len > file_len)
        return v_err("iefs.map: truncated v3 header");
    unsigned char toc_head[8];
    if (iefs_io_pread(fd, toc_head, 8, (off_t)IEFS_HEADER_SIZE, err, sizeof err) != 0)
        return v_err(err[0] ? err : "iefs.map: toc pread");
    uint32_t n_ext = get_u32(toc_head + 0);
    uint32_t names_len = get_u32(toc_head + 4);
    size_t toc_bytes = 8u + (size_t)n_ext * IEFS_V3_EXTENT_SIZE + (size_t)names_len;
    if (n_ext == 0 || n_ext > 1000000u || toc_bytes > (size_t)payload_len)
        return v_err("iefs.map: bad v3 TOC");
    size_t prefix_len = IEFS_HEADER_SIZE + toc_bytes;
    unsigned char *compact = malloc(prefix_len);
    if (!compact)
        return v_err("iefs.map: out of memory");
    if (iefs_io_pread(fd, compact, prefix_len, 0, err, sizeof err) != 0) {
        free(compact);
        return v_err(err[0] ? err : "iefs.map: prefix pread");
    }
    unsigned char *body = compact + IEFS_HEADER_SIZE;
    size_t cursor = prefix_len;
    for (uint32_t i = 0; i < n_ext; i++) {
        /* realloc below can move compact; always recompute TOC pointers. */
        body = compact + IEFS_HEADER_SIZE;
        const unsigned char *names = body + 8 + (size_t)n_ext * IEFS_V3_EXTENT_SIZE;
        unsigned char *er = body + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
        uint32_t name_off = get_u32(er + 40);
        const char *cname = NULL;
        if (name_off != 0xffffffffu && name_off < names_len) {
            /* Mirror iefs_decode_v3: the name must be NUL-terminated inside the
             * names blob before any strcmp runs against it. */
            size_t rem = (size_t)names_len - (size_t)name_off;
            int has_nul = 0;
            for (size_t k = 0; k < rem; k++) {
                if (names[name_off + k] == 0) { has_nul = 1; break; }
            }
            if (!has_nul) {
                free(compact);
                return v_err("iefs.map: column name not NUL-terminated");
            }
            cname = (const char *)(names + name_off);
        }
        uint64_t file_off = get_u64(er + 20);
        uint64_t elen = get_u64(er + 28);
        int type = er[0];
        int keep_unnamed_tlv = (type == (int)IEFS_EXT_TLV && !cname);
        if (!iefs_want_col(colnames, cname) && !keep_unnamed_tlv) {
            /* decode_v3 rejects file_off < header even when elen is 0. */
            put_u64(er + 20, (uint64_t)IEFS_HEADER_SIZE);
            put_u64(er + 28, 0);
            continue;
        }
        if (elen == 0)
            continue;
        if (file_off < IEFS_HEADER_SIZE || !iefs_io_extent_in_bounds(file_off, elen, file_len)) {
            free(compact);
            return v_err("iefs.map: bad extent");
        }
        /* Valid v3 extents are disjoint and in-bounds, so copied bytes can never
         * exceed the file size; reject crafted overlap amplification. */
        if ((uint64_t)(cursor - prefix_len) + elen > (uint64_t)file_len) {
            free(compact);
            return v_err("iefs.map: extents exceed file size");
        }
        unsigned char *grown = realloc(compact, cursor + (size_t)elen);
        if (!grown) {
            free(compact);
            return v_err("iefs.map: out of memory");
        }
        compact = grown;
        body = compact + IEFS_HEADER_SIZE;
        er = body + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
        names = body + 8 + (size_t)n_ext * IEFS_V3_EXTENT_SIZE;
        if (iefs_io_pread(fd, compact + cursor, (size_t)elen, (off_t)file_off, err, sizeof err) != 0) {
            free(compact);
            return v_err(err[0] ? err : "iefs.map: extent pread");
        }
        put_u64(er + 20, (uint64_t)cursor);
        cursor += (size_t)elen;
    }
    put_u64(compact + 8, (uint64_t)(cursor - IEFS_HEADER_SIZE));
    V *v = iefs_decode_mapped_cols(compact, cursor, NULL, colnames);
    free(compact);
    return v;
}

V *iefs_decode_mapped_cols(const unsigned char *buf, size_t len, IefsMapRegion *reg, V *colnames) {
    if (!buf || len < IEFS_HEADER_SIZE)
        return v_err("iefs: truncated header");
    if (memcmp(buf, IEFS_MAGIC, 4) != 0)
        return v_err("iefs: bad magic");
    {
        V *layout_err = iefs_check_type_layout(buf);
        if (layout_err)
            return layout_err;
    }
    uint16_t ver = get_u16(buf + 4);
    if (ver != 1 && ver != 2 && ver != 3)
        return v_errf("iefs: unsupported version %u", (unsigned)ver);
    if (ver == 3)
        return iefs_decode_v3(buf, len, reg, 0, colnames);
    uint16_t flags = get_u16(buf + 6);
    if (flags & IEFS_FLAG_CODEC_MASK)
        return v_err("iefs.map: compressed IEFS requires iefs.load");
    uint64_t payload_len = get_u64(buf + 8);
    if (payload_len > IEFS_MAX_PAYLOAD)
        return v_err("iefs: payload too large");
    if (len < IEFS_HEADER_SIZE + (size_t)payload_len)
        return v_err("iefs: truncated file");
    const unsigned char *payload = buf + IEFS_HEADER_SIZE;
    /* Skip CRC on map path (lazy open). */
    IefsR r = {.p = payload, .n = (size_t)payload_len, .off = 0, .ver = ver, .depth = 0, .err = {0}, .map_reg = reg};
    V *v = decode_value(&r);
    if (v->t == T_ERR)
        return v;
    if (r.off != r.n) {
        v_free(v);
        return v_err("iefs: trailing payload bytes");
    }
    return iefs_table_select_cols(v, colnames);
}

int iefs_store_write_ex(V *v, const char *path, int io_mode, int codec, int level, unsigned ver,
                        char *err, size_t err_cap) {
    return iefs_store_write_full(v, path, io_mode, codec, SHAKTI_CODEC_NONE, level, ver, NULL, err,
                                 err_cap);
}

int iefs_store_write_full(V *v, const char *path, int io_mode, int codec, int outer, int level,
                          unsigned ver, V *codecs, char *err, size_t err_cap) {
    return iefs_store_write_full_sync(v, path, io_mode, codec, outer, level, ver, codecs,
                                      IEFS_IO_SYNC_FULL, err, err_cap);
}

int iefs_store_write_full_sync(V *v, const char *path, int io_mode, int codec, int outer, int level,
                               unsigned ver, V *codecs, int sync, char *err, size_t err_cap) {
    if (ver == 3) {
        IefsV3Prep prep = {0};
        if (iefs_v3_prepare(v, codec, outer, level, codecs, &prep, err, err_cap) != 0)
            return -1;
        uint32_t nreg = 1;
        for (int i = 0; i < prep.n_ext; i++) {
            if (prep.ext[i].stored_len)
                nreg++;
        }
        IefsIoRegion *regs = calloc(nreg, sizeof(*regs));
        if (!regs) {
            iefs_v3_prep_free(&prep);
            set_err(err, err_cap, "iefs: out of memory");
            return -1;
        }
        regs[0].buf = prep.prefix;
        regs[0].len = prep.prefix_len;
        regs[0].off = 0;
        uint32_t k = 1;
        for (int i = 0; i < prep.n_ext; i++) {
            if (!prep.ext[i].stored_len)
                continue;
            regs[k].buf = prep.ext[i].stored;
            regs[k].len = prep.ext[i].stored_len;
            regs[k].off = (uint64_t)prep.file_offs[i];
            k++;
        }
        int rc = iefs_io_write_atomic_regions(path, (uint64_t)prep.total, regs, nreg, sync, err,
                                              err_cap);
        free(regs);
        iefs_v3_prep_free(&prep);
        if (rc != 0) {
            if (err && err_cap && err[0])
                iefs_set_last_error(err);
            return -1;
        }
        iefs_set_last_error(NULL);
        return 0;
    }

    unsigned char *buf = NULL;
    size_t len = 0;
    if (iefs_encode_full(v, codec, outer, level, ver, codecs, &buf, &len, err, err_cap) != 0)
        return -1;
    int rc = iefs_io_write_atomic_ex(path, buf, len, io_mode, sync, err, err_cap);
    iefs_io_free_buf(buf);
    if (rc != 0) {
        if (err && err_cap && err[0])
            iefs_set_last_error(err);
        return -1;
    }
    iefs_set_last_error(NULL);
    return 0;
}

int iefs_store_write_codec(V *v, const char *path, int io_mode, int codec, int level,
                           char *err, size_t err_cap) {
    unsigned ver = IEFS_VERSION;
    const char *ev = getenv("SHAKTI_IEFS_VERSION");
    if (ev && ev[0] == '3')
        ver = 3;
    return iefs_store_write_ex(v, path, io_mode, codec, level, ver, err, err_cap);
}

int iefs_store_write(V *v, const char *path, int io_mode, char *err, size_t err_cap) {
    return iefs_store_write_codec(v, path, io_mode, SHAKTI_CODEC_NONE, 0, err, err_cap);
}

V *iefs_store_read(const char *path) {
    unsigned char *buf = NULL;
    size_t len = 0;
    char err[256];

    /* v3: one open — parse TOC, validate extents, assemble via pread on same fd. */
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        struct stat st_buf;
        unsigned char hdr[IEFS_HEADER_SIZE];
        int use_v3 = 0;
        uint32_t n_ext = 0;
        uint64_t *offs = NULL;
        uint64_t *lens = NULL;
        if (fstat(fd, &st_buf) == 0 && S_ISREG(st_buf.st_mode) && st_buf.st_size >= 0 &&
            iefs_io_pread(fd, hdr, IEFS_HEADER_SIZE, 0, err, sizeof err) == 0 &&
            memcmp(hdr, IEFS_MAGIC, 4) == 0 && get_u16(hdr + 4) == 3) {
            uint64_t payload_len = get_u64(hdr + 8);
            unsigned char toc_head[8];
            size_t file_len = (size_t)st_buf.st_size;
            if (payload_len >= 8 && payload_len <= IEFS_MAX_PAYLOAD &&
                IEFS_HEADER_SIZE + (size_t)payload_len <= file_len &&
                iefs_io_pread(fd, toc_head, 8, (off_t)IEFS_HEADER_SIZE, err, sizeof err) == 0) {
                n_ext = get_u32(toc_head + 0);
                uint32_t names_len = get_u32(toc_head + 4);
                size_t toc_bytes = 8u + (size_t)n_ext * IEFS_V3_EXTENT_SIZE + (size_t)names_len;
                if (n_ext > 0 && n_ext < 1000000u && toc_bytes <= (size_t)payload_len) {
                    size_t toc_total = IEFS_HEADER_SIZE + toc_bytes;
                    unsigned char *toc = malloc(toc_total);
                    offs = calloc(n_ext, sizeof(uint64_t));
                    lens = calloc(n_ext, sizeof(uint64_t));
                    if (toc && offs && lens &&
                        iefs_io_pread(fd, toc, toc_total, 0, err, sizeof err) == 0) {
                        const unsigned char *body = toc + IEFS_HEADER_SIZE;
                        int ok = 1;
                        for (uint32_t i = 0; i < n_ext; i++) {
                            const unsigned char *er = body + 8 + (size_t)i * IEFS_V3_EXTENT_SIZE;
                            offs[i] = get_u64(er + 20);
                            lens[i] = get_u64(er + 28);
                            /* Same gate as iefs_decode_v3 before any assemble I/O. */
                            if (lens[i] != 0 &&
                                (offs[i] < IEFS_HEADER_SIZE ||
                                 !iefs_io_extent_in_bounds(offs[i], lens[i], file_len))) {
                                ok = 0;
                                break;
                            }
                        }
                        use_v3 = ok;
                    }
                    free(toc);
                }
            }
        }
        if (use_v3) {
            int rc = iefs_io_read_v3_assembled_fd(fd, (size_t)st_buf.st_size, &buf, &len, offs,
                                                  lens, n_ext, err, sizeof err);
            close(fd);
            free(offs);
            free(lens);
            if (rc == 0) {
                V *v = iefs_decode(buf, len);
                free(buf);
                if (v->t == T_ERR)
                    iefs_set_last_error(v->s);
                else
                    iefs_set_last_error(NULL);
                return v;
            }
            buf = NULL;
            len = 0;
        } else {
            close(fd);
            free(offs);
            free(lens);
        }
    }

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
        *out_mode = v->j ? IEFS_IO_DIRECT : IEFS_IO_BUF;
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
    if (e && (*e == '1' || *e == 'y' || *e == 'Y'))
        return IEFS_IO_DIRECT;
    if (e && (*e == '0' || *e == 'n' || *e == 'N'))
        return IEFS_IO_BUF;
    return IEFS_IO_AUTO;
}

static unsigned iefs_format_default(void) {
    const char *ev = getenv("SHAKTI_IEFS_VERSION");
    if (ev && ev[0] == '3')
        return 3;
    return IEFS_VERSION;
}

static int iefs_sync_from_arg(V *v, int *out_sync) {
    if (!v || v->t != T_STR || !v->s)
        return -1;
    if (!v->s[0] || !strcmp(v->s, "full")) {
        *out_sync = IEFS_IO_SYNC_FULL;
        return 0;
    }
    if (!strcmp(v->s, "data")) {
        *out_sync = IEFS_IO_SYNC_DATA;
        return 0;
    }
    if (!strcmp(v->s, "none") || !strcmp(v->s, "off")) {
        *out_sync = IEFS_IO_SYNC_NONE;
        return 0;
    }
    return -1;
}

V *bi_iefs_save(V **a, int n) {
    /* iefs_save(value, path[, direct[, codec[, level[, format[, codecs[, sync]]]]]])
     * n==8 from lib/iefs.ie: direct, codec, level, format, codecs, sync. */
    if (n < 2 || !a[1] || a[1]->t != T_STR)
        return v_err("iefs_save(value, path[, direct[, codec[, level[, format[, codecs[, sync]]]]]])");
    int mode = iefs_mode_from_env();
    int codec = SHAKTI_CODEC_NONE;
    int outer = SHAKTI_CODEC_NONE;
    int level = 3;
    unsigned ver = iefs_format_default();
    V *codecs = NULL;
    int sync = IEFS_IO_SYNC_FULL;
    if (n >= 8) {
        if (!a[2] || !a[3] || !a[4] || !a[5] || !a[6] || !a[7])
            return v_err("iefs_save(value, path[, direct[, codec[, level[, format[, codecs[, sync]]]]]])");
        if (iefs_mode_from_arg(a[2], &mode) != 0)
            return v_err("iefs_save: direct must be bool/int or \"auto\"/\"direct\"/\"buf\"");
        if (!a[3] || a[3]->t != T_STR)
            return v_err("iefs_save: codec must be a codec name string");
        codec = shakti_codec_parse2(a[3]->s, &outer);
        if (codec < 0)
            return v_err("iefs_save: unknown codec");
        if (a[4]->t != T_INT )
            return v_err("iefs_save: level must be int");
        level = (int)a[4]->j;
        if (a[5]->t != T_INT )
            return v_err("iefs_save: format must be 2 or 3");
        if (a[5]->j != 0) {
            ver = (unsigned)a[5]->j;
            if (ver != 2 && ver != 3)
                return v_err("iefs_save: format must be 2 or 3");
        }
        if (a[6]->t == T_DICT)
            codecs = a[6];
        else if (!(a[6]->t == T_LIST && a[6]->n == 0))
            return v_err("iefs_save: codecs must be a dict");
        if (iefs_sync_from_arg(a[7], &sync) != 0)
            return v_err("iefs_save: sync must be \"full\", \"data\", or \"none\"");
    } else {
        int argi = 2;
        if (n > argi) {
            int c = (a[argi]->t == T_STR) ? shakti_codec_parse2(a[argi]->s, &outer) : -2;
            if (c >= 0) {
                codec = c;
                argi++;
            } else {
                if (iefs_mode_from_arg(a[argi], &mode) != 0)
                    return v_err("iefs_save: direct must be bool/int or \"auto\"/\"direct\"/\"buf\"");
                argi++;
                if (n > argi) {
                    if (!a[argi] || a[argi]->t != T_STR)
                        return v_err("iefs_save: codec must be a codec name string");
                    codec = shakti_codec_parse2(a[argi]->s, &outer);
                    if (codec < 0)
                        return v_err("iefs_save: unknown codec");
                    argi++;
                }
            }
        }
        if (n > argi) {
            if (a[argi]->t != T_INT)
                return v_err("iefs_save: level must be int");
            level = (int)a[argi]->j;
            argi++;
        }
        if (n > argi) {
            if (a[argi]->t != T_INT)
                return v_err("iefs_save: format must be 2 or 3");
            ver = (unsigned)a[argi]->j;
            if (ver != 2 && ver != 3)
                return v_err("iefs_save: format must be 2 or 3");
            argi++;
        }
        if (n > argi) {
            if (a[argi]->t != T_DICT && !(a[argi]->t == T_LIST && a[argi]->n == 0))
                return v_err("iefs_save: codecs must be a dict");
            if (a[argi]->t == T_DICT)
                codecs = a[argi];
            argi++;
        }
        if (n > argi) {
            if (iefs_sync_from_arg(a[argi], &sync) != 0)
                return v_err("iefs_save: sync must be \"full\", \"data\", or \"none\"");
        }
    }
    char err[256];
    if (iefs_store_write_full_sync(a[0], a[1]->s, mode, codec, outer, level, ver, codecs, sync, err,
                                   sizeof err) != 0) {
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
        return v_err("iefs_map(path[, pages[, device[, columns]]])");
    int pages = IEFS_MAP_PAGES_THP;
    int gpu_warm = 0;
    V *colnames = NULL;
    int argi = 1;
    if (n > argi && a[argi] && a[argi]->t == T_LIST) {
        colnames = a[argi];
        argi++;
    } else if (n > argi && a[argi] && a[argi]->t == T_STR) {
        if (!strcmp(a[argi]->s, "1g") || !strcmp(a[argi]->s, "1G"))
            pages = IEFS_MAP_PAGES_1G;
        else if (!strcmp(a[argi]->s, "2m") || !strcmp(a[argi]->s, "2M"))
            pages = IEFS_MAP_PAGES_2M;
        else if (!strcmp(a[argi]->s, "thp") || !a[argi]->s[0])
            pages = IEFS_MAP_PAGES_THP;
        else
            return v_err("iefs_map: pages must be \"thp\", \"2m\", or \"1g\"");
        argi++;
        if (n > argi && a[argi] && a[argi]->t == T_STR) {
            if (!strcmp(a[argi]->s, "gpu") || !strcmp(a[argi]->s, "GPU"))
                gpu_warm = 1;
            else if (a[argi]->s[0] && strcmp(a[argi]->s, "cpu") != 0 && strcmp(a[argi]->s, "") != 0)
                return v_err("iefs_map: device must be \"\" or \"gpu\"");
            argi++;
        }
        if (n > argi && a[argi] && a[argi]->t == T_LIST)
            colnames = a[argi];
    } else if (n > argi && a[argi]) {
        return v_err("iefs_map(path[, pages[, device[, columns]]])");
    }
    if (colnames) {
        if (colnames->t != T_LIST)
            return v_err("iefs_map: columns must be a list of strings");
        for (int64_t i = 0; i < colnames->n; i++) {
            V *s = colnames->L[i];
            if (!s || s->t != T_STR)
                return v_err("iefs_map: columns must be a list of strings");
        }
    }
    if (gpu_warm) return v_err("iefs_map: device gpu not supported");
    return iefs_store_map_cols(a[0]->s, pages, colnames);
}

V *bi_iefs_direct_available(V **a, int n) {
    (void)a;
    (void)n;
    return v_bool(iefs_io_direct_available());
}

V *bi_iefs_uring_available(V **a, int n) {
    (void)a;
    (void)n;
    return v_bool(iefs_io_uring_available());
}

V *bi_iefs_libaio_available(V **a, int n) {
    (void)a;
    (void)n;
    return v_bool(iefs_io_libaio_available());
}

static V *u8vec_from_buf(const unsigned char *p, size_t n) {
    V *v = v_cvec((int64_t)n);
    if (n && p && v->B)
        memcpy(v->B, p, n);
    return v;
}

V *bi_iefs_dumps(V **a, int n) {
    /* iefs_dumps(value[, codec[, level[, format[, codecs]]]]) */
    if (n < 1 || !a[0])
        return v_err("iefs_dumps(value[, codec[, level[, format[, codecs]]]])");
    int codec = SHAKTI_CODEC_NONE;
    int outer = SHAKTI_CODEC_NONE;
    int level = 3;
    unsigned ver = iefs_format_default();
    V *codecs = NULL;
    if (n > 1) {
        if (!a[1] || a[1]->t != T_STR)
            return v_err("iefs_dumps: codec must be a codec name string");
        codec = shakti_codec_parse2(a[1]->s, &outer);
        if (codec < 0)
            return v_err("iefs_dumps: unknown codec");
    }
    if (n > 2) {
        if (a[2]->t != T_INT )
            return v_err("iefs_dumps: level must be int");
        level = (int)a[2]->j;
    }
    if (n > 3) {
        if (a[3]->t != T_INT )
            return v_err("iefs_dumps: format must be 2 or 3");
        ver = (unsigned)a[3]->j;
        if (ver != 2 && ver != 3)
            return v_err("iefs_dumps: format must be 2 or 3");
    }
    if (n > 4) {
        if (a[4]->t != T_DICT && !(a[4]->t == T_LIST && a[4]->n == 0))
            return v_err("iefs_dumps: codecs must be a dict");
        if (a[4]->t == T_DICT)
            codecs = a[4];
    }
    unsigned char *buf = NULL;
    size_t len = 0;
    char err[256];
    if (iefs_encode_full(a[0], codec, outer, level, ver, codecs, &buf, &len, err, sizeof err) != 0)
        return v_err(err[0] ? err : "iefs_dumps failed");
    V *r = u8vec_from_buf(buf, len);
    iefs_io_free_buf(buf);
    return r;
}

V *bi_iefs_loads(V **a, int n) {
    if (n < 1 || !a[0] || a[0]->t != T_CVEC)
        return v_err("iefs_loads(list[char][, max_plain])");
    size_t len = a[0]->n > 0 ? (size_t)a[0]->n : 0;
    const unsigned char *p = (len && a[0]->B) ? a[0]->B : (const unsigned char *)"";
    size_t max_plain = (size_t)IEFS_MAX_PAYLOAD;
    if (n > 1) {
        if (!a[1] || a[1]->t != T_INT)
            return v_err("iefs_loads: max_plain must be int");
        if (a[1]->j < 0)
            return v_err("iefs_loads: max_plain must be >= 0");
        max_plain = (size_t)a[1]->j;
    }
    return iefs_decode_max(p, len, max_plain);
}
