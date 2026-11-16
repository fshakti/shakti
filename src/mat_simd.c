// SPDX-License-Identifier: Apache-2.0
#include "mat_simd.h"
#include "i64bin.h"
#include "a.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static inline int64_t i64_wadd(int64_t x, int64_t y) { return (int64_t)((uint64_t)x + (uint64_t)y); }
static inline int64_t i64_wsub(int64_t x, int64_t y) { return (int64_t)((uint64_t)x - (uint64_t)y); }
static inline int64_t i64_wmul(int64_t x, int64_t y) { return (int64_t)((uint64_t)x * (uint64_t)y); }

#ifndef SHAKTI_USE_ACCELERATE
#define SHAKTI_USE_ACCELERATE 0
#endif
#if SHAKTI_USE_ACCELERATE
#include "shakti_accelerate.h"
#endif

#ifdef _OPENMP
static inline int isl_vec_omp_threads(int64_t ne) {
    int max = omp_get_max_threads();
    int want;
    if (max <= 1 || ne < ISL_OMP_VEC_MIN) return 1;
    want = (int)(ne / ISL_OMP_VEC_CHUNK);
    if (want < 1) want = 1;
    if (want > max) want = max;
    if (want > ISL_OMP_VEC_MAX_THREADS) want = ISL_OMP_VEC_MAX_THREADS;
    return want;
}
#else
static inline int isl_vec_omp_threads(int64_t ne) { (void)ne; return 1; }
#endif

#if defined(__aarch64__) || SHAKTI_USE_ACCELERATE
static inline int use_simd_elems(int64_t ne) { return ne >= ISL_MAT_SIMD_MIN_ELEMS; }
static inline int use_simd_mul(int64_t m, int64_t k, int64_t n) {
    return m * n >= ISL_MAT_SIMD_MIN_ELEMS && k >= ISL_MAT_SIMD_K_MIN;
}
static inline int use_accel_mul(int64_t m, int64_t k, int64_t n) {
    return m * n >= SHAKTI_ACCEL_GEMM_MIN_ELEMS && k >= SHAKTI_ACCEL_GEMM_K_MIN
        && m <= INT_MAX && k <= INT_MAX && n <= INT_MAX;
}
#else
static inline int use_simd_elems(int64_t ne) { (void)ne; return 0; }
static inline int use_simd_mul(int64_t m, int64_t k, int64_t n) {
    (void)m; (void)k; (void)n; return 0;
}
static inline int use_accel_mul(int64_t m, int64_t k, int64_t n) {
    (void)m; (void)k; (void)n; return 0;
}
#endif

static inline int mat_cmp_elem(double x, double y, int op) {
    switch (op) {
    case 3: return x == y;
    case 12: return x != y;
    case 9: return x < y;
    case 6: return x > y;
    case 8: return x <= y;
    case 5: return x >= y;
    default: return 0;
    }
}

static void mat_cmp_bmat_scalar_loop(unsigned char *r, int64_t i, int64_t ne,
                                     double (*x_at)(const void *, int64_t),
                                     double (*y_at)(const void *, int64_t),
                                     const void *a, const void *b, int op) {
    for (; i < ne; i++)
        r[i] = mat_cmp_elem(x_at(a, i), y_at(b, i), op) ? 1 : 0;
}

/* Int matrices compare as ints: through doubles, ints above 2^53 would round together. */
static inline int mat_cmp_elem_i64(int64_t x, int64_t y, int op) {
    switch (op) {
    case 3: return x == y;
    case 12: return x != y;
    case 9: return x < y;
    case 6: return x > y;
    case 8: return x <= y;
    case 5: return x >= y;
    default: return 0;
    }
}

#if defined(__aarch64__)

static inline float64x2_t load_i64_as_pd_neon(const int64_t *p) {
    return vcvtq_f64_s64(vld1q_s64(p));
}

