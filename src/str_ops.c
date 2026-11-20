// SPDX-License-Identifier: Apache-2.0
#include "str_ops.h"
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static const char *str_find(const char *s, int64_t n, const char *sep, int64_t sepn) {
    if (n < sepn || sepn <= 0 || !s || !sep) return NULL;
    if (sepn == 1) return memchr(s, (unsigned char)sep[0], (size_t)n);
    return memmem(s, (size_t)n, sep, (size_t)sepn);
}

void str_case_ascii(unsigned char *dst, const unsigned char *src, int64_t n, int upper) {
    int64_t i = 0;
    unsigned char lo = upper ? (unsigned char)'a' : (unsigned char)'A';
    unsigned char hi = upper ? (unsigned char)'z' : (unsigned char)'Z';
    int delta = upper ? -32 : 32;
    if (!dst || n <= 0 || !src) return;
#if defined(__aarch64__)
    if (n >= 16) {
        uint8x16_t vlo = vdupq_n_u8(lo);
        uint8x16_t vhi = vdupq_n_u8(hi);
        uint8x16_t vd = vdupq_n_u8((uint8_t)delta);
        for (; i + 16 <= n; i += 16) {
            uint8x16_t c = vld1q_u8(src + i);
            uint8x16_t m = vandq_u8(vcgeq_u8(c, vlo), vcleq_u8(c, vhi));
            vst1q_u8(dst + i, vaddq_u8(c, vandq_u8(m, vd)));
        }
    }
#endif
    for (; i < n; i++) {
        unsigned char c = src[i];
        if (c >= lo && c <= hi) c = (unsigned char)(c + delta);
        dst[i] = c;
    }
}

int str_has_prefix(const char *s, int64_t n, const char *p, int64_t pn) {
    if (pn <= 0) return 1;
    if (pn > n || !s || !p) return 0;
    return memcmp(s, p, (size_t)pn) == 0;
}

int str_has_suffix(const char *s, int64_t n, const char *p, int64_t pn) {
    if (pn <= 0) return 1;
    if (pn > n || !s || !p) return 0;
    return memcmp(s + (n - pn), p, (size_t)pn) == 0;
}

static int str_is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static V *str_take_n(const char *s, int64_t n) {
    char *p;
    if (n < 0) return NULL;
    p = malloc((size_t)n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, (size_t)n);
    p[n] = 0;
    return v_str_take(p);
}

V *str_split_ws(const char *s, int64_t n) {
    int64_t i = 0, count = 0, k = 0;
    V *list;
    if (!s) s = "";
    if (n < 0) n = 0;
    while (i < n) {
        while (i < n && str_is_ws((unsigned char)s[i])) i++;
        if (i >= n) break;
        count++;
        while (i < n && !str_is_ws((unsigned char)s[i])) i++;
    }
    list = v_list(count);
    i = 0;
    while (k < count) {
        int64_t start;
        V *piece;
        while (i < n && str_is_ws((unsigned char)s[i])) i++;
        start = i;
        while (i < n && !str_is_ws((unsigned char)s[i])) i++;
        piece = str_take_n(s + start, i - start);
        if (!piece) { v_free(list); return NULL; }
        list->L[k++] = piece;
    }
    return list;
}

static int64_t str_cut_count(const char *s, int64_t n, const char *sep, int64_t sepn, int64_t max_cuts) {
    int64_t pos = 0, cuts = 0;
    if (max_cuts == 0) return 0;
    while (pos <= n) {
        const char *hit;
        if (max_cuts > 0 && cuts >= max_cuts) break;
        hit = str_find(s + pos, n - pos, sep, sepn);
        if (!hit) break;
        cuts++;
        pos = (int64_t)(hit - s) + sepn;
    }
    return cuts;
}

V *str_split_lit(const char *s, int64_t n, const char *sep, int64_t sepn, int64_t max_cuts) {
    int64_t cuts, pos = 0, k = 0;
    V *list;
    if (!s) s = "";
    if (n < 0) n = 0;
    if (sepn <= 0 || !sep) return NULL;
    cuts = str_cut_count(s, n, sep, sepn, max_cuts);
    list = v_list(cuts + 1);
    while (k < cuts) {
        const char *hit = str_find(s + pos, n - pos, sep, sepn);
        int64_t at = hit ? (int64_t)(hit - s) : n;
        V *piece = str_take_n(s + pos, at - pos);
        if (!piece) { v_free(list); return NULL; }
        list->L[k++] = piece;
        pos = at + sepn;
    }
    {
        V *tail = str_take_n(s + pos, n - pos);
        if (!tail) { v_free(list); return NULL; }
        list->L[k] = tail;
    }
    return list;
}

int str_replace_lit(const char *s, int64_t n, const char *old, int64_t oldn,
                    const char *neu, int64_t neun, int64_t limit, V **out) {
    int64_t pos = 0, matches = 0, extra, outn, w = 0;
    char *buf;
    if (!out) return -1;
    *out = NULL;
    if (!s) s = "";
    if (n < 0) n = 0;
    if (oldn <= 0 || !old) return -1;
    if (!neu) { neu = ""; neun = 0; }
    while (limit < 0 || matches < limit) {
        const char *hit = str_find(s + pos, n - pos, old, oldn);
        if (!hit) break;
        matches++;
        pos = (int64_t)(hit - s) + oldn;
    }
    if (matches == 0) return 0;
    extra = neun - oldn;
    if (extra > 0 && matches > (INT64_MAX - n) / extra) return -1;
    outn = n + matches * extra;
    if (outn < 0) return -1;
    buf = malloc((size_t)outn + 1);
    if (!buf) return -1;
    pos = 0;
    matches = 0;
    while (limit < 0 || matches < limit) {
        const char *hit = str_find(s + pos, n - pos, old, oldn);
        int64_t at, gap;
        if (!hit) break;
        at = (int64_t)(hit - s);
        gap = at - pos;
        if (gap) memcpy(buf + w, s + pos, (size_t)gap);
        w += gap;
        if (neun) memcpy(buf + w, neu, (size_t)neun);
        w += neun;
        pos = at + oldn;
        matches++;
    }
    if (n > pos) memcpy(buf + w, s + pos, (size_t)(n - pos));
    buf[outn] = 0;
    *out = v_str_take(buf);
    return 1;
}
