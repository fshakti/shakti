#ifndef SHAKTI_DSP_H
#define SHAKTI_DSP_H

#include "shakti.h"

typedef struct {
    int num;
    int den;
} DspRatio;

#define DSP_PERFECT7_N 7

const DspRatio *dsp_perfect7_table(void);
void dsp_ratio_reduce(int num, int den, int *out_num, int *out_den);
double dsp_ratio_freq(double root_hz, int num, int den);
double dsp_ratio_cents(int num, int den);
double dsp_et_cents(int semitone);
double dsp_et_delta_cents(int num, int den);

V *bi_dsp_ratio_freq(V **a, int n);
V *bi_dsp_ratio_cents(V **a, int n);
V *bi_dsp_ratio_reduce(V **a, int n);
V *bi_dsp_perfect7(V **a, int n);
V *bi_dsp_degree_freq(V **a, int n);
V *bi_dsp_et_cents(V **a, int n);
V *bi_dsp_et_delta(V **a, int n);

#endif