static inline uint64x2_t cmp_mask_neon(float64x2_t vx, float64x2_t vy, int op) {
    switch (op) {
    case 3: return vceqq_f64(vx, vy);
    case 12: return veorq_u64(vceqq_f64(vx, vy), vdupq_n_u64(UINT64_MAX));
    case 9: return vcltq_f64(vx, vy);
    case 6: return vcgtq_f64(vx, vy);
    case 8: return vcleq_f64(vx, vy);
    case 5: return vcgeq_f64(vx, vy);
    default: return vceqq_f64(vx, vy);
    }
}

static inline uint64x2_t cmp_mask_s64_neon(int64x2_t vx, int64x2_t vy, int op) {
    switch (op) {
    case 3: return vceqq_s64(vx, vy);
    case 12: return veorq_u64(vceqq_s64(vx, vy), vdupq_n_u64(UINT64_MAX));
    case 9: return vcltq_s64(vx, vy);
    case 6: return vcgtq_s64(vx, vy);
    case 8: return vcleq_s64(vx, vy);
    case 5: return vcgeq_s64(vx, vy);
    default: return vceqq_s64(vx, vy);
    }
}

static inline void store_mask2(unsigned char *r, int64_t i, uint64x2_t m) {
    r[i + 0] = (unsigned char)(vgetq_lane_u64(m, 0) ? 1 : 0);
    r[i + 1] = (unsigned char)(vgetq_lane_u64(m, 1) ? 1 : 0);
}

static inline float64x2_t fmat_binop_vec_neon(float64x2_t x, float64x2_t y, int op) {
    switch (op) {
    case 0: return vaddq_f64(x, y);
    case 18: return vsubq_f64(x, y);
    case 11: return vmulq_f64(x, y);
    case 2: {
        uint64x2_t nonzero = veorq_u64(vceqq_f64(y, vdupq_n_f64(0.0)), vdupq_n_u64(UINT64_MAX));
        return vbslq_f64(nonzero, vdivq_f64(x, y), vdupq_n_f64(0.0));
    }
    default: return x;
    }
}

static inline int64x2_t imat_mul_vec_neon(int64x2_t x, int64x2_t y) {
    int64x2_t z = vdupq_n_s64(0);
    z = vsetq_lane_s64(i64_wmul(vgetq_lane_s64(x, 0), vgetq_lane_s64(y, 0)), z, 0);
    z = vsetq_lane_s64(i64_wmul(vgetq_lane_s64(x, 1), vgetq_lane_s64(y, 1)), z, 1);
    return z;
}

static void dot_row_col_fmat_neon(double *cr, const double *ar, const double *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        float64x2_t sum = vdupq_n_f64(0.0);
        int64_t t = 0;
        for (; t + 2 <= k; t += 2) {
            float64x2_t va = vld1q_f64(ar + t);
            double b0 = B[(t + 0) * n + j];
            double b1 = B[(t + 1) * n + j];
            float64x2_t vb = vsetq_lane_f64(b1, vsetq_lane_f64(b0, vdupq_n_f64(0.0), 0), 1);
            sum = vfmaq_f64(sum, va, vb);
        }
        double s = vaddvq_f64(sum);
        for (; t < k; t++)
            s += ar[t] * B[t * n + j];
        cr[j] = s;
    }
}

static void dot_row_col_imat_neon(int64_t *cr, const int64_t *ar, const int64_t *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        float64x2_t sum = vdupq_n_f64(0.0);
        int64_t t = 0;
        for (; t + 2 <= k; t += 2) {
            float64x2_t va = load_i64_as_pd_neon(ar + t);
            int64_t b0 = B[(t + 0) * n + j];
            int64_t b1 = B[(t + 1) * n + j];
            float64x2_t vb = vsetq_lane_f64((double)b1, vsetq_lane_f64((double)b0, vdupq_n_f64(0.0), 0), 1);
            sum = vfmaq_f64(sum, va, vb);
        }
        double s = vaddvq_f64(sum);
        for (; t < k; t++)
            s += (double)ar[t] * (double)B[t * n + j];
        cr[j] = (int64_t)s;
    }
}

static void copy_row_fmat_neon(double *dst, const double *src, int64_t cols) {
    int64_t c = 0;
    for (; c + 2 <= cols; c += 2)
        vst1q_f64(dst + c, vld1q_f64(src + c));
    for (; c < cols; c++)
        dst[c] = src[c];
}

