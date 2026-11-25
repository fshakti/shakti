// SPDX-License-Identifier: Apache-2.0
#ifndef STR_OPS_H
#define STR_OPS_H

#include "shakti.h"

/* ASCII A–Z / a–z. Other bytes are copied. */
void str_case_ascii(unsigned char *dst, const unsigned char *src, int64_t n, int upper);

int str_has_prefix(const char *s, int64_t n, const char *p, int64_t pn);
int str_has_suffix(const char *s, int64_t n, const char *p, int64_t pn);

/* Whitespace split. Empty pieces are dropped. */
V *str_split_ws(const char *s, int64_t n);
/* Literal separator. Leading and trailing empties are kept.
 * max_cuts < 0 means every cut. max_cuts of 0 returns the original text as one piece. */
V *str_split_lit(const char *s, int64_t n, const char *sep, int64_t sepn, int64_t max_cuts);

/* 1 and *out set, 0 when old is absent (caller keeps the original), -1 on failure.
 * limit < 0 replaces every non-overlapping match. */
int str_replace_lit(const char *s, int64_t n, const char *old, int64_t oldn,
                    const char *neu, int64_t neun, int64_t limit, V **out);

#endif
