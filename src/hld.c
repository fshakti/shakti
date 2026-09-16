/*
 * HLD1 encode/decode — IEFS payload + optional external codec for HTTP bodies.
 *
 * Layout (little-endian):
 *   magic[4]="HLD1", codec:u8, pad:u8=0, raw_len:u32, wire_len:u32, payload[wire_len]
 */
#include "hld.h"
#include "codec.h"
#include "iefs_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static void set_err(char *err, size_t err_cap, const char *msg) {
    if (!err || !err_cap) return;
    if (!msg) {
        err[0] = 0;
        return;
    }
    snprintf(err, err_cap, "%s", msg);
}

static void put_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static uint32_t get_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static V *u8vec_from_buf(const unsigned char *p, size_t n) {
    if (n > (size_t)INT64_MAX)
        return v_err("hld: output too large");
    V *v = v_cvec((int64_t)n);
    if (n && p && v->B)
        memcpy(v->B, p, n);
    return v;
}

int hld_encode(V *v, int codec, int level, unsigned char **out, size_t *out_len,
               char *err, size_t err_cap) {
    if (!out || !out_len) {
        set_err(err, err_cap, "hld_encode: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    if (!v) {
        set_err(err, err_cap, "hld_encode: null value");
        return -1;
    }
    if (codec < 0 ||
        (codec != SHAKTI_CODEC_NONE && codec != SHAKTI_CODEC_ZSTD &&
         codec != SHAKTI_CODEC_SNAPPY)) {
        set_err(err, err_cap, "hld_encode: codec must be none/zstd/snappy");
        return -1;
    }

    unsigned char *iefs = NULL;
    size_t iefs_len = 0;
    if (iefs_encode(v, &iefs, &iefs_len, err, err_cap) != 0)
        return -1;
    if (iefs_len > HLD_MAX_PAYLOAD) {
        iefs_io_free_buf(iefs);
        set_err(err, err_cap, "hld_encode: IEFS payload too large");
        return -1;
    }

    unsigned char *wire = NULL;
    size_t wire_len = 0;
    int wire_arena = 0;
    if (codec == SHAKTI_CODEC_NONE) {
        wire = iefs;
        wire_len = iefs_len;
        wire_arena = 1;
        iefs = NULL; /* ownership moved */
    } else {
        if (shakti_compress(codec, level, iefs, iefs_len, &wire, &wire_len, err, err_cap) !=
            0) {
            iefs_io_free_buf(iefs);
            return -1;
        }
        iefs_io_free_buf(iefs);
        iefs = NULL;
    }

    if (wire_len > HLD_MAX_PAYLOAD) {
        if (wire_arena)
            iefs_io_free_buf(wire);
        else
            free(wire);
        set_err(err, err_cap, "hld_encode: wire payload too large");
        return -1;
    }

    size_t total = (size_t)HLD_HEADER_SIZE + wire_len;
    unsigned char *buf = malloc(total ? total : 1);
    if (!buf) {
        if (wire_arena)
            iefs_io_free_buf(wire);
        else
            free(wire);
        set_err(err, err_cap, "hld_encode: out of memory");
        return -1;
    }
    memcpy(buf, HLD_MAGIC, 4);
    buf[4] = (unsigned char)codec;
    buf[5] = 0;
    put_u32_le(buf + 6, (uint32_t)iefs_len);
    put_u32_le(buf + 10, (uint32_t)wire_len);
    if (wire_len)
        memcpy(buf + HLD_HEADER_SIZE, wire, wire_len);
    if (wire_arena)
        iefs_io_free_buf(wire);
    else
        free(wire);

    *out = buf;
    *out_len = total;
    set_err(err, err_cap, NULL);
    return 0;
}

V *hld_decode(const unsigned char *buf, size_t len) {
    if (!buf || len < HLD_HEADER_SIZE)
        return v_err("hld_decode: truncated header");
    if (memcmp(buf, HLD_MAGIC, 4) != 0)
        return v_err("hld_decode: bad magic");
    int codec = (int)buf[4];
    if (buf[5] != 0)
        return v_err("hld_decode: reserved byte nonzero");
    if (codec != SHAKTI_CODEC_NONE && codec != SHAKTI_CODEC_ZSTD &&
        codec != SHAKTI_CODEC_SNAPPY)
        return v_err("hld_decode: unknown codec");

    uint32_t raw_len = get_u32_le(buf + 6);
    uint32_t wire_len = get_u32_le(buf + 10);
    /* wire/raw lengths are u32 — inherently below HLD_MAX_PAYLOAD (16GiB). */
    if (len < (size_t)HLD_HEADER_SIZE + (size_t)wire_len)
        return v_err("hld_decode: truncated payload");

    const unsigned char *payload = buf + HLD_HEADER_SIZE;
    unsigned char *plain = NULL;
    size_t plain_len = 0;
    char err[256];

    if (codec == SHAKTI_CODEC_NONE) {
        if (wire_len != raw_len)
            return v_err("hld_decode: raw_len mismatch");
        return iefs_decode(payload, wire_len);
    }

    if (shakti_decompress(codec, payload, wire_len, &plain, &plain_len, err, sizeof err) !=
        0)
        return v_err(err[0] ? err : "hld_decode: decompress failed");
    if (plain_len != (size_t)raw_len) {
        free(plain);
        return v_err("hld_decode: raw_len mismatch after decompress");
    }
    V *v = iefs_decode(plain, plain_len);
    free(plain);
    return v;
}

V *bi_hld_encode(V **a, int n) {
    /* hld_encode(value[, codec[, level]]) */
    if (n < 1 || !a[0])
        return v_err("hld_encode(value[, codec[, level]])");
    int codec = SHAKTI_CODEC_NONE;
    int level = 3;
    if (n > 1) {
        if (!a[1] || a[1]->t != T_STR)
            return v_err("hld_encode: codec must be \"\", \"zstd\", or \"snappy\"");
        codec = shakti_codec_parse(a[1]->s);
        if (codec < 0)
            return v_err("hld_encode: codec must be \"\", \"zstd\", or \"snappy\"");
    }
    if (n > 2) {
        if (a[2]->t != T_INT )
            return v_err("hld_encode: level must be int");
        level = (int)a[2]->j;
    }
    unsigned char *buf = NULL;
    size_t len = 0;
    char err[256];
    if (hld_encode(a[0], codec, level, &buf, &len, err, sizeof err) != 0)
        return v_err(err[0] ? err : "hld_encode failed");
    V *r = u8vec_from_buf(buf, len);
    free(buf);
    return r;
}

V *bi_hld_decode(V **a, int n) {
    if (n < 1 || !a[0] || a[0]->t != T_CVEC)
        return v_err("hld_decode(list[char])");
    size_t len = a[0]->n > 0 ? (size_t)a[0]->n : 0;
    const unsigned char *p = (len && a[0]->B) ? a[0]->B : (const unsigned char *)"";
    return hld_decode(p, len);
}
