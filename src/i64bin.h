#ifndef SHAKTI_I64BIN_H
#define SHAKTI_I64BIN_H

#include <stdint.h>

static inline int i64_pair_promotes(int64_t x, int64_t y) {
    return x == INT64_MIN && y == -1;
}

static inline int64_t i64_floordiv(int64_t x, int64_t y) {
    if (!y || i64_pair_promotes(x, y)) return 0;
    return x / y;
}

static inline int64_t i64_mod(int64_t x, int64_t y) {
    if (!y || i64_pair_promotes(x, y)) return 0;
    return x % y;
}

#endif
