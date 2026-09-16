/*
 * IE file store I/O: buffered + optional Linux O_DIRECT + optional io_uring
 * + optional libaio (io_submit) middle tier.
 *
 * O_DIRECT with aligned buffers is the Linux NVMe fast path. Batch backends:
 * io_uring (SHAKTI_HAVE_LIBURING), then libaio (SHAKTI_HAVE_LIBAIO, direct fd
 * or SHAKTI_IEFS_LIBAIO=1), else serial pread.
 * v3 save is sparse pwrite (holes, not O_DIRECT). Durability: fsync / fdatasync / none.
 */
#include "iefs_io.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif

#if defined(SHAKTI_HAVE_LIBURING)
#include <liburing.h>
#endif

#if defined(SHAKTI_HAVE_LIBAIO)
#include <libaio.h>
#endif

#ifndef IEFS_IO_DEFAULT_DIRECT_MIN
#define IEFS_IO_DEFAULT_DIRECT_MIN (1u << 20) /* 1 MiB */
#endif

static void set_err(char *err, size_t err_cap, const char *msg) {
    if (!err || !err_cap)
        return;
    snprintf(err, err_cap, "%s", msg ? msg : "iefs_io error");
}

static void set_errf(char *err, size_t err_cap, const char *fmt, int e) {
    if (!err || !err_cap)
        return;
    if (e)
        snprintf(err, err_cap, "%s: %s", fmt, strerror(e));
    else
        snprintf(err, err_cap, "%s", fmt);
}

static size_t round_up_align(size_t n) {
    return (n + (IEFS_IO_ALIGN - 1)) & ~(size_t)(IEFS_IO_ALIGN - 1);
}


#if defined(__APPLE__) && defined(F_NOCACHE)
static void iefs_maybe_nocache(int fd, size_t len, int mode) {
    if (fd < 0) return;
    if (mode == IEFS_IO_BUF) return;
    if (mode == IEFS_IO_DIRECT || len >= iefs_io_direct_threshold())
        (void)fcntl(fd, F_NOCACHE, 1);
}
#else
static void iefs_maybe_nocache(int fd, size_t len, int mode) {
    (void)fd; (void)len; (void)mode;
}
#endif

int iefs_io_direct_available(void) {
#if defined(__linux__) && defined(O_DIRECT)
    return 1;
#else
    return 0;
#endif
}

int iefs_io_uring_available(void) {
#if defined(SHAKTI_HAVE_LIBURING)
    return 1;
#else
    return 0;
#endif
}

int iefs_io_libaio_available(void) {
#if defined(SHAKTI_HAVE_LIBAIO)
    return 1;
#else
    return 0;
#endif
}

#if defined(SHAKTI_HAVE_LIBAIO)
/* 1 if libaio batch should be attempted for this fd (direct or env force). */
static int iefs_io_want_libaio(int fd) {
    if (!iefs_io_libaio_available())
        return 0;
    {
        const char *force = getenv("SHAKTI_IEFS_LIBAIO");
        if (force && (*force == '1' || *force == 'y' || *force == 'Y'))
            return 1;
        if (force && (*force == '0' || *force == 'n' || *force == 'N'))
            return 0;
    }
#if defined(__linux__) && defined(O_DIRECT)
    {
        int flags = fcntl(fd, F_GETFL);
        if (flags >= 0 && (flags & O_DIRECT))
            return 1;
    }
#else
    (void)fd;
#endif
    return 0;
}
#endif

size_t iefs_io_direct_threshold(void) {
    const char *e = getenv("SHAKTI_IEFS_DIRECT_MIN");
    if (e && *e) {
        char *end = NULL;
        unsigned long long v = strtoull(e, &end, 10);
        if (end != e)
            return (size_t)v;
    }
    return IEFS_IO_DEFAULT_DIRECT_MIN;
}

int iefs_io_want_direct(size_t len, int mode) {
    if (mode == IEFS_IO_BUF)
        return 0;
    if (!iefs_io_direct_available())
        return 0;
    if (mode == IEFS_IO_DIRECT)
        return 1;
    /* AUTO */
    {
        const char *force = getenv("SHAKTI_IEFS_DIRECT");
        if (force && (*force == '1' || *force == 'y' || *force == 'Y'))
            return 1;
        if (force && (*force == '0' || *force == 'n' || *force == 'N'))
            return 0;
    }
    return len >= iefs_io_direct_threshold();
}

