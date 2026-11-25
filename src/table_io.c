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
/* Whether a field reads as an int once the padding strip_pad takes off is gone. */
static int field_is_int(const char *line, int field, char delim) {
    const char *p = line ? line : "";
    int f;
    for (f = 0; f < field && *p; f++) {
        int ate = 0;
        p = delim_skip_field(p, delim, &ate);
        if (!ate) return 0;
    }
    int quoted = *p == '"';
    if (quoted) p++;
    W(is_pad(*p, delim), p++)
    if (*p == '-' || *p == '+') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    W(isdigit((unsigned char)*p), p++)
    W(is_end_pad(*p, delim), p++)
    /* The field ends at the end of the text, or at its closing quote if it is quoted (a "" or a
       delimiter inside the quotes is part of it), or at the delimiter if not. */
    return !*p || (quoted ? *p == '"' && p[1] != '"' : *p == delim);
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
        int nh_count = count_delim_fields(lines[0], c);
        if (nh_count <= 0 || nh_count > 1000000) {
            free(lines);
            free(raw);
            return v_errf("%s: bad header", fmt);
        }
        char **hdr_cells = calloc((size_t)nh_count, sizeof(char *));
        char **cells = calloc((size_t)nh_count, sizeof(char *));
        int *use_float = calloc((size_t)nh_count, sizeof(int));
        if (!hdr_cells || !cells || !use_float) {
            free(hdr_cells); free(cells); free(use_float); free(lines); free(raw);
            return v_errf("%s: out of memory", fmt);
        }
        int nh = split_delim_line(lines[0], hdr_cells, nh_count, c);
        if (nh <= 0) {
            free(hdr_cells); free(cells); free(use_float);
            free(lines);
            free(raw);
            return v_errf("%s: bad header", fmt);
        }
        int data_rows = 0;
        for (int li = 1; li < nl; li++) {
            strip_pad(lines[li], c);
            if (!lines[li][0])
                continue;
            data_rows++;
            if (count_delim_fields(lines[li], c) != nh) {
                free(hdr_cells); free(cells); free(use_float);
                free(lines);
                free(raw);
                return v_errf("%s: column count mismatch", fmt);
            }
            for (int cj = 0; cj < nh; cj++) {
                if (!field_is_int(lines[li], cj, c))
                    use_float[cj] = 1;
            }
        }
        if (data_rows <= 0) {
            free(hdr_cells); free(cells); free(use_float);
            free(lines);
            free(raw);
            return v_errf("%s: need header + rows", fmt);
        }
        V **cols = calloc((size_t)nh, sizeof(V *));
        if (!cols) {
            free(hdr_cells); free(cells); free(use_float);
            free(lines);
            free(raw);
            return v_errf("%s: out of memory", fmt);
        }
        for (int cj = 0; cj < nh; cj++)
            cols[cj] = use_float[cj] ? v_fvec(data_rows) : v_ivec(data_rows);
        for (int cj = 0; cj < nh; cj++)
            if (!use_float[cj])
                memset(cols[cj]->J, 0, (size_t)data_rows * sizeof(int64_t));
        int row = 0;
        for (int li = 1; li < nl; li++) {
            strip_pad(lines[li], c);
            if (!lines[li][0])
                continue;
            for (int cj = 0; cj < nh; cj++)
                cells[cj] = NULL;
            split_delim_line(lines[li], cells, nh, c);
            for (int cj = 0; cj < nh; cj++) {
                const char *cell = cells[cj] ? cells[cj] : "";
                if (use_float[cj])
                    cols[cj]->F[row] = strtod(cell, NULL);
                else {
                    errno = 0;
                    char *end = NULL;
                    long long iv = strtoll(cell, &end, 10);
                    if (errno == ERANGE) {
                        for (int k = 0; k < nh; k++) v_free(cols[k]);
                        free(cols); free(hdr_cells); free(cells); free(use_float);
                        free(lines); free(raw);
                        return v_errf("%s: integer out of range", fmt);
                    }
                    (void)end;
                    cols[cj]->J[row] = (int64_t)iv;
                }
            }
            row++;
        }
        V *kl = v_list(nh);
        V *dl = v_list(nh);
        for (int ci = 0; ci < nh; ci++) {
            kl->L[ci] = v_str(hdr_cells[ci]);
            dl->L[ci] = cols[ci];
        }
        free(cols);
        free(hdr_cells);
        free(cells);
        free(use_float);
        free(lines);
        free(raw);
        V *t = v_table(kl, dl);
        v_free(kl);
        v_free(dl);
        t->n = row;
        for (int ci = 0; ci < nh; ci++)
            t->vals->L[ci]->n = row;
        return table_keep_columns(t, columns_opt);
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
    int nh = count_delim_fields(header, c);
    if (nh <= 0 || nh > 1000000) {
        fclose(f);
        free(line);
        return v_errf("%s: bad header", fmt);
    }
    char **hdr_cells = calloc((size_t)nh, sizeof(char *));
    char **headers = calloc((size_t)nh, sizeof(char *));
    char **cells = calloc((size_t)nh, sizeof(char *));
    int *use_float = calloc((size_t)nh, sizeof(int));
    if (!hdr_cells || !headers || !cells || !use_float) {
        free(hdr_cells); free(headers); free(cells); free(use_float);
        fclose(f);
        free(line);
        return v_errf("%s: out of memory", fmt);
    }
    if (split_delim_line(header, hdr_cells, nh, c) != nh) {
        free(hdr_cells); free(headers); free(cells); free(use_float);
        fclose(f);
        free(line);
        return v_errf("%s: bad header", fmt);
    }
    for (int ci = 0; ci < nh; ci++) {
        headers[ci] = strdup(hdr_cells[ci]);
        if (!headers[ci]) {
            for (int j = 0; j < ci; j++)
                free(headers[j]);
            free(hdr_cells); free(headers); free(cells); free(use_float);
            fclose(f);
            free(line);
            return v_errf("%s: out of memory", fmt);
        }
    }
    free(hdr_cells);
    int64_t data_rows = 0;
    while ((got = get_record(&line, &line_cap, f, c, 0)) >= 0) {
        strip_pad(line, c);
        if (!line[0])
            continue;
        data_rows++;
        if (count_delim_fields(line, c) != nh) {
            for (int ci = 0; ci < nh; ci++)
                free(headers[ci]);
            free(headers); free(cells); free(use_float);
            fclose(f);
            free(line);
            return v_errf("%s: column count mismatch", fmt);
        }
        for (int cj = 0; cj < nh; cj++) {
            if (!field_is_int(line, cj, c))
                use_float[cj] = 1;
        }
    }
    if (got < REC_EOF || data_rows <= 0) {
        for (int ci = 0; ci < nh; ci++)
            free(headers[ci]);
        free(headers); free(cells); free(use_float);
        fclose(f);
        free(line);
        return record_error(got, fmt, path);
    }
    V **cols = calloc((size_t)nh, sizeof(V *));
    if (!cols) {
        for (int ci = 0; ci < nh; ci++)
            free(headers[ci]);
        free(headers); free(cells); free(use_float);
        fclose(f);
        free(line);
        return v_errf("%s: out of memory", fmt);
    }
    for (int cj = 0; cj < nh; cj++)
        cols[cj] = use_float[cj] ? v_fvec(data_rows) : v_ivec(data_rows);
    for (int cj = 0; cj < nh; cj++)
        if (!use_float[cj])
            memset(cols[cj]->J, 0, (size_t)data_rows * sizeof(int64_t));

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
        for (int cj = 0; cj < nh; cj++)
            cells[cj] = NULL;
        split_delim_line(line, cells, nh, c);
        for (int cj = 0; cj < nh; cj++) {
            const char *cell = cells[cj] ? cells[cj] : "";
            if (use_float[cj])
                cols[cj]->F[row] = strtod(cell, NULL);
            else {
                errno = 0;
                long long iv = strtoll(cell, NULL, 10);
                if (errno == ERANGE) {
                    for (int ci = 0; ci < nh; ci++) {
                        free(headers[ci]);
                        v_free(cols[ci]);
                    }
                    free(headers); free(cells); free(use_float); free(cols);
                    fclose(f);
                    free(line);
                    return v_errf("%s: integer out of range", fmt);
                }
                cols[cj]->J[row] = (int64_t)iv;
            }
        }
        row++;
    }
    if (got < REC_EOF || row < data_rows) {
        if (got >= REC_EOF) got = REC_READ_ERR;
        for (int ci = 0; ci < nh; ci++) {
            free(headers[ci]);
            v_free(cols[ci]);
        }
        free(headers); free(cells); free(use_float); free(cols);
        fclose(f);
        free(line);
        return record_error(got, fmt, path);
    }
    V *kl = v_list(nh);
    V *dl = v_list(nh);
    for (int ci = 0; ci < nh; ci++) {
        kl->L[ci] = v_str(headers[ci]);
        dl->L[ci] = cols[ci];
        free(headers[ci]);
    }
    free(headers);
    free(cells);
    free(use_float);
    free(cols);
    fclose(f);
    free(line);
    V *t = v_table(kl, dl);
    v_free(kl);
    v_free(dl);
    t->n = row;
    for (int ci = 0; ci < nh; ci++)
        t->vals->L[ci]->n = row;
    return table_keep_columns(t, columns_opt);
}

static V *table_csv_load(const char *path, V *columns_opt) {
    return table_delim_load(path, columns_opt, ',', "csv");
}

static V *table_tsv_load(const char *path, V *columns_opt) {
    return table_delim_load(path, columns_opt, '\t', "tsv");
}
