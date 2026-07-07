#include "shakti.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern V *table_xml_load(const char *path, V *columns_opt);

static V *table_csv_load(const char *path, V *columns_opt);
static int table_csv_save(V *table, const char *path);

static int ends_with(const char *s, const char *t) {
    size_t ls = strlen(s), lu = strlen(t);
    return ls >= lu && strcmp(s + ls - lu, t) == 0;
}

V *table_load(const char *path, V *columns_opt) {
    P(!path, v_err("load: path"))
    if (ends_with(path, ".csv"))
        return table_csv_load(path, columns_opt);
    if (ends_with(path, ".xml"))
        return table_xml_load(path, columns_opt);
    return v_err("load: supported formats are .csv and .xml");
}

int table_save(V *table, const char *path) {
    P(!table || !path, -1)
    if (ends_with(path, ".csv"))
        return table_csv_save(table, path);
    return -1;
}

static char *read_all(const char *s) {
    FILE *f = fopen(s, "rb");
    P(!f, NULL)
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (z < 0) {
        fclose(f);
        return NULL;
    }
    char *b = malloc((size_t)z + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    fread(b, 1, (size_t)z, f);
    b[z] = 0;
    fclose(f);
    return b;
}

static void strip_ws(char *s) {
    char *a = s;
    W(*a == ' ' || *a == '\t', a++)
    if (a != s)
        memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    W(n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'), s[--n] = 0)
}

static int split_csv_line(char *line, char **out, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        char *start = p;
        W(*p && *p != ',', p++)
        if (*p == ',')
            *p++ = 0;
        out[n++] = start;
        strip_ws(start);
    }
    return n;
}

static int all_int_cell(const char *s) {
    const char *p = s;
    if (*p == '-' || *p == '+')
        p++;
    P(!*p, 0)
    for (; *p; p++)
        P(!isdigit((unsigned char)*p), 0)
    return 1;
}

static int table_csv_save(V *table, const char *path) {
    P(!table || table->t != T_TABLE || !table->keys || !table->vals, -1)
    int nc = (int)table->keys->n;
    int64_t nrows = table->n;
    FILE *f = fopen(path, "wb");
    P(!f, -1)
    for (int j = 0; j < nc; j++) {
        if (j)
            fputc(',', f);
        V *cn = table->keys->L[j];
        const char *nm = (cn && cn->t == T_STR) ? cn->s : "col";
        fputs(nm, f);
    }
    fputc('\n', f);
    for (int64_t r = 0; r < nrows; r++) {
        for (int j = 0; j < nc; j++) {
            if (j)
                fputc(',', f);
            V *col = table->vals->L[j];
            char buf[64];
            if (col->t == T_FVEC)
                snprintf(buf, sizeof buf, "%g", col->F[r]);
            else if (col->t == T_IVEC)
                snprintf(buf, sizeof buf, "%lld", (long long)col->J[r]);
            else {
                fclose(f);
                return -1;
            }
            fputs(buf, f);
        }
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

static int scan_csv_field(const char *line, int field, char *buf, size_t bufsz) {
    const char *p = line;
    for (int f = 0; f < field && *p; f++) {
        while (*p && *p != ',')
            p++;
        if (*p == ',')
            p++;
    }
    if (!*p)
        return 0;
    const char *start = p;
    while (*p && *p != ',')
        p++;
    size_t len = (size_t)(p - start);
    if (len >= bufsz)
        len = bufsz - 1;
    memcpy(buf, start, len);
    buf[len] = 0;
    strip_ws(buf);
    return 1;
}

static int count_csv_fields(const char *line) {
    int n = 0;
    const char *p = line;
    while (1) {
        n++;
        while (*p && *p != ',')
            p++;
        if (!*p)
            break;
        p++;
    }
    return n;
}

static V *table_csv_load(const char *path, V *columns_opt) {
    (void)columns_opt;
    char *raw = read_all(path);
    P(!raw, v_errf("csv: cannot read '%s'", path))
    char *buf = raw;
    if ((unsigned char)buf[0] == 0xef && (unsigned char)buf[1] == 0xbb && (unsigned char)buf[2] == 0xbf)
        buf += 3;
    int cap = 256, nl = 0;
    char **lines = malloc((size_t)cap * sizeof(char *));
    if (!lines) {
        free(raw);
        return v_err("csv: out of memory");
    }
    char *at = buf;
    while (*at) {
        if (nl >= cap) {
            cap *= 2;
            char **nlines = realloc(lines, (size_t)cap * sizeof(char *));
            if (!nlines) {
                free(lines);
                free(raw);
                return v_err("csv: out of memory");
            }
            lines = nlines;
        }
        char *e = strchr(at, '\n');
        if (e) {
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
        return v_err("csv: need header + rows");
    }
    char *hdr_cells[64];
    int nh = split_csv_line(lines[0], hdr_cells, 64);
    if (nh <= 0) {
        free(lines);
        free(raw);
        return v_err("csv: bad header");
    }
    int data_rows = 0;
    char *cells[64];
    char cell_buf[64];
    int use_float[64];
    memset(use_float, 0, sizeof(use_float));
    for (int li = 1; li < nl; li++) {
        strip_ws(lines[li]);
        if (!lines[li][0])
            continue;
        data_rows++;
        if (count_csv_fields(lines[li]) != nh) {
            free(lines);
            free(raw);
            return v_err("csv: column count mismatch");
        }
        for (int cj = 0; cj < nh; cj++) {
            if (!scan_csv_field(lines[li], cj, cell_buf, sizeof cell_buf))
                continue;
            if (!all_int_cell(cell_buf))
                use_float[cj] = 1;
        }
    }
    if (data_rows <= 0) {
        free(lines);
        free(raw);
        return v_err("csv: need header + rows");
    }
    V **cols = calloc((size_t)nh, sizeof(V *));
    for (int cj = 0; cj < nh; cj++)
        cols[cj] = use_float[cj] ? v_fvec(data_rows) : v_ivec(data_rows);
    for (int cj = 0; cj < nh; cj++)
        if (!use_float[cj])
            memset(cols[cj]->J, 0, (size_t)data_rows * sizeof(int64_t));
    int row = 0;
    for (int li = 1; li < nl; li++) {
        strip_ws(lines[li]);
        if (!lines[li][0])
            continue;
        split_csv_line(lines[li], cells, 64);
        for (int cj = 0; cj < nh; cj++) {
            if (use_float[cj])
                cols[cj]->F[row] = strtod(cells[cj], NULL);
            else
                cols[cj]->J[row] = (int64_t)strtoll(cells[cj], NULL, 10);
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
    free(lines);
    free(raw);
    V *t = v_table(kl, dl);
    v_free(kl);
    v_free(dl);
    t->n = row;
    for (int ci = 0; ci < nh; ci++)
        t->vals->L[ci]->n = row;
    return t;
}
