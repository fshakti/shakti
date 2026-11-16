// SPDX-License-Identifier: Apache-2.0
#include "shakti_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern V *table_xml_load(const char *path, V *columns_opt);

static V *table_tsv_load(const char *path, V *columns_opt);
static V *table_csv_load(const char *path, V *columns_opt);
static int table_csv_save(V *table, const char *path);
static int table_tsv_save(V *table, const char *path);

static int ends_with(const char *s, const char *t) {
    size_t ls = strlen(s), lu = strlen(t);
    return ls >= lu && strcmp(s + ls - lu, t) == 0;
}

V *table_load(const char *path, V *columns_opt) {
    P(!path, v_err("load: path"))
    {
        char err[512];
        if (shakti_fs_check(path, 0, err, sizeof err) != 0) return v_errf("load: %s", err);
    }
    if (ends_with(path, ".csv"))
        return table_csv_load(path, columns_opt);
    if (ends_with(path, ".xml"))
        return table_xml_load(path, columns_opt);
    if (ends_with(path, ".tsv"))
        return table_tsv_load(path, columns_opt);
    return v_err("load: supported formats are .csv, .xml and .tsv");
}

int table_save(V *table, const char *path) {
    P(!table || !path, -1)
    if (shakti_fs_check(path, 1, NULL, 0) != 0) return -1;
    if (ends_with(path, ".csv"))
        return table_csv_save(table, path);
    if (ends_with(path, ".tsv"))
        return table_tsv_save(table, path);
    return -1;
}

/* Upper bound on a single CSV/TSV file read fully into memory.
 * Override with SHAKTI_CSV_MAX_BYTES. */
static unsigned long csv_max_file_bytes(void) {
    const char *env = getenv("SHAKTI_CSV_MAX_BYTES");
    if (env && env[0]) {
        char *end = NULL;
        unsigned long v = strtoul(env, &end, 10);
        if (end != env && v > 0)
            return v;
    }
    return 1024ul * 1024ul * 1024ul;
}
/* The whole file, or NULL when it can't be read in one go: it can't be opened, its size isn't known
   (a pipe), it is too big for one read, or there is no memory for it (the caller streams it then).
   *bad is set when reading it fails or reads less than its size. */
