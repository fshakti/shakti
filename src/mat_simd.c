#include "mat_simd.h"
#include "a.h"
#include <math.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#if defined(__AVX512F__) && defined(__AVX512BW__)
#include <immintrin.h>
#endif

#if defined(__AVX512F__) && defined(__AVX512BW__)
static inline int use_simd_elems(int64_t ne) { return ne >= ISL_MAT_SIMD_MIN_ELEMS; }
static inline int use_simd_mul(int64_t m, int64_t k, int64_t n) {
    return m * n >= ISL_MAT_SIMD_MIN_ELEMS && k >= ISL_MAT_SIMD_K_MIN;
}
#else
static inline int use_simd_elems(int64_t ne) { (void)ne; return 0; }
static inline int use_simd_mul(int64_t m, int64_t k, int64_t n) {
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

#if defined(__AVX512F__) && defined(__AVX512BW__)

static inline double hsum512(__m512d v) {
#if defined(__AVX512DQ__)
    return _mm512_reduce_add_pd(v);
#else
    __m256d lo = _mm512_castpd512_pd256(v);
    __m256d hi = _mm512_extractf64x4_pd(v, 1);
    __m256d s = _mm256_add_pd(lo, hi);
    __m128d s2 = _mm256_castpd256_pd128(s);
    __m128d hi64 = _mm_unpackhi_pd(s2, s2);
    return _mm_cvtsd_f64(_mm_add_sd(s2, hi64));
#endif
}

static inline __m512d load_i64_as_pd(const int64_t *p) {
#if defined(__AVX512DQ__)
    return _mm512_cvtepi64_pd(_mm512_loadu_si512((const void *)p));
#else
    double buf[8];
    for (int l = 0; l < 8; l++)
        buf[l] = (double)p[l];
    return _mm512_loadu_pd(buf);
#endif
}

static inline __m512i gather_col_idx(int64_t t, int64_t n, int64_t j) {
    return _mm512_set_epi64((t + 7) * n + j, (t + 6) * n + j, (t + 5) * n + j, (t + 4) * n + j,
                            (t + 3) * n + j, (t + 2) * n + j, (t + 1) * n + j, (t + 0) * n + j);
}

static inline __mmask8 cmp_mask8(__m512d vx, __m512d vy, int op) {
    switch (op) {
    case 3: return _mm512_cmp_pd_mask(vx, vy, _CMP_EQ_OQ);
    case 12: return _mm512_cmp_pd_mask(vx, vy, _CMP_NEQ_OQ);
    case 9: return _mm512_cmp_pd_mask(vx, vy, _CMP_LT_OQ);
    case 6: return _mm512_cmp_pd_mask(vx, vy, _CMP_GT_OQ);
    case 8: return _mm512_cmp_pd_mask(vx, vy, _CMP_LE_OQ);
    case 5: return _mm512_cmp_pd_mask(vx, vy, _CMP_GE_OQ);
    default: return _mm512_cmp_pd_mask(vx, vy, _CMP_EQ_OQ);
    }
}

static inline void store_mask8(unsigned char *r, int64_t i, __mmask8 m) {
    r[i + 0] = (unsigned char)((m >> 0) & 1);
    r[i + 1] = (unsigned char)((m >> 1) & 1);
    r[i + 2] = (unsigned char)((m >> 2) & 1);
    r[i + 3] = (unsigned char)((m >> 3) & 1);
    r[i + 4] = (unsigned char)((m >> 4) & 1);
    r[i + 5] = (unsigned char)((m >> 5) & 1);
    r[i + 6] = (unsigned char)((m >> 6) & 1);
    r[i + 7] = (unsigned char)((m >> 7) & 1);
}

static inline __m512d fmat_binop_vec(__m512d x, __m512d y, int op) {
    switch (op) {
    case 0: return _mm512_add_pd(x, y);
    case 18: return _mm512_sub_pd(x, y);
    case 11: return _mm512_mul_pd(x, y);
    case 2: return _mm512_div_pd(x, y);
    default: return x;
    }
}

static inline __m512d gather_i64_as_pd(const int64_t *base, __m512i idx) {
#if defined(__AVX512DQ__)
    return _mm512_cvtepi64_pd(_mm512_i64gather_epi64(idx, base, 8));
#else
    int64_t tmp[8];
    _mm512_storeu_si512(tmp, _mm512_i64gather_epi64(idx, base, 8));
    return load_i64_as_pd(tmp);
#endif
}

static void dot_row_col_fmat(double *cr, const double *ar, const double *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        __m512d sum = _mm512_setzero_pd();
        int64_t t = 0;
        for (; t + 8 <= k; t += 8) {
            __m512d va = _mm512_loadu_pd(ar + t);
            __m512d vb = _mm512_i64gather_pd(gather_col_idx(t, n, j), B, 8);
            sum = _mm512_fmadd_pd(va, vb, sum);
        }
        double s = hsum512(sum);
        for (; t < k; t++)
            s += ar[t] * B[t * n + j];
        cr[j] = s;
    }
}

