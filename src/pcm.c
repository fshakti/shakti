/*
 * pcm.c — minimal raw-PCM audio output for the Shakti language.
 *
 * Exposes three builtins to .ie code:
 *   pcm_open(rate:44100, channels:1)  -> nil on success, error otherwise
 *   pcm_write(samples)                -> nil; samples is an fvec or list of
 *                                        floats in [-1, 1] (interleaved when
 *                                        channels = 2). Blocks with natural
 *                                        backpressure until the device can
 *                                        accept more audio.
 *   pcm_close()                       -> nil; drains and releases the device.
 *
 * Backends: macOS AudioQueue (Core Audio C API), Linux ALSA. The audio runs
 * on the device's own thread (AudioQueue internal thread / ALSA blocking
 * writes), never the caller's main loop.
 */
#include "shakti.h"

/* a.h installs short-name macros (in, im, it, ...) that collide with system
 * audio headers; drop them before including platform SDKs. */
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

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#define PCM_HAVE 1
#elif defined(__linux__) && defined(SHAKTI_PCM_ALSA)
/* SHAKTI_PCM_ALSA is defined by the Makefile only when it also links -lasound,
 * keeping the compiled backend and the linked library in sync. */
#include <alsa/asoundlib.h>
#define PCM_HAVE 1
#define PCM_ALSA 1
#endif

/* Channel count of the currently-open stream (0 = closed), used by the builtin
 * layer to reject malformed (non-interleaved) sample buffers before any samples
 * can be silently dropped by the backend's frame math. */
static int g_pcm_channels;

#if defined(PCM_HAVE) && defined(__APPLE__)
#define PCM_NBUF 4
#define PCM_FRAMES 2048
static AudioQueueRef g_q;
static AudioQueueBufferRef g_bufs[PCM_NBUF];
static int g_free[PCM_NBUF];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_chans;
static int g_open;

static void pcm_cb(void *user, AudioQueueRef q, AudioQueueBufferRef b) {
    (void)user;
    (void)q;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < PCM_NBUF; i++)
        if (g_bufs[i] == b) { g_free[i] = 1; break; }
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mu);
}

static int pcm_backend_open(int rate, int chans) {
    AudioStreamBasicDescription f;
    if (g_open) return -1;
    memset(&f, 0, sizeof f);
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    f.mBitsPerChannel = 16;
    f.mChannelsPerFrame = (UInt32)chans;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = (UInt32)(2 * chans);
    f.mBytesPerPacket = (UInt32)(2 * chans);
    /* NULL run loop -> AudioQueue services buffers on its own audio thread. */
    if (AudioQueueNewOutput(&f, pcm_cb, NULL, NULL, NULL, 0, &g_q) != noErr) return -1;
    for (int i = 0; i < PCM_NBUF; i++) {
        if (AudioQueueAllocateBuffer(g_q, (UInt32)(PCM_FRAMES * 2 * chans), &g_bufs[i]) != noErr) {
            AudioQueueDispose(g_q, true);
            g_q = NULL;
            return -1;
        }
        g_free[i] = 1;
    }
    g_chans = chans;
    if (AudioQueueStart(g_q, NULL) != noErr) {
        AudioQueueDispose(g_q, true);
        g_q = NULL;
        return -1;
    }
    g_open = 1;
    return 0;
}

static int pcm_backend_write(const double *samp, int64_t nsamp) {
    int chans = g_chans;
    int64_t i = 0;
    if (!g_open) return -1;
    while (i < nsamp) {
        int idx = -1;
        AudioQueueBufferRef b;
        int64_t cap_samp, take, k;
        int16_t *out;
        pthread_mutex_lock(&g_mu);
        while (idx < 0) {
            for (int j = 0; j < PCM_NBUF; j++)
                if (g_free[j]) { idx = j; break; }
            if (idx < 0) pthread_cond_wait(&g_cond, &g_mu);
        }
        g_free[idx] = 0;
        pthread_mutex_unlock(&g_mu);
        b = g_bufs[idx];
        cap_samp = (int64_t)PCM_FRAMES * chans;
        take = nsamp - i;
        if (take > cap_samp) take = cap_samp;
        out = (int16_t *)b->mAudioData;
        for (k = 0; k < take; k++) {
            double s = samp[i + k];
            if (s > 1.0) s = 1.0;
            else if (s < -1.0) s = -1.0;
            out[k] = (int16_t)lrint(s * 32767.0);
        }
        b->mAudioDataByteSize = (UInt32)(take * 2);
        if (AudioQueueEnqueueBuffer(g_q, b, 0, NULL) != noErr) {
            pthread_mutex_lock(&g_mu);
            g_free[idx] = 1;
            pthread_mutex_unlock(&g_mu);
            return -1;
        }
        i += take;
    }
    return 0;
}

