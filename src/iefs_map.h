/*
 * IEFS mmap region ownership — shared by aliased column V payloads.
 */
#ifndef SHAKTI_IEFS_MAP_H
#define SHAKTI_IEFS_MAP_H

#include "shakti.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    IEFS_MAP_PAGES_THP = 0,
    IEFS_MAP_PAGES_2M = 1,
    IEFS_MAP_PAGES_1G = 2
};

typedef struct IefsMapRegion {
    void *base;
    size_t len;
    int rc;
    int hugetlb; /* 1 if MAP_HUGETLB anon arena */
} IefsMapRegion;

IefsMapRegion *iefs_map_region_new(void *base, size_t len, int hugetlb);
IefsMapRegion *iefs_map_region_ref(IefsMapRegion *r);
void iefs_map_region_release(IefsMapRegion *r);

/* Attach alias ownership: does not free prior malloc buffers (caller must). */
void iefs_v_set_map_alias(V *v, IefsMapRegion *r);

/* Copy payload out of map into malloc ownership. Returns 0 on success, -1 on OOM. */
int iefs_v_materialize(V *v);

/* Open path as mmap-backed value. pages: IEFS_MAP_PAGES_*. */
V *iefs_store_map(const char *path, int pages_mode);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_IEFS_MAP_H */