static char *read_all(const char *s, size_t *len, int *bad) {
    FILE *f = fopen(s, "rb");
    P(!f, NULL)
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (z < 0 || (unsigned long)z > csv_max_file_bytes()) {
        fclose(f);
        return NULL;
    }
    char *b = malloc((size_t)z + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(b, 1, (size_t)z, f);
    if (got != (size_t)z || ferror(f)) {
        free(b);
        fclose(f);
        *bad = 1;
        return NULL;
    }
    b[got] = 0;
    *len = got;
    fclose(f);
    return b;
}

/* Padding around a field: spaces, and tabs unless the tab is the delimiter (TSV tabs are fields).
   At the end of a field a \r or \n is padding too. */
static int is_pad(char ch, char delim) { return ch == ' ' || (ch == '\t' && delim != '\t'); }
static int is_end_pad(char ch, char delim) { return is_pad(ch, delim) || ch == '\r' || ch == '\n'; }

/* Trim padding whitespace; never trim `delim` (TSV tabs are fields). */
static void strip_pad(char *s, char delim) {
    char *a = s;
    W(is_pad(*a, delim), a++)
    if (a != s)
        memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    W(n && is_end_pad(s[n - 1], delim), s[--n] = 0)
}

static const char *delim_skip_field(const char *p, char c, int *ate_delim) {
    *ate_delim = 0;
    if (*p == '"') {
        p++;
        while (*p) {
            if (*p == '"') {
                if (p[1] == '"') { p += 2; continue; }
                p++;
                break;
            }
            p++;
        }
    } else {
        while (*p && *p != c) p++;
    }
    if (*p == c) { p++; *ate_delim = 1; }
    return p;
}
/* Trim the padding of a quoted field: spaces, and tabs unless the tab is the delimiter, as strip_pad
   does, but not a \r or \n at the end: a line break inside the quotes is part of the field. */
static void strip_quoted_pad(char *s, char delim) {
    char *a = s;
    W(*a == ' ' || (*a == '\t' && delim != '\t'), a++)
    if (a != s)
        memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    W(n && (s[n - 1] == ' ' || (s[n - 1] == '\t' && delim != '\t')), s[--n] = 0)
}
static int split_delim_line(char *line, char **out, int max, char c) {
    int n = 0;
    char *p = line;
    while (n < max) {
        int ate = 0;
        if (*p == '"') {
            char *w = p;
            char *r = p + 1;
            out[n++] = w;
            while (*r) {
                if (*r == '"') {
                    if (r[1] == '"') { *w++ = '"'; r += 2; continue; }
                    r++;
                    break;
                }
                *w++ = *r++;
            }
            *w = 0;
            if (*r == c) { r++; ate = 1; }
            strip_quoted_pad(out[n - 1], c);
            p = r;
        } else {
            char *start = p;
            while (*p && *p != c) p++;
            if (*p == c) { *p++ = 0; ate = 1; }
            out[n++] = start;
            strip_pad(start, c);
        }
        if (!ate) break;
    }
    return n;
}

/* Where the record at p ends: its first newline outside a quoted field, or end. A field is quoted
   when it starts with '"' (the record's first field once its padding is off), as split_delim_line
   reads it. *quoted says whether p is inside a quoted field already, and is left saying whether the
   text ends inside one. The text runs to end: inside quotes a NUL is a byte like any other here,
   but outside them it ends the record's text, so the record runs to the next newline. */
static char *record_end(char *p, char *end, char c, int *quoted) {
    if (!*quoted) {
        char *e = memchr(p, '\n', (size_t)(end - p));
        if (!e) e = end;
        if (!memchr(p, '"', (size_t)(e - p))) return e; /* no quote in the line: the usual case */
        W(*p == ' ' || (*p == '\t' && c != '\t'), p++)
    }
    for (;;) {
        if (!*quoted && p < end && *p == '"') { *quoted = 1; p++; }
        if (*quoted) {
            char *q;
            while ((q = memchr(p, '"', (size_t)(end - p))) && q + 1 < end && q[1] == '"')
                p = q + 2;
            if (!q) return end;
            *quoted = 0;
            p = q + 1;
        }
        while (p < end && *p != c && *p != '\n' && *p) p++;
        if (p < end && !*p) {
            char *e = memchr(p, '\n', (size_t)(end - p));
            return e ? e : end;
        }
        if (p == end || *p != c) return p;
        p++;
    }
}
/* What get_record returns when there is no record. */
enum { REC_EOF = -1, REC_UNCLOSED = -2, REC_NOMEM = -3, REC_READ_ERR = -4 };
static V *record_error(ssize_t got, const char *fmt, const char *path) {
    if (got == REC_NOMEM) return v_errf("%s: out of memory", fmt);
    if (got == REC_UNCLOSED) return v_errf("%s: quoted field not closed", fmt);
    if (got == REC_READ_ERR) return v_errf("%s: cannot read '%s'", fmt, path);
    return v_errf("%s: need header + rows", fmt);
}
/* Why getline failed: the end of the file, or no memory for the line, or a read error. */
static int line_error(FILE *f) {
    return feof(f) ? REC_EOF : errno == ENOMEM ? REC_NOMEM : REC_READ_ERR;
}
/* getline for a record: while a quoted field is open at the end of a line, the next line is part
   of it. Returns the record's length, or REC_EOF at the end of the file, REC_UNCLOSED when the file
   ends inside a quoted field, REC_NOMEM when there is no memory to hold the record, REC_READ_ERR
   when reading it fails. With bom, a byte order mark at the start is skipped when looking for quotes
   (the caller takes it off). */
static ssize_t get_record(char **line, size_t *cap, FILE *f, char c, int bom) {
    errno = 0;
    ssize_t got = getline(line, cap, f);
    char *more = NULL;
    size_t more_cap = 0, from = 0;
    int quoted = 0;
    if (got < 0) return line_error(f);
    if (!memchr(*line, '"', (size_t)got)) return got; /* no quote in the line: the usual case */
    if (bom && got >= 3 && (unsigned char)(*line)[0] == 0xef && (unsigned char)(*line)[1] == 0xbb &&
        (unsigned char)(*line)[2] == 0xbf)
        from = 3;
    for (;;) {
        record_end(*line + from, *line + got, c, &quoted);
        if (!quoted) break;
        errno = 0;
        ssize_t n = getline(&more, &more_cap, f);
        if (n < 0) {
            int err = line_error(f);
            free(more);
            return err == REC_EOF ? REC_UNCLOSED : err;
        }
        if ((size_t)got + (size_t)n + 1 > *cap) {
            size_t want = (size_t)got + (size_t)n + 1;
            if (want < *cap * 2) want = *cap * 2;
            char *grown = realloc(*line, want);
            if (!grown) { free(more); return REC_NOMEM; }
            *line = grown;
            *cap = want;
        }
        memcpy(*line + got, more, (size_t)n + 1);
        from = (size_t)got;
        got += n;
    }
    free(more);
    return got;
}

static int field_needs_quote(const char *s, char delim) {
    if (!s) return 0;
    for (; *s; s++)
        if (*s == delim || *s == '"' || *s == '\n' || *s == '\r') return 1;
    return 0;
}
static int fputs_field(FILE *f, const char *s, char delim) {
    if (!s) s = "";
    if (!field_needs_quote(s, delim)) return fputs(s, f) == EOF ? -1 : 0;
    if (fputc('"', f) == EOF) return -1;
    for (; *s; s++) {
        if (*s == '"' && fputc('"', f) == EOF) return -1;
        if (fputc(*s, f) == EOF) return -1;
    }
    return fputc('"', f) == EOF ? -1 : 0;
}
/* A float as saved: rounded to 15 significant digits, or to 16 or 17 when fewer don't read back as
   the same float (a whole float below 1e15 in full), always with a '.' or an exponent so that load
   reads its column back as floats; inf and nan as strtod reads them. */
static void save_float_format(double d, char *buf, size_t cap) {
    if (!isfinite(d)) { snprintf(buf, cap, "%g", nan_unsigned(d)); return; }
    if (d < 1e15 && d > -1e15 && d == (int64_t)d) { snprintf(buf, cap, "%.1f", d); return; }
    /* Below DBL_MIN (subnormal) fewer than 15 digits may be all there are. */
    for (int prec = fabs(d) < DBL_MIN ? 1 : 15; prec <= 17; prec++) {
        snprintf(buf, cap, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) break;
    }
    if (!strpbrk(buf, ".e")) {
        size_t l = strlen(buf);
        if (l + 2 < cap) memcpy(buf + l, ".0", 3);
    }
}
static int table_delim_save(V *table, const char *path, char c) {
    P(!table || table->t != T_TABLE || !table->keys || !table->vals, -1)
    int nc = (int)table->keys->n;
    int64_t nrows = table->n;
    char tmp[4096];
    unsigned rnd = 0;
    int rfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (rfd >= 0) {
        if (read(rfd, &rnd, sizeof rnd) != (ssize_t)sizeof rnd) rnd = (unsigned)getpid();
        close(rfd);
    } else {
        rnd = (unsigned)getpid();
    }
    if (snprintf(tmp, sizeof tmp, "%s.part-%u", path, rnd) >= (int)sizeof tmp) return -1;
    int fd = open(tmp, O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); return -1; }
    for (int j = 0; j < nc; j++) {
        if (j && fputc(c, f) == EOF) goto fail;
        V *cn = table->keys->L[j];
        const char *nm = (cn && cn->t == T_STR) ? cn->s : "col";
        if (fputs_field(f, nm, c) != 0) goto fail;
    }
    if (fputc('\n', f) == EOF) goto fail;
    for (int64_t r = 0; r < nrows; r++) {
        for (int j = 0; j < nc; j++) {
            if (j && fputc(c, f) == EOF) goto fail;
            V *col = table->vals->L[j];
            char buf[128];
            const char *out = "";
            if (!col || r >= col->n) goto fail;
            if (col->t == T_FVEC) {
                save_float_format(col->F[r], buf, sizeof buf);
                out = buf;
            } else if (col->t == T_IVEC) {
                snprintf(buf, sizeof buf, "%lld", (long long)col->J[r]);
                out = buf;
            } else if (col->t == T_BVEC) {
                out = col->B[r] ? "true" : "false";
            } else if (col->t == T_CVEC) {
                snprintf(buf, sizeof buf, "%u", (unsigned)col->B[r]);
                out = buf;
            } else if (col->t == T_LIST) {
                V *cell = col->L[r];
                if (cell && cell->t == T_STR) out = cell->s ? cell->s : "";
                else if (cell && cell->t == T_INT) {
                    snprintf(buf, sizeof buf, "%lld", (long long)cell->j);
                    out = buf;
                } else if (cell && cell->t == T_FLOAT) {
                    save_float_format(cell->f, buf, sizeof buf);
                    out = buf;
                }
            } else goto fail;
            if (fputs_field(f, out, c) != 0) goto fail;
        }
        if (fputc('\n', f) == EOF) goto fail;
    }
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
fail:
    fclose(f);
    unlink(tmp);
    return -1;
}

