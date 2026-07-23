#ifndef SHAKTI_STEM_H
#define SHAKTI_STEM_H

#include "shakti.h"

/* Streaming 4-stem separator (drums / bass / vocals / other).
 * Classical STFT + HPSS + band masks. Algorithmic look-ahead ~64–100 ms. */

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

#endif
