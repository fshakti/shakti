/*
 * IE file store (IEFS) — versioned little-endian durable V codec.
 * Magic "IEF1", CRC32 over payload, atomic writers via iefs_io.
 * Language surface: import iefs → iefs.save / iefs.load / iefs.map (see lib/iefs.ie).
 *
 * mmap path: iefs.map / iefs_store_map aliases contiguous vector/matrix payloads
 * into an IefsMapRegion (skip CRC). Mutating aliases materializes via
 * v_ensure_writable.
 */
#ifndef SHAKTI_IEFS_FORMAT_H
#define SHAKTI_IEFS_FORMAT_H

#include "shakti.h"

struct IefsMapRegion;

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

/* Decode mmap-backed buffer (skip CRC); alias payloads into reg when non-NULL. */
V *iefs_decode_mapped(const unsigned char *buf, size_t len, struct IefsMapRegion *reg);

/* Save/load helpers (atomic write, owned roundtrip). */
int iefs_store_write(V *v, const char *path, int io_mode, char *err, size_t err_cap);
V *iefs_store_read(const char *path);

const char *iefs_last_error(void);
void iefs_set_last_error(const char *msg);

/* Language builtins (wrapped by lib/iefs.ie). */
V *bi_iefs_save(V **a, int n);
V *bi_iefs_load(V **a, int n);
V *bi_iefs_map(V **a, int n);
V *bi_iefs_direct_available(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_FORMAT_H */
