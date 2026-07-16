#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "vec_kernels.h"

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    const int64_t N = 1 << 20;
    const int64_t M = 1 << 18;
    int64_t *keys = (int64_t *)malloc((size_t)N * sizeof(int64_t));
    int64_t *qs = (int64_t *)malloc((size_t)M * sizeof(int64_t));
    int64_t *out = (int64_t *)malloc((size_t)M * sizeof(int64_t));
    if (!keys || !qs || !out) return 1;
    for (int64_t i = 0; i < N; i++) keys[i] = i * 3;
    for (int64_t j = 0; j < M; j++) qs[j] = (j * 7 + 13) % (N * 3);

    double t0 = now_sec();
    int64_t sink = 0;
    for (int64_t j = 0; j < M; j++) sink += shakti_bin_i64(keys, N, qs[j]);
    double t1 = now_sec();
    printf("scalar_bin  N=%lld M=%lld  %.3f ms  sink=%lld\n",
           (long long)N, (long long)M, (t1 - t0) * 1000.0, (long long)sink);

    t0 = now_sec();
    shakti_bin_i64_batch(keys, N, qs, M, out);
    t1 = now_sec();
    sink = 0;
    for (int64_t j = 0; j < M; j++) sink += out[j];
    printf("batch_bin   N=%lld M=%lld  %.3f ms  sink=%lld\n",
           (long long)N, (long long)M, (t1 - t0) * 1000.0, (long long)sink);

    for (int64_t j = 0; j < M; j++) qs[j] = (j * (N * 3)) / M;
    t0 = now_sec();
    shakti_bin_i64_batch(keys, N, qs, M, out);
    t1 = now_sec();
    sink = 0;
    for (int64_t j = 0; j < M; j++) sink += out[j];
    printf("merge_bin   N=%lld M=%lld  %.3f ms  sink=%lld\n",
           (long long)N, (long long)M, (t1 - t0) * 1000.0, (long long)sink);

    free(keys); free(qs); free(out);
    return 0;
}
