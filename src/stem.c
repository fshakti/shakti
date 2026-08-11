/*
 * stem.c — streaming 4-stem separator for Shakti (drums / bass / vocals / other).
 *
 * Classical: ring-fed STFT → median HPSS → band soft-masks → ISTFT overlap-add.
 * Algorithmic look-ahead is STEM_HPSS_HALF hops (~64–100 ms at typical rates).
 *
 * Offline ML: Shakti spectrogram MLP (SHAKST01) — mag frames → soft masks → ISTFT.
 * Matmuls are CPU-only (optional OpenMP). No GPU path.
 */
#include "stem.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

/* a.h short macros collide with common DSP names (im, st, i0, ...). */
#undef ia
#undef it
#undef ih
#undef ii
#undef ij
#undef ik
#undef il
#undef im
#undef in
#undef cc
#undef cd
#undef ss
#undef st
#undef i0
#undef g0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STEM_NFFT 1024
#define STEM_HOP 256
#define STEM_BINS ((STEM_NFFT / 2) + 1)
#define STEM_HPSS_LEN 17 /* odd; half = look-ahead frames */
#define STEM_HPSS_HALF (STEM_HPSS_LEN / 2)
#define STEM_OLA_CAP (STEM_NFFT * 8)
#define STEM_RING_CAP (1 << 16)
/* Hann² constant-overlap-add gain for hop = n_fft/4 */
#define STEM_COLA 1.5f

/* Shakti-owned offline MLP checkpoint (not Isolde STEMML01). */
#define STEM_ML_MAGIC "SHAKST01"
#define STEM_ML_HIDDEN_DEFAULT 64
#define STEM_ML_BATCH_MAX 256
#define STEM_ML_FLAG_LOG1P 1
#define STEM_ML_MAX_SAMP (8 * 1024 * 1024)
#define STEM_ML_SPIKE_ITERS_MAX 10000
/* Assumed training / feature sample rate for SHAKST01 spectrogram MLP. */
#define STEM_ML_SR 44100

enum { STEM_DRUMS = 0, STEM_BASS = 1, STEM_VOCALS = 2, STEM_OTHER = 3, STEM_N = 4 };

typedef struct {
    int open;
    int sr;
    int block; /* preferred process block; any size accepted */
    float gains[STEM_N];

    /* STFT analysis state */
    float win[STEM_NFFT];
    float frame[STEM_NFFT];
    int frame_fill;

    /* complex FFT scratch */
    float re[STEM_NFFT];
    float im[STEM_NFFT];

    /* magnitude + complex spectrogram ring for HPSS (STEM_HPSS_LEN frames) */
    float mag_hist[STEM_HPSS_LEN][STEM_BINS];
    float re_hist[STEM_HPSS_LEN][STEM_BINS];
    float im_hist[STEM_HPSS_LEN][STEM_BINS];
    int mag_w;   /* next write index */
    int mag_n;   /* frames filled */
    int hop_count;

    /* ISTFT overlap buffers per stem */
    float ola[STEM_N][STEM_OLA_CAP];
    int ola_len;

    /* ready output ring per stem */
    float out[STEM_N][STEM_RING_CAP];
    int out_r[STEM_N], out_w[STEM_N], out_n[STEM_N];

    /* reusable process() scratch (avoids malloc per call) */
    float *proc_scratch;
    int64_t proc_scratch_cap;
} StemState;

/* Precomputed 1024-pt FFT: bit-reversal + packed stage twiddles. */
static int g_fft_br[STEM_NFFT];
static float g_fft_wr[STEM_NFFT];
static float g_fft_wi[STEM_NFFT];
static int g_fft_ready;

typedef struct {
    int loaded;
    int nbins;
    int nhidden;
    int nstems;
    int flags;
    double *W1, *b1, *W2, *b2, *W3, *b3;
    double *x_batch;
    double *h1_batch;
    double *h2_batch;
    double *y_batch;
    int batch_cap;
    char path[512];
} StemMl;

static StemState g_stem;
static StemMl g_ml;

static float stem_hz_to_bin(float hz, int sr) {
    float b = hz * (float)STEM_NFFT / (float)sr;
    if (b < 0.f) b = 0.f;
    if (b > (float)(STEM_BINS - 1)) b = (float)(STEM_BINS - 1);
    return b;
}

static void stem_fft_init(void) {
    int i, j, m, k, off;
    if (g_fft_ready) return;
    for (i = 0; i < STEM_NFFT; i++) {
        int x = i, y = 0;
        for (j = 0; j < 10; j++) { /* log2(1024) = 10 */
            y = (y << 1) | (x & 1);
            x >>= 1;
        }
        g_fft_br[i] = y;
    }
    off = 0;
    for (m = 2; m <= STEM_NFFT; m <<= 1) {
        float theta = -2.0f * (float)M_PI / (float)m;
        for (k = 0; k < m / 2; k++) {
            float a = theta * (float)k;
            g_fft_wr[off + k] = cosf(a);
            g_fft_wi[off + k] = sinf(a);
        }
        off += m / 2;
    }
    g_fft_ready = 1;
}

