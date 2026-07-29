/*
 * stem.c — streaming 4-stem separator for Shakti (drums / bass / vocals / other).
 *
 * Pipeline: ring-fed STFT → median HPSS → band soft-masks → ISTFT overlap-add.
 * Algorithmic look-ahead is STEM_HPSS_HALF hops (~64–100 ms at typical rates).
 */
#include "stem.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* last process latency sample (wall not available here; algorithmic only) */
} StemState;

static StemState g_stem;

static float stem_hz_to_bin(float hz, int sr) {
    float b = hz * (float)STEM_NFFT / (float)sr;
    if (b < 0.f) b = 0.f;
    if (b > (float)(STEM_BINS - 1)) b = (float)(STEM_BINS - 1);
    return b;
}

static void stem_fft(float *re, float *im, int n, int inverse) {
    /* in-place radix-2 Cooley–Tukey */
    int i, j, k, m;
    float tr, ti, ur, ui, wr, wi, theta, tmp;
    j = 0;
    for (i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        theta = (inverse ? 2.0f : -2.0f) * (float)M_PI / (float)m;
        wr = cosf(theta);
        wi = sinf(theta);
        for (i = 0; i < n; i += m) {
            ur = 1.f;
            ui = 0.f;
            for (k = 0; k < m / 2; k++) {
                int ia0 = i + k, ia1 = i + k + m / 2;
                tr = ur * re[ia1] - ui * im[ia1];
                ti = ur * im[ia1] + ui * re[ia1];
                re[ia1] = re[ia0] - tr;
                im[ia1] = im[ia0] - ti;
                re[ia0] += tr;
                im[ia0] += ti;
                tmp = ur * wr - ui * wi;
                ui = ur * wi + ui * wr;
                ur = tmp;
            }
        }
    }
    if (inverse) {
        float s = 1.f / (float)n;
        for (i = 0; i < n; i++) {
            re[i] *= s;
            im[i] *= s;
        }
    }
}

static float stem_median_copy(float *tmp, int n) {
    /* insertion sort small n */
    int i, j;
    for (i = 1; i < n; i++) {
        float v = tmp[i];
        for (j = i; j > 0 && tmp[j - 1] > v; j--) tmp[j] = tmp[j - 1];
        tmp[j] = v;
    }
    return tmp[n / 2];
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
    int i;
    /* STEM_NFFT (1024) is always <= STEM_OLA_CAP (8192); loop bound is the real guard. */
    for (i = 0; i < STEM_NFFT; i++)
        st->ola[stem][i] += frame[i];
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

    for (b = 0; b < STEM_BINS; b++) {
        mag[b] = sqrtf(st->re[b] * st->re[b] + st->im[b] * st->im[b]);
        phase_re[b] = st->re[b];
        phase_im[b] = st->im[b];
    }

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

/* ---- WAV helpers (PCM16 mono/stereo LE) ---- */
static int stem_read_wav(const char *path, float **out, int *n_out, int *sr_out) {
    FILE *fp = fopen(path, "rb");
    unsigned char hdr[44];
    unsigned int sr, data_bytes;
    unsigned short ch, bps, fmt;
    int nframes, i, j;
    int16_t *pcm;
    if (!fp) return -1;
    if (fread(hdr, 1, 44, fp) != 44) { fclose(fp); return -1; }
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fclose(fp); return -1; }
    /* naive: assume standard 44-byte PCM header */
    fmt = (unsigned short)(hdr[20] | (hdr[21] << 8));
    ch = (unsigned short)(hdr[22] | (hdr[23] << 8));
    sr = (unsigned int)(hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24));
    bps = (unsigned short)(hdr[34] | (hdr[35] << 8));
    data_bytes = (unsigned int)(hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | (hdr[43] << 24));
    if (fmt != 1 || (bps != 16 && bps != 32) || ch < 1 || ch > 2) { fclose(fp); return -1; }
    if (bps == 16) {
        nframes = (int)(data_bytes / (2 * ch));
        pcm = (int16_t *)malloc((size_t)nframes * ch * sizeof(int16_t));
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
        /* float32 */
        float *fpcm;
        nframes = (int)(data_bytes / (4 * ch));
        fpcm = (float *)malloc((size_t)nframes * ch * sizeof(float));
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
    fwrite(hdr, 1, 44, fp);
    for (i = 0; i < n; i++) {
        float v = x[i];
        int s;
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        s = (int)lrintf(v * 32767.f);
        fputc(s & 255, fp);
        fputc((s >> 8) & 255, fp);
    }
    fclose(fp);
    return 0;
}

/* ---- builtins ---- */

V *bi_stem_open(V **a, int n) {
    int sr = 44100, block = STEM_HOP;
    int i;
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
    memset(&g_stem, 0, sizeof g_stem);
    g_stem.sr = sr;
    g_stem.block = block;
    for (i = 0; i < STEM_N; i++) g_stem.gains[i] = 1.f;
    for (i = 0; i < STEM_NFFT; i++)
        g_stem.win[i] = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)STEM_NFFT);
    g_stem.open = 1;
    return v_nil();
}

V *bi_stem_close(V **a, int n) {
    (void)a;
    (void)n;
    memset(&g_stem, 0, sizeof g_stem);
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
    tmp = (float *)malloc((size_t)count * sizeof(float));
    if (!tmp) return v_err("stem_process: oom");
    if (samples->t == T_FVEC) {
        for (i = 0; i < count; i++) tmp[i] = (float)samples->F[i];
    } else {
        for (i = 0; i < count; i++) {
            V *e = samples->L[i];
            tmp[i] = e->t == T_FLOAT ? (float)e->f : (e->t == T_INT ? (float)e->j : 0.f);
        }
    }
    stem_feed(st, tmp, (int)count);
    free(tmp);
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
    acc_cap = n_samp + delay + STEM_HPSS_LEN * STEM_HOP + STEM_NFFT + STEM_HOP;
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