static void copy_row_imat_neon(int64_t *dst, const int64_t *src, int64_t cols) {
    int64_t c = 0;
    for (; c + 2 <= cols; c += 2)
        vst1q_s64(dst + c, vld1q_s64(src + c));
    for (; c < cols; c++)
        dst[c] = src[c];
}

#endif /* SIMD backend */

static int dot_row_col_imat_scalar(int64_t *cr, const int64_t *ar, const int64_t *B, int64_t k, int64_t n) {
    int overflow = 0;
    for (int64_t j = 0; j < n; j++) {
        __int128 sum = 0;
        for (int64_t t = 0; t < k; t++)
            sum += (__int128)ar[t] * (__int128)B[t * n + j];
        if (sum > INT64_MAX || sum < INT64_MIN) overflow = 1;
        cr[j] = (int64_t)sum;
    }
    return overflow;
}

static void dot_row_col_fmat_scalar(double *cr, const double *ar, const double *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        double sum = 0;
        for (int64_t t = 0; t < k; t++)
            sum += ar[t] * B[t * n + j];
        cr[j] = sum;
    }
}

void mat_fmat_mul(double *C, const double *A, const double *B, int64_t m, int64_t k, int64_t n) {
#if SHAKTI_USE_ACCELERATE
    if (use_accel_mul(m, k, n)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        /* Prefer classic cblas until ACCELERATE_NEW_LAPACK can coexist with a.h macros. */
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    (int)m, (int)n, (int)k, 1.0, A, (int)k, B, (int)n, 0.0, C, (int)n);
#pragma clang diagnostic pop
        return;
    }
#endif
#if defined(__aarch64__)
    if (use_simd_mul(m, k, n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (m >= ISL_MAT_OMP_ROWS_MIN)
#endif
        for (int64_t i = 0; i < m; i++)
            dot_row_col_fmat_neon(C + i * n, A + i * k, B, k, n);
        return;
    }
#endif
    for (int64_t i = 0; i < m; i++)
        dot_row_col_fmat_scalar(C + i * n, A + i * k, B, k, n);
}

int mat_imat_mul(int64_t *C, const int64_t *A, const int64_t *B, int64_t m, int64_t k, int64_t n) {
    int overflow = 0;
    for (int64_t i = 0; i < m; i++) {
        if (dot_row_col_imat_scalar(C + i * n, A + i * k, B, k, n))
            overflow = 1;
    }
    return overflow ? -1 : 0;
}