/* Fixed-size 1024-pt in-place radix-2 using precomputed tables. */
static void stem_fft(float *re, float *im, int n, int inverse) {
    int i, k, m, off;
    float tmp, s;
    (void)n;
    stem_fft_init();
    for (i = 0; i < STEM_NFFT; i++) {
        int j = g_fft_br[i];
        if (i < j) {
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }
    off = 0;
    for (m = 2; m <= STEM_NFFT; m <<= 1) {
        int half = m / 2;
        for (i = 0; i < STEM_NFFT; i += m) {
            for (k = 0; k < half; k++) {
                float wr = g_fft_wr[off + k];
                float wi = g_fft_wi[off + k];
                int ia0 = i + k, ia1 = i + k + half;
                float tr, ti;
                if (inverse) wi = -wi;
                tr = wr * re[ia1] - wi * im[ia1];
                ti = wr * im[ia1] + wi * re[ia1];
                re[ia1] = re[ia0] - tr;
                im[ia1] = im[ia0] - ti;
                re[ia0] += tr;
                im[ia0] += ti;
            }
        }
        off += half;
    }
    if (inverse) {
        s = 1.f / (float)STEM_NFFT;
        for (i = 0; i < STEM_NFFT; i++) {
            re[i] *= s;
            im[i] *= s;
        }
    }
}

static float stem_median_copy(float *tmp, int n) {
    /* insertion sort; n <= STEM_HPSS_LEN */
    int i, j;
    for (i = 1; i < n; i++) {
        float v = tmp[i];
        for (j = i; j > 0 && tmp[j - 1] > v; j--) tmp[j] = tmp[j - 1];
        tmp[j] = v;
    }
    return tmp[n / 2];
}

static void stem_mag_from_cplx(const float *re, const float *im, float *mag, int n) {
    int b = 0;
#if defined(__AVX2__)
    for (; b + 8 <= n; b += 8) {
        __m256 r = _mm256_loadu_ps(re + b);
        __m256 imv = _mm256_loadu_ps(im + b);
        __m256 m = _mm256_sqrt_ps(_mm256_fmadd_ps(r, r, _mm256_mul_ps(imv, imv)));
        _mm256_storeu_ps(mag + b, m);
    }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (; b + 4 <= n; b += 4) {
        float32x4_t r = vld1q_f32(re + b);
        float32x4_t imv = vld1q_f32(im + b);
        float32x4_t m = vsqrtq_f32(vmlaq_f32(vmulq_f32(r, r), imv, imv));
        vst1q_f32(mag + b, m);
    }
#endif
    for (; b < n; b++)
        mag[b] = sqrtf(re[b] * re[b] + im[b] * im[b]);
}

static void stem_ring_push(float *buf, int *r, int *w, int *n, float x) {
    if (*n >= STEM_RING_CAP) {
        /* drop oldest */
        *r = (*r + 1) & (STEM_RING_CAP - 1);
        (*n)--;
    }
    buf[*w] = x;
    *w = (*w + 1) & (STEM_RING_CAP - 1);
    (*n)++;
}

static int stem_ring_pop(float *buf, int *r, int *w, int *n, float *out) {
    (void)w;
    if (*n <= 0) return 0;
    *out = buf[*r];
    *r = (*r + 1) & (STEM_RING_CAP - 1);
    (*n)--;
    return 1;
}

static void stem_ola_add(StemState *st, int stem, const float *frame) {
    int i = 0;
    float *dst = st->ola[stem];
#if defined(__AVX2__)
    for (; i + 8 <= STEM_NFFT; i += 8) {
        __m256 a = _mm256_loadu_ps(dst + i);
        __m256 b = _mm256_loadu_ps(frame + i);
        _mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
    }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (; i + 4 <= STEM_NFFT; i += 4) {
        float32x4_t a = vld1q_f32(dst + i);
        float32x4_t b = vld1q_f32(frame + i);
        vst1q_f32(dst + i, vaddq_f32(a, b));
    }
#endif
    for (; i < STEM_NFFT; i++) dst[i] += frame[i];
    if (st->ola_len < STEM_NFFT) st->ola_len = STEM_NFFT;
}

static void stem_ola_emit_hop(StemState *st) {
    int s, i;
    for (s = 0; s < STEM_N; s++) {
        for (i = 0; i < STEM_HOP; i++)
            stem_ring_push(st->out[s], &st->out_r[s], &st->out_w[s], &st->out_n[s],
                           st->ola[s][i] * st->gains[s]);
        memmove(st->ola[s], st->ola[s] + STEM_HOP,
                (size_t)(STEM_OLA_CAP - STEM_HOP) * sizeof(float));
        memset(st->ola[s] + (STEM_OLA_CAP - STEM_HOP), 0, STEM_HOP * sizeof(float));
    }
    if (st->ola_len >= STEM_HOP) st->ola_len -= STEM_HOP;
}

static void stem_process_frame(StemState *st) {
    float mag[STEM_BINS];
    float harm[STEM_BINS], perc[STEM_BINS];
    float tmp[STEM_HPSS_LEN];
    float masks[STEM_N][STEM_BINS];
    float phase_re[STEM_BINS], phase_im[STEM_BINS];
    float synth[STEM_NFFT];
    float bass_hi, voc_lo, voc_hi;
    int i, b, h, center, s;
    float eps = 1e-8f;

    /* window + FFT */
    for (i = 0; i < STEM_NFFT; i++) {
        st->re[i] = st->frame[i] * st->win[i];
        st->im[i] = 0.f;
    }
    stem_fft(st->re, st->im, STEM_NFFT, 0);

    stem_mag_from_cplx(st->re, st->im, mag, STEM_BINS);
    memcpy(phase_re, st->re, sizeof phase_re);
    memcpy(phase_im, st->im, sizeof phase_im);

    /* push into magnitude + complex history (phase must match HPSS center frame) */
    memcpy(st->mag_hist[st->mag_w], mag, sizeof mag);
    memcpy(st->re_hist[st->mag_w], phase_re, sizeof phase_re);
    memcpy(st->im_hist[st->mag_w], phase_im, sizeof phase_im);
    st->mag_w = (st->mag_w + 1) % STEM_HPSS_LEN;
    if (st->mag_n < STEM_HPSS_LEN) st->mag_n++;
    st->hop_count++;

    /* need full HPSS window before emitting (look-ahead = HALF hops) */
    if (st->mag_n < STEM_HPSS_LEN) return;

    center = (st->mag_w + STEM_HPSS_LEN - 1 - STEM_HPSS_HALF) % STEM_HPSS_LEN;

    /* harmonic: median over time; percussive: median over frequency */
    for (b = 0; b < STEM_BINS; b++) {
        for (h = 0; h < STEM_HPSS_LEN; h++)
            tmp[h] = st->mag_hist[(center - STEM_HPSS_HALF + h + STEM_HPSS_LEN) % STEM_HPSS_LEN][b];
        harm[b] = stem_median_copy(tmp, STEM_HPSS_LEN);
    }
    for (b = 0; b < STEM_BINS; b++) {
        int lo = b - STEM_HPSS_HALF;
        int hi = b + STEM_HPSS_HALF;
        int nn = 0;
        if (lo < 0) lo = 0;
        if (hi >= STEM_BINS) hi = STEM_BINS - 1;
        for (i = lo; i <= hi; i++) tmp[nn++] = st->mag_hist[center][i];
        perc[b] = stem_median_copy(tmp, nn);
    }

    bass_hi = stem_hz_to_bin(250.f, st->sr);
    voc_lo = stem_hz_to_bin(200.f, st->sr);
    voc_hi = stem_hz_to_bin(4000.f, st->sr);

    for (b = 0; b < STEM_BINS; b++) {
        float H = harm[b], P = perc[b];
        float total = H + P + eps;
        float soft_h = H / total;
        float soft_p = P / total;
        float bf = (float)b;
        float bass_w = 1.f / (1.f + expf(0.35f * (bf - bass_hi)));
        float voc_w = 1.f / (1.f + expf(-0.25f * (bf - voc_lo)));
        voc_w *= 1.f / (1.f + expf(0.25f * (bf - voc_hi)));

        masks[STEM_DRUMS][b] = soft_p;
        masks[STEM_BASS][b] = soft_h * bass_w;
        masks[STEM_VOCALS][b] = soft_h * voc_w * (1.f - 0.7f * bass_w);
        {
            float used = masks[STEM_DRUMS][b] + masks[STEM_BASS][b] + masks[STEM_VOCALS][b];
            masks[STEM_OTHER][b] = fmaxf(0.f, 1.f - used);
        }
        /* renormalize so masks sum ~1 */
        {
            float ssum = masks[STEM_DRUMS][b] + masks[STEM_BASS][b] + masks[STEM_VOCALS][b] +
                         masks[STEM_OTHER][b] + eps;
            for (s = 0; s < STEM_N; s++) masks[s][b] /= ssum;
        }
    }

    /* ISTFT each stem using the HPSS center frame's phase */
    for (s = 0; s < STEM_N; s++) {
        memset(st->re, 0, sizeof st->re);
        memset(st->im, 0, sizeof st->im);
        for (b = 0; b < STEM_BINS; b++) {
            st->re[b] = st->re_hist[center][b] * masks[s][b];
            st->im[b] = st->im_hist[center][b] * masks[s][b];
            if (b > 0 && b < STEM_BINS - 1) {
                st->re[STEM_NFFT - b] = st->re[b];
                st->im[STEM_NFFT - b] = -st->im[b];
            }
        }
        stem_fft(st->re, st->im, STEM_NFFT, 1);
        for (i = 0; i < STEM_NFFT; i++)
            synth[i] = (st->re[i] * st->win[i]) / STEM_COLA;
        stem_ola_add(st, s, synth);
    }
    stem_ola_emit_hop(st);
}

static void stem_feed_sample(StemState *st, float x) {
    if (st->frame_fill < STEM_NFFT)
        st->frame[st->frame_fill++] = x;
    if (st->frame_fill == STEM_NFFT) {
        stem_process_frame(st);
        memmove(st->frame, st->frame + STEM_HOP, (STEM_NFFT - STEM_HOP) * sizeof(float));
        st->frame_fill = STEM_NFFT - STEM_HOP;
    }
}

static void stem_feed(StemState *st, const float *x, int n) {
    int i;
    for (i = 0; i < n; i++) stem_feed_sample(st, x[i]);
}

static double stem_latency_samples(int sr) {
    (void)sr;
    /* look-ahead = STEM_HPSS_HALF hops + (NFFT-HOP) analysis delay */
    return (double)STEM_HPSS_HALF * (double)STEM_HOP + (double)(STEM_NFFT - STEM_HOP);
}

static double stem_latency_ms_calc(int sr) {
    double samples = stem_latency_samples(sr);
    return 1000.0 * samples / (double)(sr > 0 ? sr : 44100);
}

static V *stem_dict_from_outs(StemState *st, int n_out) {
    static const char *names[STEM_N] = {"drums", "bass", "vocals", "other"};
    V *d = v_dict_empty();
    int s, i;
    for (s = 0; s < STEM_N; s++) {
        V *fv = v_fvec(n_out);
        for (i = 0; i < n_out; i++) {
            float v = 0.f;
            if (!stem_ring_pop(st->out[s], &st->out_r[s], &st->out_w[s], &st->out_n[s], &v))
                v = 0.f;
            fv->F[i] = (double)v;
        }
        v_dict_set(d, names[s], fv);
        v_free(fv);
    }
    return d;
}

/* ---- WAV helpers (PCM16 mono/stereo LE; RIFF chunk walk) ---- */
static int stem_read_u32le(FILE *fp, unsigned int *out) {
    unsigned char b[4];
    if (fread(b, 1, 4, fp) != 4) return -1;
    *out = (unsigned int)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    return 0;
}

static int stem_read_u16le(FILE *fp, unsigned short *out) {
    unsigned char b[2];
    if (fread(b, 1, 2, fp) != 2) return -1;
    *out = (unsigned short)(b[0] | (b[1] << 8));
    return 0;
}

static int stem_read_wav(const char *path, float **out, int *n_out, int *sr_out) {
    FILE *fp = fopen(path, "rb");
    unsigned char riff[12];
    unsigned int chunk_sz = 0, sr = 0, data_bytes = 0;
    unsigned short ch = 0, bps = 0, fmt = 0;
    int have_fmt = 0, have_data = 0;
    long data_pos = -1;
    int nframes, i, j;
    int16_t *pcm;

    *out = NULL;
    if (!fp) return -1;
    if (fread(riff, 1, 12, fp) != 12) { fclose(fp); return -1; }
    if (memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) { fclose(fp); return -1; }

    while (!have_data) {
        unsigned char idb[4];
        if (fread(idb, 1, 4, fp) != 4) break;
        if (stem_read_u32le(fp, &chunk_sz) != 0) break;

        if (memcmp(idb, "fmt ", 4) == 0) {
            unsigned short audio_fmt = 0, channels = 0, bits = 0, align = 0;
            unsigned int rate = 0, byterate = 0;
            if (chunk_sz < 16) { fclose(fp); return -1; }
            if (stem_read_u16le(fp, &audio_fmt) ||
                stem_read_u16le(fp, &channels) ||
                stem_read_u32le(fp, &rate) ||
                stem_read_u32le(fp, &byterate) ||
                stem_read_u16le(fp, &align) ||
                stem_read_u16le(fp, &bits)) {
                fclose(fp); return -1;
            }
            (void)byterate;
            (void)align;
            if (chunk_sz > 16 && fseek(fp, (long)(chunk_sz - 16), SEEK_CUR) != 0) {
                fclose(fp); return -1;
            }
            if (chunk_sz & 1u) fseek(fp, 1, SEEK_CUR);
            fmt = audio_fmt;
            ch = channels;
            sr = rate;
            bps = bits;
            have_fmt = 1;
            continue;
        }

        if (memcmp(idb, "data", 4) == 0) {
            data_pos = ftell(fp);
            data_bytes = chunk_sz;
            have_data = 1;
            break;
        }

        if (fseek(fp, (long)chunk_sz + (long)(chunk_sz & 1u), SEEK_CUR) != 0) {
            fclose(fp); return -1;
        }
    }

    if (!have_fmt || !have_data || data_pos < 0) { fclose(fp); return -1; }
    if (fmt != 1 || (bps != 16 && bps != 32) || ch < 1 || ch > 2) { fclose(fp); return -1; }
    /* Clamp declared data size to remaining file bytes and a 256 MiB sample cap. */
    {
        long file_end;
        unsigned int avail, frame_bytes, max_bytes = 256u * 1024u * 1024u;
        if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
        file_end = ftell(fp);
        if (file_end < data_pos) { fclose(fp); return -1; }
        avail = (unsigned int)((file_end - data_pos) > (long)UINT_MAX
                               ? UINT_MAX : (file_end - data_pos));
        if (data_bytes > avail) data_bytes = avail;
        frame_bytes = (unsigned)ch * (bps / 8u);
        if (frame_bytes == 0) { fclose(fp); return -1; }
        if (data_bytes / frame_bytes > max_bytes / frame_bytes)
            data_bytes = (max_bytes / frame_bytes) * frame_bytes;
    }
    if (fseek(fp, data_pos, SEEK_SET) != 0) { fclose(fp); return -1; }

    if (bps == 16) {
        if (data_bytes / (2u * (unsigned)ch) > (unsigned)INT_MAX) { fclose(fp); return -1; }
        nframes = (int)(data_bytes / (2u * (unsigned)ch));
        if (nframes < 1) { fclose(fp); return -1; }
        pcm = (int16_t *)malloc((size_t)nframes * (size_t)ch * sizeof(int16_t));
        if (!pcm) { fclose(fp); return -1; }
        if ((int)fread(pcm, 2 * ch, (size_t)nframes, fp) != nframes) {
            free(pcm); fclose(fp); return -1;
        }
        *out = (float *)malloc((size_t)nframes * sizeof(float));
        if (!*out) { free(pcm); fclose(fp); return -1; }
        for (i = 0; i < nframes; i++) {
            float acc = 0.f;
            for (j = 0; j < ch; j++) acc += (float)pcm[i * ch + j] / 32768.f;
            (*out)[i] = acc / (float)ch;
        }
        free(pcm);
    } else {
        float *fpcm;
        if (data_bytes / (4u * (unsigned)ch) > (unsigned)INT_MAX) { fclose(fp); return -1; }
        nframes = (int)(data_bytes / (4u * (unsigned)ch));
        if (nframes < 1) { fclose(fp); return -1; }
        fpcm = (float *)malloc((size_t)nframes * (size_t)ch * sizeof(float));
        if (!fpcm) { fclose(fp); return -1; }
        if ((int)fread(fpcm, 4 * ch, (size_t)nframes, fp) != nframes) {
            free(fpcm); fclose(fp); return -1;
        }
        *out = (float *)malloc((size_t)nframes * sizeof(float));
        if (!*out) { free(fpcm); fclose(fp); return -1; }
        for (i = 0; i < nframes; i++) {
            float acc = 0.f;
            for (j = 0; j < ch; j++) acc += fpcm[i * ch + j];
            (*out)[i] = acc / (float)ch;
        }
        free(fpcm);
    }

    fclose(fp);
    *n_out = nframes;
    *sr_out = (int)sr;
    return 0;
}

static int stem_write_wav(const char *path, const float *x, int n, int sr) {
    FILE *fp = fopen(path, "wb");
    unsigned int data_bytes = (unsigned int)n * 2u;
    unsigned char hdr[44];
    int i;
    if (!fp) return -1;
    if (n < 0) { fclose(fp); return -1; }
    memset(hdr, 0, 44);
    memcpy(hdr, "RIFF", 4);
    {
        unsigned int chunk = 36u + data_bytes;
        hdr[4] = chunk & 255; hdr[5] = (chunk >> 8) & 255;
        hdr[6] = (chunk >> 16) & 255; hdr[7] = (chunk >> 24) & 255;
    }
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    hdr[16] = 16;
    hdr[20] = 1; /* PCM */
    hdr[22] = 1; /* mono */
    hdr[24] = sr & 255; hdr[25] = (sr >> 8) & 255;
    hdr[26] = (sr >> 16) & 255; hdr[27] = (sr >> 24) & 255;
    {
        unsigned int br = (unsigned int)sr * 2u;
        hdr[28] = br & 255; hdr[29] = (br >> 8) & 255;
        hdr[30] = (br >> 16) & 255; hdr[31] = (br >> 24) & 255;
    }
    hdr[32] = 2; hdr[34] = 16;
    memcpy(hdr + 36, "data", 4);
    hdr[40] = data_bytes & 255; hdr[41] = (data_bytes >> 8) & 255;
    hdr[42] = (data_bytes >> 16) & 255; hdr[43] = (data_bytes >> 24) & 255;
    if (fwrite(hdr, 1, 44, fp) != 44) { fclose(fp); return -1; }
    for (i = 0; i < n; i++) {
        float v = x[i];
        int s;
        unsigned char le[2];
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        s = (int)lrintf(v * 32767.f);
        le[0] = (unsigned char)(s & 255);
        le[1] = (unsigned char)((s >> 8) & 255);
        if (fwrite(le, 1, 2, fp) != 2) { fclose(fp); return -1; }
    }
    if (fclose(fp) != 0) return -1;
    return 0;
}

/* ---- builtins ---- */

V *bi_stem_open(V **a, int n) {
    int sr = 44100, block = STEM_HOP;
    int i;
    float *keep_scratch;
    int64_t keep_cap;
    if (g_stem.open) return v_err("stem_open: already open");
    if (n >= 1) {
        if (a[0]->t == T_INT) sr = (int)a[0]->j;
        else if (a[0]->t == T_FLOAT) sr = (int)a[0]->f;
        else return v_err("stem_open(sr, block)");
    }
    if (n >= 2) {
        if (a[1]->t == T_INT) block = (int)a[1]->j;
        else if (a[1]->t == T_FLOAT) block = (int)a[1]->f;
        else return v_err("stem_open(sr, block)");
    }
    if (sr < 8000) sr = 8000;
    if (sr > 192000) sr = 192000;
    if (block < 32) block = 32;
    keep_scratch = g_stem.proc_scratch;
    keep_cap = g_stem.proc_scratch_cap;
    memset(&g_stem, 0, sizeof g_stem);
    g_stem.proc_scratch = keep_scratch;
    g_stem.proc_scratch_cap = keep_cap;
    g_stem.sr = sr;
    g_stem.block = block;
    for (i = 0; i < STEM_N; i++) g_stem.gains[i] = 1.f;
    for (i = 0; i < STEM_NFFT; i++)
        g_stem.win[i] = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)STEM_NFFT);
    stem_fft_init();
    g_stem.open = 1;
    return v_nil();
}

