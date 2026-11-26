// SPDX-License-Identifier: Apache-2.0
#ifndef SHAKTI_I64BIN_H
#define SHAKTI_I64BIN_H

#include <stdint.h>
#include <math.h>

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

/* Remainder of floor division: x - floor(x/y)*y, same sign as y.
 * Shared by the scalar, vector and matrix % paths so they agree. */
static inline double floor_mod(double x, double y) {
    double q;
    if (y == 0.0) return 0.0;
    /* x % ±inf is x when x is finite and non-negative, and +inf when x is finite and negative. */
    if (isinf(y)) {
        if (!isfinite(x)) return (double)NAN;
        return x < 0.0 ? (double)INFINITY : x;
    }
    q = floor(x / y);
    return x - q * y;
}

#endif