void mat_mul_mixed(double *Cf, int64_t *Ci, const int64_t *Aj, const double *Af,
                   const int64_t *Bj, const double *Bf, int64_t m, int64_t k, int64_t n,
                   int a_imat, int b_imat, int out_fmat) {
#if defined(__aarch64__)
    if (use_simd_mul(m, k, n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (m >= ISL_MAT_OMP_ROWS_MIN)
#endif
        for (int64_t i = 0; i < m; i++) {
            for (int64_t j = 0; j < n; j++) {
                float64x2_t sum = vdupq_n_f64(0.0);
                int64_t t = 0;
                for (; t + 2 <= k; t += 2) {
                    float64x2_t va = a_imat ? load_i64_as_pd_neon(Aj + i * k + t)
                                              : vld1q_f64(Af + i * k + t);
                    double b0 = b_imat ? (double)Bj[(t + 0) * n + j] : Bf[(t + 0) * n + j];
                    double b1 = b_imat ? (double)Bj[(t + 1) * n + j] : Bf[(t + 1) * n + j];
                    float64x2_t vb = vsetq_lane_f64(b1, vsetq_lane_f64(b0, vdupq_n_f64(0.0), 0), 1);
                    sum = vfmaq_f64(sum, va, vb);
                }
                double s = vaddvq_f64(sum);
                for (; t < k; t++) {
                    double av = a_imat ? (double)Aj[i * k + t] : Af[i * k + t];
                    double bv = b_imat ? (double)Bj[t * n + j] : Bf[t * n + j];
                    s += av * bv;
                }
                if (out_fmat)
                    Cf[i * n + j] = s;
                else
                    Ci[i * n + j] = (int64_t)s;
            }
        }
        return;
    }
#endif
    for (int64_t i = 0; i < m; i++) {
        for (int64_t j = 0; j < n; j++) {
            double sum = 0;
            for (int64_t t = 0; t < k; t++) {
                double av = a_imat ? (double)Aj[i * k + t] : Af[i * k + t];
                double bv = b_imat ? (double)Bj[t * n + j] : Bf[t * n + j];
                sum += av * bv;
            }
            if (out_fmat)
                Cf[i * n + j] = sum;
            else
                Ci[i * n + j] = (int64_t)sum;
        }
    }
}

static int fmat_binop_mm_scalar(double *r, const double *a, const double *b, int64_t ne, int op) {
    int zero = 0;
    for (int64_t i = 0; i < ne; i++) {
        double x = a[i], y = b[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 2: zero |= y == 0; r[i] = y != 0 ? x / y : 0; break;
        case 4: zero |= y == 0; r[i] = y != 0 ? floor(x / y) : 0; break;
        case 10: zero |= y == 0; r[i] = y != 0 ? fmod(x, y) : 0; break;
        case 17: r[i] = pow(x, y); break;
        default: r[i] = x; break;
        }
    }
    return zero;
}

/* The divide kernels with a vector of divisors (the _mm and _rev ones) give 0 for an item whose
 * divisor is zero and return 1 when there was one (/, //, %), so the caller can make it an error;
 * for +, - and * the int ones return 1 when an item overflowed instead (imat_arith). */
int mat_fmat_binop_mm(double *r, const double *a, const double *b, int64_t ne, int op) {
#if defined(__aarch64__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11 || op == 2)) {
        int64_t i = 0;
        uint64x2_t zm = vdupq_n_u64(0);
        for (; i + 2 <= ne; i += 2) {
            float64x2_t x = vld1q_f64(a + i);
            float64x2_t y = vld1q_f64(b + i);
            if (op == 2) {
                uint64x2_t iszero = vceqq_f64(y, vdupq_n_f64(0.0));
                zm = vorrq_u64(zm, iszero);
                vst1q_f64(r + i, vbslq_f64(iszero, vdupq_n_f64(0.0), vdivq_f64(x, y)));
            } else
                vst1q_f64(r + i, fmat_binop_vec_neon(x, y, op));
        }
        int zero = (vgetq_lane_u64(zm, 0) | vgetq_lane_u64(zm, 1)) != 0;
        for (; i < ne; i++) {
            double x = a[i], y = b[i];
            switch (op) {
            case 0: r[i] = x + y; break;
            case 18: r[i] = x - y; break;
            case 11: r[i] = x * y; break;
            case 2: zero |= y == 0; r[i] = y != 0 ? x / y : 0; break;
            default: break;
            }
        }
        return zero;
    }
#endif
    return fmat_binop_mm_scalar(r, a, b, ne, op);
}

void mat_fmat_binop_scalar(double *r, const double *a, double y, int64_t ne, int op) {
#if defined(__aarch64__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11 || op == 2)) {
        float64x2_t vy = vdupq_n_f64(y);
        int64_t i = 0;
        for (; i + 2 <= ne; i += 2)
            vst1q_f64(r + i, fmat_binop_vec_neon(vld1q_f64(a + i), vy, op));
        for (; i < ne; i++) {
            double x = a[i];
            switch (op) {
            case 0: r[i] = x + y; break;
            case 18: r[i] = x - y; break;
            case 11: r[i] = x * y; break;
            case 2: r[i] = y != 0 ? x / y : 0; break;
            default: break;
            }
        }
        return;
    }
#endif
    for (int64_t i = 0; i < ne; i++) {
        double x = a[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 2: r[i] = y != 0 ? x / y : 0; break;
        case 4: r[i] = y != 0 ? floor(x / y) : 0; break;
        case 10: r[i] = y != 0 ? fmod(x, y) : 0; break;
        case 17: r[i] = pow(x, y); break;
        default: break;
        }
    }
}