int iefs_io_ptr_aligned(const void *p) {
    return p && (((uintptr_t)p & (uintptr_t)(IEFS_IO_ALIGN - 1)) == 0);
}

void *iefs_io_alloc_buf(size_t logical_len) {
    size_t cap = round_up_align(logical_len ? logical_len : 1);
    if (cap == 0)
        cap = IEFS_IO_ALIGN;
    int thp = (cap >= (size_t)IEFS_IO_THP);
    size_t align = thp ? (size_t)IEFS_IO_THP : (size_t)IEFS_IO_ALIGN;
    if (thp)
        cap = (cap + align - 1) & ~(align - 1);
    void *p = NULL;
#if defined(_POSIX_C_SOURCE) || defined(__linux__) || defined(__APPLE__)
    if (posix_memalign(&p, align, cap) != 0)
        return NULL;
#else
    p = malloc(cap);
    if (!p)
        return NULL;
#endif
    if (thp) {
#if defined(__linux__)
        (void)madvise(p, cap, MADV_HUGEPAGE);
#endif
        /* glibc large posix_memalign is mmap (already zero); do not memset holes. */
    } else {
        memset(p, 0, cap);
    }
    return p;
}

void iefs_io_free_buf(void *p) { free(p); }

static void *aligned_alloc_pages(size_t nbytes) {
    void *p = NULL;
#if defined(_POSIX_C_SOURCE) || defined(__linux__) || defined(__APPLE__)
    if (posix_memalign(&p, IEFS_IO_ALIGN, nbytes) != 0)
        return NULL;
    return p;
#else
    return malloc(nbytes);
#endif
}

