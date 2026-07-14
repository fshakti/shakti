#ifndef SHAKTI_TALK_H
#define SHAKTI_TALK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TalkPcm {
    float *samples;
    int n_samples;
    int sample_rate;
} TalkPcm;

void talk_pcm_free(TalkPcm *pcm);

/* Capture mic audio until trailing silence (macOS when SHAKTI_HAVE_TALK). */
TalkPcm *talk_capture(float silence_sec, char *err, size_t err_cap);

/* Transcribe 16 kHz mono float32 PCM; returns malloc'd UTF-8 string. */
char *talk_transcribe(const float *samples, int n_samples, char *err, size_t err_cap);

/* Override BCP-47 locale (e.g. "en-US"). Also SHAKTI_TALK_LOCALE env var. */
int talk_set_locale(const char *locale, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