static void dot_row_col_imat(int64_t *cr, const int64_t *ar, const int64_t *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        __m512d sum = _mm512_setzero_pd();
        int64_t t = 0;
        for (; t + 8 <= k; t += 8) {
            __m512d va = load_i64_as_pd(ar + t);
            __m512d vb = gather_i64_as_pd(B, gather_col_idx(t, n, j));
            sum = _mm512_fmadd_pd(va, vb, sum);
        }
        double s = hsum512(sum);
        for (; t < k; t++)
            s += (double)ar[t] * (double)B[t * n + j];
        cr[j] = (int64_t)s;
    }
}

static void copy_row_fmat(double *dst, const double *src, int64_t cols) {
    int64_t c = 0;
    for (; c + 8 <= cols; c += 8)
        _mm512_storeu_pd(dst + c, _mm512_loadu_pd(src + c));
    for (; c < cols; c++)
        dst[c] = src[c];
}

static void copy_row_imat(int64_t *dst, const int64_t *src, int64_t cols) {
    int64_t c = 0;
    for (; c + 8 <= cols; c += 8)
        _mm512_storeu_si512((void *)(dst + c), _mm512_loadu_si512((const void *)(src + c)));
    for (; c < cols; c++)
        dst[c] = src[c];
}

#endif /* __AVX512F__ && __AVX512BW__ */

static void dot_row_col_imat_scalar(int64_t *cr, const int64_t *ar, const int64_t *B, int64_t k, int64_t n) {
    for (int64_t j = 0; j < n; j++) {
        double sum = 0;
        for (int64_t t = 0; t < k; t++)
            sum += (double)ar[t] * (double)B[t * n + j];
        cr[j] = (int64_t)sum;
    }
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
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_mul(m, k, n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (m >= ISL_MAT_OMP_ROWS_MIN)
#endif
        for (int64_t i = 0; i < m; i++)
            dot_row_col_fmat(C + i * n, A + i * k, B, k, n);
        return;
    }
#endif
    for (int64_t i = 0; i < m; i++)
        dot_row_col_fmat_scalar(C + i * n, A + i * k, B, k, n);
}

void mat_imat_mul(int64_t *C, const int64_t *A, const int64_t *B, int64_t m, int64_t k, int64_t n) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_mul(m, k, n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (m >= ISL_MAT_OMP_ROWS_MIN)
#endif
        for (int64_t i = 0; i < m; i++)
            dot_row_col_imat(C + i * n, A + i * k, B, k, n);
        return;
    }
#endif
    for (int64_t i = 0; i < m; i++)
        dot_row_col_imat_scalar(C + i * n, A + i * k, B, k, n);
}

