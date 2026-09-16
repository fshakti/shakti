/*
 * IE file store I/O backend.
 * Portable buffered POSIX path; optional Linux O_DIRECT for large payloads;
 * optional io_uring (SHAKTI_HAVE_LIBURING) then libaio (SHAKTI_HAVE_LIBAIO)
 * for batched reads.
 */
#ifndef SHAKTI_IEFS_IO_H
#define SHAKTI_IEFS_IO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IEFS_IO_ALIGN
#define IEFS_IO_ALIGN 4096u
#endif
#ifndef IEFS_IO_THP
#define IEFS_IO_THP (2u << 20) /* 2 MiB PMD / mTHP */
#endif

enum {
    IEFS_IO_BUF = 0,
    IEFS_IO_DIRECT = 1,
    IEFS_IO_AUTO = 2
};

enum {
    IEFS_IO_SYNC_FULL = 0, /* fsync file + best-effort dir fsync (default) */
    IEFS_IO_SYNC_DATA = 1, /* fdatasync file; skip directory fsync */
    IEFS_IO_SYNC_NONE = 2  /* no sync (scratch / tmpfs) */
};

/* Aligned alloc for encode→direct-write: capacity is round_up(logical, IEFS_IO_ALIGN).
 * ≥2MiB: posix_memalign(2MiB) + MADV_HUGEPAGE (no HugeTLB reservation).
 * Large mappings are already zero (mmap); small ones are memset. Free with iefs_io_free_buf. */
void *iefs_io_alloc_buf(size_t logical_len);
void iefs_io_free_buf(void *p);

/* 1 if p is IEFS_IO_ALIGN-aligned. */
int iefs_io_ptr_aligned(const void *p);

/* Sparse / gathered write: one file region at a byte offset. */
typedef struct {
    const unsigned char *buf;
    size_t len;
    uint64_t off;
} IefsIoRegion;

/* Read entire file into a malloc'd / aligned buffer (*out_len = logical size).
 * mode: IEFS_IO_BUF / IEFS_IO_DIRECT / IEFS_IO_AUTO (same knobs as write). */
int iefs_io_read_all_mode(const char *path, unsigned char **out, size_t *out_len, int mode,
                          char *err, size_t err_cap);

/* Convenience: AUTO mode. */
int iefs_io_read_all(const char *path, unsigned char **out, size_t *out_len, char *err,
                     size_t err_cap);

/* Pread len bytes at off into buf. Returns 0 on success. */
int iefs_io_pread(int fd, void *buf, size_t len, off_t off, char *err, size_t err_cap);

/* Overflow-safe: 1 if [off, off+len) fits in a buffer of size n (len==0 always ok). */
int iefs_io_extent_in_bounds(uint64_t off, uint64_t len, size_t n);

/*
 * Assemble v3 from an already-open fd (file_len = st_size). Validates extents first.
 * Does not close fd. Returns 0 on success (*out owned by caller).
 */
int iefs_io_read_v3_assembled_fd(int fd, size_t file_len, unsigned char **out, size_t *out_len,
                                 const uint64_t *offs, const uint64_t *lens, uint32_t n_ext,
                                 char *err, size_t err_cap);

/*
 * Atomically write buf[0..len) to path (temp + fsync + rename).
 * mode: IEFS_IO_BUF / IEFS_IO_DIRECT / IEFS_IO_AUTO.
 * AUTO uses O_DIRECT on Linux when len >= threshold (or SHAKTI_IEFS_DIRECT=1).
 * No-copy O_DIRECT only when buf is aligned and len is a multiple of IEFS_IO_ALIGN.
 */
int iefs_io_write_atomic(const char *path, const unsigned char *buf, size_t len, int mode,
                         char *err, size_t err_cap);

/* Like write_atomic with an explicit durability mode (IEFS_IO_SYNC_*). */
int iefs_io_write_atomic_ex(const char *path, const unsigned char *buf, size_t len, int mode,
                            int sync, char *err, size_t err_cap);

/*
 * Atomic sparse write: ftruncate to file_len, pwrite only regs[] (holes stay sparse zeros).
 * Buffered I/O (O_DIRECT cannot write unaligned v3 extent tails into holes).
 * sync: IEFS_IO_SYNC_*.
 */
int iefs_io_write_atomic_regions(const char *path, uint64_t file_len, const IefsIoRegion *regs,
                                 uint32_t n_reg, int sync, char *err, size_t err_cap);

/* Probe: 1 if O_DIRECT path is available on this build/OS. */
int iefs_io_direct_available(void);

/* Probe: 1 if built with liburing. */
int iefs_io_uring_available(void);

/* Probe: 1 if built with libaio. */
int iefs_io_libaio_available(void);

/* Default AUTO size threshold in bytes (1 MiB). Overridable via SHAKTI_IEFS_DIRECT_MIN. */
size_t iefs_io_direct_threshold(void);

/* Shared AUTO/DIRECT decision (also used by encode alloc). */
int iefs_io_want_direct(size_t len, int mode);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_IO_H */