int mat_fmat_binop_scalar_rev(double *r, double x, const double *b, int64_t ne, int op) {
    int zero = 0;
#if defined(__aarch64__)
    if (use_simd_elems(ne) && (op == 18 || op == 2)) {
        float64x2_t vx = vdupq_n_f64(x);
        uint64x2_t zm = vdupq_n_u64(0);
        int64_t i = 0;
        for (; i + 2 <= ne; i += 2) {
            float64x2_t y = vld1q_f64(b + i);
            float64x2_t z;
            if (op == 18) {
                z = vsubq_f64(vx, y);
            } else {
                uint64x2_t iszero = vceqq_f64(y, vdupq_n_f64(0.0));
                zm = vorrq_u64(zm, iszero);
                z = vbslq_f64(iszero, vdupq_n_f64(0.0), vdivq_f64(vx, y));
            }
            vst1q_f64(r + i, z);
        }
        zero = (vgetq_lane_u64(zm, 0) | vgetq_lane_u64(zm, 1)) != 0;
        for (; i < ne; i++) {
            double y = b[i];
            if (op == 18) r[i] = x - y;
            else { zero |= y == 0; r[i] = y != 0 ? x / y : 0; }
        }
        return zero;
    }
#endif
    for (int64_t i = 0; i < ne; i++) {
        double y = b[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 2: zero |= y == 0; r[i] = y != 0 ? x / y : 0; break;
        case 4: zero |= y == 0; r[i] = y != 0 ? floor(x / y) : 0; break;
        case 10: zero |= y == 0; r[i] = y != 0 ? fmod(x, y) : 0; break;
        case 17: r[i] = pow(x, y); break;
        default: break;
        }
    }
    return zero;
}

/* Wrapping + - * that also reports whether any element overflowed int64.
 * a/b NULL means the scalar xa/yb is broadcast. */
static inline __attribute__((always_inline)) int imat_arith(int64_t *r, const int64_t *a, int64_t xa,
                                                            const int64_t *b, int64_t yb, int64_t ne, int op) {
    uint64_t ovf = 0;
    int64_t i = 0;
#if defined(__aarch64__)
    if (use_simd_elems(ne) && op != 11) {
        int64x2_t vx = vdupq_n_s64(xa), vy = vdupq_n_s64(yb), vo = vdupq_n_s64(0);
        for (; i + 2 <= ne; i += 2) {
            int64x2_t x = a ? vld1q_s64(a + i) : vx;
            int64x2_t y = b ? vld1q_s64(b + i) : vy;
            int64x2_t z;
            if (op == 0) {
                z = vreinterpretq_s64_u64(vaddq_u64(vreinterpretq_u64_s64(x), vreinterpretq_u64_s64(y)));
                vo = vorrq_s64(vo, vandq_s64(veorq_s64(x, z), veorq_s64(y, z)));
            } else {
                z = vreinterpretq_s64_u64(vsubq_u64(vreinterpretq_u64_s64(x), vreinterpretq_u64_s64(y)));
                vo = vorrq_s64(vo, vandq_s64(veorq_s64(x, y), veorq_s64(x, z)));
            }
            vst1q_s64(r + i, z);
        }
        ovf = (uint64_t)(vgetq_lane_s64(vo, 0) | vgetq_lane_s64(vo, 1));
    }
#endif
    if (op == 0) {
        int nt = isl_vec_omp_threads(ne - i);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(|:ovf) if (ne - i >= ISL_OMP_VEC_MIN) num_threads(nt)
#endif
        for (int64_t k = i; k < ne; k++) {
            int64_t x = a ? a[k] : xa, y = b ? b[k] : yb, z = i64_wadd(x, y);
            r[k] = z;
            ovf |= (uint64_t)((x ^ z) & (y ^ z));
        }
    } else if (op == 18) {
        int nt = isl_vec_omp_threads(ne - i);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(|:ovf) if (ne - i >= ISL_OMP_VEC_MIN) num_threads(nt)
#endif
        for (int64_t k = i; k < ne; k++) {
            int64_t x = a ? a[k] : xa, y = b ? b[k] : yb, z = i64_wsub(x, y);
            r[k] = z;
            ovf |= (uint64_t)((x ^ y) & (x ^ z));
        }
    } else {
        int nt = isl_vec_omp_threads(ne - i);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(|:ovf) if (ne - i >= ISL_OMP_VEC_MIN) num_threads(nt)
#endif
        for (int64_t k = i; k < ne; k++) {
            int64_t x = a ? a[k] : xa, y = b ? b[k] : yb, z;
            if (__builtin_mul_overflow(x, y, &z)) { z = i64_wmul(x, y); ovf |= UINT64_C(1) << 63; }
            r[k] = z;
        }
    }
    return (int)(ovf >> 63);
}