void mat_mul_mixed(double *Cf, int64_t *Ci, const int64_t *Aj, const double *Af,
                   const int64_t *Bj, const double *Bf, int64_t m, int64_t k, int64_t n,
                   int a_imat, int b_imat, int out_fmat) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_mul(m, k, n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (m >= ISL_MAT_OMP_ROWS_MIN)
#endif
        for (int64_t i = 0; i < m; i++) {
            for (int64_t j = 0; j < n; j++) {
                __m512d sum = _mm512_setzero_pd();
                int64_t t = 0;
                for (; t + 8 <= k; t += 8) {
                    __m512d va = a_imat ? load_i64_as_pd(Aj + i * k + t) : _mm512_loadu_pd(Af + i * k + t);
                    __m512d vb = b_imat ? gather_i64_as_pd(Bj, gather_col_idx(t, n, j))
                                        : _mm512_i64gather_pd(gather_col_idx(t, n, j), Bf, 8);
                    sum = _mm512_fmadd_pd(va, vb, sum);
                }
                double s = hsum512(sum);
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

static void fmat_binop_mm_scalar(double *r, const double *a, const double *b, int64_t ne, int op) {
    for (int64_t i = 0; i < ne; i++) {
        double x = a[i], y = b[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 2: r[i] = y != 0 ? x / y : 0; break;
        case 4: r[i] = y != 0 ? floor(x / y) : 0; break;
        case 10: r[i] = y != 0 ? fmod(x, y) : 0; break;
        case 17: r[i] = pow(x, y); break;
        default: r[i] = x; break;
        }
    }
}

void mat_fmat_binop_mm(double *r, const double *a, const double *b, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11 || op == 2)) {
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8) {
            __m512d x = _mm512_loadu_pd(a + i);
            __m512d y = _mm512_loadu_pd(b + i);
            _mm512_storeu_pd(r + i, fmat_binop_vec(x, y, op));
        }
        for (; i < ne; i++) {
            double x = a[i], y = b[i];
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
    fmat_binop_mm_scalar(r, a, b, ne, op);
}

void mat_fmat_binop_scalar(double *r, const double *a, double y, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11 || op == 2)) {
        __m512d vy = _mm512_set1_pd(y);
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8)
            _mm512_storeu_pd(r + i, fmat_binop_vec(_mm512_loadu_pd(a + i), vy, op));
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

void mat_fmat_binop_scalar_rev(double *r, double x, const double *b, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne) && (op == 18 || op == 2)) {
        __m512d vx = _mm512_set1_pd(x);
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8) {
            __m512d y = _mm512_loadu_pd(b + i);
            __m512d z = (op == 18) ? _mm512_sub_pd(vx, y) : _mm512_div_pd(vx, y);
            _mm512_storeu_pd(r + i, z);
        }
        for (; i < ne; i++) {
            double y = b[i];
            if (op == 18) r[i] = x - y;
            else r[i] = y != 0 ? x / y : 0;
        }
        return;
    }
#endif
    for (int64_t i = 0; i < ne; i++) {
        double y = b[i];
        switch (op) {
        case 18: r[i] = x - y; break;
        case 2: r[i] = y != 0 ? x / y : 0; break;
        case 4: r[i] = y != 0 ? floor(x / y) : 0; break;
        case 10: r[i] = y != 0 ? fmod(x, y) : 0; break;
        default: break;
        }
    }
}

static void imat_binop_mm_scalar(int64_t *r, const int64_t *a, const int64_t *b, int64_t ne, int op) {
    for (int64_t i = 0; i < ne; i++) {
        int64_t x = a[i], y = b[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 4: r[i] = y ? x / y : 0; break;
        case 10: r[i] = y ? x % y : 0; break;
        default: r[i] = x; break;
        }
    }
}

void mat_imat_binop_mm(int64_t *r, const int64_t *a, const int64_t *b, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11)) {
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8) {
            __m512i x = _mm512_loadu_si512((const void *)(a + i));
            __m512i y = _mm512_loadu_si512((const void *)(b + i));
            __m512i z;
            switch (op) {
            case 0: z = _mm512_add_epi64(x, y); break;
            case 18: z = _mm512_sub_epi64(x, y); break;
            default: z = _mm512_mullo_epi64(x, y); break;
            }
            _mm512_storeu_si512((void *)(r + i), z);
        }
        for (; i < ne; i++) {
            int64_t x = a[i], y = b[i];
            switch (op) {
            case 0: r[i] = x + y; break;
            case 18: r[i] = x - y; break;
            case 11: r[i] = x * y; break;
            default: break;
            }
        }
        return;
    }
#endif
    imat_binop_mm_scalar(r, a, b, ne, op);
}