static void pcm_backend_close(void) {
    if (!g_open) return;
    /* Wait for every buffer to finish playing before tearing down. */
    pthread_mutex_lock(&g_mu);
    for (;;) {
        int all = 1;
        for (int j = 0; j < PCM_NBUF; j++)
            if (!g_free[j]) { all = 0; break; }
        if (all) break;
        pthread_cond_wait(&g_cond, &g_mu);
    }
    pthread_mutex_unlock(&g_mu);
    AudioQueueStop(g_q, true);
    for (int j = 0; j < PCM_NBUF; j++) {
        if (g_bufs[j]) { AudioQueueFreeBuffer(g_q, g_bufs[j]); g_bufs[j] = NULL; }
    }
    AudioQueueDispose(g_q, true);
    g_q = NULL;
    g_open = 0;
}

#elif defined(PCM_HAVE) && defined(PCM_ALSA)
static snd_pcm_t *g_pcm;
static int g_chans;
static int g_open;

static int pcm_backend_open(int rate, int chans) {
    if (g_open) return -1;
    if (snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return -1;
    if (snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           (unsigned)chans, (unsigned)rate, 1, 200000) < 0) {
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    g_chans = chans;
    g_open = 1;
    return 0;
}

static int pcm_backend_write(const double *samp, int64_t nsamp) {
    int chans = g_chans;
    int64_t frames, off = 0, k;
    int16_t *buf;
    if (!g_open) return -1;
    frames = nsamp / chans;
    buf = (int16_t *)malloc((size_t)nsamp * sizeof(int16_t));
    if (!buf) return -1;
    for (k = 0; k < nsamp; k++) {
        double s = samp[k];
        if (s > 1.0) s = 1.0;
        else if (s < -1.0) s = -1.0;
        buf[k] = (int16_t)lrint(s * 32767.0);
    }
    while (off < frames) {
        snd_pcm_sframes_t w = snd_pcm_writei(g_pcm, buf + off * chans, (snd_pcm_uframes_t)(frames - off));
        if (w < 0) {
            w = snd_pcm_recover(g_pcm, (int)w, 1);
            if (w < 0) { free(buf); return -1; }
            continue;
        }
        off += w;
    }
    free(buf);
    return 0;
}

static void pcm_backend_close(void) {
    if (!g_open) return;
    snd_pcm_drain(g_pcm);
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
    g_open = 0;
}
#endif

V *bi_pcm_open(V **a, int n) {
    int rate = 44100, chans = 1;
    if (n >= 1) {
        if (a[0]->t == T_INT) rate = (int)a[0]->j;
        else if (a[0]->t == T_FLOAT) rate = (int)a[0]->f;
    }
    if (n >= 2) {
        if (a[1]->t == T_INT) chans = (int)a[1]->j;
        else if (a[1]->t == T_FLOAT) chans = (int)a[1]->f;
    }
    if (chans < 1) chans = 1;
    if (chans > 2) chans = 2;
    if (rate < 8000) rate = 8000;
    if (rate > 192000) rate = 192000;
#ifdef PCM_HAVE
    if (pcm_backend_open(rate, chans) != 0)
        return v_err("pcm_open: audio device init failed (already open?)");
    g_pcm_channels = chans;
    return v_nil();
#else
    (void)rate;
    (void)chans;
    return v_err("pcm_open: no audio backend on this platform");
#endif
}

V *bi_pcm_write(V **a, int n) {
    if (n < 1) return v_err("pcm_write(samples): expected fvec or list of floats");
#ifndef PCM_HAVE
    (void)a;
    return v_err("pcm_write: no audio backend on this platform");
#else
    {
        V *s = a[0];
        int rc;
        int64_t count = s->t == T_FVEC ? s->n : (s->t == T_LIST ? s->n : -1);
        if (count > 0 && g_pcm_channels > 1 && (count % g_pcm_channels) != 0)
            return v_err("pcm_write: sample count is not a multiple of the channel count (expected interleaved samples)");
        if (s->t == T_FVEC) {
            rc = pcm_backend_write(s->F, s->n);
        } else if (s->t == T_LIST) {
            int64_t m = s->n, i;
            double *tmp;
            if (m <= 0) return v_nil();
            tmp = (double *)malloc((size_t)m * sizeof(double));
            if (!tmp) return v_err("pcm_write: out of memory");
            for (i = 0; i < m; i++) {
                V *e = s->L[i];
                tmp[i] = e->t == T_FLOAT ? e->f : (e->t == T_INT ? (double)e->j : 0.0);
            }
            rc = pcm_backend_write(tmp, m);
            free(tmp);
        } else {
            return v_err("pcm_write: expected fvec or list of floats in [-1, 1]");
        }
        if (rc != 0) return v_err("pcm_write: write failed (device not open?)");
        return v_nil();
    }
#endif
}

V *bi_pcm_close(V **a, int n) {
    (void)a;
    (void)n;
#ifdef PCM_HAVE
    pcm_backend_close();
#endif
    g_pcm_channels = 0;
    return v_nil();
}
