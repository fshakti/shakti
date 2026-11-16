// SPDX-License-Identifier: Apache-2.0
#ifndef SHAKTI_I64BIN_H
#define SHAKTI_I64BIN_H

#include <stdint.h>

static inline int i64_pair_promotes(int64_t x, int64_t y) {
    return x == INT64_MIN && y == -1;
}

/* Floor division, so (x // y) * y + (x % y) == x for both signs. */
static inline int64_t i64_floordiv(int64_t x, int64_t y) {
    if (!y || i64_pair_promotes(x, y)) return 0;
    int64_t q = x / y, r = x % y;
    if (r && ((x < 0) != (y < 0))) q--;
    return q;
}

static inline int64_t i64_mod(int64_t x, int64_t y) {
    if (!y || i64_pair_promotes(x, y)) return 0;
    int64_t r = x % y;
    if (r && ((x < 0) != (y < 0))) r += y;
    return r;
}

#endif
