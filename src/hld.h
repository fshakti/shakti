/*
 * HLD1 — HTTP wire wrapper for Shakti values.
 * Payload is an IEFS frame; optional external zstd/snappy via codec.c.
 * Language: import hld → hld.encode / hld.decode (see lib/hld.ie).
 */
#ifndef SHAKTI_HLD_H
#define SHAKTI_HLD_H

#include "shakti.h"
#include "iefs_format.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HLD_MAGIC "HLD1"
#define HLD_HEADER_SIZE 14u
#define HLD_MAX_PAYLOAD IEFS_MAX_PAYLOAD

int hld_encode(V *v, int codec, int level, unsigned char **out, size_t *out_len,
               char *err, size_t err_cap);
V *hld_decode(const unsigned char *buf, size_t len);

V *bi_hld_encode(V **a, int n);
V *bi_hld_decode(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_HLD_H */