static int table_csv_save(V *table, const char *path) {
    return table_delim_save(table, path, ',');
}

static int table_tsv_save(V *table, const char *path) {
    return table_delim_save(table, path, '\t');
}

static int count_delim_fields(const char *line, char c) {
    int n = 0;
    const char *p = line ? line : "";
    if (!*p) return 0;
    for (;;) {
        int ate = 0;
        n++;
        p = delim_skip_field(p, c, &ate);
        if (!ate) break;
    }
    return n;
}

static V *table_keep_columns(V *t, V *columns_opt) {
    V *kl, *dl;
    int64_t i, n;
    if (!columns_opt || columns_opt->t == T_NIL) return t;
    if (columns_opt->t != T_LIST) { v_free(t); return v_err("load: columns must be a list of names"); }
    n = columns_opt->n;
    kl = v_list(n);
    dl = v_list(n);
    for (i = 0; i < n; i++) {
        V *name = columns_opt->L[i];
        int found = 0;
        int64_t c;
        if (!name || name->t != T_STR) {
            v_free(kl); v_free(dl); v_free(t);
            return v_err("load: column name must be a string");
        }
        for (c = 0; c < t->keys->n; c++) {
            if (t->keys->L[c] && t->keys->L[c]->t == T_STR && !strcmp(t->keys->L[c]->s, name->s)) {
                kl->L[i] = v_ref(t->keys->L[c]);
                dl->L[i] = v_ref(t->vals->L[c]);
                found = 1;
                break;
            }
        }
        if (!found) {
            v_free(kl); v_free(dl); v_free(t);
            return v_errf("load: unknown column '%s'", name->s);
        }
    }
    {
        V *out = v_table(kl, dl);
        v_free(kl); v_free(dl); v_free(t);
        return out;
    }
}
/* Column kinds inferred from the fields of a delimited file. */
enum { KIND_INT = 0, KIND_FLOAT = 1, KIND_STR = 2 };

