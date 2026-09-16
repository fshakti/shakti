/* Host byte codecs: zstd + snappy + Sprintz-shaped residual (optional LZ at build). */
#ifndef SHAKTI_CODEC_H
#define SHAKTI_CODEC_H

#include "shakti.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cap decompress output — hard cap on decompress output. */
#define SHAKTI_CODEC_MAX_PLAIN (16ull << 30)

enum ShaktiCodec {
    SHAKTI_CODEC_NONE = 0,
    SHAKTI_CODEC_ZSTD = 1,
    SHAKTI_CODEC_SNAPPY = 2,
    SHAKTI_CODEC_DELTA_I64 = 3,    /* δδ + zigzag + bitpack (i64 lanes) */
    SHAKTI_CODEC_FIRE_I64 = 4,     /* FIRE forecast + zigzag + bitpack */
    SHAKTI_CODEC_GORILLA_F64 = 5,  /* Gorilla XOR (f64 lanes) */
    SHAKTI_CODEC_DATE = 6,         /* UTC midnight-ms → day δδ (restore midnight-ms) */
    SHAKTI_CODEC_TIME = 7,         /* ms-in-day → 27-bit pack (0..86399999) */
    SHAKTI_CODEC_DATETIME = 8      /* epoch-ms → day δδ + 27-bit time-of-day */
};

/* Parse "zstd" / "snappy" / "delta_i64" / "fire_i64" / "gorilla_f64" /
 * "date" / "time" / "datetime" / "" / NULL.
 * Compound "fire_i64+zstd" / "date+snappy" → residual id; outer via parse2. */
int shakti_codec_parse(const char *name);

/* Like parse, but also returns outer LZ in *outer (NONE if no +suffix). -1 on unknown. */
int shakti_codec_parse2(const char *name, int *outer);

int shakti_codec_available(int codec);

/* 1 if codec is a residual (column) codec that must restore typed lanes. */
int shakti_codec_is_residual(int codec);

/* Compress src → malloc'd *out (*out_len set). Returns 0 on success. */
int shakti_compress(int codec, int level, const unsigned char *src, size_t src_len,
                    unsigned char **out, size_t *out_len, char *err, size_t err_cap);

/* Decompress src → malloc'd *out. Returns 0 on success.
 * Cap is SHAKTI_CODEC_MAX_PLAIN (see shakti_decompress_max for a tighter bound). */
int shakti_decompress(int codec, const unsigned char *src, size_t src_len,
                      unsigned char **out, size_t *out_len, char *err, size_t err_cap);

/* Like shakti_decompress, but rejects / never allocates above max_plain
 * (including ZSTD CONTENTSIZE_UNKNOWN grow loops). */
int shakti_decompress_max(int codec, const unsigned char *src, size_t src_len,
                          unsigned char **out, size_t *out_len, size_t max_plain,
                          char *err, size_t err_cap);

V *bi_compress(V **a, int n);
V *bi_decompress(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_CODEC_H */
