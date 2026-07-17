#ifndef VEC_KERNELS_H
#define VEC_KERNELS_H

#include <stdint.h>

double shakti_sum_f64(const double *d, int64_t n);
double shakti_min_f64(const double *d, int64_t n);
double shakti_max_f64(const double *d, int64_t n);
int64_t shakti_min_i64(const int64_t *d, int64_t n);
int64_t shakti_max_i64(const int64_t *d, int64_t n);
double shakti_dot_f64(const double *a, const double *b, int64_t n);
double shakti_dot_numeric(const int64_t *aj, const double *af, int a_fvec,
                          const int64_t *bj, const double *bf, int b_fvec,
                          int64_t n);

/* q-compatible bin: last index i with keys[i] <= q; -1 if below range.
 * keys must be ascending (unsorted input is undefined, as in q). */
int64_t shakti_bin_i64(const int64_t *keys, int64_t n, int64_t q);
void shakti_bin_i64_batch(const int64_t *keys, int64_t n,
                          const int64_t *qs, int64_t m, int64_t *out);
int64_t shakti_bin_f64(const double *keys, int64_t n, double q);
void shakti_bin_f64_batch(const double *keys, int64_t n,
                          const double *qs, int64_t m, int64_t *out);

/* Sort (eq,time) pairs ascending for grouped asof / aj. */
void shakti_asof_sort_i64(const int64_t *eq, const int64_t *tm, int64_t n,
                          int64_t *eq_out, int64_t *tm_out);

/* Grouped asof: right side sorted by (eq,time); returns absolute row index or -1.
 * query_time may be NULL to broadcast scalar_time to every query_eq. */
void shakti_asof_bin_i64(const int64_t *eq, const int64_t *tm, int64_t n,
                         const int64_t *query_eq, const int64_t *query_tm,
                         int64_t m, int64_t scalar_tm, int64_t *out);

#endif