/* A parsed header plus, per output column, where it comes from in the file and what kind it
 * inferred. Output columns are the file's columns, or the ones `columns_opt` asks for. */
typedef struct {
    int nh;
    int n_out;
    char **hdr;
    int *src;
    int *out_of;
    int *kind;
    int *seen;
} DelimPlan;

static void delim_plan_free(DelimPlan *p) {
    if (!p) return;
    if (p->hdr) {
        for (int i = 0; i < p->nh; i++) free(p->hdr[i]);
        free(p->hdr);
    }
    free(p->src);
    free(p->out_of);
    free(p->kind);
    free(p->seen);
    memset(p, 0, sizeof *p);
}

/* Build the plan from a header record (split in place; the names are copied). On failure
 * returns an error value and leaves the plan freed. */
static V *delim_plan_init(DelimPlan *p, char *header, char delim, V *columns_opt, const char *fmt) {
    memset(p, 0, sizeof *p);
    if (columns_opt && columns_opt->t != T_LIST)
        return v_err("load: columns must be a list of names");
    int nh = count_delim_fields(header, delim);
    if (nh <= 0 || nh > 1000000) return v_errf("%s: bad header", fmt);
    char **hdr_cells = calloc((size_t)nh, sizeof(char *));
    if (!hdr_cells) return v_errf("%s: out of memory", fmt);
    int got = split_delim_line(header, hdr_cells, nh, delim);
    if (got <= 0) {
        free(hdr_cells);
        return v_errf("%s: bad header", fmt);
    }
    nh = got;
    p->nh = nh;
    p->hdr = calloc((size_t)nh, sizeof(char *));
    p->out_of = malloc((size_t)nh * sizeof(int));
    if (!p->hdr || !p->out_of) {
        free(hdr_cells);
        delim_plan_free(p);
        return v_errf("%s: out of memory", fmt);
    }
    for (int i = 0; i < nh; i++) {
        p->out_of[i] = -1;
        p->hdr[i] = strdup(hdr_cells[i] ? hdr_cells[i] : "");
        if (!p->hdr[i]) {
            free(hdr_cells);
            delim_plan_free(p);
            return v_errf("%s: out of memory", fmt);
        }
    }
    free(hdr_cells);
    int n_out = columns_opt ? (int)columns_opt->n : nh;
    p->n_out = n_out;
    p->src = calloc((size_t)(n_out > 0 ? n_out : 1), sizeof(int));
    p->kind = calloc((size_t)(n_out > 0 ? n_out : 1), sizeof(int));
    p->seen = calloc((size_t)(n_out > 0 ? n_out : 1), sizeof(int));
    if (!p->src || !p->kind || !p->seen) {
        delim_plan_free(p);
        return v_errf("%s: out of memory", fmt);
    }
    if (columns_opt) {
        for (int i = 0; i < n_out; i++) {
            V *name = columns_opt->L[i];
            if (!name || name->t != T_STR) {
                delim_plan_free(p);
                return v_err("load: column name must be a string");
            }
            int found = -1;
            for (int j = 0; j < nh; j++)
                if (!strcmp(p->hdr[j], name->s)) { found = j; break; }
            if (found < 0) {
                delim_plan_free(p);
                return v_errf("load: unknown column '%s'", name->s);
            }
            p->src[i] = found;
            p->out_of[found] = i;
        }
    } else {
        for (int i = 0; i < n_out; i++) {
            p->src[i] = i;
            p->out_of[i] = i;
        }
    }
    return NULL;
}

