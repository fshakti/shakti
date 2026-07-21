/*
 * IE file store I/O backend.
 * Portable buffered POSIX path; optional Linux O_DIRECT for large payloads.
 */
#ifndef SHAKTI_IEFS_IO_H
#define SHAKTI_IEFS_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    IEFS_IO_BUF = 0,
    IEFS_IO_DIRECT = 1,
    IEFS_IO_AUTO = 2
};

/* Read entire file into a malloc'd buffer (*out_len set). Returns 0 on success. */
int iefs_io_read_all(const char *path, unsigned char **out, size_t *out_len, char *err, size_t err_cap);

/*
 * Atomically write buf[0..len) to path (temp + fsync + rename).
 * mode: IEFS_IO_BUF / IEFS_IO_DIRECT / IEFS_IO_AUTO.
 * AUTO uses O_DIRECT on Linux when len >= threshold (or SHAKTI_IEFS_DIRECT=1).
 */
int iefs_io_write_atomic(const char *path, const unsigned char *buf, size_t len, int mode,
                        char *err, size_t err_cap);

/* Probe: 1 if O_DIRECT path is available on this build/OS. */
int iefs_io_direct_available(void);

/* Default AUTO size threshold in bytes (1 MiB). Overridable via SHAKTI_IEFS_DIRECT_MIN. */
size_t iefs_io_direct_threshold(void);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_IO_H */
