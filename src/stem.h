#ifndef SHAKTI_STEM_H
#define SHAKTI_STEM_H

#include "shakti.h"

/* Streaming 4-stem separator (drums / bass / vocals / other).
 * Classical STFT + HPSS + band masks. Algorithmic look-ahead ~64–100 ms.
 * Offline ML: SHAKST01 spectrogram MLP (CPU-only).
 * WAV I/O: read PCM16/24/32-int / IEEE f32; write pcm16/pcm24/f32. Engine is f32. */

V *bi_stem_open(V **a, int n);
V *bi_stem_close(V **a, int n);
V *bi_stem_alive(V **a, int n);
V *bi_stem_process(V **a, int n);
V *bi_stem_set_gains(V **a, int n);
V *bi_stem_gains(V **a, int n);
V *bi_stem_latency_ms(V **a, int n);
V *bi_stem_mix(V **a, int n);
V *bi_stem_separate_file(V **a, int n);
V *bi_stem_info(V **a, int n);

V *bi_stem_load_ml(V **a, int n);
V *bi_stem_unload_ml(V **a, int n);
V *bi_stem_ml_info(V **a, int n);
V *bi_stem_write_ml_synth(V **a, int n);
V *bi_stem_ml_spike(V **a, int n);
V *bi_stem_separate_file_ml(V **a, int n);

V *bi_stem_si_sdr(V **a, int n);
V *bi_stem_write_wav(V **a, int n);
V *bi_stem_read_wav(V **a, int n);

#endif