/* Missing-value spellings: no text at all, or NA, NAN, NR or NULL in any case. */
static int is_na_token(const char *s, size_t n) {
    if (!s || n == 0) return 1;
    if (n > 4) return 0;
    unsigned char c0 = (unsigned char)s[0];
    if (c0 != 'N' && c0 != 'n') return 0;
    char buf[8];
    for (size_t i = 0; i < n; i++) buf[i] = (char)toupper((unsigned char)s[i]);
    buf[n] = 0;
    return !strcmp(buf, "NA") || !strcmp(buf, "NAN") || !strcmp(buf, "NR") ||
           !strcmp(buf, "NULL");
}

static int all_int_span(const char *s, size_t n) {
    size_t i = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) i++;
    if (i >= n) return 0;
    for (; i < n; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int all_float_span(const char *s, size_t n) {
    if (!s || n == 0 || n >= 128) return 0;
    char buf[128];
    memcpy(buf, s, n);
    buf[n] = 0;
    char *end = NULL;
    (void)strtod(buf, &end);
    return end && end != buf && *end == 0;
}

static int cell_kind_span(const char *s, size_t n, int was_quoted) {
    if (was_quoted) return KIND_STR;
    if (is_na_token(s, n)) return KIND_INT; /* missing: does not promote; the scan skips it */
    if (all_int_span(s, n)) {
        /* A value strtoll would clamp to the max int is a float column, not that max. */
        size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        while (i < n && s[i] == '0') i++;
        size_t sig = n - i;
        if (sig > 19) return KIND_FLOAT;
        if (sig > 0) {
            char buf[32];
            size_t lead = (s[0] == '-' || s[0] == '+') ? 1 : 0;
            size_t total = lead + sig;
            memcpy(buf, s, lead);
            memcpy(buf + lead, s + i, sig);
            buf[total] = 0;
            errno = 0;
            (void)strtoll(buf, NULL, 10);
            if (errno == ERANGE) return KIND_FLOAT;
        }
        return KIND_INT;
    }
    if (all_float_span(s, n)) return KIND_FLOAT;
    return KIND_STR;
}

/* Advance past one field without copying: *start/*len give its text (padding off, as
 * strip_pad takes it), *quoted whether it was quoted, *ate_delim whether a delimiter
 * followed it. The text is not NUL-terminated at *start + *len. */
static void next_field_view(const char **pp, char delim, const char **start, size_t *len,
                            int *quoted, int *ate_delim) {
    const char *p = *pp;
    *quoted = 0;
    *ate_delim = 0;
    if (*p == '"') {
        *quoted = 1;
        p++;
        *start = p;
        *len = 0;
        while (*p) {
            if (*p == '"') {
                if (p[1] == '"') { p += 2; continue; }
                *len = (size_t)(p - *start);
                p++;
                break;
            }
            p++;
        }
        if (!*p && p > *start && p[-1] != '"') *len = (size_t)(p - *start);
        while (*p && *p != delim) p++;
        if (*p == delim) { p++; *ate_delim = 1; }
        *pp = p;
        return;
    }
    const char *s = p;
    while (*p && *p != delim && *p != '\n') p++;
    const char *e = p;
    while (s < e && is_pad(*s, delim)) s++;
    while (e > s && is_end_pad(e[-1], delim)) e--;
    *start = s;
    *len = (size_t)(e - s);
    if (*p == delim) { p++; *ate_delim = 1; }
    *pp = p;
}

/* Fold one record into the plan's inferred kinds. Returns the field count. */
static int scan_record_kinds(DelimPlan *plan, const char *line, char delim) {
    const char *p = line ? line : "";
    int nf = 0;
    for (;;) {
        const char *s = NULL;
        size_t n = 0;
        int quoted = 0, ate = 0;
        next_field_view(&p, delim, &s, &n, &quoted, &ate);
        int oi = nf < plan->nh ? plan->out_of[nf] : -1;
        if (oi >= 0) {
            if (quoted) {
                plan->kind[oi] = KIND_STR;
                plan->seen[oi]++;
            } else if (!is_na_token(s, n)) {
                int k = cell_kind_span(s, n, 0);
                if (k > plan->kind[oi]) plan->kind[oi] = k;
                plan->seen[oi]++;
            }
        }
        nf++;
        if (!ate) break;
    }
    return nf;
}

/* Columns nothing real was ever seen in are string columns, not zero-filled ints. */
static void delim_plan_finish_kinds(DelimPlan *plan) {
    for (int i = 0; i < plan->n_out; i++)
        if (plan->kind[i] == KIND_INT && plan->seen[i] == 0) plan->kind[i] = KIND_STR;
}

static void csv_trim_span(const char **a, const char **b) {
    const char *s = *a, *e = *b;
    while (s < e && (*s == ' ' || *s == '\t')) s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    *a = s;
    *b = e;
}

static int csv_parse_i64_span(const char *s, const char *e, int64_t *out) {
    csv_trim_span(&s, &e);
    if (s == e || is_na_token(s, (size_t)(e - s))) { *out = 0; return 1; }
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = *s == '-'; s++; }
    if (s == e) return 0;
    uint64_t v = 0;
    for (; s < e; s++) {
        unsigned d = (unsigned)((unsigned char)*s - '0');
        if (d > 9) return 0;
        if (v > (UINT64_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    if (neg) {
        if (v > (uint64_t)INT64_MAX + 1u) return 0;
        *out = v == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)v;
    } else {
        if (v > (uint64_t)INT64_MAX) return 0;
        *out = (int64_t)v;
    }
    return 1;
}

static const double csv_p10[] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0,
                                 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16,
                                 1e17, 1e18};

/* Fast path for plain decimal text of at most 18 digits and no exponent. Returns 0 when
 * the text is not of that shape (the caller falls back to strtod, which also covers
 * exponents, long digit strings and subnormals exactly). */
static int csv_parse_f64_span(const char *s, const char *e, double *out) {
    csv_trim_span(&s, &e);
    if (s == e || is_na_token(s, (size_t)(e - s))) { *out = 0.0; return 1; }
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = *s == '-'; s++; }
    if (s == e) return 0;
    const char *p = s;
    uint64_t mant = 0;
    int ndig = 0, int_digits = 0;
    while (p < e && isdigit((unsigned char)*p)) {
        if (ndig < 18) mant = mant * 10u + (unsigned)(*p - '0');
        ndig++;
        p++;
    }
    int_digits = ndig;
    int frac_digits = 0;
    if (p < e && *p == '.') {
        p++;
        while (p < e && isdigit((unsigned char)*p)) {
            if (ndig < 18) mant = mant * 10u + (unsigned)(*p - '0');
            ndig++;
            p++;
        }
        frac_digits = ndig - int_digits;
    }
    if (p != e || ndig == 0 || ndig > 18) return 0;
    if (frac_digits >= (int)(sizeof csv_p10 / sizeof csv_p10[0])) return 0;
    double v = (double)mant;
    if (frac_digits > 0) v /= csv_p10[frac_digits];
    if (v != 0.0 && fabs(v) < DBL_MIN) return 0;
    *out = neg ? -v : v;
    return 1;
}

