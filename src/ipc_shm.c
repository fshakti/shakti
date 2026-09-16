#include "ipc_internal.h"

#if defined(SHAKTI_WASM) || (!defined(__linux__) && !defined(__APPLE__))

int ipc_shm_push(IpcShmHdr *hdr, int which, const void *data, uint32_t len, char *err, size_t err_cap) {
    (void)hdr; (void)which; (void)data; (void)len;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_pop(IpcShmHdr *hdr, int which, int block, int timeout_ms,
                char **out, size_t *out_len, char *err, size_t err_cap) {
    (void)hdr; (void)which; (void)block; (void)timeout_ms; (void)out; (void)out_len;
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
                      int *owner, char *err, size_t err_cap) {
    (void)user_name; (void)size; (void)create; (void)ptr_out; (void)size_out;
    (void)posix_out; (void)posix_cap; (void)owner;
    snprintf(err, err_cap, "ipc: shm not supported on this platform");
    return -1;
}
int ipc_shm_readable(IpcHandle *s) { (void)s; return 0; }
size_t ipc_bcast_total_size(size_t ring_cap, uint32_t max_readers) {
    (void)ring_cap; (void)max_readers;
    return 0;
}

#else

static uint8_t *ipc_shm_ring(IpcShmHdr *hdr, int which) {
    uint8_t *base = (uint8_t *)hdr + IPC_SHM_HDR;
    return base + (size_t)which * (size_t)hdr->capacity;
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

int ipc_shm_push(IpcShmHdr *hdr, int which, const void *data, uint32_t len, char *err, size_t err_cap) {
    if (len > IPC_MAX_MSG || len + 4 > hdr->capacity) {
        snprintf(err, err_cap, "ipc: shm message too large (%u)", len);
        return -1;
    }
    atomic_uint *headp = which ? &hdr->head1 : &hdr->head0;
    atomic_uint *tailp = which ? &hdr->tail1 : &hdr->tail0;
    atomic_uint *genp = which ? &hdr->gen1 : &hdr->gen0;
    uint32_t cap = hdr->capacity;
    uint8_t *ring = ipc_shm_ring(hdr, which);
    uint32_t head = atomic_load_explicit(headp, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(tailp, memory_order_acquire);
    uint32_t used = head - tail;
    uint32_t need = len + 4;
    if (used + need > cap) {
        snprintf(err, err_cap, "ipc: shm ring full");
        return -1;
    }
    uint32_t be = htonl(len);
    ipc_shm_ring_copy_in(ring, cap, head, &be, 4);
    ipc_shm_ring_copy_in(ring, cap, head + 4, data, len);
    atomic_store_explicit(headp, head + need, memory_order_release);
    atomic_fetch_add_explicit(genp, 1, memory_order_release);
#if defined(__linux__)
    syscall(SYS_futex, (int *)genp, FUTEX_WAKE, 1, NULL, NULL, 0);
#endif
    return 0;
}

int ipc_shm_pop(IpcShmHdr *hdr, int which, int block, int timeout_ms,
                char **out, size_t *out_len, char *err, size_t err_cap) {
    atomic_uint *headp = which ? &hdr->head1 : &hdr->head0;
    atomic_uint *tailp = which ? &hdr->tail1 : &hdr->tail0;
    atomic_uint *genp = which ? &hdr->gen1 : &hdr->gen0;
    uint32_t cap = hdr->capacity;
    uint8_t *ring = ipc_shm_ring(hdr, which);

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int spins = 0;

    for (;;) {
        uint32_t head = atomic_load_explicit(headp, memory_order_acquire);
        uint32_t tail = atomic_load_explicit(tailp, memory_order_relaxed);
        if (head != tail) {
            if (head - tail < 4) {
                snprintf(err, err_cap, "ipc: shm ring corrupt");
                return -1;
            }
            uint32_t be;
            ipc_shm_ring_copy_out(ring, cap, tail, &be, 4);
            uint32_t len = ntohl(be);
            if (len > IPC_MAX_MSG || 4 + len > head - tail) {
                snprintf(err, err_cap, "ipc: shm bad frame len");
                return -1;
            }
            char *msg = malloc(len + 1);
            if (!msg) {
                snprintf(err, err_cap, "ipc: oom");
                return -1;
            }
            ipc_shm_ring_copy_out(ring, cap, tail + 4, msg, len);
            msg[len] = 0;
            atomic_store_explicit(tailp, tail + 4 + len, memory_order_release);
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
        if (len > IPC_MAX_MSG || len + 4 > bh->capacity) {
            snprintf(err, err_cap, "ipc: shm message too large (%zu)", len);
            return -1;
        }
        uint32_t cap = bh->capacity;
        uint8_t *ring = (uint8_t *)bh + IPC_SHM_HDR;
        uint32_t head = atomic_load_explicit(&bh->head, memory_order_relaxed);
        uint32_t be = htonl((uint32_t)len);
        ipc_shm_ring_copy_in(ring, cap, head, &be, 4);
        ipc_shm_ring_copy_in(ring, cap, head + 4, data, (uint32_t)len);
        atomic_store_explicit(&bh->head, head + 4 + (uint32_t)len, memory_order_release);
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
    return ipc_shm_push(hdr, s->shm_side, data, (uint32_t)len, err, err_cap);
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
        if (s->shm_side < 0 || (uint32_t)s->shm_side >= bh->max_readers) {
            snprintf(err, err_cap, "ipc: shm broadcast: not a reader");
            return -1;
        }
        atomic_uint *actives = (atomic_uint *)((uint8_t *)bh + IPC_SHM_HDR + bh->capacity);
        atomic_uint *tails = actives + bh->max_readers;
        uint32_t cap = bh->capacity;
        uint8_t *ring = (uint8_t *)bh + IPC_SHM_HDR;
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int spins = 0;
        for (;;) {
            uint32_t head = atomic_load_explicit(&bh->head, memory_order_acquire);
            uint32_t tail = atomic_load_explicit(&tails[s->shm_side], memory_order_relaxed);
            if (head != tail) {
                if (head - tail < 4) {
                    atomic_store_explicit(&tails[s->shm_side], head, memory_order_release);
                    snprintf(err, err_cap, "ipc: shm broadcast: reader overrun");
                    return -1;
                }
                uint32_t be;
                ipc_shm_ring_copy_out(ring, cap, tail, &be, 4);
                uint32_t len = ntohl(be);
                if (len > IPC_MAX_MSG || 4 + len > head - tail) {
                    atomic_store_explicit(&tails[s->shm_side], head, memory_order_release);
                    snprintf(err, err_cap, "ipc: shm broadcast: bad frame");
                    return -1;
                }
                char *msg = malloc(len + 1);
                if (!msg) {
                    snprintf(err, err_cap, "ipc: oom");
                    return -1;
                }
                ipc_shm_ring_copy_out(ring, cap, tail + 4, msg, len);
                msg[len] = 0;
                atomic_store_explicit(&tails[s->shm_side], tail + 4 + len, memory_order_release);
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
    return ipc_shm_pop(hdr, which, block, timeout_ms, out, out_len, err, err_cap);
}

int ipc_shm_chan_open(const char *user_name, size_t size, int create,
                      void **ptr_out, size_t *size_out, char *posix_out, size_t posix_cap,
                      int *owner, char *err, size_t err_cap) {
    if (!ipc_valid_shm_user_name(user_name)) {
        snprintf(err, err_cap, "ipc: bad shm name");
        return -1;
    }
    if (ipc_format_posix_shm_name(posix_out, posix_cap, "/shakti_ipc_", user_name) != 0) {
        snprintf(err, err_cap, "ipc: shm name too long (max %zu incl. prefix)",
                 ipc_posix_shm_name_max());
        return -1;
    }
    if (create && size < IPC_SHM_HDR + 128) {
        snprintf(err, err_cap, "ipc: shm size too small");
        return -1;
    }
    int fd;
    size_t map_size = size;
    if (create) {
        shm_unlink(posix_out);
        fd = shm_open(posix_out, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            snprintf(err, err_cap, "ipc: shm_create: %s", strerror(errno));
            return -1;
        }
        if (ftruncate(fd, (off_t)map_size) != 0) {
            snprintf(err, err_cap, "ipc: shm ftruncate: %s", strerror(errno));
            close(fd);
            shm_unlink(posix_out);
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
        struct stat sb;
        if (fstat(fd, &sb) != 0) {
            snprintf(err, err_cap, "ipc: shm fstat: %s", strerror(errno));
            close(fd);
            return -1;
        }
        map_size = (size_t)sb.st_size;
    }
    void *ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        snprintf(err, err_cap, "ipc: shm mmap: %s", strerror(errno));
        if (create) shm_unlink(posix_out);
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
        if (s->shm_side < 0 || (uint32_t)s->shm_side >= bh->max_readers) return 0;
        atomic_uint *actives = (atomic_uint *)((uint8_t *)bh + IPC_SHM_HDR + bh->capacity);
        atomic_uint *tails = actives + bh->max_readers;
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