int mat_imat_binop_mm(int64_t *r, const int64_t *a, const int64_t *b, int64_t ne, int op) {
    if (op == 0 || op == 18 || op == 11) return imat_arith(r, a, 0, b, 0, ne, op);
    int zero = 0;
    for (int64_t i = 0; i < ne; i++) {
        int64_t x = a[i], y = b[i];
        switch (op) {
        case 4: zero |= y == 0; r[i] = i64_floordiv(x, y); break;
        case 10: zero |= y == 0; r[i] = i64_mod(x, y); break;
        default: r[i] = x; break;
        }
    }
    return zero;
}

int mat_imat_binop_scalar(int64_t *r, const int64_t *a, int64_t y, int64_t ne, int op) {
    if (op == 0 || op == 18 || op == 11) return imat_arith(r, a, 0, NULL, y, ne, op);
    for (int64_t i = 0; i < ne; i++) {
        int64_t x = a[i];
        switch (op) {
        case 4: r[i] = i64_floordiv(x, y); break;
        case 10: r[i] = i64_mod(x, y); break;
        default: break;
        }
    }
    return 0;
}

int mat_imat_binop_scalar_rev(int64_t *r, int64_t x, const int64_t *b, int64_t ne, int op) {
    if (op == 0 || op == 18 || op == 11) return imat_arith(r, NULL, x, b, 0, ne, op);
    int zero = 0;
    for (int64_t i = 0; i < ne; i++) {
        int64_t y = b[i];
        switch (op) {
        case 4: zero |= y == 0; r[i] = i64_floordiv(x, y); break;
        case 10: zero |= y == 0; r[i] = i64_mod(x, y); break;
        default: break;
        }
    }
    return zero;
}

static double fmat_at(const void *p, int64_t i) { return ((const double *)p)[i]; }
static double fmat_y_at(const void *p, int64_t i) { (void)i; return *(const double *)p; }