V *bi_stem_close(V **a, int n) {
    float *keep_scratch;
    int64_t keep_cap;
    (void)a;
    (void)n;
    keep_scratch = g_stem.proc_scratch;
    keep_cap = g_stem.proc_scratch_cap;
    memset(&g_stem, 0, sizeof g_stem);
    g_stem.proc_scratch = keep_scratch;
    g_stem.proc_scratch_cap = keep_cap;
    return v_nil();
}

V *bi_stem_alive(V **a, int n) {
    (void)a;
    (void)n;
    return v_bool(g_stem.open);
}

V *bi_stem_latency_ms(V **a, int n) {
    (void)a;
    (void)n;
    return v_float(stem_latency_ms_calc(g_stem.open ? g_stem.sr : 44100));
}

V *bi_stem_info(V **a, int n) {
    V *d;
    (void)a;
    (void)n;
    d = v_dict_empty();
    v_dict_put(d, "n_fft", v_int(STEM_NFFT));
    v_dict_put(d, "hop", v_int(STEM_HOP));
    v_dict_put(d, "hpss_len", v_int(STEM_HPSS_LEN));
    v_dict_put(d, "latency_samples", v_int((int)stem_latency_samples(g_stem.open ? g_stem.sr : 44100)));
    v_dict_put(d, "latency_ms", v_float(stem_latency_ms_calc(g_stem.open ? g_stem.sr : 44100)));
    v_dict_put(d, "open", v_bool(g_stem.open));
    if (g_stem.open) {
        v_dict_put(d, "sr", v_int(g_stem.sr));
        v_dict_put(d, "block", v_int(g_stem.block));
    }
    return d;
}

