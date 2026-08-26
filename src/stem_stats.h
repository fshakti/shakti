/* stem_stats.h — scalar f32 spectrogram stats for stem.
 * Live callback: one-core only. Do not OpenMP from the stem process path. */
#ifndef STEM_STATS_H
#define STEM_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STEM_STATS_MAX_BINS
#define STEM_STATS_MAX_BINS 513 /* (1024/2)+1 */
#endif

void stem_window_mul_f32(const float *frame, const float *win, float *re, float *im, int n);
void stem_win_scale_f32(const float *re, const float *win, float scale, float *out, int n);
void stem_ola_add_f32(float *dst, const float *frame, int n);
void stem_wiener_irm_f32(const float *harm, const float *perc, float *drums, float *bass,
                         float *vocals, float *other, int nbins, float bass_hi, float voc_lo,
                         float voc_hi);
float stem_si_sdr_f32(const float *est, const float *ref, int n);

#ifdef __cplusplus
}
#endif

#endif