static V **delim_alloc_cols(DelimPlan *plan, int64_t rows, const char *fmt) {
    V **cols = calloc((size_t)(plan->n_out > 0 ? plan->n_out : 1), sizeof(V *));
    if (!cols) return NULL;
    for (int i = 0; i < plan->n_out; i++) {
        cols[i] = plan->kind[i] == KIND_STR    ? v_list(rows)
                  : plan->kind[i] == KIND_FLOAT ? v_fvec(rows)
                                                : v_ivec(rows);
        if (!cols[i] || cols[i]->t == T_ERR) {
            for (int k = 0; k <= i; k++) v_free(cols[k]);
            free(cols);
            return NULL;
        }
        (void)fmt;
    }
    return cols;
}

static void delim_free_cols(V **cols, int n) {
    if (!cols) return;
    for (int i = 0; i < n; i++) v_free(cols[i]);
    free(cols);
}

/* Fill one output row from a record already split by split_delim_line. */
static void delim_fill_row(DelimPlan *plan, V **cols, char **cells, int64_t row) {
    for (int i = 0; i < plan->n_out; i++) {
        const char *cell = cells[plan->src[i]] ? cells[plan->src[i]] : "";
        if (plan->kind[i] == KIND_STR) {
            cols[i]->L[row] = v_str(cell);
        } else if (plan->kind[i] == KIND_FLOAT) {
            double x = 0.0;
            if (!csv_parse_f64_span(cell, cell + strlen(cell), &x)) x = strtod(cell, NULL);
            cols[i]->F[row] = x;
        } else {
            int64_t v = 0;
            if (!csv_parse_i64_span(cell, cell + strlen(cell), &v))
                v = (int64_t)strtoll(cell, NULL, 10);
            cols[i]->J[row] = v;
        }
    }
}

