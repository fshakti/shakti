/*
 * IE file store (IEFS) — versioned little-endian durable V codec.
 * Magic "IEF1", CRC32 over payload, atomic writers via iefs_io.
 * Language surface: import iefs → iefs.save / iefs.load (see lib/iefs.ie).
 *
 * Follow-ups (not in v1):
 * - mmap-backed columns: extend V with owner_kind/map_base/map_len so v_free
 *   can munmap; then load(..., mmap=1) can expose typed extents without copy.
 * - GPUDirect Storage: optional cuFile path on Data Center/Quadro + supported
 *   FS once device-resident tensors exist; GeForce stays host O_DIRECT.
 */
#ifndef SHAKTI_IEFS_FORMAT_H
#define SHAKTI_IEFS_FORMAT_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IEFS_MAGIC "IEF1"
#define IEFS_VERSION 1u
#define IEFS_HEADER_SIZE 24u
#define IEFS_MAX_PAYLOAD (16ull << 30) /* 16 GiB hard cap */
#define IEFS_MAX_ELEMS (1ull << 32)

/* Encode value into a newly malloc'd buffer (*out_len includes header). 0 = ok. */
int iefs_encode(V *v, unsigned char **out, size_t *out_len, char *err, size_t err_cap);

/* Decode buffer; returns owned V or T_ERR. */
V *iefs_decode(const unsigned char *buf, size_t len);

/* Save/load helpers (atomic write, owned roundtrip). */
int iefs_store_write(V *v, const char *path, int io_mode, char *err, size_t err_cap);
V *iefs_store_read(const char *path);

const char *iefs_last_error(void);
void iefs_set_last_error(const char *msg);

/* Language builtins (wrapped by lib/iefs.ie). */
V *bi_iefs_save(V **a, int n);
V *bi_iefs_load(V **a, int n);
V *bi_iefs_direct_available(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_FORMAT_H */