void mat_fmat_cmp_bmat_scalar(unsigned char *r, const double *a, double y, int64_t ne, int op) {
#if defined(__aarch64__)
    if (use_simd_elems(ne)) {
        float64x2_t vy = vdupq_n_f64(y);
        int64_t i = 0;
        for (; i + 2 <= ne; i += 2)
            store_mask2(r, i, cmp_mask_neon(vld1q_f64(a + i), vy, op));
        mat_cmp_bmat_scalar_loop(r, i, ne, fmat_at, fmat_y_at, a, &y, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, fmat_at, fmat_y_at, a, &y, op);
}

void mat_fmat_cmp_bmat_mm(unsigned char *r, const double *a, const double *b, int64_t ne, int op) {
#if defined(__aarch64__)
    if (use_simd_elems(ne)) {
        int64_t i = 0;
        for (; i + 2 <= ne; i += 2)
            store_mask2(r, i, cmp_mask_neon(vld1q_f64(a + i), vld1q_f64(b + i), op));
        mat_cmp_bmat_scalar_loop(r, i, ne, fmat_at, fmat_at, a, b, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, fmat_at, fmat_at, a, b, op);
}

void mat_imat_cmp_bmat_scalar(unsigned char *r, const int64_t *a, int64_t y, int64_t ne, int op) {
    int64_t i = 0;
#if defined(__aarch64__)
    if (use_simd_elems(ne)) {
        int64x2_t vy = vdupq_n_s64(y);
        for (; i + 2 <= ne; i += 2)
            store_mask2(r, i, cmp_mask_s64_neon(vld1q_s64(a + i), vy, op));
    }
#endif
    for (; i < ne; i++)
        r[i] = mat_cmp_elem_i64(a[i], y, op) ? 1 : 0;
}

void mat_imat_cmp_bmat_mm(unsigned char *r, const int64_t *a, const int64_t *b, int64_t ne, int op) {
    int64_t i = 0;
#if defined(__aarch64__)
    if (use_simd_elems(ne)) {
        for (; i + 2 <= ne; i += 2)
            store_mask2(r, i, cmp_mask_s64_neon(vld1q_s64(a + i), vld1q_s64(b + i), op));
    }
#endif
    for (; i < ne; i++)
        r[i] = mat_cmp_elem_i64(a[i], b[i], op) ? 1 : 0;
}

void mat_filter_fmat_rows(double *dst, const double *src, const unsigned char *mask,
                          int64_t nr, int64_t cols) {
    int64_t j = 0;
#if defined(__aarch64__)
    if (cols >= 2) {
        for (int64_t k = 0; k < nr; k++) {
            if (!mask[k]) continue;
            copy_row_fmat_neon(dst + j * cols, src + k * cols, cols);
            j++;
        }
        return;
    }
#endif
    for (int64_t k = 0; k < nr; k++) {
        if (!mask[k]) continue;
        memcpy(dst + j * cols, src + k * cols, (size_t)cols * 8);
        j++;
    }
}

void mat_filter_imat_rows(int64_t *dst, const int64_t *src, const unsigned char *mask,
                          int64_t nr, int64_t cols) {
    int64_t j = 0;
#if defined(__aarch64__)
    if (cols >= 2) {
        for (int64_t k = 0; k < nr; k++) {
            if (!mask[k]) continue;
            copy_row_imat_neon(dst + j * cols, src + k * cols, cols);
            j++;
        }
        return;
    }
#endif
    for (int64_t k = 0; k < nr; k++) {
        if (!mask[k]) continue;
        memcpy(dst + j * cols, src + k * cols, (size_t)cols * 8);
        j++;
    }
}

void mat_filter_bmat_rows(unsigned char *dst, const unsigned char *src, const unsigned char *mask,
                          int64_t nr, int64_t cols) {
    int64_t j = 0;
    for (int64_t k = 0; k < nr; k++) {
        if (!mask[k]) continue;
        memcpy(dst + j * cols, src + k * cols, (size_t)cols);
        j++;
    }
}

int64_t mat_compress_i64_masked(int64_t *dst, int64_t j, const int64_t *src,
                                const unsigned char *mask, int64_t nr) {
#if defined(__aarch64__)
    int64_t k = 0;
    for (; k + 2 <= nr; k += 2) {
        if (mask[k]) dst[j++] = src[k];
        if (mask[k + 1]) dst[j++] = src[k + 1];
    }
    for (; k < nr; k++) {
        if (mask[k]) dst[j++] = src[k];
    }
    return j;
#else
    for (int64_t k = 0; k < nr; k++) {
        if (mask[k]) dst[j++] = src[k];
    }
    return j;
#endif
}

int64_t mat_compress_f64_masked(double *dst, int64_t j, const double *src,
                                const unsigned char *mask, int64_t nr) {
#if defined(__aarch64__)
    int64_t k = 0;
    for (; k + 2 <= nr; k += 2) {
        if (mask[k] && mask[k + 1]) {
            vst1q_f64(&dst[j], vld1q_f64(&src[k]));
            j += 2;
        } else {
            if (mask[k]) dst[j++] = src[k];
            if (mask[k + 1]) dst[j++] = src[k + 1];
        }
    }
    for (; k < nr; k++) {
        if (mask[k]) dst[j++] = src[k];
    }
    return j;
#else
    for (int64_t k = 0; k < nr; k++) {
        if (mask[k]) dst[j++] = src[k];
    }
    return j;
#endif
}