int iefs_io_pread(int fd, void *buf, size_t len, off_t off, char *err, size_t err_cap) {
    if (fd < 0 || (!buf && len) || off < 0) {
        set_err(err, err_cap, "iefs_io_pread: bad args");
        return -1;
    }
    size_t got = 0;
    while (got < len) {
        ssize_t r = pread(fd, (unsigned char *)buf + got, len - got, off + (off_t)got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            set_errf(err, err_cap, "iefs pread", errno);
            return -1;
        }
        if (r == 0) {
            set_err(err, err_cap, "iefs pread: short read");
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

/* Match iefs_decode_v3: elen > len || file_off > len - elen (no uint64 wrap). */
int iefs_io_extent_in_bounds(uint64_t off, uint64_t len, size_t n) {
    if (len == 0)
        return 1;
    if (len > (uint64_t)n)
        return 0;
    if (off > (uint64_t)n - len)
        return 0;
    return 1;
}

static int extents_overlap(const uint64_t *offs, const uint64_t *lens, uint32_t n_ext) {
    for (uint32_t i = 0; i < n_ext; i++) {
        if (lens[i] == 0)
            continue;
        /* offs+lens already in-bounds ⇒ no wrap */
        uint64_t a0 = offs[i], a1 = offs[i] + lens[i];
        for (uint32_t j = i + 1; j < n_ext; j++) {
            if (lens[j] == 0)
                continue;
            uint64_t b0 = offs[j], b1 = offs[j] + lens[j];
            if (a0 < b1 && b0 < a1)
                return 1;
        }
    }
    return 0;
}

static int validate_extents(const uint64_t *offs, const uint64_t *lens, uint32_t n_ext, size_t n,
                            char *err, size_t err_cap) {
    for (uint32_t i = 0; i < n_ext; i++) {
        if (!iefs_io_extent_in_bounds(offs[i], lens[i], n)) {
            set_err(err, err_cap, "iefs: bad extent range");
            return -1;
        }
    }
    int overlaps = extents_overlap(offs, lens, n_ext);
    if (overlaps) {
        set_err(err, err_cap, "iefs: overlapping extents");
        return -1;
    }
    return 0;
}

#if defined(SHAKTI_HAVE_LIBURING)
/* Returns: 0 ok, -1 hard I/O failure, -2 uring unavailable (caller may serial-fallback). */
static int uring_pread_regions(int fd, unsigned char *base, size_t n, const uint64_t *offs,
                               const uint64_t *lens, uint32_t n_ext, char *err, size_t err_cap) {
    uint32_t todo = 0;
    for (uint32_t i = 0; i < n_ext; i++) {
        if (lens[i] == 0)
            continue;
        if (!iefs_io_extent_in_bounds(offs[i], lens[i], n) || lens[i] > (uint64_t)UINT_MAX) {
            set_err(err, err_cap, "iefs: bad extent range");
            return -1;
        }
        todo++;
    }
    if (todo == 0)
        return 0;

    struct io_uring ring;
    unsigned qd = todo < 64u ? todo : 64u;
    if (io_uring_queue_init(qd, &ring, 0) < 0) {
        set_errf(err, err_cap, "iefs io_uring init", errno);
        return -2;
    }
    uint32_t next = 0;
    uint32_t in_flight = 0;
    uint32_t done = 0;
    int fail = 0;
    while (done < todo && !fail) {
        while (next < n_ext && in_flight < qd) {
            if (lens[next] == 0) {
                next++;
                continue;
            }
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe)
                break;
            io_uring_prep_read(sqe, fd, base + (size_t)offs[next], (unsigned)lens[next],
                               (off_t)offs[next]);
            io_uring_sqe_set_data64(sqe, next);
            next++;
            in_flight++;
        }
        if (in_flight == 0)
            break;
        int ret = io_uring_submit_and_wait(&ring, 1);
        if (ret < 0) {
            set_errf(err, err_cap, "iefs io_uring submit", -ret);
            fail = 1;
            break;
        }
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned nready = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            nready++;
            uint32_t idx = (uint32_t)io_uring_cqe_get_data64(cqe);
            if (cqe->res < 0 || (size_t)cqe->res != (size_t)lens[idx]) {
                set_err(err, err_cap, "iefs io_uring: short or failed read");
                fail = 1;
            }
            if (in_flight)
                in_flight--;
            done++;
        }
        io_uring_cq_advance(&ring, nready);
    }
    io_uring_queue_exit(&ring);
    return fail ? -1 : 0;
}
#endif

#if defined(SHAKTI_HAVE_LIBAIO)
/* Returns: 0 ok, -1 hard I/O failure, -2 setup/unavailable (caller may serial-fallback). */
static int libaio_pread_regions(int fd, unsigned char *base, size_t n, const uint64_t *offs,
                                const uint64_t *lens, uint32_t n_ext, char *err, size_t err_cap) {
    uint32_t todo = 0;
    for (uint32_t i = 0; i < n_ext; i++) {
        if (lens[i] == 0)
            continue;
        if (!iefs_io_extent_in_bounds(offs[i], lens[i], n) || lens[i] > (uint64_t)SIZE_MAX) {
            set_err(err, err_cap, "iefs: bad extent range");
            return -1;
        }
        todo++;
    }
    if (todo == 0)
        return 0;

    unsigned qd = todo < 64u ? todo : 64u;
    io_context_t ctx = 0;
    if (io_setup((int)qd, &ctx) != 0) {
        set_errf(err, err_cap, "iefs libaio setup", errno);
        return -2;
    }

    struct iocb *cbs = (struct iocb *)calloc(qd, sizeof(struct iocb));
    struct iocb **cbp = (struct iocb **)calloc(qd, sizeof(struct iocb *));
    struct io_event *ev = (struct io_event *)calloc(qd, sizeof(struct io_event));
    if (!cbs || !cbp || !ev) {
        free(cbs);
        free(cbp);
        free(ev);
        io_destroy(ctx);
        set_err(err, err_cap, "iefs: out of memory");
        return -1;
    }

    uint32_t next = 0;
    uint32_t done = 0;
    int fail = 0;
    while (done < todo && !fail) {
        unsigned batch = 0;
        uint32_t idxs[64];
        while (next < n_ext && batch < qd) {
            if (lens[next] == 0) {
                next++;
                continue;
            }
            io_prep_pread(&cbs[batch], fd, base + (size_t)offs[next], (size_t)lens[next],
                          (off_t)offs[next]);
            cbs[batch].data = (void *)(uintptr_t)next;
            cbp[batch] = &cbs[batch];
            idxs[batch] = next;
            next++;
            batch++;
        }
        if (batch == 0)
            break;
        int sub = io_submit(ctx, (long)batch, cbp);
        if (sub < 0) {
            set_errf(err, err_cap, "iefs libaio submit", -sub);
            fail = (sub == -EAGAIN) ? 2 : 1; /* 2 → treat as -2 */
            break;
        }
        if (sub == 0) {
            /* No progress — treat as unavailable so caller can serial-fallback. */
            fail = 2;
            break;
        }
        /* io_submit may accept fewer than batch; rewind unsubmitted extents. */
        if ((unsigned)sub < batch)
            next = idxs[sub];
        unsigned got = 0;
        while (got < (unsigned)sub && !fail) {
            int nwait = io_getevents(ctx, 1, (long)(sub - got), ev + got, NULL);
            if (nwait < 0) {
                set_errf(err, err_cap, "iefs libaio getevents", -nwait);
                fail = 1;
                break;
            }
            for (int i = 0; i < nwait; i++) {
                uint32_t idx = (uint32_t)(uintptr_t)ev[got + i].data;
                long res = (long)ev[got + i].res;
                if (res < 0 || (size_t)res != (size_t)lens[idx]) {
                    set_err(err, err_cap, "iefs libaio: short or failed read");
                    fail = 1;
                }
                done++;
            }
            got += (unsigned)nwait;
        }
    }

    free(cbs);
    free(cbp);
    free(ev);
    io_destroy(ctx);
    if (fail == 2)
        return -2;
    return fail ? -1 : 0;
}
#endif

static int pread_extents_serial(int fd, unsigned char *buf, size_t n, const uint64_t *offs,
                                const uint64_t *lens, uint32_t n_ext, char *err, size_t err_cap) {
    for (uint32_t i = 0; i < n_ext; i++) {
        if (lens[i] == 0)
            continue;
        if (!iefs_io_extent_in_bounds(offs[i], lens[i], n)) {
            set_err(err, err_cap, "iefs: bad extent range");
            return -1;
        }
        if (iefs_io_pread(fd, buf + (size_t)offs[i], (size_t)lens[i], (off_t)offs[i], err,
                          err_cap) != 0)
            return -1;
    }
    return 0;
}

int iefs_io_read_v3_assembled_fd(int fd, size_t file_len, unsigned char **out, size_t *out_len,
                                 const uint64_t *offs, const uint64_t *lens, uint32_t n_ext,
                                 char *err, size_t err_cap) {
    if (fd < 0 || !out || !out_len || (n_ext && (!offs || !lens))) {
        set_err(err, err_cap, "iefs_io_read_v3_assembled_fd: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    size_t n = file_len;
    if (validate_extents(offs, lens, n_ext, n, err, err_cap) != 0)
        return -1;

    unsigned char *buf = calloc(n ? n : 1, 1);
    if (!buf) {
        set_err(err, err_cap, "iefs: out of memory");
        return -1;
    }

    size_t prefix = n;
    for (uint32_t i = 0; i < n_ext; i++) {
        if (lens[i] == 0)
            continue;
        if ((size_t)offs[i] < prefix)
            prefix = (size_t)offs[i];
    }
    if (prefix == 0)
        prefix = n;
    if (iefs_io_pread(fd, buf, prefix, 0, err, err_cap) != 0) {
        free(buf);
        return -1;
    }

    if (n_ext > 1) {
        int assembled = 0;
#if defined(SHAKTI_HAVE_LIBURING)
        if (iefs_io_uring_available()) {
            int urc = uring_pread_regions(fd, buf, n, offs, lens, n_ext, err, err_cap);
            if (urc == 0)
                assembled = 1;
            else if (urc == -1) {
                free(buf);
                return -1;
            }
            /* -2: fall through to libaio / serial */
        }
#endif
#if defined(SHAKTI_HAVE_LIBAIO)
        if (!assembled && iefs_io_want_libaio(fd)) {
            int arc = libaio_pread_regions(fd, buf, n, offs, lens, n_ext, err, err_cap);
            if (arc == 0)
                assembled = 1;
            else if (arc == -1) {
                free(buf);
                return -1;
            }
            /* -2: fall through to serial */
        }
#endif
        if (!assembled) {
            if (pread_extents_serial(fd, buf, n, offs, lens, n_ext, err, err_cap) != 0) {
                free(buf);
                return -1;
            }
        }
    } else {
        if (pread_extents_serial(fd, buf, n, offs, lens, n_ext, err, err_cap) != 0) {
            free(buf);
            return -1;
        }
    }
    *out = buf;
    *out_len = n;
    return 0;
}

static int read_all_buffered(int fd, size_t n, unsigned char **out, size_t *out_len, char *err,
                             size_t err_cap) {
    unsigned char *buf = malloc(n ? n : 1);
    if (!buf) {
        set_err(err, err_cap, "iefs: out of memory");
        return -1;
    }
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            set_errf(err, err_cap, "iefs read", errno);
            free(buf);
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    *out = buf;
    *out_len = got;
    return 0;
}

static int read_all_direct(int fd, size_t n, unsigned char **out, size_t *out_len, char *err,
                           size_t err_cap) {
#if defined(__linux__) && defined(O_DIRECT)
    /* Returns: 0 ok, -1 hard fail, -2 retry buffered (EINVAL / unsupported). */
    size_t aligned_n = n & ~(size_t)(IEFS_IO_ALIGN - 1);
    size_t alloc_n = round_up_align(n ? n : 1);
    unsigned char *buf = (unsigned char *)aligned_alloc_pages(alloc_n);
    if (!buf) {
        set_err(err, err_cap, "iefs: out of memory");
        return -1;
    }
    size_t got = 0;
    while (got < aligned_n) {
        ssize_t r = read(fd, buf + got, aligned_n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EINVAL) {
                free(buf);
                return -2;
            }
            set_errf(err, err_cap, "iefs read", errno);
            free(buf);
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    if (got < aligned_n) {
        set_err(err, err_cap, "iefs read: short read");
        free(buf);
        return -1;
    }
    if (n > aligned_n) {
        int flags = fcntl(fd, F_GETFL);
        if (flags >= 0)
            (void)fcntl(fd, F_SETFL, flags & ~O_DIRECT);
        while (got < n) {
            ssize_t r = read(fd, buf + got, n - got);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                set_errf(err, err_cap, "iefs read", errno);
                free(buf);
                return -1;
            }
            if (r == 0)
                break;
            got += (size_t)r;
        }
        if (got < n) {
            set_err(err, err_cap, "iefs read: short read");
            free(buf);
            return -1;
        }
    }
    *out = buf;
    *out_len = n;
    return 0;
#else
    (void)fd;
    (void)n;
    (void)out;
    (void)out_len;
    set_err(err, err_cap, "iefs: O_DIRECT not available");
    return -2;
#endif
}

int iefs_io_read_all_mode(const char *path, unsigned char **out, size_t *out_len, int mode,
                          char *err, size_t err_cap) {
    if (!path || !out || !out_len) {
        set_err(err, err_cap, "iefs_io_read_all: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;

    int use_direct = 0;
    int fd = -1;
#if defined(__linux__) && defined(O_DIRECT)
    {
        struct stat st0;
        if (stat(path, &st0) == 0 && S_ISREG(st0.st_mode) && st0.st_size >= 0)
            use_direct = iefs_io_want_direct((size_t)st0.st_size, mode);
        else if (mode == IEFS_IO_DIRECT)
            use_direct = 1;
    }
    if (use_direct) {
        fd = open(path, O_RDONLY | O_DIRECT);
        if (fd < 0)
            use_direct = 0; /* FS rejected O_DIRECT open → buffered */
    }
#endif
    if (fd < 0) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            set_errf(err, err_cap, "iefs open", errno);
            return -1;
        }
        use_direct = 0;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        set_errf(err, err_cap, "iefs fstat", errno);
        close(fd);
        return -1;
    }
    iefs_maybe_nocache(fd, (size_t)st.st_size, mode);
    if (!S_ISREG(st.st_mode)) {
        set_err(err, err_cap, "iefs: not a regular file");
        close(fd);
        return -1;
    }
    if (st.st_size < 0) {
        set_err(err, err_cap, "iefs: bad size");
        close(fd);
        return -1;
    }
    size_t n = (size_t)st.st_size;
    int rc;
    if (use_direct) {
        rc = read_all_direct(fd, n, out, out_len, err, err_cap);
        if (rc == -2) {
            close(fd);
            fd = open(path, O_RDONLY);
            if (fd < 0) {
                set_errf(err, err_cap, "iefs open", errno);
                return -1;
            }
            rc = read_all_buffered(fd, n, out, out_len, err, err_cap);
        }
    } else {
        rc = read_all_buffered(fd, n, out, out_len, err, err_cap);
    }
    close(fd);
    return rc;
}

int iefs_io_read_all(const char *path, unsigned char **out, size_t *out_len, char *err,
                     size_t err_cap) {
    return iefs_io_read_all_mode(path, out, out_len, IEFS_IO_AUTO, err, err_cap);
}

static int write_all_fd(int fd, const unsigned char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        off += (size_t)w;
    }
    return 0;
}

static int write_direct_fd(int fd, const unsigned char *buf, size_t len) {
#if defined(__linux__) && defined(O_DIRECT)
    if (len == 0)
        return ftruncate(fd, 0) < 0 ? -1 : 0;

    size_t padded = round_up_align(len);
    const unsigned char *src = buf;
    unsigned char *owned = NULL;
    int need_trunc = (padded > len);

    /* No-copy only when aligned and transfer length is ALIGN-multiple (no pad). */
    if (iefs_io_ptr_aligned(buf) && (len % IEFS_IO_ALIGN) == 0) {
        src = buf;
        owned = NULL;
        need_trunc = 0;
        padded = len;
    } else {
        owned = (unsigned char *)aligned_alloc_pages(padded);
        if (!owned)
            return -1;
        memcpy(owned, buf, len);
        if (padded > len)
            memset(owned + len, 0, padded - len);
        src = owned;
        need_trunc = (padded > len);
    }

    size_t off = 0;
    while (off < padded) {
        ssize_t w = write(fd, src + off, padded - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            free(owned);
            return -1;
        }
        if (w == 0) {
            free(owned);
            return -1;
        }
        off += (size_t)w;
    }
    free(owned);
    if (need_trunc && ftruncate(fd, (off_t)len) < 0)
        return -1;
    return 0;
#else
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
#endif
}

static int make_temp_path(const char *path, char *tmp, size_t tmp_cap) {
    if (snprintf(tmp, tmp_cap, "%s.iefs.tmp.%d", path, (int)getpid()) >= (int)tmp_cap)
        return -1;
    return 0;
}

static int sync_fd(int fd, int sync) {
    if (sync == IEFS_IO_SYNC_NONE)
        return 0;
    if (sync == IEFS_IO_SYNC_DATA)
        return fdatasync(fd);
    return fsync(fd);
}

static void dir_fsync_best_effort(const char *path) {
    char dirbuf[4096];
    const char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        size_t dlen = (size_t)(slash - path);
        if (dlen < sizeof dirbuf) {
            memcpy(dirbuf, path, dlen);
            dirbuf[dlen] = 0;
            int dfd = open(dirbuf, O_RDONLY | O_DIRECTORY);
            if (dfd >= 0) {
                (void)fsync(dfd);
                close(dfd);
            }
        }
    }
}

static int finish_atomic(int fd, const char *tmp, const char *path, int sync, char *err,
                         size_t err_cap) {
    if (sync_fd(fd, sync) != 0) {
        set_errf(err, err_cap, sync == IEFS_IO_SYNC_DATA ? "iefs fdatasync" : "iefs fsync", errno);
        close(fd);
        unlink(tmp);
        return -1;
    }
    if (close(fd) != 0) {
        set_errf(err, err_cap, "iefs close", errno);
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        set_errf(err, err_cap, "iefs rename", errno);
        unlink(tmp);
        return -1;
    }
    if (sync == IEFS_IO_SYNC_FULL)
        dir_fsync_best_effort(path);
    return 0;
}

int iefs_io_write_atomic_ex(const char *path, const unsigned char *buf, size_t len, int mode,
                            int sync, char *err, size_t err_cap) {
    if (!path || (len > 0 && !buf)) {
        set_err(err, err_cap, "iefs_io_write_atomic: bad args");
        return -1;
    }
    char tmp[4096];
    if (make_temp_path(path, tmp, sizeof tmp) != 0) {
        set_err(err, err_cap, "iefs: path too long");
        return -1;
    }

    int use_direct = iefs_io_want_direct(len, mode);
    int fd = -1;
#if defined(__linux__) && defined(O_DIRECT)
    if (use_direct) {
        fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0600);
        if (fd < 0) {
            /* Fall back to buffered if filesystem rejects O_DIRECT. */
            use_direct = 0;
        }
    }
#endif
    if (fd < 0) {
        fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            set_errf(err, err_cap, "iefs open temp", errno);
            return -1;
        }
        use_direct = 0;
    }
    iefs_maybe_nocache(fd, len, mode);

    int rc = 0;
    if (use_direct)
        rc = write_direct_fd(fd, buf ? buf : (const unsigned char *)"", len);
    else
        rc = write_all_fd(fd, buf ? buf : (const unsigned char *)"", len);

    if (rc != 0) {
        set_errf(err, err_cap, "iefs write", errno);
        close(fd);
        unlink(tmp);
        return -1;
    }
    return finish_atomic(fd, tmp, path, sync, err, err_cap);
}