V *bi_stem_set_gains(V **a, int n) {
    int i;
    if (!g_stem.open) return v_err("stem_set_gains: not open");
    if (n < 4) return v_err("stem_set_gains(drums, bass, vocals, other)");
    for (i = 0; i < STEM_N; i++) {
        if (a[i]->t == T_FLOAT) g_stem.gains[i] = (float)a[i]->f;
        else if (a[i]->t == T_INT) g_stem.gains[i] = (float)a[i]->j;
        else return v_err("stem_set_gains: expected numbers");
    }
    return v_nil();
}

V *bi_stem_gains(V **a, int n) {
    V *d;
    (void)a;
    (void)n;
    if (!g_stem.open) return v_err("stem_gains: not open");
    d = v_dict_empty();
    v_dict_put(d, "drums", v_float(g_stem.gains[0]));
    v_dict_put(d, "bass", v_float(g_stem.gains[1]));
    v_dict_put(d, "vocals", v_float(g_stem.gains[2]));
    v_dict_put(d, "other", v_float(g_stem.gains[3]));
    return d;
}

V *bi_stem_process(V **a, int n) {
    StemState *st = &g_stem;
    float *tmp = NULL;
    int64_t count, i;
    int n_out;
    V *samples;
    if (!st->open) return v_err("stem_process: not open");
    if (n < 1) return v_err("stem_process(samples)");
    samples = a[0];
    if (samples->t == T_FVEC) {
        count = samples->n;
    } else if (samples->t == T_LIST) {
        count = samples->n;
    } else {
        return v_err("stem_process: expected fvec or list");
    }
    if (count < 0) return v_err("stem_process: bad length");
    if (count == 0) return stem_dict_from_outs(st, 0);
    if (count > 8 * 1024 * 1024) return v_err("stem_process: block too large");
    if (count > st->proc_scratch_cap) {
        float *nbuf = (float *)realloc(st->proc_scratch, (size_t)count * sizeof(float));
        if (!nbuf) return v_err("stem_process: oom");
        st->proc_scratch = nbuf;
        st->proc_scratch_cap = count;
    }
    tmp = st->proc_scratch;
    if (samples->t == T_FVEC) {
        for (i = 0; i < count; i++) tmp[i] = (float)samples->F[i];
    } else {
        for (i = 0; i < count; i++) {
            V *e = samples->L[i];
            tmp[i] = e->t == T_FLOAT ? (float)e->f : (e->t == T_INT ? (float)e->j : 0.f);
        }
    }
    stem_feed(st, tmp, (int)count);
    n_out = (int)count;
    return stem_dict_from_outs(st, n_out);
}

static int stem_as_mono(V *v, double **out, int64_t *n_out) {
    int64_t i;
    if (v->t == T_FVEC) {
        *n_out = v->n;
        *out = v->F;
        return 0;
    }
    if (v->t == T_LIST) {
        double *tmp = (double *)malloc((size_t)v->n * sizeof(double));
        if (!tmp) return -1;
        for (i = 0; i < v->n; i++) {
            V *e = v->L[i];
            tmp[i] = e->t == T_FLOAT ? e->f : (e->t == T_INT ? (double)e->j : 0.0);
        }
        *out = tmp;
        *n_out = v->n;
        return 1; /* caller must free */
    }
    return -1;
}

