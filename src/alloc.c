/* shakti/src/alloc.c — checked allocators */
#include "shakti_internal.h"
#include <fcntl.h>
#include <unistd.h>

static uint32_t hash_seed(void) {
    static uint32_t seed;
    static int ready;
    if (!ready) {
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        seed = 0;
        if (fd >= 0) {
            if (read(fd, &seed, sizeof seed) != (ssize_t)sizeof seed) seed = 0;
            close(fd);
        }
        if (!seed) seed = 0x9E3779B9u ^ (uint32_t)getpid();
        ready = 1;
    }
    return seed;
}
uint32_t fnv1a(const char *s) {
    uint32_t seed = hash_seed();
    uint32_t h = 2166136261u ^ seed;
    if (!s) return h ? h : 1;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
        h ^= seed;
    }
    return h ? h : 1;
}
void shakti_oom(const char *where) {
    fprintf(stderr, "shakti: fatal: out of memory in %s\n", where ? where : "?");
    exit(1);
}
/* Checked allocators for core interpreter internals (value layer, AST, env),
 * whose callers assume non-NULL. On OOM they abort via shakti_oom() rather
 * than returning NULL and risking a downstream deref. */
void *x_malloc(size_t sz, const char *where) {
    void *p = malloc(sz ? sz : 1);
    if (!p) shakti_oom(where);
    return p;
}
void *x_calloc(size_t nmemb, size_t sz, const char *where) {
    void *p = calloc(nmemb ? nmemb : 1, sz ? sz : 1);
    if (!p) shakti_oom(where);
    return p;
}
void *x_realloc(void *ptr, size_t sz, const char *where) {
    void *p = realloc(ptr, sz ? sz : 1);
    if (!p) shakti_oom(where);
    return p;
}
char *x_strdup(const char *s, const char *where) {
    char *p = strdup(s);
    if (!p) shakti_oom(where);
    return p;
}
/* Overflow-checked size multiply for allocation math. Aborts via shakti_oom()
 * if a*b would wrap, preventing an undersized allocation followed by OOB use. */
size_t x_mul(size_t a, size_t b, const char *where) {
    size_t r;
    if (__builtin_mul_overflow(a, b, &r)) shakti_oom(where);
    return r;
}