int iefs_io_write_atomic(const char *path, const unsigned char *buf, size_t len, int mode,
                         char *err, size_t err_cap) {
    return iefs_io_write_atomic_ex(path, buf, len, mode, IEFS_IO_SYNC_FULL, err, err_cap);
}

static int pwrite_all(int fd, const unsigned char *buf, size_t len, off_t off) {
    size_t got = 0;
    while (got < len) {
        ssize_t w = pwrite(fd, buf + got, len - got, off + (off_t)got);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        got += (size_t)w;
    }
    return 0;
}

int iefs_io_write_atomic_regions(const char *path, uint64_t file_len, const IefsIoRegion *regs,
                                 uint32_t n_reg, int sync, char *err, size_t err_cap) {
    if (!path || (n_reg && !regs) || file_len > (uint64_t)INT64_MAX) {
        set_err(err, err_cap, "iefs_io_write_atomic_regions: bad args");
        return -1;
    }
    for (uint32_t i = 0; i < n_reg; i++) {
        if (regs[i].len == 0)
            continue;
        if (!regs[i].buf) {
            set_err(err, err_cap, "iefs_io_write_atomic_regions: bad args");
            return -1;
        }
        if (regs[i].off > file_len || regs[i].len > file_len - regs[i].off) {
            set_err(err, err_cap, "iefs_io_write_atomic_regions: region past EOF");
            return -1;
        }
    }

    char tmp[4096];
    if (make_temp_path(path, tmp, sizeof tmp) != 0) {
        set_err(err, err_cap, "iefs: path too long");
        return -1;
    }

    int fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        set_errf(err, err_cap, "iefs open temp", errno);
        return -1;
    }
    iefs_maybe_nocache(fd, (size_t)file_len, IEFS_IO_BUF);
    if (ftruncate(fd, (off_t)file_len) != 0) {
        set_errf(err, err_cap, "iefs ftruncate", errno);
        close(fd);
        unlink(tmp);
        return -1;
    }
    for (uint32_t i = 0; i < n_reg; i++) {
        if (regs[i].len == 0)
            continue;
        if (pwrite_all(fd, regs[i].buf, regs[i].len, (off_t)regs[i].off) != 0) {
            set_errf(err, err_cap, "iefs pwrite", errno);
            close(fd);
            unlink(tmp);
            return -1;
        }
    }
    return finish_atomic(fd, tmp, path, sync, err, err_cap);
}