V *bi_stem_mix(V **a, int n) {
    V *d, *drums, *bass, *voc, *other, *out;
    double *pd = NULL, *pb = NULL, *pv = NULL, *po = NULL;
    int64_t nd, nb, nv, no, m, i;
    int fd = 0, fb = 0, fv = 0, fo = 0;
    if (n < 1 || a[0]->t != T_DICT) return v_err("stem_mix(dict)");
    d = a[0];
    drums = bass = voc = other = NULL;
    for (i = 0; i < d->n; i++) {
        V *k = d->keys->L[i];
        V *v = d->vals->L[i];
        if (k->t != T_STR) continue;
        if (!strcmp(k->s, "drums")) drums = v;
        else if (!strcmp(k->s, "bass")) bass = v;
        else if (!strcmp(k->s, "vocals")) voc = v;
        else if (!strcmp(k->s, "other")) other = v;
    }
    if (!drums || !bass || !voc || !other)
        return v_err("stem_mix: need drums/bass/vocals/other");
    fd = stem_as_mono(drums, &pd, &nd);
    fb = stem_as_mono(bass, &pb, &nb);
    fv = stem_as_mono(voc, &pv, &nv);
    fo = stem_as_mono(other, &po, &no);
    if (fd < 0 || fb < 0 || fv < 0 || fo < 0) {
        if (fd == 1) free(pd);
        if (fb == 1) free(pb);
        if (fv == 1) free(pv);
        if (fo == 1) free(po);
        return v_err("stem_mix: stems must be fvec or list");
    }
    m = nd;
    if (nb < m) m = nb;
    if (nv < m) m = nv;
    if (no < m) m = no;
    out = v_fvec(m);
    for (i = 0; i < m; i++)
        out->F[i] = pd[i] + pb[i] + pv[i] + po[i];
    if (fd == 1) free(pd);
    if (fb == 1) free(pb);
    if (fv == 1) free(pv);
    if (fo == 1) free(po);
    return out;
}

static V *stem_dict_lookup(V *d, const char *name) {
    int64_t i;
    for (i = 0; i < d->n; i++) {
        V *k = d->keys->L[i];
        if (k->t == T_STR && !strcmp(k->s, name)) return d->vals->L[i];
    }
    return NULL;
}

V *bi_stem_separate_file(V **a, int n) {
    const char *path;
    const char *outdir = NULL;
    float *mono = NULL;
    int n_samp = 0, sr = 44100, i, s;
    int chunk, pos;
    int delay, out_len;
    char outpath[1024];
    static const char *names[STEM_N] = {"drums", "bass", "vocals", "other"};
    float *acc[STEM_N];
    int acc_n = 0, acc_cap = 0;
    V *result, *argv[2], *open_rc;
    int was_open = g_stem.open;

    if (n < 1 || a[0]->t != T_STR) return v_err("stem_separate_file(path, [outdir])");
    path = a[0]->s;
    if (n >= 2) {
        if (a[1]->t != T_STR) return v_err("stem_separate_file: outdir must be string");
        outdir = a[1]->s;
        if (outdir && !outdir[0]) outdir = NULL;
    }
    if (stem_read_wav(path, &mono, &n_samp, &sr) != 0)
        return v_err("stem_separate_file: failed to read wav (PCM16/F32)");
    if (n_samp <= 0) {
        free(mono);
        return v_err("stem_separate_file: empty wav");
    }
    if (n_samp > STEM_ML_MAX_SAMP) {
        free(mono);
        return v_err("stem_separate_file: wav too long");
    }

    if (was_open) bi_stem_close(NULL, 0);
    argv[0] = v_int(sr);
    argv[1] = v_int(STEM_HOP);
    open_rc = bi_stem_open(argv, 2);
    v_free(argv[0]);
    v_free(argv[1]);
    if (open_rc->t == T_ERR) {
        free(mono);
        return open_rc;
    }
    v_free(open_rc);

    delay = (int)stem_latency_samples(sr);
    {
        int64_t acc_cap64 = (int64_t)n_samp + (int64_t)delay +
                            (int64_t)STEM_HPSS_LEN * STEM_HOP + STEM_NFFT + STEM_HOP;
        /* Cap like ML path; also reject values that cannot fit a float buffer. */
        if (acc_cap64 <= 0 || acc_cap64 > (int64_t)STEM_ML_MAX_SAMP * 2 ||
            (uint64_t)acc_cap64 > (uint64_t)SIZE_MAX / sizeof(float)) {
            free(mono);
            bi_stem_close(NULL, 0);
            return v_err("stem_separate_file: buffer too large");
        }
        acc_cap = (int)acc_cap64;
    }
    for (s = 0; s < STEM_N; s++) {
        acc[s] = (float *)calloc((size_t)acc_cap, sizeof(float));
        if (!acc[s]) {
            for (i = 0; i < s; i++) free(acc[i]);
            free(mono);
            bi_stem_close(NULL, 0);
            return v_err("stem_separate_file: oom");
        }
    }

    chunk = STEM_HOP * 4;
    pos = 0;
    while (pos < n_samp) {
        int take = chunk;
        V *fv, *dict, *parg[1];
        if (take > n_samp - pos) take = n_samp - pos;
        fv = v_fvec(take);
        for (i = 0; i < take; i++) fv->F[i] = mono[pos + i];
        parg[0] = fv;
        dict = bi_stem_process(parg, 1);
        v_free(fv);
        if (dict->t == T_ERR) {
            for (s = 0; s < STEM_N; s++) free(acc[s]);
            free(mono);
            bi_stem_close(NULL, 0);
            return dict;
        }
        for (s = 0; s < STEM_N; s++) {
            V *stemv = stem_dict_lookup(dict, names[s]);
            if (stemv && stemv->t == T_FVEC) {
                int64_t j;
                for (j = 0; j < stemv->n; j++) {
                    if (acc_n + (int)j >= acc_cap) break;
                    acc[s][acc_n + (int)j] = (float)stemv->F[j];
                }
            }
        }
        acc_n += take;
        if (acc_n > acc_cap) acc_n = acc_cap;
        v_free(dict);
        pos += take;
    }

    /* flush look-ahead so delayed tail lands in acc */
    {
        int flush = delay + STEM_HPSS_LEN * STEM_HOP + STEM_NFFT;
        V *fv = v_fvec(flush);
        V *dict, *parg[1];
        for (i = 0; i < flush; i++) fv->F[i] = 0.0;
        parg[0] = fv;
        dict = bi_stem_process(parg, 1);
        v_free(fv);
        if (dict->t != T_ERR) {
            for (s = 0; s < STEM_N; s++) {
                V *stemv = stem_dict_lookup(dict, names[s]);
                if (stemv && stemv->t == T_FVEC) {
                    int64_t j;
                    int base = acc_n;
                    for (j = 0; j < stemv->n && base + (int)j < acc_cap; j++)
                        acc[s][base + (int)j] = (float)stemv->F[j];
                }
            }
            acc_n += flush;
            if (acc_n > acc_cap) acc_n = acc_cap;
            v_free(dict);
        } else {
            v_free(dict);
        }
    }

    /* compensate algorithmic delay: skip leading warm-up zeros */
    out_len = n_samp;
    if (delay < 0) delay = 0;
    if (delay + out_len > acc_n) {
        out_len = acc_n > delay ? acc_n - delay : 0;
    }

    if (outdir) {
        for (s = 0; s < STEM_N; s++) {
            snprintf(outpath, sizeof outpath, "%s/%s.wav", outdir, names[s]);
            stem_write_wav(outpath, acc[s] + delay, out_len, sr);
        }
    }

    result = v_dict_empty();
    for (s = 0; s < STEM_N; s++) {
        V *fv = v_fvec(out_len);
        for (i = 0; i < out_len; i++) fv->F[i] = acc[s][delay + i];
        v_dict_set(result, names[s], fv);
        v_free(fv);
        free(acc[s]);
    }
    v_dict_put(result, "sr", v_int(sr));
    v_dict_put(result, "n", v_int(out_len));
    v_dict_put(result, "latency_ms", v_float(stem_latency_ms_calc(sr)));
    v_dict_put(result, "latency_samples", v_int(delay));
    free(mono);
    bi_stem_close(NULL, 0);
    return result;
}

/* ---- SHAKST01 offline spectrogram MLP (CPU) ---- */

static int stem_mul_size(size_t a, size_t b, size_t *out) {
    if (__builtin_mul_overflow(a, b, out)) return -1;
    return 0;
}

static void *stem_malloc_nm(size_t n, size_t elem) {
    size_t bytes;
    if (n == 0) return malloc(1);
    if (stem_mul_size(n, elem, &bytes) != 0) return NULL;
    return malloc(bytes);
}

