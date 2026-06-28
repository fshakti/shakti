#include "shakti.h"
#include "talk.h"
#include "input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) && defined(SHAKTI_HAVE_TALK)
/* a.h short-name macros collide with Apple SDK identifiers (uuid.h in, CISampler.h im, etc.) */
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
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <unistd.h>
#define TALK_SAMPLE_RATE 16000
#define TALK_FRAME_MS 20
#define TALK_FRAME_SAMPLES (TALK_SAMPLE_RATE * TALK_FRAME_MS / 1000)
#define TALK_SPEECH_RMS 0.012f
#define TALK_MAX_RECORD_SEC 60
typedef struct TalkCaptureState {
    float *samples;
    int n_samples;
    int cap_samples;
    int sample_rate;
    float silence_sec;
    int silent_frames;
    int has_speech;
    int done;
    int max_samples;
    pthread_mutex_t mu;
    AudioQueueRef queue;
    AudioStreamBasicDescription format;
} TalkCaptureState;
static char g_locale_override[64];
V(talk_log,if(getenv("SHAKTI_TALK_VERBOSE")&&msg)fputs(msg,stderr),const char*msg)
static inline float talk_frame_rms(const float*buf,int n){return n<=0?0.f:({double sum=0.;int k;for(k=0;k<n;k++)sum+=(double)buf[k]*(double)buf[k];(float)sqrt(sum/(double)n);});}
static void talk_vad_update(TalkCaptureState *st, const float *frame, int n) {
    float rms = talk_frame_rms(frame, n);
    if (rms >= TALK_SPEECH_RMS) {
        st->has_speech = 1;
        st->silent_frames = 0;
        return;
    }
    if (!st->has_speech) return;
    st->silent_frames++;
    if ((float)st->silent_frames * (float)TALK_FRAME_MS / 1000.f >= st->silence_sec)
        st->done = 1;
}
static int talk_samples_grow(TalkCaptureState *st, int need) {
    int want = st->n_samples + need;
    P(want <= st->cap_samples, 0)
    int new_cap = st->cap_samples ? st->cap_samples : 4096;
    W(new_cap < want, new_cap *= 2)
    float *p = (float *)realloc(st->samples, (size_t)new_cap * sizeof(float));
    P(!p, -1)
    st->samples = p;
    st->cap_samples = new_cap;
    return 0;
}
static void talk_capture_callback(void *user_data, AudioQueueRef queue, AudioQueueBufferRef buffer,
                                  const AudioTimeStamp *ts, UInt32 n_packets,
                                  const AudioStreamPacketDescription *desc) {
    TalkCaptureState *st = (TalkCaptureState *)user_data;
    (void)queue;
    (void)ts;
    (void)n_packets;
    (void)desc;
    if (st->done) {
        AudioQueueEnqueueBuffer(st->queue, buffer, 0, NULL);
        return;
    }
    int n_frames = (int)(buffer->mAudioDataByteSize / sizeof(float));
    float *src = (float *)buffer->mAudioData;
    pthread_mutex_lock(&st->mu);
    if (st->n_samples < st->max_samples) {
        int room = st->max_samples - st->n_samples;
        int take = n_frames < room ? n_frames : room;
        if (take > 0 && talk_samples_grow(st, take) == 0) {
            memcpy(st->samples + st->n_samples, src, (size_t)take * sizeof(float));
            st->n_samples += take;
            int offset = 0;
            while (offset + TALK_FRAME_SAMPLES <= take && !st->done) {
                talk_vad_update(st, src + offset, TALK_FRAME_SAMPLES);
                offset += TALK_FRAME_SAMPLES;
            }
        }
        if (st->n_samples >= st->max_samples) st->done = 1;
    } else {
        st->done = 1;
    }
    pthread_mutex_unlock(&st->mu);
    AudioQueueEnqueueBuffer(st->queue, buffer, 0, NULL);
}
static int talk_write_wav(const char *path, const float *samples, int n_samples, int sample_rate) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int16_t *pcm16 = (int16_t *)malloc((size_t)n_samples * sizeof(int16_t));
    if (!pcm16) {
        fclose(f);
        return -1;
    }
    int i;
    for (i = 0; i < n_samples; i++) {
        float s = samples[i];
        if (s > 1.f) s = 1.f;
        if (s < -1.f) s = -1.f;
        pcm16[i] = (int16_t)(s * 32767.f);
    }
    uint32_t data_bytes = (uint32_t)((size_t)n_samples * sizeof(int16_t));
    uint32_t riff_size = 36 + data_bytes;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t byte_rate = (uint32_t)(sample_rate * channels * (bits / 8));
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    uint16_t fmt_tag = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&fmt_tag, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    uint16_t block_align = (uint16_t)(channels * (bits / 8));
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(pcm16, sizeof(int16_t), (size_t)n_samples, f);
    free(pcm16);
    fclose(f);
    return 0;
}
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
static int talk_ensure_mic_permission(char *err, size_t err_cap) {
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusAuthorized) return 0;
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
        snprintf(err, err_cap,
                 "talk: microphone access denied (System Settings → Privacy → Microphone)");
        return -1;
    }
    __block BOOL granted = NO;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL ok) {
                               granted = ok;
                               dispatch_semaphore_signal(sem);
                             }];
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    if (!granted) {
        snprintf(err, err_cap, "talk: microphone access denied");
        return -1;
    }
    return 0;
}
static int talk_ensure_speech_permission(char *err, size_t err_cap) {
    if ([SFSpeechRecognizer authorizationStatus] == SFSpeechRecognizerAuthorizationStatusAuthorized)
        return 0;
    __block SFSpeechRecognizerAuthorizationStatus st = SFSpeechRecognizerAuthorizationStatusNotDetermined;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
      st = status;
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    if (st != SFSpeechRecognizerAuthorizationStatusAuthorized) {
        snprintf(err, err_cap,
                 "talk: speech recognition access denied (System Settings → Privacy → Speech Recognition)");
        return -1;
    }
    return 0;
}
static const char *talk_locale_id(void) {
    if (g_locale_override[0]) return g_locale_override;
    const char *env = getenv("SHAKTI_TALK_LOCALE");
    if (env && env[0]) return env;
    return "en-US";
}
static char *talk_recognize_wav(const char *wav_path, char *err, size_t err_cap) {
    NSLocale *locale = [NSLocale localeWithLocaleIdentifier:[NSString stringWithUTF8String:talk_locale_id()]];
    SFSpeechRecognizer *recognizer = [[SFSpeechRecognizer alloc] initWithLocale:locale];
    if (!recognizer || !recognizer.isAvailable) {
        snprintf(err, err_cap, "talk: speech recognizer unavailable for locale '%s'", talk_locale_id());
        return NULL;
    }
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:wav_path]];
    SFSpeechURLRecognitionRequest *request = [[SFSpeechURLRecognitionRequest alloc] initWithURL:url];
    request.shouldReportPartialResults = NO;
    __block NSString *text = nil;
    __block NSError *speech_err = nil;
    __block int done = 0;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    SFSpeechRecognitionTask *task =
        [recognizer recognitionTaskWithRequest:request
                               resultHandler:^(SFSpeechRecognitionResult *result, NSError *error) {
                                 if (done) return;
                                 if (error) {
                                     speech_err = error;
                                     done = 1;
                                     dispatch_semaphore_signal(sem);
                                     return;
                                 }
                                 if (result.isFinal) {
                                     text = result.bestTranscription.formattedString;
                                     done = 1;
                                     dispatch_semaphore_signal(sem);
                                 }
                               }];
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(120 * NSEC_PER_SEC))) != 0) {
        [task cancel];
        snprintf(err, err_cap, "talk: recognition timed out");
        return NULL;
    }
    if (speech_err) {
        const char *msg = [[speech_err localizedDescription] UTF8String];
        snprintf(err, err_cap, "talk: recognition failed: %s", msg ? msg : "unknown");
        return NULL;
    }
    if (!text || text.length == 0) {
        snprintf(err, err_cap, "talk: no speech recognized");
        return NULL;
    }
    const char *utf8 = [text UTF8String];
    size_t len = strlen(utf8);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        snprintf(err, err_cap, "talk: out of memory");
        return NULL;
    }
    memcpy(out, utf8, len + 1);
    return out;
}
void talk_pcm_free(TalkPcm *pcm) {
    if (!pcm) return;
    free(pcm->samples);
    free(pcm);
}
TalkPcm *talk_capture(float silence_sec, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = 0;
    if (talk_ensure_mic_permission(err, err_cap) != 0) return NULL;
    TalkCaptureState st;
    memset(&st, 0, sizeof st);
    st.silence_sec = silence_sec > 0.f ? silence_sec : 1.5f;
    st.sample_rate = TALK_SAMPLE_RATE;
    st.max_samples = TALK_MAX_RECORD_SEC * TALK_SAMPLE_RATE;
    pthread_mutex_init(&st.mu, NULL);
    memset(&st.format, 0, sizeof st.format);
    st.format.mSampleRate = TALK_SAMPLE_RATE;
    st.format.mFormatID = kAudioFormatLinearPCM;
    st.format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    st.format.mBitsPerChannel = 32;
    st.format.mChannelsPerFrame = 1;
    st.format.mFramesPerPacket = 1;
    st.format.mBytesPerFrame = sizeof(float);
    st.format.mBytesPerPacket = sizeof(float);
    OSStatus status = AudioQueueNewInput(&st.format, talk_capture_callback, &st, NULL, kCFRunLoopCommonModes,
                                         0, &st.queue);
    if (status != noErr) {
        pthread_mutex_destroy(&st.mu);
        snprintf(err, err_cap, "talk: AudioQueueNewInput failed (%d)", (int)status);
        return NULL;
    }
    const int n_buffers = 3;
    const UInt32 buffer_bytes = 4096 * sizeof(float);
    int i;
    for (i = 0; i < n_buffers; i++) {
        AudioQueueBufferRef buffer = NULL;
        status = AudioQueueAllocateBuffer(st.queue, buffer_bytes, &buffer);
        if (status != noErr || !buffer) {
            AudioQueueDispose(st.queue, true);
            pthread_mutex_destroy(&st.mu);
            free(st.samples);
            snprintf(err, err_cap, "talk: AudioQueueAllocateBuffer failed (%d)", (int)status);
            return NULL;
        }
        buffer->mAudioDataByteSize = buffer_bytes;
        AudioQueueEnqueueBuffer(st.queue, buffer, 0, NULL);
    }
    status = AudioQueueStart(st.queue, NULL);
    if (status != noErr) {
        AudioQueueDispose(st.queue, true);
        pthread_mutex_destroy(&st.mu);
        free(st.samples);
        snprintf(err, err_cap, "talk: AudioQueueStart failed (%d)", (int)status);
        return NULL;
    }
    talk_log("talk: listening… (Enter or click to stop)\n");
    input_hub_init();
    input_hub_take_stop();
    while (!st.done) {
        input_hub_pump(10);
        if (input_hub_take_stop()) {
            pthread_mutex_lock(&st.mu);
            st.done = 1;
            pthread_mutex_unlock(&st.mu);
            break;
        }
    }
    input_hub_shutdown();
    AudioQueueStop(st.queue, true);
    AudioQueueDispose(st.queue, true);
    pthread_mutex_destroy(&st.mu);
    if (!st.has_speech || st.n_samples <= 0) {
        free(st.samples);
        snprintf(err, err_cap, "talk: no speech detected");
        return NULL;
    }
    TalkPcm *out = (TalkPcm *)calloc(1, sizeof(TalkPcm));
    if (!out) {
        free(st.samples);
        snprintf(err, err_cap, "talk: out of memory");
        return NULL;
    }
    out->samples = st.samples;
    out->n_samples = st.n_samples;
    out->sample_rate = st.sample_rate;
    return out;
}
char *talk_transcribe(const float *samples, int n_samples, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = 0;
    if (!samples || n_samples <= 0) {
        snprintf(err, err_cap, "talk: empty audio buffer");
        return NULL;
    }
    if (talk_ensure_speech_permission(err, err_cap) != 0) return NULL;
    char path[] = "/tmp/shakti-talk-XXXXXX.wav";
    int fd = mkstemps(path, 4);
    if (fd < 0) {
        snprintf(err, err_cap, "talk: failed to create temp wav file");
        return NULL;
    }
    close(fd);
    if (talk_write_wav(path, samples, n_samples, TALK_SAMPLE_RATE) != 0) {
        unlink(path);
        snprintf(err, err_cap, "talk: failed to write temp wav file");
        return NULL;
    }
    char *text = talk_recognize_wav(path, err, err_cap);
    unlink(path);
    return text;
}
int talk_set_locale(const char *locale, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = 0;
    if (!locale || !locale[0]) {
        snprintf(err, err_cap, "talk_set_locale: empty locale");
        return -1;
    }
    if (strlen(locale) >= sizeof g_locale_override) {
        snprintf(err, err_cap, "talk_set_locale: locale too long");
        return -1;
    }
    strcpy(g_locale_override, locale);
    return 0;
}
#else
void talk_pcm_free(TalkPcm *pcm) {
    if (!pcm) return;
    free(pcm->samples);
    free(pcm);
}
TalkPcm *talk_capture(float silence_sec, char *err, size_t err_cap) {
    (void)silence_sec;
    if (err && err_cap)
        snprintf(err, err_cap, "talk: macOS desktop only (build with SHAKTI_TALK=1 on Darwin)");
    return NULL;
}
char *talk_transcribe(const float *samples, int n_samples, char *err, size_t err_cap) {
    (void)samples;
    (void)n_samples;
    if (err && err_cap)
        snprintf(err, err_cap, "talk: macOS desktop only (build with SHAKTI_TALK=1 on Darwin)");
    return NULL;
}
int talk_set_locale(const char *locale, char *err, size_t err_cap) {
    (void)locale;
    if (err && err_cap)
        snprintf(err, err_cap, "talk: macOS desktop only (build with SHAKTI_TALK=1 on Darwin)");
    return -1;
}
#endif
static inline double talk_arg_float(V**a,int n,int idx,double fallback){return n<=idx?fallback:a[idx]->t==T_FLOAT?a[idx]->f:a[idx]->t==T_INT?(double)a[idx]->j:fallback;}
V *bi_talk_listen(V **a, int n) {
    float silence = (float)talk_arg_float(a, n, 0, 1.5f);
    if (silence <= 0.f) silence = 1.5f;
    char err[512];
    err[0] = 0;
    TalkPcm *pcm = talk_capture(silence, err, sizeof err);
    if (!pcm) {
        if (err[0]) return v_err(err);
        return v_err("talk_listen: capture failed");
    }
    char *text = talk_transcribe(pcm->samples, pcm->n_samples, err, sizeof err);
    talk_pcm_free(pcm);
    if (!text) {
        if (err[0]) return v_err(err);
        return v_err("talk_listen: transcription failed");
    }
    V *out = v_str(text);
    free(text);
    return out;
}
V *bi_talk_set_locale(V **a, in) {
    P(n<1||a[0]->t!=T_STR,v_err("talk_set_locale(locale)"))
    char err[512];
    err[0] = 0;
    if (talk_set_locale(a[0]->s, err, sizeof err) != 0) {
        if (err[0]) return v_err(err);
        return v_err("talk_set_locale failed");
    }
    return v_nil();
}
V *bi_talk_set_model(V **a, in) {
    return bi_talk_set_locale(a, n);
}
