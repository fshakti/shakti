#ifndef MAT_SIMD_H
#define MAT_SIMD_H

#include <stdint.h>

void mat_fmat_mul(double *C, const double *A, const double *B, int64_t m, int64_t k, int64_t n);
void mat_imat_mul(int64_t *C, const int64_t *A, const int64_t *B, int64_t m, int64_t k, int64_t n);
void mat_mul_mixed(double *Cf, int64_t *Ci, const int64_t *Aj, const double *Af,
                   const int64_t *Bj, const double *Bf, int64_t m, int64_t k, int64_t n,
                   int a_imat, int b_imat, int out_fmat);

void mat_fmat_binop_mm(double *r, const double *a, const double *b, int64_t ne, int op);
void mat_fmat_binop_scalar(double *r, const double *a, double y, int64_t ne, int op);
void mat_fmat_binop_scalar_rev(double *r, double x, const double *b, int64_t ne, int op);

void mat_imat_binop_mm(int64_t *r, const int64_t *a, const int64_t *b, int64_t ne, int op);
void mat_imat_binop_scalar(int64_t *r, const int64_t *a, int64_t y, int64_t ne, int op);
void mat_imat_binop_scalar_rev(int64_t *r, int64_t x, const int64_t *b, int64_t ne, int op);

void mat_fmat_cmp_bmat_scalar(unsigned char *r, const double *a, double y, int64_t ne, int op);
void mat_fmat_cmp_bmat_mm(unsigned char *r, const double *a, const double *b, int64_t ne, int op);
void mat_imat_cmp_bmat_scalar(unsigned char *r, const int64_t *a, double y, int64_t ne, int op);
void mat_imat_cmp_bmat_mm(unsigned char *r, const int64_t *a, const int64_t *b, int64_t ne, int op);

void mat_filter_fmat_rows(double *dst, const double *src, const unsigned char *mask,
                          int64_t nr, int64_t cols);
void mat_filter_imat_rows(int64_t *dst, const int64_t *src, const unsigned char *mask,
                          int64_t nr, int64_t cols);
void mat_filter_bmat_rows(unsigned char *dst, const unsigned char *src, const unsigned char *mask,
                          int64_t nr, int64_t cols);

#endif