static void *stem_calloc_nm(size_t n, size_t elem) {
    size_t bytes;
    void *p;
    if (n == 0) return calloc(1, 1);
    if (stem_mul_size(n, elem, &bytes) != 0) return NULL;
    p = malloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}

static void stem_ml_free(void) {
    free(g_ml.W1); free(g_ml.b1);
    free(g_ml.W2); free(g_ml.b2);
    free(g_ml.W3); free(g_ml.b3);
    free(g_ml.x_batch); free(g_ml.h1_batch);
    free(g_ml.h2_batch); free(g_ml.y_batch);
    memset(&g_ml, 0, sizeof g_ml);
}

/* y[r] = W[r,c] @ x[c]  (row-major W) */
static void stem_mvm(double *y, const double *W, const double *x, int r, int c) {
    int i, j;
#ifdef _OPENMP
#pragma omp parallel for private(j)
#endif
    for (i = 0; i < r; i++) {
        const double *row = W + (size_t)i * (size_t)c;
        double s = 0.0;
        for (j = 0; j < c; j++) s += row[j] * x[j];
        y[i] = s;
    }
}

/* For each batch k: y[k*r..] = W @ x[k*c..] */
static void stem_mvm_batched(double *y, const double *W, const double *x,
                             int r, int c, int batch) {
    int k;
    if (batch == 1) {
        stem_mvm(y, W, x, r, c);
        return;
    }
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (k = 0; k < batch; k++)
        stem_mvm(y + (size_t)k * (size_t)r, W, x + (size_t)k * (size_t)c, r, c);
}

static void stem_ml_relu(double *x, int n) {
    int i;
    for (i = 0; i < n; i++) if (x[i] < 0.0) x[i] = 0.0;
}

static void stem_ml_sigmoid(double *x, int n) {
    int i;
    for (i = 0; i < n; i++) x[i] = 1.0 / (1.0 + exp(-x[i]));
}

static void stem_ml_add_bias_rows(double *y, const double *b, int rows, int batch) {
    int i, k;
    for (k = 0; k < batch; k++)
        for (i = 0; i < rows; i++)
            y[(size_t)k * (size_t)rows + i] += b[i];
}

static int stem_ml_ensure_batch(int batch) {
    int H, B, Y;
    double *xb, *h1, *h2, *yb;
    if (!g_ml.loaded) return -1;
    if (batch < 1) batch = 1;
    if (batch > STEM_ML_BATCH_MAX) batch = STEM_ML_BATCH_MAX;
    if (batch <= g_ml.batch_cap) return batch;
    H = g_ml.nhidden;
    B = g_ml.nbins;
    Y = g_ml.nstems * B;
    xb = (double *)malloc((size_t)batch * (size_t)B * sizeof(double));
    h1 = (double *)malloc((size_t)batch * (size_t)H * sizeof(double));
    h2 = (double *)malloc((size_t)batch * (size_t)H * sizeof(double));
    yb = (double *)malloc((size_t)batch * (size_t)Y * sizeof(double));
    if (!xb || !h1 || !h2 || !yb) {
        free(xb); free(h1); free(h2); free(yb);
        return -1;
    }
    free(g_ml.x_batch); free(g_ml.h1_batch);
    free(g_ml.h2_batch); free(g_ml.y_batch);
    g_ml.x_batch = xb;
    g_ml.h1_batch = h1;
    g_ml.h2_batch = h2;
    g_ml.y_batch = yb;
    g_ml.batch_cap = batch;
    return batch;
}

static void stem_ml_pack_input(int row, const float *mag) {
    int b, B = g_ml.nbins;
    double *dst = g_ml.x_batch + (size_t)row * (size_t)B;
    double peak = 1e-8;
    if (g_ml.flags & STEM_ML_FLAG_LOG1P) {
        for (b = 0; b < B; b++) {
            double v = log1p((double)mag[b]);
            dst[b] = v;
            if (v > peak) peak = v;
        }
    } else {
        for (b = 0; b < B; b++) {
            double v = (double)mag[b];
            dst[b] = v;
            if (v > peak) peak = v;
        }
    }
    for (b = 0; b < B; b++) dst[b] /= peak;
}

static int stem_ml_forward_batch(int batch) {
    int H = g_ml.nhidden, B = g_ml.nbins, Y = g_ml.nstems * g_ml.nbins;
    int k, i;
    if (stem_ml_ensure_batch(batch) < 0) return -1;

    stem_mvm_batched(g_ml.h1_batch, g_ml.W1, g_ml.x_batch, H, B, batch);
    stem_ml_add_bias_rows(g_ml.h1_batch, g_ml.b1, H, batch);
    stem_ml_relu(g_ml.h1_batch, H * batch);

    stem_mvm_batched(g_ml.h2_batch, g_ml.W2, g_ml.h1_batch, H, H, batch);
    stem_ml_add_bias_rows(g_ml.h2_batch, g_ml.b2, H, batch);
    stem_ml_relu(g_ml.h2_batch, H * batch);

    stem_mvm_batched(g_ml.y_batch, g_ml.W3, g_ml.h2_batch, Y, H, batch);
    stem_ml_add_bias_rows(g_ml.y_batch, g_ml.b3, Y, batch);
    stem_ml_sigmoid(g_ml.y_batch, Y * batch);

    for (k = 0; k < batch; k++) {
        double *yk = g_ml.y_batch + (size_t)k * (size_t)Y;
        for (i = 0; i < B; i++) {
            double ssum = 1e-8;
            int s;
            for (s = 0; s < g_ml.nstems; s++) ssum += yk[s * B + i];
            for (s = 0; s < g_ml.nstems; s++) yk[s * B + i] /= ssum;
        }
    }
    return 0;
}

static int stem_ml_read_f32_as_f64(FILE *fp, double *dst, size_t n) {
    size_t i;
    float *tmp = (float *)malloc(n * sizeof(float));
    if (!tmp) return -1;
    if (fread(tmp, sizeof(float), n, fp) != n) {
        free(tmp);
        return -1;
    }
    for (i = 0; i < n; i++) dst[i] = (double)tmp[i];
    free(tmp);
    return 0;
}

static V *stem_ml_load_path(const char *path) {
    FILE *fp;
    char magic[8];
    int32_t hdr[4];
    int nbins, nhidden, nstems, flags;
    size_t nW1, nW2, nW3;

    fp = fopen(path, "rb");
    if (!fp) return v_err("stem_load_ml: cannot open file");
    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, STEM_ML_MAGIC, 8) != 0) {
        fclose(fp);
        return v_err("stem_load_ml: bad magic (want SHAKST01)");
    }
    if (fread(hdr, sizeof(int32_t), 4, fp) != 4) {
        fclose(fp);
        return v_err("stem_load_ml: bad header");
    }
    nbins = (int)hdr[0];
    nhidden = (int)hdr[1];
    nstems = (int)hdr[2];
    flags = (int)hdr[3];
    if (nbins != STEM_BINS || nstems != STEM_N || nhidden < 8 || nhidden > 2048) {
        fclose(fp);
        return v_err("stem_load_ml: unsupported dims");
    }

    stem_ml_free();
    g_ml.nbins = nbins;
    g_ml.nhidden = nhidden;
    g_ml.nstems = nstems;
    g_ml.flags = flags;
    nW1 = (size_t)nhidden * (size_t)nbins;
    nW2 = (size_t)nhidden * (size_t)nhidden;
    nW3 = (size_t)(nstems * nbins) * (size_t)nhidden;
    g_ml.W1 = (double *)malloc(nW1 * sizeof(double));
    g_ml.b1 = (double *)malloc((size_t)nhidden * sizeof(double));
    g_ml.W2 = (double *)malloc(nW2 * sizeof(double));
    g_ml.b2 = (double *)malloc((size_t)nhidden * sizeof(double));
    g_ml.W3 = (double *)malloc(nW3 * sizeof(double));
    g_ml.b3 = (double *)malloc((size_t)(nstems * nbins) * sizeof(double));
    if (!g_ml.W1 || !g_ml.b1 || !g_ml.W2 || !g_ml.b2 || !g_ml.W3 || !g_ml.b3) {
        fclose(fp);
        stem_ml_free();
        return v_err("stem_load_ml: oom");
    }
    if (stem_ml_read_f32_as_f64(fp, g_ml.W1, nW1) ||
        stem_ml_read_f32_as_f64(fp, g_ml.b1, (size_t)nhidden) ||
        stem_ml_read_f32_as_f64(fp, g_ml.W2, nW2) ||
        stem_ml_read_f32_as_f64(fp, g_ml.b2, (size_t)nhidden) ||
        stem_ml_read_f32_as_f64(fp, g_ml.W3, nW3) ||
        stem_ml_read_f32_as_f64(fp, g_ml.b3, (size_t)(nstems * nbins))) {
        fclose(fp);
        stem_ml_free();
        return v_err("stem_load_ml: truncated weights");
    }
    fclose(fp);
    strncpy(g_ml.path, path, sizeof g_ml.path - 1);
    g_ml.path[sizeof g_ml.path - 1] = 0;
    g_ml.loaded = 1;
    if (stem_ml_ensure_batch(32) < 0) {
        stem_ml_free();
        return v_err("stem_load_ml: scratch oom");
    }
    return v_nil();
}

