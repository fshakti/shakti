#include "dsp.h"

#include <math.h>

static const DspRatio g_perfect7[DSP_PERFECT7_N] = {
    {1, 1},
    {9, 8},
    {5, 4},
    {3, 2},
    {15, 8},
    {3, 1},
    {3, 5},
};

const DspRatio *dsp_perfect7_table(void) { return g_perfect7; }

static int dsp_gcd(int a, int b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

void dsp_ratio_reduce(int num, int den, int *out_num, int *out_den) {
    int g;
    if (!out_num || !out_den) return;
    if (den == 0) {
        *out_num = num;
        *out_den = 1;
        return;
    }
    if (num <= 0 || den <= 0) {
        *out_num = num > 0 ? num : 1;
        *out_den = den > 0 ? den : 1;
        return;
    }
    g = dsp_gcd(num, den);
    *out_num = num / g;
    *out_den = den / g;
}

double dsp_ratio_freq(double root_hz, int num, int den) {
    int rn, rd;
    if (den <= 0 || num <= 0) return root_hz;
    dsp_ratio_reduce(num, den, &rn, &rd);
    return root_hz * (double)rn / (double)rd;
}

double dsp_ratio_cents(int num, int den) {
    int rn, rd;
    if (den <= 0 || num <= 0) return 0.0;
    dsp_ratio_reduce(num, den, &rn, &rd);
    return 1200.0 * log2((double)rn / (double)rd);
}

double dsp_et_cents(int semitone) { return (double)semitone * 100.0; }

double dsp_et_delta_cents(int num, int den) {
    double just = dsp_ratio_cents(num, den);
    double nearest = round(just / 100.0) * 100.0;
    return just - nearest;
}

static V *dsp_ratio_dict(int degree, int num, int den, double root_hz) {
    V *d = v_dict_empty();
    v_dict_put(d, "degree", v_int(degree));
    v_dict_put(d, "num", v_int(num));
    v_dict_put(d, "den", v_int(den));
    if (root_hz > 0.0)
        v_dict_put(d, "hz", v_float(dsp_ratio_freq(root_hz, num, den)));
    v_dict_put(d, "cents", v_float(dsp_ratio_cents(num, den)));
    return d;
}

V *bi_dsp_ratio_freq(V **a, int n) {
    P(n < 3 || a[0]->t != T_FLOAT || a[1]->t != T_INT || a[2]->t != T_INT,
      v_err("dsp_ratio_freq(root_hz, num, den)"))
    return v_float(dsp_ratio_freq(a[0]->f, (int)a[1]->j, (int)a[2]->j));
}

V *bi_dsp_ratio_cents(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_INT, v_err("dsp_ratio_cents(num, den)"))
    return v_float(dsp_ratio_cents((int)a[0]->j, (int)a[1]->j));
}

V *bi_dsp_ratio_reduce(V **a, int n) {
    int rn, rd;
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_INT, v_err("dsp_ratio_reduce(num, den)"))
    dsp_ratio_reduce((int)a[0]->j, (int)a[1]->j, &rn, &rd);
    V *keys = v_list(2);
    V *vals = v_list(2);
    keys->L[0] = v_str("num");
    vals->L[0] = v_int(rn);
    keys->L[1] = v_str("den");
    vals->L[1] = v_int(rd);
    V *out = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    return out;
}

V *bi_dsp_perfect7(V **a, int n) {
    double root = 0.0;
    if (n > 0) {
        P(a[0]->t != T_FLOAT && a[0]->t != T_INT, v_err("dsp_perfect7([root_hz])"))
        root = a[0]->t == T_FLOAT ? a[0]->f : (double)a[0]->j;
    }
    V *out = v_list(DSP_PERFECT7_N);
    for (int i = 0; i < DSP_PERFECT7_N; i++) {
        const DspRatio *r = &g_perfect7[i];
        out->L[i] = dsp_ratio_dict(i + 1, r->num, r->den, root);
    }
    return out;
}

V *bi_dsp_degree_freq(V **a, int n) {
    int degree;
    P(n < 2 || a[0]->t != T_FLOAT || a[1]->t != T_INT, v_err("dsp_degree_freq(root_hz, degree)"))
    degree = (int)a[1]->j;
    P(degree < 1 || degree > DSP_PERFECT7_N, v_err("dsp_degree_freq: degree 1..7"))
    {
        const DspRatio *r = &g_perfect7[degree - 1];
        return v_float(dsp_ratio_freq(a[0]->f, r->num, r->den));
    }
}

V *bi_dsp_et_cents(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("dsp_et_cents(semitone)"))
    return v_float(dsp_et_cents((int)a[0]->j));
}

V *bi_dsp_et_delta(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_INT, v_err("dsp_et_delta(num, den)"))
    return v_float(dsp_et_delta_cents((int)a[0]->j, (int)a[1]->j));
}
