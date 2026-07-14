#ifndef VEC_KERNELS_H
#define VEC_KERNELS_H

#include <stdint.h>

double shakti_sum_f64(const double *d, int64_t n);
double shakti_dot_f64(const double *a, const double *b, int64_t n);
double shakti_dot_numeric(const int64_t *aj, const double *af, int a_fvec,
                          const int64_t *bj, const double *bf, int b_fvec,
                          int64_t n);

#endif