V *bi_stem_unload_ml(V **a, int n) {
    (void)a;
    (void)n;
    stem_ml_free();
    return v_nil();
}

V *bi_stem_load_ml(V **a, int n) {
    const char *path;
    if (n < 1 || a[0]->t != T_STR || !a[0]->s || !a[0]->s[0])
        return v_err("stem_load_ml(path) — pass a SHAKST01 checkpoint");
    path = a[0]->s;
    return stem_ml_load_path(path);
}

V *bi_stem_ml_info(V **a, int n) {
    V *d;
    (void)a;
    (void)n;
    d = v_dict_empty();
    v_dict_put(d, "loaded", v_bool(g_ml.loaded));
    v_dict_put(d, "device_gpu", v_bool(0));
    v_dict_put(d, "magic", v_str(STEM_ML_MAGIC));
    v_dict_put(d, "nbins", v_int(g_ml.loaded ? g_ml.nbins : STEM_BINS));
    v_dict_put(d, "nhidden", v_int(g_ml.loaded ? g_ml.nhidden : 0));
    v_dict_put(d, "nstems", v_int(g_ml.loaded ? g_ml.nstems : STEM_N));
    v_dict_put(d, "flags", v_int(g_ml.loaded ? g_ml.flags : 0));
    v_dict_put(d, "log1p", v_bool(g_ml.loaded && (g_ml.flags & STEM_ML_FLAG_LOG1P)));
    v_dict_put(d, "n_fft", v_int(STEM_NFFT));
    v_dict_put(d, "hop", v_int(STEM_HOP));
    v_dict_put(d, "sr", v_int(STEM_ML_SR));
    if (g_ml.loaded && g_ml.path[0])
        v_dict_put(d, "path", v_str(g_ml.path));
    return d;
}

V *bi_stem_write_ml_synth(V **a, int n) {
    const char *path;
    FILE *fp;
    int32_t hdr[4];
    int H = STEM_ML_HIDDEN_DEFAULT, B = STEM_BINS, S = STEM_N;
    size_t i, nW1, nW2, nW3;
    float *buf;
    unsigned seed = 0xA11CEu;

    if (n < 1 || a[0]->t != T_STR) return v_err("stem_write_ml_synth(path, [nhidden])");
    path = a[0]->s;
    if (n >= 2) {
        if (a[1]->t == T_INT) H = (int)a[1]->j;
        else if (a[1]->t == T_FLOAT) H = (int)a[1]->f;
    }
    if (H < 8) H = 8;
    if (H > 512) H = 512;

    nW1 = (size_t)H * (size_t)B;
    nW2 = (size_t)H * (size_t)H;
    nW3 = (size_t)(S * B) * (size_t)H;
    buf = (float *)malloc((nW1 > nW3 ? nW1 : nW3) * sizeof(float));
    if (!buf) return v_err("stem_write_ml_synth: oom");

    fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        return v_err("stem_write_ml_synth: cannot write");
    }
    fwrite(STEM_ML_MAGIC, 1, 8, fp);
    hdr[0] = B; hdr[1] = H; hdr[2] = S; hdr[3] = STEM_ML_FLAG_LOG1P;
    fwrite(hdr, sizeof(int32_t), 4, fp);

#define STEM_RANDF() ((float)((seed = seed * 1664525u + 1013904223u) & 0xffff) / 65535.f * 0.02f - 0.01f)
    for (i = 0; i < nW1; i++) buf[i] = STEM_RANDF();
    fwrite(buf, sizeof(float), nW1, fp);
    for (i = 0; i < (size_t)H; i++) buf[i] = 0.f;
    fwrite(buf, sizeof(float), (size_t)H, fp);
    for (i = 0; i < nW2; i++) buf[i] = STEM_RANDF();
    fwrite(buf, sizeof(float), nW2, fp);
    for (i = 0; i < (size_t)H; i++) buf[i] = 0.f;
    fwrite(buf, sizeof(float), (size_t)H, fp);
    for (i = 0; i < nW3; i++) buf[i] = STEM_RANDF();
    fwrite(buf, sizeof(float), nW3, fp);
    for (i = 0; i < (size_t)(S * B); i++) buf[i] = 0.f;
    fwrite(buf, sizeof(float), (size_t)(S * B), fp);
#undef STEM_RANDF
    fclose(fp);
    free(buf);
    return v_nil();
}

V *bi_stem_ml_spike(V **a, int n) {
    int batch = 64, iters = 20, i, k;
    int H, B;
    double ms;
    V *d;
    if (!g_ml.loaded) return v_err("stem_ml_spike: load weights first");
    if (n >= 1) {
        if (a[0]->t == T_INT) batch = (int)a[0]->j;
        else if (a[0]->t == T_FLOAT) batch = (int)a[0]->f;
    }
    if (n >= 2) {
        if (a[1]->t == T_INT) iters = (int)a[1]->j;
        else if (a[1]->t == T_FLOAT) iters = (int)a[1]->f;
    }
    if (batch < 1) batch = 1;
    if (batch > STEM_ML_BATCH_MAX) batch = STEM_ML_BATCH_MAX;
    if (iters < 1) iters = 1;
    if (iters > STEM_ML_SPIKE_ITERS_MAX) iters = STEM_ML_SPIKE_ITERS_MAX;
    if (stem_ml_ensure_batch(batch) < 0) return v_err("stem_ml_spike: oom");
    H = g_ml.nhidden;
    B = g_ml.nbins;
    for (k = 0; k < batch; k++)
        for (i = 0; i < B; i++)
            g_ml.x_batch[k * B + i] = 0.1 + 0.001 * (double)((k + i) % 97);

    stem_mvm_batched(g_ml.h1_batch, g_ml.W1, g_ml.x_batch, H, B, batch);

#if defined(CLOCK_MONOTONIC)
    {
        struct timespec ts0, ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts0);
        for (i = 0; i < iters; i++)
            stem_mvm_batched(g_ml.h1_batch, g_ml.W1, g_ml.x_batch, H, B, batch);
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 +
             (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
    }
#else
    {
        clock_t c0 = clock();
        for (i = 0; i < iters; i++)
            stem_mvm_batched(g_ml.h1_batch, g_ml.W1, g_ml.x_batch, H, B, batch);
        ms = 1000.0 * ((double)(clock() - c0)) / (double)CLOCKS_PER_SEC;
    }
#endif
    d = v_dict_empty();
    v_dict_put(d, "batch", v_int(batch));
    v_dict_put(d, "iters", v_int(iters));
    v_dict_put(d, "rows", v_int(H));
    v_dict_put(d, "cols", v_int(B));
    v_dict_put(d, "flops_per_iter", v_float((double)H * (double)B * (double)batch));
    v_dict_put(d, "total_ms", v_float(ms));
    v_dict_put(d, "ms_per_iter", v_float(ms / (double)iters));
    v_dict_put(d, "device_gpu", v_bool(0));
    return d;
}