void mat_imat_binop_scalar(int64_t *r, const int64_t *a, int64_t y, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne) && (op == 0 || op == 18 || op == 11)) {
        __m512i vy = _mm512_set1_epi64(y);
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8) {
            __m512i x = _mm512_loadu_si512((const void *)(a + i));
            __m512i z;
            switch (op) {
            case 0: z = _mm512_add_epi64(x, vy); break;
            case 18: z = _mm512_sub_epi64(x, vy); break;
            default: z = _mm512_mullo_epi64(x, vy); break;
            }
            _mm512_storeu_si512((void *)(r + i), z);
        }
        for (; i < ne; i++) {
            int64_t x = a[i];
            switch (op) {
            case 0: r[i] = x + y; break;
            case 18: r[i] = x - y; break;
            case 11: r[i] = x * y; break;
            default: break;
            }
        }
        return;
    }
#endif
    for (int64_t i = 0; i < ne; i++) {
        int64_t x = a[i];
        switch (op) {
        case 0: r[i] = x + y; break;
        case 18: r[i] = x - y; break;
        case 11: r[i] = x * y; break;
        case 4: r[i] = y ? x / y : 0; break;
        case 10: r[i] = y ? x % y : 0; break;
        default: break;
        }
    }
}

void mat_imat_binop_scalar_rev(int64_t *r, int64_t x, const int64_t *b, int64_t ne, int op) {
    for (int64_t i = 0; i < ne; i++) {
        int64_t y = b[i];
        switch (op) {
        case 18: r[i] = x - y; break;
        case 4: r[i] = y ? x / y : 0; break;
        case 10: r[i] = y ? x % y : 0; break;
        default: break;
        }
    }
}

static double fmat_at(const void *p, int64_t i) { return ((const double *)p)[i]; }
static double fmat_y_at(const void *p, int64_t i) { (void)i; return *(const double *)p; }
static double imat_at(const void *p, int64_t i) { return (double)((const int64_t *)p)[i]; }

void mat_fmat_cmp_bmat_scalar(unsigned char *r, const double *a, double y, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne)) {
        __m512d vy = _mm512_set1_pd(y);
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8)
            store_mask8(r, i, cmp_mask8(_mm512_loadu_pd(a + i), vy, op));
        mat_cmp_bmat_scalar_loop(r, i, ne, fmat_at, fmat_y_at, a, &y, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, fmat_at, fmat_y_at, a, &y, op);
}

void mat_fmat_cmp_bmat_mm(unsigned char *r, const double *a, const double *b, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne)) {
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8)
            store_mask8(r, i, cmp_mask8(_mm512_loadu_pd(a + i), _mm512_loadu_pd(b + i), op));
        mat_cmp_bmat_scalar_loop(r, i, ne, fmat_at, fmat_at, a, b, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, fmat_at, fmat_at, a, b, op);
}

void mat_imat_cmp_bmat_scalar(unsigned char *r, const int64_t *a, double y, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne)) {
        __m512d vy = _mm512_set1_pd(y);
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8)
            store_mask8(r, i, cmp_mask8(load_i64_as_pd(a + i), vy, op));
        mat_cmp_bmat_scalar_loop(r, i, ne, imat_at, fmat_y_at, a, &y, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, imat_at, fmat_y_at, a, &y, op);
}

void mat_imat_cmp_bmat_mm(unsigned char *r, const int64_t *a, const int64_t *b, int64_t ne, int op) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (use_simd_elems(ne)) {
        int64_t i = 0;
        for (; i + 8 <= ne; i += 8)
            store_mask8(r, i, cmp_mask8(load_i64_as_pd(a + i), load_i64_as_pd(b + i), op));
        mat_cmp_bmat_scalar_loop(r, i, ne, imat_at, imat_at, a, b, op);
        return;
    }
#endif
    mat_cmp_bmat_scalar_loop(r, 0, ne, imat_at, imat_at, a, b, op);
}

void mat_filter_fmat_rows(double *dst, const double *src, const unsigned char *mask,
                          int64_t nr, int64_t cols) {
    int64_t j = 0;
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (cols >= 8) {
        for (int64_t k = 0; k < nr; k++) {
            if (!mask[k]) continue;
            copy_row_fmat(dst + j * cols, src + k * cols, cols);
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
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (cols >= 8) {
        for (int64_t k = 0; k < nr; k++) {
            if (!mask[k]) continue;
            copy_row_imat(dst + j * cols, src + k * cols, cols);
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
