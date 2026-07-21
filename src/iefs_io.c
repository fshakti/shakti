/*
 * IE file store I/O: buffered + optional Linux O_DIRECT atomic writes.
 *
 * io_uring is intentionally not required for v1 (liburing headers may be
 * absent). O_DIRECT with aligned buffers is the Linux NVMe fast path;
 * add io_uring later behind an optional compile probe for liburing.h.
 */
#include "iefs_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef IEFS_IO_ALIGN
#define IEFS_IO_ALIGN 4096u
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

int iefs_io_direct_available(void) {
#if defined(__linux__) && defined(O_DIRECT)
    return 1;
#else
    return 0;
#endif
}

size_t iefs_io_direct_threshold(void) {
    const char *e = getenv("SHAKTI_IEFS_DIRECT_MIN");
    if (!e || !*e)
        e = getenv("ISOLDE_IEFS_DIRECT_MIN");
    if (e && *e) {
        char *end = NULL;
        unsigned long long v = strtoull(e, &end, 10);
        if (end != e)
            return (size_t)v;
    }
    return IEFS_IO_DEFAULT_DIRECT_MIN;
}

static int want_direct(size_t len, int mode) {
    if (mode == IEFS_IO_BUF)
        return 0;
    if (!iefs_io_direct_available())
        return 0;
    if (mode == IEFS_IO_DIRECT)
        return 1;
    /* AUTO */
    {
        const char *force = getenv("SHAKTI_IEFS_DIRECT");
        if (!force || !*force)
            force = getenv("ISOLDE_IEFS_DIRECT");
        if (force && (*force == '1' || *force == 'y' || *force == 'Y'))
            return 1;
        if (force && (*force == '0' || *force == 'n' || *force == 'N'))
            return 0;
    }
    return len >= iefs_io_direct_threshold();
}

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

int iefs_io_read_all(const char *path, unsigned char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!path || !out || !out_len) {
        set_err(err, err_cap, "iefs_io_read_all: bad args");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_errf(err, err_cap, "iefs open", errno);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        set_errf(err, err_cap, "iefs fstat", errno);
        close(fd);
        return -1;
    }
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
    unsigned char *buf = malloc(n ? n : 1);
    if (!buf) {
        set_err(err, err_cap, "iefs: out of memory");
        close(fd);
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
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    close(fd);
    *out = buf;
    *out_len = got;
    return 0;
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
    size_t padded = (len + (IEFS_IO_ALIGN - 1)) & ~(size_t)(IEFS_IO_ALIGN - 1);
    unsigned char *aligned = (unsigned char *)aligned_alloc_pages(padded ? padded : IEFS_IO_ALIGN);
    if (!aligned)
        return -1;
    memcpy(aligned, buf, len);
    if (padded > len)
        memset(aligned + len, 0, padded - len);
    size_t off = 0;
    while (off < padded) {
        ssize_t w = write(fd, aligned + off, padded - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            free(aligned);
            return -1;
        }
        if (w == 0) {
            free(aligned);
            return -1;
        }
        off += (size_t)w;
    }
    free(aligned);
    /* Truncate to logical length so readers see the real payload size. */
    if (ftruncate(fd, (off_t)len) < 0)
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

int iefs_io_write_atomic(const char *path, const unsigned char *buf, size_t len, int mode,
                        char *err, size_t err_cap) {
    if (!path || (len > 0 && !buf)) {
        set_err(err, err_cap, "iefs_io_write_atomic: bad args");
        return -1;
    }
    char tmp[4096];
    if (make_temp_path(path, tmp, sizeof tmp) != 0) {
        set_err(err, err_cap, "iefs: path too long");
        return -1;
    }

    int use_direct = want_direct(len, mode);
    int fd = -1;
#if defined(__linux__) && defined(O_DIRECT)
    if (use_direct) {
        fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0666);
        if (fd < 0) {
            /* Fall back to buffered if filesystem rejects O_DIRECT. */
            use_direct = 0;
        }
    }
#endif
    if (fd < 0) {
        fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            set_errf(err, err_cap, "iefs open temp", errno);
            return -1;
        }
        use_direct = 0;
    }

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
    if (fsync(fd) != 0) {
        set_errf(err, err_cap, "iefs fsync", errno);
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
    /* Best-effort directory fsync for durability. */
    {
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
    return 0;
}
