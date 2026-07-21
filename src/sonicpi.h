#ifndef SHAKTI_SONICPI_H
#define SHAKTI_SONICPI_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SONICPI_MAX_ARGS 16
#define SONICPI_MAX_MSG 1024

V *bi_sonicpi_configure(V **a, int n);
V *bi_sonicpi_send(V **a, int n);
V *bi_sonicpi_play(V **a, int n);
V *bi_sonicpi_synth(V **a, int n);
V *bi_sonicpi_stop(V **a, int n);
V *bi_sonicpi_bpm(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif
