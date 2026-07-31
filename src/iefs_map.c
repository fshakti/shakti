/*
 * IEFS mmap region + iefs_store_map entry (pages / GPU warm hint).
 */
#include "iefs_map.h"
#include "iefs_format.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* shakti a.h macros collide with common POSIX names. */
#ifdef st
#undef st
#endif

#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif
#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT 26
#endif
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif
#ifndef MAP_HUGE_1GB
#define MAP_HUGE_1GB (30 << MAP_HUGE_SHIFT)
#endif

IefsMapRegion *iefs_map_region_new(void *base, size_t len, int hugetlb) {
    IefsMapRegion *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->base = base;
    r->len = len;
    r->rc = 1;
    r->hugetlb = hugetlb;
    return r;
}

IefsMapRegion *iefs_map_region_ref(IefsMapRegion *r) {
    if (r)
        r->rc++;
    return r;
}

void iefs_map_region_release(IefsMapRegion *r) {
    if (!r)
        return;
    if (--r->rc > 0)
        return;
    if (r->base && r->len)
        munmap(r->base, r->len);
    free(r);
}

void iefs_v_set_map_alias(V *v, IefsMapRegion *r) {
    if (!v || !r)
        return;
    v->owner_kind = V_OWNER_MAP_ALIAS;
    v->map_reg = iefs_map_region_ref(r);
}

static size_t payload_nbytes(const V *v) {
    if (!v)
        return 0;
    switch (v->t) {
    case T_IVEC:
        return (size_t)(v->n > 0 ? v->n : 0) * 8;
    case T_FVEC:
        return (size_t)(v->n > 0 ? v->n : 0) * 8;
    case T_BVEC:
        return (size_t)(v->n > 0 ? v->n : 0);
    case T_IMAT:
    case T_FMAT: {
        int64_t cols = (int64_t)v->_ht_cap;
        int64_t cells = v->n * cols;
        return (size_t)(cells > 0 ? cells : 0) * 8;
    }
    case T_BMAT: {
        int64_t cols = (int64_t)v->_ht_cap;
        return (size_t)(v->n * cols > 0 ? v->n * cols : 0);
    }
    default:
        return 0;
    }
}

int iefs_v_materialize(V *v) {
    if (!v || v->owner_kind != V_OWNER_MAP_ALIAS)
        return 0;
    size_t nbytes = payload_nbytes(v);
    unsigned char *src = NULL;
    if (v->B)
        src = v->B;
    else if (v->J)
        src = (unsigned char *)v->J;
    else if (v->F)
        src = (unsigned char *)v->F;
    unsigned char *dst = NULL;
    if (nbytes) {
        dst = malloc(nbytes);
        if (!dst)
            return -1;
        if (src)
            memcpy(dst, src, nbytes);
        else
            memset(dst, 0, nbytes);
    }
    IefsMapRegion *reg = (IefsMapRegion *)v->map_reg;
    v->owner_kind = V_OWNER_MALLOC;
    v->map_reg = NULL;
    v->J = NULL;
    v->F = NULL;
    v->B = NULL;
    switch (v->t) {
    case T_IVEC:
    case T_IMAT:
        v->J = (int64_t *)dst;
        break;
    case T_FVEC:
    case T_FMAT:
        v->F = (double *)dst;
        break;
    case T_BVEC:
    case T_BMAT:
        v->B = dst;
        break;
    default:
        free(dst);
        break;
    }
    iefs_map_region_release(reg);
    return 0;
}

static void *mmap_hugetlb(size_t len, int pages_mode, char *err, size_t err_cap) {
#if defined(MAP_HUGETLB)
    size_t page = (pages_mode == IEFS_MAP_PAGES_1G) ? (1ull << 30) : (2ull << 20);
    size_t map_len = (len + page - 1) & ~(page - 1);
    if (map_len == 0)
        map_len = page;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB |
                (pages_mode == IEFS_MAP_PAGES_1G ? MAP_HUGE_1GB : MAP_HUGE_2MB);
    void *p = mmap(NULL, map_len, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED) {
        snprintf(err, err_cap,
                 pages_mode == IEFS_MAP_PAGES_1G
                     ? "iefs.map: 1GiB HugePages not reserved"
                     : "iefs.map: 2MiB HugePages not reserved");
        return MAP_FAILED;
    }
    return p;
#else
    (void)len;
    (void)pages_mode;
    snprintf(err, err_cap, "iefs.map: MAP_HUGETLB not available on this platform");
    return MAP_FAILED;
#endif
}

V *iefs_store_map(const char *path, int pages_mode) {
    if (!path || !path[0])
        return v_err("iefs.map: path");
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char err[256];
        snprintf(err, sizeof err, "iefs.map: open failed (%s)", strerror(errno));
        iefs_set_last_error(err);
        return v_err(err);
    }
    struct stat st_buf;
    if (fstat(fd, &st_buf) < 0 || !S_ISREG(st_buf.st_mode) || st_buf.st_size < 0) {
        close(fd);
        return v_err("iefs.map: bad file");
    }
    size_t len = (size_t)st_buf.st_size;
    if (len < IEFS_HEADER_SIZE) {
        close(fd);
        return v_err("iefs.map: truncated header");
    }
    void *file_map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_map == MAP_FAILED)
        return v_err("iefs.map: mmap failed");

    void *base = file_map;
    size_t base_len = len;
    int hugetlb = 0;
    char err[256];

    if (pages_mode == IEFS_MAP_PAGES_1G || pages_mode == IEFS_MAP_PAGES_2M) {
        void *arena = mmap_hugetlb(len, pages_mode, err, sizeof err);
        if (arena == MAP_FAILED) {
            munmap(file_map, len);
            iefs_set_last_error(err);
            return v_err(err);
        }
        size_t page = (pages_mode == IEFS_MAP_PAGES_1G) ? (1ull << 30) : (2ull << 20);
        size_t map_len = (len + page - 1) & ~(page - 1);
        if (map_len == 0)
            map_len = page;
        memcpy(arena, file_map, len);
        munmap(file_map, len);
        base = arena;
        base_len = map_len;
        hugetlb = 1;
    } else {
#if defined(__linux__)
        (void)madvise(file_map, len, MADV_HUGEPAGE);
#endif
    }

    IefsMapRegion *reg = iefs_map_region_new(base, base_len, hugetlb);
    if (!reg) {
        munmap(base, base_len);
        return v_err("iefs.map: out of memory");
    }

    V *v = iefs_decode_mapped((const unsigned char *)base, len, reg);
    /* Creating ref (rc=1 at new) is distinct from per-alias refs taken in
     * iefs_v_set_map_alias. On decode failure, v_free of the partial tree
     * drops only alias refs and leaves this creating ref, so this release is
     * not a double munmap. On success, leaves keep the region alive. */
    iefs_map_region_release(reg);
    if (v->t == T_ERR)
        iefs_set_last_error(v->s);
    else
        iefs_set_last_error(NULL);
    return v;
}
