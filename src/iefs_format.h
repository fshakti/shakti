/*
 * IE file store (IEFS) — versioned little-endian durable V codec.
 * Magic "IEF1", CRC32 over payload, atomic writers via iefs_io.
 * Language surface: import iefs → iefs.save / iefs.load / iefs.map /
 * iefs.dumps / iefs.loads (see lib/iefs.ie).
 *
 * map: mmap + alias payloads (skip CRC); load: full read + CRC + copy.
 * Optional payload compression via header flags (zstd / snappy); map rejects
 * compressed files (requires load / loads).
 * pages:"1g"/"2m" via MAP_HUGETLB when pre-reserved.
 */
#ifndef SHAKTI_IEFS_FORMAT_H
#define SHAKTI_IEFS_FORMAT_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IEFS_MAGIC "IEF1"
#define IEFS_VERSION 2u          /* default write version */
#define IEFS_VERSION_MAX 3u
#define IEFS_TYPE_LAYOUT 1u      /* 0 = pre-char (tag 4 was str); 1 = char=4, str=5 */
#define IEFS_HEADER_SIZE 24u
#define IEFS_MAX_PAYLOAD (64ull << 30) /* 64 GiB hard cap (was 16; Basic one-day quotes ~25 GiB) */
#define IEFS_MAX_ELEMS (1ull << 32)
#define IEFS_MAX_NESTING 256 /* max decode_value recursion (list/dict/table) */
#define IEFS_V3_ALIGN (2u << 20) /* 2 MiB extent alignment */
#define IEFS_V3_EXTENT_SIZE 48u
#define IEFS_EXT_TLV 0xFEu       /* nested v2 TLV blob extent */

/* Header flags (u16 @ offset 6). Mutually exclusive codecs (v1/v2 whole-file). */
#define IEFS_FLAG_ZSTD   0x0001u
#define IEFS_FLAG_SNAPPY 0x0002u
#define IEFS_FLAG_CODEC_MASK (IEFS_FLAG_ZSTD | IEFS_FLAG_SNAPPY)

struct IefsMapRegion; /* iefs_map.h */

int iefs_encode(V *v, unsigned char **out, size_t *out_len, char *err, size_t err_cap);
int iefs_encode_codec(V *v, int codec, int level, unsigned char **out, size_t *out_len,
                      char *err, size_t err_cap);
/* ver: 2 (TLV) or 3 (TOC + 2MiB extents). codec is whole-file (v2) or per-extent (v3). */
int iefs_encode_ex(V *v, int codec, int level, unsigned ver, unsigned char **out, size_t *out_len,
                   char *err, size_t err_cap);
/* outer: optional LZ after residual (v3 er[3]). codecs: optional dict name→codec string (v3). */
int iefs_encode_full(V *v, int codec, int outer, int level, unsigned ver, V *codecs,
                     unsigned char **out, size_t *out_len, char *err, size_t err_cap);
V *iefs_decode(const unsigned char *buf, size_t len);
/* Like iefs_decode, but rejects uncompressed/compressed payloads larger than max_plain. */
V *iefs_decode_max(const unsigned char *buf, size_t len, size_t max_plain);
V *iefs_decode_mapped(const unsigned char *buf, size_t len, struct IefsMapRegion *reg);
/* colnames: list[str] of table columns to materialize (v3 skips other extents). NULL/empty = all. */
V *iefs_decode_mapped_cols(const unsigned char *buf, size_t len, struct IefsMapRegion *reg, V *colnames);
/* v3 + colnames: pread stored extent bytes only (skip 2MiB alignment holes).
 * Returns NULL if the fd is not IEFS v3 (caller should fall back to mmap). */
V *iefs_map_v3_cols_pread(int fd, size_t file_len, V *colnames);

int iefs_store_write(V *v, const char *path, int io_mode, char *err, size_t err_cap);
int iefs_store_write_codec(V *v, const char *path, int io_mode, int codec, int level,
                           char *err, size_t err_cap);
int iefs_store_write_ex(V *v, const char *path, int io_mode, int codec, int level, unsigned ver,
                        char *err, size_t err_cap);
int iefs_store_write_full(V *v, const char *path, int io_mode, int codec, int outer, int level,
                          unsigned ver, V *codecs, char *err, size_t err_cap);
int iefs_store_write_full_sync(V *v, const char *path, int io_mode, int codec, int outer, int level,
                               unsigned ver, V *codecs, int sync, char *err, size_t err_cap);
V *iefs_store_read(const char *path);

const char *iefs_last_error(void);
void iefs_set_last_error(const char *msg);

V *bi_iefs_save(V **a, int n);
V *bi_iefs_load(V **a, int n);
V *bi_iefs_map(V **a, int n);
V *bi_iefs_direct_available(V **a, int n);
V *bi_iefs_uring_available(V **a, int n);
V *bi_iefs_libaio_available(V **a, int n);
V *bi_iefs_dumps(V **a, int n);
V *bi_iefs_loads(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_FORMAT_H */