V *bi_stem_separate_file_ml(V **a, int n) {
    const char *path;
    const char *outdir = NULL;
    float *mono = NULL;
    int n_samp = 0, sr = 44100;
    int n_frames, f, b, s, i, pos;
    float *re_s = NULL, *im_s = NULL, *mag_s = NULL;
    float *acc[STEM_N];
    float win[STEM_NFFT];
    float frame_re[STEM_NFFT], frame_im[STEM_NFFT];
    float synth[STEM_NFFT];
    int out_len, acc_cap, batch, base, n_out;
    size_t spec_n;
    char outpath[1024];
    static const char *names[STEM_N] = {"drums", "bass", "vocals", "other"};
    V *result;

    for (s = 0; s < STEM_N; s++) acc[s] = NULL;

    if (!g_ml.loaded)
        return v_err("stem_separate_file_ml: call stem_load_ml(path) first");
    if (n < 1 || a[0]->t != T_STR) return v_err("stem_separate_file_ml(path, [outdir])");
    path = a[0]->s;
    if (n >= 2) {
        if (a[1]->t != T_STR) return v_err("stem_separate_file_ml: outdir must be string");
        outdir = a[1]->s;
        if (outdir && !outdir[0]) outdir = NULL;
    }
    if (stem_read_wav(path, &mono, &n_samp, &sr) != 0)
        return v_err("stem_separate_file_ml: failed to read wav");
    if (n_samp < STEM_NFFT) {
        free(mono);
        return v_err("stem_separate_file_ml: audio too short");
    }
    if (n_samp > STEM_ML_MAX_SAMP) {
        free(mono);
        return v_err("stem_separate_file_ml: audio too long");
    }

    /* SHAKST01 features assume STEM_ML_SR bin frequencies; refuse mismatched WAVs. */
    if (sr != STEM_ML_SR) {
        free(mono);
        return v_err("stem_separate_file_ml: sample rate must be 44100 Hz "
                     "(got mismatched rate; resample before calling)");
    }

    n_frames = 1 + (n_samp - STEM_NFFT) / STEM_HOP;
    if (n_frames < 1) {
        free(mono);
        return v_err("stem_separate_file_ml: no frames");
    }
    if (stem_mul_size((size_t)n_frames, (size_t)STEM_BINS, &spec_n) != 0) {
        free(mono);
        return v_err("stem_separate_file_ml: size overflow");
    }

    re_s = (float *)stem_malloc_nm(spec_n, sizeof(float));
    im_s = (float *)stem_malloc_nm(spec_n, sizeof(float));
    mag_s = (float *)stem_malloc_nm(spec_n, sizeof(float));
    if (!re_s || !im_s || !mag_s) {
        free(re_s); free(im_s); free(mag_s); free(mono);
        return v_err("stem_separate_file_ml: oom");
    }

    out_len = (n_frames - 1) * STEM_HOP + STEM_NFFT;
    /* Match classical separate_file: return n_samp samples (zero-pad short STFT tail). */
    n_out = n_samp;
    acc_cap = out_len > n_out ? out_len : n_out;
    if (acc_cap > INT_MAX - STEM_NFFT) {
        free(re_s); free(im_s); free(mag_s); free(mono);
        return v_err("stem_separate_file_ml: size overflow");
    }
    acc_cap += STEM_NFFT;
    for (s = 0; s < STEM_N; s++) {
        acc[s] = (float *)stem_calloc_nm((size_t)acc_cap, sizeof(float));
        if (!acc[s]) {
            for (i = 0; i < s; i++) free(acc[i]);
            free(re_s); free(im_s); free(mag_s); free(mono);
            return v_err("stem_separate_file_ml: oom");
        }
    }

    for (i = 0; i < STEM_NFFT; i++)
        win[i] = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)STEM_NFFT);
    stem_fft_init();

    for (f = 0; f < n_frames; f++) {
        pos = f * STEM_HOP;
        for (i = 0; i < STEM_NFFT; i++) {
            float x = (pos + i < n_samp) ? mono[pos + i] : 0.f;
            frame_re[i] = x * win[i];
            frame_im[i] = 0.f;
        }
        stem_fft(frame_re, frame_im, STEM_NFFT, 0);
        for (b = 0; b < STEM_BINS; b++) {
            re_s[f * STEM_BINS + b] = frame_re[b];
            im_s[f * STEM_BINS + b] = frame_im[b];
            mag_s[f * STEM_BINS + b] =
                sqrtf(frame_re[b] * frame_re[b] + frame_im[b] * frame_im[b]);
        }
    }

    for (base = 0; base < n_frames; base += STEM_ML_BATCH_MAX) {
        batch = n_frames - base;
        if (batch > STEM_ML_BATCH_MAX) batch = STEM_ML_BATCH_MAX;
        if (stem_ml_ensure_batch(batch) < 0) {
            for (s = 0; s < STEM_N; s++) free(acc[s]);
            free(re_s); free(im_s); free(mag_s); free(mono);
            return v_err("stem_separate_file_ml: batch oom");
        }
        for (f = 0; f < batch; f++)
            stem_ml_pack_input(f, mag_s + (base + f) * STEM_BINS);
        if (stem_ml_forward_batch(batch) != 0) {
            for (s = 0; s < STEM_N; s++) free(acc[s]);
            free(re_s); free(im_s); free(mag_s); free(mono);
            return v_err("stem_separate_file_ml: forward failed");
        }
        for (f = 0; f < batch; f++) {
            int fi = base + f;
            double *yk = g_ml.y_batch + (size_t)f * (size_t)(STEM_N * STEM_BINS);
            for (s = 0; s < STEM_N; s++) {
                memset(frame_re, 0, sizeof frame_re);
                memset(frame_im, 0, sizeof frame_im);
                for (b = 0; b < STEM_BINS; b++) {
                    float m = (float)yk[s * STEM_BINS + b];
                    frame_re[b] = re_s[fi * STEM_BINS + b] * m;
                    frame_im[b] = im_s[fi * STEM_BINS + b] * m;
                    if (b > 0 && b < STEM_BINS - 1) {
                        frame_re[STEM_NFFT - b] = frame_re[b];
                        frame_im[STEM_NFFT - b] = -frame_im[b];
                    }
                }
                stem_fft(frame_re, frame_im, STEM_NFFT, 1);
                for (i = 0; i < STEM_NFFT; i++)
                    synth[i] = (frame_re[i] * win[i]) / STEM_COLA;
                pos = fi * STEM_HOP;
                for (i = 0; i < STEM_NFFT; i++) {
                    if (pos + i < acc_cap)
                        acc[s][pos + i] += synth[i];
                }
            }
        }
    }

    if (outdir) {
        for (s = 0; s < STEM_N; s++) {
            snprintf(outpath, sizeof outpath, "%s/%s_ml.wav", outdir, names[s]);
            stem_write_wav(outpath, acc[s], n_out, sr);
        }
    }

    result = v_dict_empty();
    for (s = 0; s < STEM_N; s++) {
        V *fv = v_fvec(n_out);
        for (i = 0; i < n_out; i++) fv->F[i] = acc[s][i];
        v_dict_set(result, names[s], fv);
        v_free(fv);
        free(acc[s]);
    }
    v_dict_put(result, "sr", v_int(sr));
    v_dict_put(result, "n", v_int(n_out));
    v_dict_put(result, "n_frames", v_int(n_frames));
    v_dict_put(result, "backend", v_str("ml"));
    v_dict_put(result, "device_gpu", v_bool(0));
    v_dict_put(result, "nhidden", v_int(g_ml.nhidden));
    free(re_s); free(im_s); free(mag_s); free(mono);
    return result;
}