static V *delim_finish_table(DelimPlan *plan, V **cols, int64_t row) {
    V *kl = v_list(plan->n_out);
    V *dl = v_list(plan->n_out);
    for (int i = 0; i < plan->n_out; i++) {
        kl->L[i] = v_str(plan->hdr[plan->src[i]]);
        dl->L[i] = cols[i];
        if (cols[i]) cols[i]->n = row;
    }
    V *t = v_table(kl, dl);
    v_free(kl);
    v_free(dl);
    t->n = row;
    return t;
}
static V *table_delim_load(const char *path, V *columns_opt, char c, const char *fmt) {
    /* Prefer a single buffered read for normal-sized files (fast path / benches).
     * Fall back to two-pass getline streaming when the file exceeds
     * SHAKTI_CSV_MAX_BYTES (default 1 GiB) or cannot be mapped that way. */
    size_t raw_len = 0;
    int raw_bad = 0;
    char *raw = read_all(path, &raw_len, &raw_bad);
    P(raw_bad, v_errf("%s: cannot read '%s'", fmt, path))
    if (raw) {
        char *buf = raw, *end = raw + raw_len;
        if ((unsigned char)buf[0] == 0xef && (unsigned char)buf[1] == 0xbb &&
            (unsigned char)buf[2] == 0xbf)
            buf += 3;
        int64_t cap = 256, nl = 0;
        char **lines = malloc((size_t)cap * sizeof(char *));
        if (!lines) {
            free(raw);
            return v_errf("%s: out of memory", fmt);
        }
        char *at = buf;
        while (at < end) {
            if (nl >= cap) {
                if (cap > (INT64_C(1) << 30)) {
                    free(lines);
                    free(raw);
                    return v_errf("%s: too many lines", fmt);
                }
                cap *= 2;
                char **nlines = realloc(lines, (size_t)cap * sizeof(char *));
                if (!nlines) {
                    free(lines);
                    free(raw);
                    return v_errf("%s: out of memory", fmt);
                }
                lines = nlines;
            }
            int quoted = 0;
            char *e = record_end(at, end, c, &quoted);
            if (quoted) {
                free(lines);
                free(raw);
                return v_errf("%s: quoted field not closed", fmt);
            }
            if (e < end) {
                *e = 0;
                if (e > at && e[-1] == '\r')
                    e[-1] = 0;
                lines[nl++] = at;
                at = e + 1;
            } else {
                lines[nl++] = at;
                break;
            }
        }
        if (nl < 2) {
            free(lines);
            free(raw);
            return v_errf("%s: need header + rows", fmt);
        }
        strip_pad(lines[0], c);
        DelimPlan plan;
        V *perr = delim_plan_init(&plan, lines[0], c, columns_opt, fmt);
        if (perr) {
            free(lines);
            free(raw);
            return perr;
        }
        int *rowmap = malloc((size_t)(nl > 1 ? nl : 1) * sizeof(int));
        if (!rowmap) {
            delim_plan_free(&plan);
            free(lines);
            free(raw);
            return v_errf("%s: out of memory", fmt);
        }
        int data_rows = 0;
        for (int li = 1; li < nl; li++) {
            strip_pad(lines[li], c);
            if (!lines[li][0])
                continue;
            if (scan_record_kinds(&plan, lines[li], c) != plan.nh) {
                free(rowmap);
                delim_plan_free(&plan);
                free(lines);
                free(raw);
                return v_errf("%s: column count mismatch", fmt);
            }
            rowmap[data_rows++] = li;
        }
        if (data_rows <= 0) {
            free(rowmap);
            delim_plan_free(&plan);
            free(lines);
            free(raw);
            return v_errf("%s: need header + rows", fmt);
        }
        delim_plan_finish_kinds(&plan);
        V **cols = delim_alloc_cols(&plan, data_rows, fmt);
        if (!cols) {
            free(rowmap);
            delim_plan_free(&plan);
            free(lines);
            free(raw);
            return v_errf("%s: out of memory", fmt);
        }
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (data_rows > 1024)
#endif
        for (int r = 0; r < data_rows; r++) {
            int li = rowmap[r];
            char *cellbuf[64];
            char **cells = plan.nh <= 64 ? cellbuf
                                         : malloc((size_t)plan.nh * sizeof(char *));
            if (!cells) continue;
            for (int cj = 0; cj < plan.nh; cj++)
                cells[cj] = NULL;
            split_delim_line(lines[li], cells, plan.nh, c);
            delim_fill_row(&plan, cols, cells, r);
            if (cells != cellbuf) free(cells);
        }
        free(rowmap);
        V *t = delim_finish_table(&plan, cols, data_rows);
        free(cols);
        delim_plan_free(&plan);
        free(lines);
        free(raw);
        return t;
    }

    FILE *f = fopen(path, "rb");
    P(!f, v_errf("%s: cannot read '%s'", fmt, path))
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t got = get_record(&line, &line_cap, f, c, 1);
    if (got < 0) {
        fclose(f);
        free(line);
        return record_error(got, fmt, path);
    }
    char *header = line;
    if (got >= 3 && (unsigned char)header[0] == 0xef &&
        (unsigned char)header[1] == 0xbb && (unsigned char)header[2] == 0xbf)
        header += 3;
    strip_pad(header, c);
    DelimPlan plan;
    V *perr = delim_plan_init(&plan, header, c, columns_opt, fmt);
    if (perr) {
        fclose(f);
        free(line);
        return perr;
    }
    char **cells = calloc((size_t)plan.nh, sizeof(char *));
    if (!cells) {
        delim_plan_free(&plan);
        fclose(f);
        free(line);
        return v_errf("%s: out of memory", fmt);
    }
    int64_t data_rows = 0;
    while ((got = get_record(&line, &line_cap, f, c, 0)) >= 0) {
        strip_pad(line, c);
        if (!line[0])
            continue;
        data_rows++;
        if (scan_record_kinds(&plan, line, c) != plan.nh) {
            free(cells);
            delim_plan_free(&plan);
            fclose(f);
            free(line);
            return v_errf("%s: column count mismatch", fmt);
        }
    }
    if (got < REC_EOF || data_rows <= 0) {
        free(cells);
        delim_plan_free(&plan);
        fclose(f);
        free(line);
        return record_error(got, fmt, path);
    }
    delim_plan_finish_kinds(&plan);
    V **cols = delim_alloc_cols(&plan, data_rows, fmt);
    if (!cols) {
        free(cells);
        delim_plan_free(&plan);
        fclose(f);
        free(line);
        return v_errf("%s: out of memory", fmt);
    }

    /* Pass 2 reads the file again from the start: it is a read error if it can't go back there (a
       pipe), or if the file now ends before the rows pass 1 counted (it shrank), or "quoted field
       not closed" if it now ends inside one. */
    got = fseek(f, 0, SEEK_SET) ? REC_READ_ERR : get_record(&line, &line_cap, f, c, 1); /* header */
    int64_t row = 0;
    while (got >= 0 && (got = get_record(&line, &line_cap, f, c, 0)) >= 0) {
        strip_pad(line, c);
        if (!line[0])
            continue;
        if (row >= data_rows)
            break;
        for (int cj = 0; cj < plan.nh; cj++)
            cells[cj] = NULL;
        split_delim_line(line, cells, plan.nh, c);
        delim_fill_row(&plan, cols, cells, row);
        row++;
    }
    if (got < REC_EOF || row < data_rows) {
        if (got >= REC_EOF) got = REC_READ_ERR;
        delim_free_cols(cols, plan.n_out);
        free(cells);
        delim_plan_free(&plan);
        fclose(f);
        free(line);
        return record_error(got, fmt, path);
    }
    V *t = delim_finish_table(&plan, cols, row);
    free(cols);
    free(cells);
    delim_plan_free(&plan);
    fclose(f);
    free(line);
    return table_keep_columns(t, columns_opt);
}

static V *table_csv_load(const char *path, V *columns_opt) {
    return table_delim_load(path, columns_opt, ',', "csv");
}

static V *table_tsv_load(const char *path, V *columns_opt) {
    return table_delim_load(path, columns_opt, '\t', "tsv");
}
