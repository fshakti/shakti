/* stem_stats.c — scalar f32 window/OLA/Wiener/SI-SDR for stem. */
#include "stem_stats.h"
#include "vec_kernels.h"

#include <math.h>
#include <stdint.h>

#ifndef STEM_STATS_EPS
#define STEM_STATS_EPS 1e-8f
#endif

void stem_window_mul_f32(const float *frame, const float *win, float *re, float *im, int n) {
    int i;
    if (!frame || !win || !re || !im || n <= 0) return;
    for (i = 0; i < n; i++) {
        re[i] = frame[i] * win[i];
        im[i] = 0.f;
    }
}

void stem_win_scale_f32(const float *re, const float *win, float scale, float *out, int n) {
    int i;
    if (!re || !win || !out || n <= 0) return;
    for (i = 0; i < n; i++) out[i] = (re[i] * win[i]) * scale;
}

void stem_ola_add_f32(float *dst, const float *frame, int n) {
    int i;
    if (!dst || !frame || n <= 0) return;
    for (i = 0; i < n; i++) dst[i] += frame[i];
}

void stem_wiener_irm_f32(const float *harm, const float *perc, float *drums, float *bass,
                         float *vocals, float *other, int nbins, float bass_hi, float voc_lo,
                         float voc_hi) {
    float bass_w[STEM_STATS_MAX_BINS];
    float voc_w[STEM_STATS_MAX_BINS];
    int b, n;
    const float eps = STEM_STATS_EPS;
    if (!harm || !perc || !drums || !bass || !vocals || !other || nbins <= 0) return;
    n = nbins;
    if (n > STEM_STATS_MAX_BINS) n = STEM_STATS_MAX_BINS;
    for (b = 0; b < n; b++) {
        float bf = (float)b;
        float vw;
        bass_w[b] = 1.f / (1.f + expf(0.35f * (bf - bass_hi)));
        vw = 1.f / (1.f + expf(-0.25f * (bf - voc_lo)));
        vw *= 1.f / (1.f + expf(0.25f * (bf - voc_hi)));
        voc_w[b] = vw;
    }
    for (b = 0; b < n; b++) {
        float H = harm[b], P = perc[b];
        float total = H + P + eps;
        float soft_h = H / total;
        float soft_p = P / total;
        float d, ba, vo, oth, used, ssum;
        d = soft_p;
        ba = soft_h * bass_w[b];
        vo = soft_h * voc_w[b] * (1.f - 0.7f * bass_w[b]);
        used = d + ba + vo;
        oth = used < 1.f ? 1.f - used : 0.f;
        ssum = d + ba + vo + oth + eps;
        drums[b] = d / ssum;
        bass[b] = ba / ssum;
        vocals[b] = vo / ssum;
        other[b] = oth / ssum;
    }
}

float stem_si_sdr_f32(const float *est, const float *ref, int n) {
    double num, den, est2, alpha, s_pow, e_pow;
    int64_t nn;
    if (!est || !ref || n <= 0) return 0.f;
    nn = n;
    num = shakti_dot_f32(est, ref, nn);
    den = shakti_sumsq_f32(ref, nn);
    est2 = shakti_sumsq_f32(est, nn);
    if (den < 1e-24) return 0.f;
    alpha = num / den;
    s_pow = alpha * alpha * den;
    e_pow = est2 + s_pow - 2.0 * alpha * num;
    if (e_pow < 1e-24) return 120.f;
    if (s_pow < 1e-24) return -120.f;
    return (float)(10.0 * log10(s_pow / e_pow));
}
