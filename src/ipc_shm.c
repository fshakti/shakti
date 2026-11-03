#include "ipc_internal.h"

#if defined(SHAKTI_WASM) || (!defined(__linux__) && !defined(__APPLE__))

int ipc_shm_push(IpcShmHdr *hdr, int which, const void *data, uint32_t len, uint32_t cap, char *err, size_t err_cap) {
    (void)hdr; (void)which; (void)data; (void)len; (void)cap;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_pop(IpcShmHdr *hdr, int which, int block, int timeout_ms, uint32_t cap,
                char **out, size_t *out_len, char *err, size_t err_cap) {
    (void)hdr; (void)which; (void)block; (void)timeout_ms; (void)cap; (void)out; (void)out_len;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_send_side(IpcHandle *s, const void *data, size_t len, char *err, size_t err_cap) {
    (void)s; (void)data; (void)len;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_recv_side(IpcHandle *s, int block, int timeout_ms,
                      char **out, size_t *out_len, char *err, size_t err_cap) {
    (void)s; (void)block; (void)timeout_ms; (void)out; (void)out_len;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_chan_open(const char *user_name, size_t size, int create,
                      void **ptr_out, size_t *size_out, char *posix_out, size_t posix_cap,
                      int *owner, int *lock_out, char lock_path[IPC_SHM_LOCK_MAX], char *err,
                      size_t err_cap) {
    (void)user_name; (void)size; (void)create; (void)ptr_out; (void)size_out;
    (void)posix_out; (void)posix_cap; (void)owner; (void)lock_out; (void)lock_path;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_readable(IpcHandle *s) { (void)s; return 0; }
size_t ipc_bcast_total_size(size_t ring_cap, uint32_t max_readers) {
    (void)ring_cap; (void)max_readers;
    return 0;
}

#else

/* Ring positions live in [0, wrap), wrap a multiple of cap, so pos % cap stays continuous. */
static uint32_t ipc_shm_ring_wrap(uint32_t cap) {
    return (UINT32_MAX / cap) * cap;
}

static uint32_t ipc_shm_ring_add(uint32_t pos, uint32_t n, uint32_t cap) {
    return (uint32_t)(((uint64_t)pos + n) % ipc_shm_ring_wrap(cap));
}

static uint32_t ipc_shm_ring_dist(uint32_t from, uint32_t to, uint32_t cap) {
    uint32_t wrap = ipc_shm_ring_wrap(cap);
    from %= wrap;
    to %= wrap;
    return to >= from ? to - from : to + (wrap - from);
}

static void ipc_shm_ring_copy_out(uint8_t *ring, uint32_t cap, uint32_t pos, void *dst, uint32_t n) {
    uint32_t off = pos % cap;
    uint32_t first = cap - off;
    if (first >= n) {
        memcpy(dst, ring + off, n);
    } else {
        memcpy(dst, ring + off, first);
        memcpy((char *)dst + first, ring, n - first);
    }
}

static void ipc_shm_ring_copy_in(uint8_t *ring, uint32_t cap, uint32_t pos, const void *src, uint32_t n) {
    uint32_t off = pos % cap;
    uint32_t first = cap - off;
    if (first >= n) {
        memcpy(ring + off, src, n);
    } else {
        memcpy(ring + off, src, first);
        memcpy(ring, (const char *)src + first, n - first);
    }
}

int ipc_shm_push(IpcShmHdr *hdr, int which, const void *data, uint32_t len, uint32_t cap, char *err, size_t err_cap) {
    if (cap == 0 || len > IPC_MAX_MSG || len + 4 > cap) {
        snprintf(err, err_cap, "ipc: shm message too large (%u)", len);
        return -1;
    }
    atomic_uint *headp = which ? &hdr->head1 : &hdr->head0;
    atomic_uint *tailp = which ? &hdr->tail1 : &hdr->tail0;
    atomic_uint *genp = which ? &hdr->gen1 : &hdr->gen0;
    uint8_t *ring = (uint8_t *)hdr + IPC_SHM_HDR + (size_t)which * (size_t)cap;
    uint32_t head = atomic_load_explicit(headp, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(tailp, memory_order_acquire);
    uint32_t used = ipc_shm_ring_dist(tail, head, cap);
    uint32_t need = len + 4;
    if (used > cap || used + need > cap) {
        snprintf(err, err_cap, "ipc: shm ring full");
        return -1;
    }
    uint32_t be = htonl(len);
    ipc_shm_ring_copy_in(ring, cap, head, &be, 4);
    ipc_shm_ring_copy_in(ring, cap, ipc_shm_ring_add(head, 4, cap), data, len);
    atomic_store_explicit(headp, ipc_shm_ring_add(head, need, cap), memory_order_release);
    atomic_fetch_add_explicit(genp, 1, memory_order_release);
#if defined(__linux__)
    syscall(SYS_futex, (int *)genp, FUTEX_WAKE, 1, NULL, NULL, 0);
#endif
    return 0;
}

int ipc_shm_pop(IpcShmHdr *hdr, int which, int block, int timeout_ms, uint32_t cap,
                char **out, size_t *out_len, char *err, size_t err_cap) {
    if (cap == 0) {
        snprintf(err, err_cap, "ipc: shm bad capacity");
        return -1;
    }
    atomic_uint *headp = which ? &hdr->head1 : &hdr->head0;
    atomic_uint *tailp = which ? &hdr->tail1 : &hdr->tail0;
    atomic_uint *genp = which ? &hdr->gen1 : &hdr->gen0;
    uint8_t *ring = (uint8_t *)hdr + IPC_SHM_HDR + (size_t)which * (size_t)cap;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int spins = 0;

    for (;;) {
        uint32_t head = atomic_load_explicit(headp, memory_order_acquire);
        uint32_t tail = atomic_load_explicit(tailp, memory_order_relaxed);
        if (head != tail) {
            uint32_t used = ipc_shm_ring_dist(tail, head, cap);
            if (used < 4 || used > cap) {
                atomic_store_explicit(tailp, head, memory_order_release);
                snprintf(err, err_cap, "ipc: shm ring corrupt");
                return -1;
            }
            uint32_t be;
            ipc_shm_ring_copy_out(ring, cap, tail, &be, 4);
            uint32_t len = ntohl(be);
            if (len > IPC_MAX_MSG || 4u + len > used || 4u + len > cap) {
                atomic_store_explicit(tailp, head, memory_order_release);
                snprintf(err, err_cap, "ipc: shm bad frame len");
                return -1;
            }
            char *msg = malloc(len + 1);
            if (!msg) {
                snprintf(err, err_cap, "ipc: oom");
                return -1;
            }
            ipc_shm_ring_copy_out(ring, cap, ipc_shm_ring_add(tail, 4, cap), msg, len);
            msg[len] = 0;
            atomic_store_explicit(tailp, ipc_shm_ring_add(tail, 4 + len, cap), memory_order_release);
            *out = msg;
            *out_len = len;
            return 0;
        }
        if (!block) return -2;
        if (timeout_ms >= 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed = (now.tv_sec - start.tv_sec) * 1000L +
                           (now.tv_nsec - start.tv_nsec) / 1000000L;
            if (elapsed >= timeout_ms) return -2;
        }
        if (spins < 64) {
            spins++;
            sched_yield();
            continue;
        }
#if defined(__linux__)
        {
            uint32_t g = atomic_load_explicit(genp, memory_order_acquire);
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L;
            syscall(SYS_futex, (int *)genp, FUTEX_WAIT, (int)g, &ts, NULL, 0);
        }
#else
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L;
            nanosleep(&ts, NULL);
        }
#endif
        spins = 0;
    }
}

int ipc_shm_send_side(IpcHandle *s, const void *data, size_t len, char *err, size_t err_cap) {
    if (!s->shm_ptr) {
        snprintf(err, err_cap, "ipc: shm not mapped");
        return -1;
    }
    if (s->shm_bcast) {
        IpcShmBcastHdr *bh = (IpcShmBcastHdr *)s->shm_ptr;
        if (bh->magic != IPC_SHM_MAGIC_BCAST) {
            snprintf(err, err_cap, "ipc: shm bad bcast magic");
            return -1;
        }
        if (s->shm_side >= 0) {
            snprintf(err, err_cap, "ipc: shm broadcast: readers cannot publish");
            return -1;
        }
        uint32_t cap = s->shm_cap ? s->shm_cap : bh->capacity;
        if (cap == 0 || len > IPC_MAX_MSG || len + 4 > cap) {
            snprintf(err, err_cap, "ipc: shm message too large (%zu)", len);
            return -1;
        }
        uint8_t *ring = (uint8_t *)bh + IPC_SHM_HDR;
        uint32_t head = atomic_load_explicit(&bh->head, memory_order_relaxed);
        uint32_t max_r = s->shm_max_readers ? s->shm_max_readers : bh->max_readers;
        atomic_uint *actives = (atomic_uint *)((uint8_t *)bh + IPC_SHM_HDR + cap);
        atomic_uint *tails = actives + max_r;
        uint32_t used = 0;
        for (uint32_t r = 0; r < max_r; r++) {
            if (!atomic_load_explicit(&actives[r], memory_order_acquire)) continue;
            uint32_t t = atomic_load_explicit(&tails[r], memory_order_acquire);
            uint32_t d = ipc_shm_ring_dist(t, head, cap);
            if (d > used) used = d;
        }
        uint32_t need = (uint32_t)len + 4;
        if (used > cap || used + need > cap) {
            snprintf(err, err_cap, "ipc: shm ring full");
            return -1;
        }
        uint32_t be = htonl((uint32_t)len);
        ipc_shm_ring_copy_in(ring, cap, head, &be, 4);
        ipc_shm_ring_copy_in(ring, cap, ipc_shm_ring_add(head, 4, cap), data, (uint32_t)len);
        atomic_store_explicit(&bh->head, ipc_shm_ring_add(head, need, cap), memory_order_release);
        atomic_fetch_add_explicit(&bh->gen, 1, memory_order_release);
#if defined(__linux__)
        syscall(SYS_futex, (int *)&bh->gen, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
#endif
        return 0;
    }
    IpcShmHdr *hdr = (IpcShmHdr *)s->shm_ptr;
    if (hdr->magic != IPC_SHM_MAGIC || hdr->version != IPC_SHM_VERSION) {
        snprintf(err, err_cap, "ipc: shm bad magic");
        return -1;
    }
    return ipc_shm_push(hdr, s->shm_side, data, (uint32_t)len, s->shm_cap, err, err_cap);
}

int ipc_shm_recv_side(IpcHandle *s, int block, int timeout_ms,
                      char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!s->shm_ptr) {
        snprintf(err, err_cap, "ipc: shm not mapped");
        return -1;
    }
    if (s->shm_bcast) {
        IpcShmBcastHdr *bh = (IpcShmBcastHdr *)s->shm_ptr;
        if (bh->magic != IPC_SHM_MAGIC_BCAST) {
            snprintf(err, err_cap, "ipc: shm bad bcast magic");
            return -1;
        }
        uint32_t max_r = s->shm_max_readers ? s->shm_max_readers : bh->max_readers;
        uint32_t cap = s->shm_cap ? s->shm_cap : bh->capacity;
        if (cap == 0 || s->shm_side < 0 || (uint32_t)s->shm_side >= max_r) {
            snprintf(err, err_cap, "ipc: shm broadcast: not a reader");
            return -1;
        }
        atomic_uint *actives = (atomic_uint *)((uint8_t *)bh + IPC_SHM_HDR + cap);
        atomic_uint *tails = actives + max_r;
        uint8_t *ring = (uint8_t *)bh + IPC_SHM_HDR;
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int spins = 0;
        for (;;) {
            uint32_t head = atomic_load_explicit(&bh->head, memory_order_acquire);
            uint32_t tail = atomic_load_explicit(&tails[s->shm_side], memory_order_relaxed);
            if (head != tail) {
                uint32_t used = ipc_shm_ring_dist(tail, head, cap);
                if (used < 4 || used > cap) {
                    atomic_store_explicit(&tails[s->shm_side], head, memory_order_release);
                    snprintf(err, err_cap, "ipc: shm broadcast: reader overrun");
                    return -1;
                }
                uint32_t be;
                ipc_shm_ring_copy_out(ring, cap, tail, &be, 4);
                uint32_t len = ntohl(be);
                if (len > IPC_MAX_MSG || 4u + len > used || 4u + len > cap) {
                    atomic_store_explicit(&tails[s->shm_side], head, memory_order_release);
                    snprintf(err, err_cap, "ipc: shm broadcast: bad frame");
                    return -1;
                }
                char *msg = malloc(len + 1);
                if (!msg) {
                    snprintf(err, err_cap, "ipc: oom");
                    return -1;
                }
                ipc_shm_ring_copy_out(ring, cap, ipc_shm_ring_add(tail, 4, cap), msg, len);
                msg[len] = 0;
                atomic_store_explicit(&tails[s->shm_side], ipc_shm_ring_add(tail, 4 + len, cap),
                                      memory_order_release);
                *out = msg;
                *out_len = len;
                return 0;
            }
            if (!block) return -2;
            if (timeout_ms >= 0) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed = (now.tv_sec - start.tv_sec) * 1000L +
                               (now.tv_nsec - start.tv_nsec) / 1000000L;
                if (elapsed >= timeout_ms) return -2;
            }
            if (spins++ < 64) { sched_yield(); continue; }
            struct timespec ts = {0, 1000000L};
            nanosleep(&ts, NULL);
            spins = 0;
        }
    }
    IpcShmHdr *hdr = (IpcShmHdr *)s->shm_ptr;
    if (hdr->magic != IPC_SHM_MAGIC || hdr->version != IPC_SHM_VERSION) {
        snprintf(err, err_cap, "ipc: shm bad magic");
        return -1;
    }
    int which = s->shm_side ? 0 : 1;
    return ipc_shm_pop(hdr, which, block, timeout_ms, s->shm_cap, out, out_len, err, err_cap);
}

int ipc_shm_chan_name(char *dst, size_t cap, const char *user) {
#if defined(__linux__)
    char prefix[48];
    snprintf(prefix, sizeof prefix, "/shakti_ipc_%lu_", (unsigned long)geteuid());
    return ipc_format_posix_shm_name(dst, cap, prefix, user);
#else
    return ipc_format_posix_shm_name(dst, cap, "/shakti_ipc_", user);
#endif
}

int ipc_shm_check_fd(int fd, size_t *size_out, char *err, size_t err_cap) {
    struct stat sb;
    if (fstat(fd, &sb) != 0) {
        snprintf(err, err_cap, "ipc: shm fstat: %s", strerror(errno));
        return -1;
    }
    if (sb.st_uid != geteuid()) {
        snprintf(err, err_cap, "ipc: shm segment is owned by another user");
        return -1;
    }
    if ((sb.st_mode & 077) != 0) {
        snprintf(err, err_cap, "ipc: shm segment has unsafe permissions (%03o)", (unsigned)(sb.st_mode & 0777));
        return -1;
    }
    if (sb.st_size < (off_t)IPC_SHM_HDR || (uint64_t)sb.st_size > IPC_MAX_SHM) {
        snprintf(err, err_cap, "ipc: shm segment size out of range");
        return -1;
    }
    *size_out = (size_t)sb.st_size;
    return 0;
}

/* Create the shared-memory object posix, holding its name's lock (*lock_out, its file lock_path).
   A name whose lock is held is in use; one whose lock nobody holds was left by a process that ended
   without closing it, and is replaced. */
int ipc_shm_create_fd(const char *posix, int *lock_out, char lock_path[IPC_SHM_LOCK_MAX], char *err,
                      size_t err_cap) {
    int lock = ipc_shm_lock(posix, lock_path, err, err_cap);
    if (lock < 0) return -1;
    shm_unlink(posix);
    int fd = shm_open(posix, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        if (errno == EEXIST)
            snprintf(err, err_cap, "ipc: shm name already in use by another user (%s)", posix);
        else
            snprintf(err, err_cap, "ipc: shm_create: %s", strerror(errno));
        ipc_shm_unlock(posix, lock_path, lock, 0);
        return -1;
    }
    *lock_out = lock;
    return fd;
}

int ipc_shm_chan_open(const char *user_name, size_t size, int create,
                      void **ptr_out, size_t *size_out, char *posix_out, size_t posix_cap,
                      int *owner, int *lock_out, char lock_path[IPC_SHM_LOCK_MAX], char *err,
                      size_t err_cap) {
    *lock_out = -1;
    if (!ipc_valid_shm_user_name(user_name)) {
        snprintf(err, err_cap, "ipc: bad shm name");
        return -1;
    }
    if (ipc_shm_chan_name(posix_out, posix_cap, user_name) != 0) {
        snprintf(err, err_cap, "ipc: shm name too long (max %zu incl. prefix)",
                 ipc_posix_shm_name_max());
        return -1;
    }
    if (create && (size < IPC_SHM_HDR + 128 || size > IPC_MAX_SHM)) {
        snprintf(err, err_cap, "ipc: shm size out of range");
        return -1;
    }
    int fd;
    size_t map_size = size;
    if (create) {
        long page = sysconf(_SC_PAGESIZE);
        if (page < 4096) page = 4096;
        size_t p = (size_t)page;
        size_t rounded = (map_size + p - 1) & ~(p - 1);
        if (rounded < map_size || rounded > IPC_MAX_SHM) {
            snprintf(err, err_cap, "ipc: shm size out of range");
            return -1;
        }
        map_size = rounded;
        fd = ipc_shm_create_fd(posix_out, lock_out, lock_path, err, err_cap);
        if (fd < 0) return -1;
        if (ftruncate(fd, (off_t)map_size) != 0) {
            snprintf(err, err_cap, "ipc: shm ftruncate: %s", strerror(errno));
            close(fd);
            ipc_shm_unlock(posix_out, lock_path, *lock_out, 1);
            *lock_out = -1;
            return -1;
        }
        *owner = 1;
    } else {
        fd = shm_open(posix_out, O_RDWR, 0600);
        if (fd < 0) {
            snprintf(err, err_cap, "ipc: shm_attach: %s", strerror(errno));
            return -1;
        }
        *owner = 0;
        if (ipc_shm_check_fd(fd, &map_size, err, err_cap) != 0) {
            close(fd);
            return -1;
        }
    }
    void *ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        snprintf(err, err_cap, "ipc: shm mmap: %s", strerror(errno));
        if (create) {
            ipc_shm_unlock(posix_out, lock_path, *lock_out, 1);
            *lock_out = -1;
        }
        return -1;
    }
    if (create) {
        memset(ptr, 0, map_size);
        IpcShmHdr *hdr = (IpcShmHdr *)ptr;
        hdr->magic = IPC_SHM_MAGIC;
        hdr->version = IPC_SHM_VERSION;
        hdr->total_size = (uint32_t)map_size;
        hdr->capacity = (uint32_t)((map_size - IPC_SHM_HDR) / 2);
        atomic_init(&hdr->head0, 0);
        atomic_init(&hdr->tail0, 0);
        atomic_init(&hdr->gen0, 0);
        atomic_init(&hdr->head1, 0);
        atomic_init(&hdr->tail1, 0);
        atomic_init(&hdr->gen1, 0);
        atomic_init(&hdr->side_b_claimed, 0);
    } else {
        IpcShmHdr *hdr = (IpcShmHdr *)ptr;
        if (map_size < IPC_SHM_HDR || hdr->magic != IPC_SHM_MAGIC || hdr->version != IPC_SHM_VERSION) {
            munmap(ptr, map_size);
            snprintf(err, err_cap, "ipc: shm_attach: bad segment");
            return -1;
        }
        size_t need = IPC_SHM_HDR + (size_t)hdr->capacity * 2;
        if (!(need <= map_size && hdr->capacity > 0 && hdr->total_size == (uint32_t)map_size)) {
            munmap(ptr, map_size);
            snprintf(err, err_cap, "ipc: shm_attach: bad layout");
            return -1;
        }
    }
    *ptr_out = ptr;
    *size_out = map_size;
    return 0;
}

int ipc_shm_readable(IpcHandle *s) {
    if (!s->shm_ptr) return 0;
    if (s->shm_bcast) {
        IpcShmBcastHdr *bh = (IpcShmBcastHdr *)s->shm_ptr;
        if (bh->magic != IPC_SHM_MAGIC_BCAST) return 0;
        uint32_t max_r = s->shm_max_readers ? s->shm_max_readers : bh->max_readers;
        uint32_t cap = s->shm_cap ? s->shm_cap : bh->capacity;
        if (cap == 0 || s->shm_side < 0 || (uint32_t)s->shm_side >= max_r) return 0;
        atomic_uint *actives = (atomic_uint *)((uint8_t *)bh + IPC_SHM_HDR + cap);
        atomic_uint *tails = actives + max_r;
        uint32_t head = atomic_load_explicit(&bh->head, memory_order_acquire);
        uint32_t tail = atomic_load_explicit(&tails[s->shm_side], memory_order_relaxed);
        return head != tail;
    }
    IpcShmHdr *hdr = (IpcShmHdr *)s->shm_ptr;
    int which = s->shm_side ? 0 : 1;
    atomic_uint *headp = which ? &hdr->head1 : &hdr->head0;
    atomic_uint *tailp = which ? &hdr->tail1 : &hdr->tail0;
    return atomic_load_explicit(headp, memory_order_acquire) !=
           atomic_load_explicit(tailp, memory_order_relaxed);
}

size_t ipc_bcast_total_size(size_t ring_cap, uint32_t max_readers) {
    return IPC_SHM_HDR + ring_cap + sizeof(atomic_uint) * max_readers * 2;
}

_Static_assert(sizeof(IpcShmHdr) == IPC_SHM_HDR, "IpcShmHdr must be 256 bytes");
_Static_assert(sizeof(IpcShmBcastHdr) == IPC_SHM_HDR, "IpcShmBcastHdr must be 256 bytes");

#endif
