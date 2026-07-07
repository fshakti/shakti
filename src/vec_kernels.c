/* vec_kernels.c — SIMD/OpenMP vector reductions (dot, sum). Ported from isolde k.c. */
#include "vec_kernels.h"
#include "a.h"
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#if defined(__x86_64__) && defined(__AVX512F__)
#include <immintrin.h>
#define SHAKTI_USE_AVX512 1
#else
#define SHAKTI_USE_AVX512 0
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#if SHAKTI_USE_AVX512
static inline double hsum512(__m512d v) {
#if defined(__AVX512DQ__)
    return _mm512_reduce_add_pd(v);
#else
    __m256d lo = _mm512_castpd512_pd256(v);
    __m256d hi = _mm256_castpd128_pd256(_mm512_extractf64x4_pd(v, 1));
    __m256d s = _mm256_add_pd(lo, hi);
    __m128d s128 = _mm_add_pd(_mm256_castpd256_pd128(s),
                              _mm256_extractf128_pd(s, 1));
    s128 = _mm_hadd_pd(s128, s128);
    return _mm_cvtsd_f64(s128);
#endif
}

static double sum_f64_avx512(const double *d, int64_t n) {
    double r = 0;
#ifdef _OPENMP
    #pragma omp parallel reduction(+:r)
#endif
    {
#ifdef _OPENMP
        int tid = omp_get_thread_num(), nt = omp_get_num_threads();
        int64_t chunk = (n + nt - 1) / nt;
        int64_t start = (int64_t)tid * chunk;
        int64_t len = (start + chunk > n) ? (n - start) : chunk;
        const double *ptr = d + start;
#else
        int64_t len = n;
        const double *ptr = d;
#endif
        if (len > 0) {
            __m512d sum = _mm512_setzero_pd();
            int64_t i = 0;
            for (; i + 8 <= len; i += 8)
                sum = _mm512_add_pd(sum, _mm512_loadu_pd(ptr + i));
            double tsum = hsum512(sum);
            for (; i < len; i++) tsum += ptr[i];
            r += tsum;
        }
    }
    return r;
}

static double dot_f64_avx512(const double *a, const double *b, int64_t n) {
    double r = 0;
#ifdef _OPENMP
    #pragma omp parallel reduction(+:r)
#endif
    {
#ifdef _OPENMP
        int tid = omp_get_thread_num(), nt = omp_get_num_threads();
        int64_t chunk = (n + nt - 1) / nt;
        int64_t start = (int64_t)tid * chunk;
        int64_t len = (start + chunk > n) ? (n - start) : chunk;
        const double *pa = a + start, *pb = b + start;
#else
        int64_t len = n;
        const double *pa = a, *pb = b;
#endif
        if (len > 0) {
            __m512d sum = _mm512_setzero_pd();
            int64_t i = 0;
            for (; i + 8 <= len; i += 8)
                sum = _mm512_fmadd_pd(_mm512_loadu_pd(pa + i),
                                      _mm512_loadu_pd(pb + i), sum);
            double tsum = hsum512(sum);
            for (; i < len; i++) tsum += pa[i] * pb[i];
            r += tsum;
        }
    }
    return r;
}
#endif /* SHAKTI_USE_AVX512 */

#if defined(__aarch64__)
static double sum_f64_neon(const double *d, int64_t n) {
    float64x2_t acc = vdupq_n_f64(0);
    int64_t i = 0;
    for (; i + 2 <= n; i += 2)
        acc = vaddq_f64(acc, vld1q_f64(d + i));
    double r = vaddvq_f64(acc);
    for (; i < n; i++) r += d[i];
    return r;
}

static double dot_f64_neon(const double *a, const double *b, int64_t n) {
    float64x2_t acc = vdupq_n_f64(0);
    int64_t i = 0;
    for (; i + 2 <= n; i += 2)
        acc = vfmaq_f64(acc, vld1q_f64(a + i), vld1q_f64(b + i));
    double r = vaddvq_f64(acc);
    for (; i < n; i++) r += a[i] * b[i];
    return r;
}
#endif

double shakti_sum_f64(const double *d, int64_t n) {
    if (n <= 0) return 0.0;
#if SHAKTI_USE_AVX512
    return sum_f64_avx512(d, n);
#elif defined(__aarch64__)
    if (n >= ISL_OMP_VEC_MIN) {
        double r = 0;
#ifdef _OPENMP
        #pragma omp parallel reduction(+:r)
        {
            int tid = omp_get_thread_num(), nt = omp_get_num_threads();
            int64_t chunk = (n + nt - 1) / nt;
            int64_t start = (int64_t)tid * chunk;
            int64_t len = (start + chunk > n) ? (n - start) : chunk;
            if (len > 0) r += sum_f64_neon(d + start, len);
        }
#else
        r = sum_f64_neon(d, n);
#endif
        return r;
    }
    return sum_f64_neon(d, n);
#else
    double r = 0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:r) if (n >= ISL_OMP_VEC_MIN)
#endif
    for (int64_t i = 0; i < n; i++) r += d[i];
    return r;
#endif
}

double shakti_dot_f64(const double *a, const double *b, int64_t n) {
    if (n <= 0) return 0.0;
#if SHAKTI_USE_AVX512
    return dot_f64_avx512(a, b, n);
#elif defined(__aarch64__)
    if (n >= ISL_OMP_VEC_MIN) {
        double r = 0;
#ifdef _OPENMP
        #pragma omp parallel for reduction(+:r)
#endif
        for (int64_t i = 0; i < n; i++) r += a[i] * b[i];
        return r;
    }
    return dot_f64_neon(a, b, n);
#else
    double r = 0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:r) if (n >= ISL_OMP_VEC_MIN)
#endif
    for (int64_t i = 0; i < n; i++) r += a[i] * b[i];
    return r;
#endif
}

double shakti_dot_numeric(const int64_t *aj, const double *af, int a_fvec,
                          const int64_t *bj, const double *bf, int b_fvec,
                          int64_t n) {
    if (n <= 0) return 0.0;
    if (a_fvec && b_fvec) return shakti_dot_f64(af, bf, n);
    double r = 0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:r) if (n >= ISL_OMP_VEC_MIN)
#endif
    for (int64_t i = 0; i < n; i++) {
        double x = a_fvec ? af[i] : (double)aj[i];
        double y = b_fvec ? bf[i] : (double)bj[i];
        r += x * y;
    }
    return r;
}
