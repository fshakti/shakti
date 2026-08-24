/* shakti/src/value.c — V constructors, copy/free, print, serialize */
#include "shakti_internal.h"
#include "mat_simd.h"
#ifdef SHAKTI_HAVE_IEFS
#include "iefs_format.h"
#include "iefs_io.h"
#include "iefs_map.h"
#endif
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

V *v_alloc(int t) {
    V *v = x_calloc(1, sizeof(V), "v_alloc");
    v->t = t; v->rc = 1;
    return v;
}
#define ISL_INT_CACHE_MAX 1048576
static V **isl_int_cache;

V *v_nil(void)           { return v_alloc(T_NIL); }
V *v_bool(int b)         { V *v=v_alloc(T_BOOL); v->b=b; return v; }
V *v_int(int64_t j) {
    if (j >= 0 && j < ISL_INT_CACHE_MAX) {
        if (!isl_int_cache)
            isl_int_cache = calloc((size_t)ISL_INT_CACHE_MAX, sizeof(V*));
        if (isl_int_cache) {
            V *c = isl_int_cache[j];
            if (c) return v_ref(c);
            c = v_alloc(T_INT);
            c->j = j;
            isl_int_cache[j] = c;
            return v_ref(c);
        }
    }
    V *v = v_alloc(T_INT);
    v->j = j;
    return v;
}
V *v_float(double f)     { V *v=v_alloc(T_FLOAT); v->f=f; return v; }
V *v_str(const char *s)  { V *v=v_alloc(T_STR);  v->s=x_strdup(s, "v_str"); return v; }
V *v_str_take(char *s)   { V *v=v_alloc(T_STR);  v->s=s; return v; }
V *v_date(int64_t utc_midnight_ms) {
    V *v = v_alloc(T_DATE);
    v->j = utc_midnight_ms;
    return v;
}
V *v_time(int64_t ms_since_midnight) {
    V *v = v_alloc(T_TIME);
    v->j = ms_since_midnight;
    return v;
}
V *v_err(const char *s)  { V *v=v_alloc(T_ERR);  v->s=x_strdup(s, "v_err"); return v; }
V *v_errf(const char *fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap,fmt);
    vsnprintf(buf,sizeof(buf),fmt,ap);
    va_end(ap);
    return v_err(buf);
}
V *v_ivec(int64_t n) {
    V *v=v_alloc(T_IVEC); v->n=n;
    if (n > (int64_t)UINT32_MAX) shakti_oom("v_ivec");
    v->_ht_cap = n > 0 ? (uint32_t)n : 0;
    v->J = x_malloc(x_mul((size_t)(n > 0 ? n : 1), sizeof(int64_t), "v_ivec"), "v_ivec");
    return v;
}
V *v_fvec(int64_t n) {
    V *v=v_alloc(T_FVEC); v->n=n;
    v->F = x_calloc(n?n:1, sizeof(double), "v_fvec");
    return v;
}
V *v_bvec(int64_t n) {
    V *v=v_alloc(T_BVEC); v->n=n;
    v->B = x_calloc(n?n:1, 1, "v_bvec");
    return v;
}
V *v_cvec(int64_t n) {
    V *v=v_alloc(T_CVEC); v->n=n;
    v->B = x_calloc(n?n:1, 1, "v_cvec");
    return v;
}
V *v_char(unsigned char b) {
    V *v=v_alloc(T_CHAR); v->j=b; return v;
}
V *v_subprocess(int fd, int64_t pid) {
    V *v = v_alloc(T_SUBPROCESS);
    v->j = fd;
    v->n = pid;
    return v;
}
V *v_imat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_IMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz;
    if (__builtin_mul_overflow(rows, (cols > 0 ? cols : 1), &sz) || sz < 0)
        shakti_oom("v_imat");
    v->J = x_calloc((size_t)sz, sizeof(int64_t), "v_imat");
    return v;
}
V *v_fmat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_FMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz;
    if (__builtin_mul_overflow(rows, (cols > 0 ? cols : 1), &sz) || sz < 0)
        shakti_oom("v_fmat");
    v->F = x_calloc((size_t)sz, sizeof(double), "v_fmat");
    return v;
}
V *v_bmat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_BMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz;
    if (__builtin_mul_overflow(rows, (cols > 0 ? cols : 1), &sz) || sz < 0)
        shakti_oom("v_bmat");
    v->B = x_calloc((size_t)sz, 1, "v_bmat");
    return v;
}
V *v_cmat(int64_t rows, int64_t cols) {
    V *v = v_bmat(rows, cols);
    v->t = T_CMAT;
    return v;
}
static void mat_cell_format(V *v, int64_t r, int64_t c, char *buf, size_t cap) {
    if (v->t == T_IMAT)
        snprintf(buf, cap, "%lld", (long long)v->J[mat_idx(v, r, c)]);
    else if (v->t == T_CMAT)
        snprintf(buf, cap, "0x%02x", v->B[mat_idx(v, r, c)]);
    else if (v->t == T_FMAT) {
        double d = v->F[mat_idx(v, r, c)];
        if (d == (int64_t)d && d < 1e15 && d > -1e15)
            snprintf(buf, cap, "%.1f", d);
        else
            snprintf(buf, cap, "%g", d);
    } else
        snprintf(buf, cap, "%s", v->B[mat_idx(v, r, c)] ? "True" : "False");
}
static void print_mat_compact(V *v, FILE *fp) {
    int64_t cols = mat_cols(v);
    fprintf(fp, "[");
    for (int64_t r = 0; r < v->n; r++) {
        if (r) fprintf(fp, ", ");
        fprintf(fp, "[");
        for (int64_t c = 0; c < cols; c++) {
            if (c) fprintf(fp, ", ");
            char buf[64];
            mat_cell_format(v, r, c, buf, sizeof buf);
            fputs(buf, fp);
        }
        fprintf(fp, "]");
    }
    fprintf(fp, "]");
}
static void print_mat_pretty(V *v, FILE *fp) {
    int64_t rows = v->n, cols = mat_cols(v);
    if (rows == 0) {
        fprintf(fp, "[]");
        return;
    }
    if (cols == 0) {
        fprintf(fp, "[");
        for (int64_t r = 0; r < rows; r++) {
            if (r) fprintf(fp, ", ");
            fprintf(fp, "[]");
        }
        fprintf(fp, "]");
        return;
    }
    int *widths = calloc((size_t)cols, sizeof(int));
    char buf[64];
    for (int64_t c = 0; c < cols; c++) {
        int w = 1;
        for (int64_t r = 0; r < rows; r++) {
            mat_cell_format(v, r, c, buf, sizeof buf);
            int l = (int)strlen(buf);
            if (l > w) w = l;
        }
        widths[c] = w;
    }
    fputc('[', fp);
    for (int64_t r = 0; r < rows; r++) {
        if (r) fputs(",\n ", fp);
        fputc('[', fp);
        for (int64_t c = 0; c < cols; c++) {
            if (c) fputs(", ", fp);
            mat_cell_format(v, r, c, buf, sizeof buf);
            fprintf(fp, "%*s", widths[c], buf);
        }
        fputc(']', fp);
    }
    fputc(']', fp);
    free(widths);
}
static void print_mat_val(V *v, FILE *fp, int repr_mode) {
    if (repr_mode) print_mat_compact(v, fp);
    else print_mat_pretty(v, fp);
}
V *v_mat_row(V *m, int64_t r) {
    int64_t cols = mat_cols(m);
    if (r < 0) r += m->n;
    P(r < 0 || r >= m->n, v_err("index out of range"))
    if (m->t == T_IMAT) {
        V *rv = v_ivec(cols);
        memcpy(rv->J, m->J + mat_idx(m, r, 0), (size_t)cols * sizeof(int64_t));
        return rv;
    }
    if (m->t == T_FMAT) {
        V *rv = v_fvec(cols);
        memcpy(rv->F, m->F + mat_idx(m, r, 0), (size_t)cols * sizeof(double));
        return rv;
    }
    V *rv = m->t == T_CMAT ? v_cvec(cols) : v_bvec(cols);
    memcpy(rv->B, m->B + mat_idx(m, r, 0), (size_t)cols);
    return rv;
}
V *try_promote_matrix(V **elems, int nch) {
    if (nch <= 0) return NULL;
    int64_t cols = -1;
    int all_int = 1, all_u8 = 1, all_num = 1, all_bool = 1;
    for (int i = 0; i < nch; i++) {
        V *row = elems[i];
        int64_t row_len = 0;
        if (row->t == T_IVEC) {
            row_len = row->n; all_u8 = 0;
        } else if (row->t == T_CVEC) {
            row_len = row->n;
        } else if (row->t == T_FVEC) {
            row_len = row->n; all_int = 0; all_u8 = 0;
        } else if (row->t == T_BVEC) {
            row_len = row->n; all_int = 0; all_u8 = 0; all_num = 0;
        } else if (row->t == T_LIST) {
            row_len = row->n;
            for (int64_t j = 0; j < row->n; j++) {
                if (!elems[i]->L[j]) return NULL;
                if (elems[i]->L[j]->t != T_CHAR) all_u8 = 0;
                if (elems[i]->L[j]->t != T_INT && elems[i]->L[j]->t != T_CHAR) all_int = 0;
                if (elems[i]->L[j]->t != T_CHAR && elems[i]->L[j]->t != T_INT && elems[i]->L[j]->t != T_FLOAT) all_num = 0;
                if (elems[i]->L[j]->t != T_BOOL) all_bool = 0;
            }
        } else return NULL;
        if (cols < 0) cols = row_len;
        else if (cols != row_len) return NULL;
    }
    if (all_u8) {
        V *r = v_cmat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_CVEC) memcpy(r->B + mat_idx(r, i, 0), row->B, (size_t)cols);
            else for (int64_t j = 0; j < cols; j++) r->B[mat_idx(r, i, j)] = (unsigned char)row->L[j]->j;
        }
        return r;
    }
    if (all_int) {
        V *r = v_imat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_IVEC) memcpy(r->J + mat_idx(r, i, 0), row->J, (size_t)cols * 8);
            else if (row->t == T_CVEC) for (int64_t j = 0; j < cols; j++) r->J[mat_idx(r, i, j)] = row->B[j];
            else for (int64_t j = 0; j < cols; j++) r->J[mat_idx(r, i, j)] = row->L[j]->j;
        }
        return r;
    }
    if (all_num) {
        V *r = v_fmat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_IVEC) for (int64_t j = 0; j < cols; j++) r->F[mat_idx(r, i, j)] = (double)row->J[j];
            else if (row->t == T_CVEC) for (int64_t j = 0; j < cols; j++) r->F[mat_idx(r, i, j)] = (double)row->B[j];
            else if (row->t == T_FVEC) memcpy(r->F + mat_idx(r, i, 0), row->F, (size_t)cols * 8);
            else for (int64_t j = 0; j < cols; j++) {
                V *e = row->L[j];
                r->F[mat_idx(r, i, j)] = e->t == T_INT || e->t == T_CHAR ? (double)e->j : e->f;
            }
        }
        return r;
    }
    if (all_bool) {
        V *r = v_bmat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_BVEC) memcpy(r->B + mat_idx(r, i, 0), row->B, (size_t)cols);
            else for (int64_t j = 0; j < cols; j++) r->B[mat_idx(r, i, j)] = row->L[j]->b ? 1 : 0;
        }
        return r;
    }
    return NULL;
}
V *mat_matmul(V *a, V *b) {
    P(!is_mat_t(a->t) || !is_mat_t(b->t), v_err("mmul requires numeric matrices"))
    P(a->t == T_BMAT || b->t == T_BMAT, v_err("mmul not supported for matrix[bool]"))
    P(a->t == T_CMAT || b->t == T_CMAT, v_err("mmul not supported for matrix[char]"))
    P(mat_cols(a) != b->n, v_err("mmul shape mismatch"))
    int64_t m = a->n, k = mat_cols(a), n = mat_cols(b);
    int out_t = (a->t == T_FMAT || b->t == T_FMAT) ? T_FMAT : T_IMAT;
    V *r = out_t == T_FMAT ? v_fmat(m, n) : (V *)v_imat(m, n);
    if (out_t == T_FMAT && a->t == T_FMAT && b->t == T_FMAT) {
        mat_fmat_mul(r->F, a->F, b->F, m, k, n);
    } else if (out_t == T_IMAT && a->t == T_IMAT && b->t == T_IMAT) {
        mat_imat_mul(r->J, a->J, b->J, m, k, n);
    } else {
        mat_mul_mixed(r->F, r->J, a->J, a->F, b->J, b->F, m, k, n,
                      a->t == T_IMAT, b->t == T_IMAT, out_t == T_FMAT);
    }
    return r;
}
V *v_list(int64_t n) {
    V *v = x_calloc(1, sizeof(V), "v_list"); v->t = T_LIST; v->rc = 1; v->n = n;
    v->_ht_cap = n > 0 ? (int)n : 0;
    v->L = x_calloc(n > 0 ? (size_t)n : 1, sizeof(V*), "v_list");
    return v;
}
void v_list_append(V *v, V *item) {
    Pv(v->t != T_LIST)
    if (v->n >= v->_ht_cap) {
        /* _ht_cap is a signed int; guard the doubling so it can't overflow to a
         * negative/undefined value that would corrupt the realloc size. */
        if (v->_ht_cap > (1 << 30)) shakti_oom("v_list_append");
        int cap = v->_ht_cap ? v->_ht_cap * 2 : 8;
        v->L = x_realloc(v->L, (size_t)cap * sizeof(V*), "v_list_append");
        v->_ht_cap = cap;
    }
    v->L[v->n++] = v_ref(item);
}
V *v_dict(V *keys, V *vals) {
    V *v=v_alloc(T_DICT); v->n=keys->n;
    v->keys=v_ref(keys); v->vals=v_ref(vals);
    return v;
}
/* Build an empty dict with balanced refs on the temporary key/val lists. */
V *v_dict_empty(void) {
    V *k = v_list(0), *vl = v_list(0);
    V *d = v_dict(k, vl);
    v_free(k); v_free(vl);
    return d;
}
/* Like v_dict, but drops the caller's refs on keys/vals (for fresh construction). */
V *v_dict_own(V *keys, V *vals) {
    V *d = v_dict(keys, vals);
    v_free(keys); v_free(vals);
    return d;
}
V *v_table(V *cols, V *data) {
    int64_t n = 0;
    if (!data || data->t != T_LIST) return v_err("table: bad columns");
    if (data->n > 0) {
        if (!data->L || !data->L[0]) return v_err("table: ragged columns");
        n = data->L[0]->n;
        for (int64_t i = 1; i < data->n; i++) {
            if (!data->L[i] || data->L[i]->n != n)
                return v_err("table: ragged columns");
        }
    }
    V *v = v_alloc(T_TABLE);
    v->n = n;
    v->keys = v_ref(cols);
    v->vals = v_ref(data);
    return v;
}
/* Like v_table, but drops the caller's refs on cols/data (for fresh construction). */
V *v_table_own(V *cols, V *data) {
    V *t = v_table(cols, data);
    v_free(cols); v_free(data);
    return t;
}
void v_list_append_own(V *v, V *item) {
    v_list_append(v, item);
    v_free(item);
}
void v_dict_put(V *d, const char *key, V *val) {
    v_dict_set(d, key, val);
    v_free(val);
}
V *v_fn(V *params, V *defaults, Node *body_ast, Env *closure) {
    int idx = fn_ast_store(body_ast);
    if(idx < 0) return v_err("too many functions");
    V *v=v_alloc(T_FN);
    v->params = v_ref(params);
    v->defaults = defaults ? v_ref(defaults) : NULL;
    v->j = idx;
    v->closure = closure;
    if(closure) env_ref(closure);
    return v;
}
V *v_datetime(int64_t ms_utc) {
    V *v = v_alloc(T_DATETIME);
    v->j = ms_utc;
    return v;
}
int shakti_parse_datetime_ms(const char *s, int64_t *out_ms) {
    int y, M, d, H, m, S, ms;
    P(!s || !out_ms,0)
    P(strlen(s) < 23,0)
    P(s[4] != '.' || s[7] != '.' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != '.',0)
    P(sscanf(s, "%4d.%2d.%2dT%2d:%2d:%2d.%3d", &y, &M, &d, &H, &m, &S, &ms) != 7,0)
    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = y - 1900;
    tmv.tm_mon = M - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = H;
    tmv.tm_min = m;
    tmv.tm_sec = S;
    time_t sec = SHAKTI_TIMEGM(&tmv);
    P(sec == (time_t)-1,0)
    *out_ms = (int64_t)sec * 1000LL + (int64_t)ms;
    return 1;
}
void shakti_format_datetime_ms(int64_t ms, char *buf, size_t cap) {
    if(!buf || cap < 24) { if(buf && cap) buf[0] = 0; return; }
    int64_t sec = ms / 1000;
    int msec = (int)(ms % 1000);
    if(msec < 0) { msec += 1000; sec--; }
    time_t t = (time_t)sec;
    struct tm *u = gmtime(&t);
    if(!u) { buf[0] = 0; return; }
    snprintf(buf, cap, "%04d.%02d.%02dT%02d:%02d:%02d.%03d",
        u->tm_year + 1900, u->tm_mon + 1, u->tm_mday,
        u->tm_hour, u->tm_min, u->tm_sec, msec);
}
int shakti_parse_date_ymd(const char *s, int64_t *out_ms) {
    int y, M, d;
    P(!s || !out_ms,0)
    P(sscanf(s, "%4d-%2d-%2d", &y, &M, &d) != 3,0)
    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = y - 1900;
    tmv.tm_mon = M - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    time_t sec = SHAKTI_TIMEGM(&tmv);
    P(sec == (time_t)-1,0)
    *out_ms = (int64_t)sec * 1000LL;
    return 1;
}
void shakti_format_date_ms(int64_t utc_midnight_ms, char *buf, size_t cap) {
    if(!buf || cap < 12) { if(buf && cap) buf[0] = 0; return; }
    int64_t sec = utc_midnight_ms / 1000;
    time_t t = (time_t)sec;
    struct tm *u = gmtime(&t);
    if(!u) { buf[0] = 0; return; }
    snprintf(buf, cap, "%04d-%02d-%02d",
        u->tm_year + 1900, u->tm_mon + 1, u->tm_mday);
}
void shakti_format_time_ms(int64_t ms_in_day, char *buf, size_t cap) {
    if(!buf || cap < 16) { if(buf && cap) buf[0] = 0; return; }
    int64_t x = ms_in_day % 86400000LL;
    if(x < 0) x += 86400000LL;
    int64_t H = x / 3600000LL;
    int64_t M = (x % 3600000LL) / 60000LL;
    int64_t S = (x % 60000LL) / 1000LL;
    int msec = (int)(x % 1000LL);
    snprintf(buf, cap, "%02lld:%02lld:%02lld.%03d",
        (long long)H, (long long)M, (long long)S, msec);
}
V *v_ref(V *v) { if(v) v->rc++; return v; }
void v_free(V *v) {
    Pv(!v)
    if (--v->rc > 0) return;
    if (v->t == T_INT && isl_int_cache && v->j >= 0 && v->j < ISL_INT_CACHE_MAX && isl_int_cache[v->j] == v) {
        v->rc = 1;
        return;
    }
    int mapped = (v->owner_kind == V_OWNER_MAP_ALIAS);
    switch(v->t) {
    case T_STR: case T_ERR: free(v->s); break;
    case T_IVEC: if (!mapped) free(v->J); break;
    case T_FVEC: if (!mapped) free(v->F); break;
    case T_BVEC: case T_CVEC: if (!mapped) free(v->B); break;
    case T_IMAT: if (!mapped) free(v->J); break;
    case T_FMAT: if (!mapped) free(v->F); break;
    case T_BMAT: case T_CMAT: if (!mapped) free(v->B); break;
    case T_LIST:
        for(int64_t i=0;i<v->n;i++) v_free(v->L[i]);
        free(v->L); break;
    case T_DICT: case T_TABLE:
        free(v->_ht); v_free(v->keys); v_free(v->vals); break;
    case T_FN:
        free(v->s); /* builtin-name wrappers (N_NAME) store strdup'd name here */
        v_free(v->params);
        if(v->defaults) v_free(v->defaults);
        if(v->closure) env_free(v->closure);
        break;
    case T_INPUT:
        free(v->s);
        break;
    case T_SUBPROCESS:
#ifndef _WIN32
        if (v->j >= 0) close((int)v->j);
        if (v->n > 1) {
            int wstatus;
            waitpid((pid_t)v->n, &wstatus, 0);
        }
#endif
        break;
    default: break;
    }
#ifdef SHAKTI_HAVE_IEFS
    if (mapped && v->map_reg)
        iefs_map_region_release((IefsMapRegion *)v->map_reg);
#endif
    free(v);
}

int v_ensure_writable(V *v) {
    if (!v)
        return 0;
    if (v->owner_kind != V_OWNER_MAP_ALIAS)
        return 0;
#ifdef SHAKTI_HAVE_IEFS
    if (iefs_v_materialize(v) != 0)
        return -1;
    return (v->owner_kind == V_OWNER_MALLOC) ? 0 : -1;
#else
    return -1;
#endif
}

const char *type_name(int t) {
    const char *names[] = {
        "NoneType", "bool", "int", "float", "str",
        "date",
        "error",
        "list[int]", "list[float]", "list[bool]",
        "list", "dict", "table", "function",
        "datetime",
        "time",
        "input_stream",
        "matrix[int]", "matrix[float]", "matrix[bool]",
        "subprocess",
        "char", "list[char]", "matrix[char]"
    };
    P(t >= 0 && t <= T_CMAT, names[t])
    return "unknown";
}
V *v_copy(V *v) {
    P(!v,v_nil())
    switch(v->t) {
    case T_NIL:   return v_nil();
    case T_BOOL:  return v_bool(v->b);
    case T_INT:   return v_int(v->j);
    case T_CHAR:  return v_char((unsigned char)v->j);
    case T_FLOAT: return v_float(v->f);
    case T_STR:   return v_str(v->s);
    case T_ERR:   return v_err(v->s);
    case T_DATE:  return v_date(v->j);
    case T_TIME:  return v_time(v->j);
    case T_IVEC:  { V *r=v_ivec(v->n); memcpy(r->J,v->J,v->n*8); return r; }
    case T_FVEC:  { V *r=v_fvec(v->n); memcpy(r->F,v->F,v->n*8); return r; }
    case T_BVEC:  { V *r=v_bvec(v->n); memcpy(r->B,v->B,v->n); return r; }
    case T_CVEC:  { V *r=v_cvec(v->n); memcpy(r->B,v->B,v->n); return r; }
    case T_IMAT:  { V *r=v_imat(v->n, mat_cols(v)); memcpy(r->J,v->J,(size_t)v->n*mat_cols(v)*8); return r; }
    case T_FMAT:  { V *r=v_fmat(v->n, mat_cols(v)); memcpy(r->F,v->F,(size_t)v->n*mat_cols(v)*8); return r; }
    case T_BMAT:  { V *r=v_bmat(v->n, mat_cols(v)); memcpy(r->B,v->B,(size_t)v->n*mat_cols(v)); return r; }
    case T_CMAT:  { V *r=v_cmat(v->n, mat_cols(v)); memcpy(r->B,v->B,(size_t)v->n*mat_cols(v)); return r; }
    case T_LIST:  {
        V *r=v_list(v->n);
        for(int64_t i=0;i<v->n;i++) r->L[i]=v_copy(v->L[i]);
        return r;
    }
    case T_DICT: {
        V *k=v_copy(v->keys), *vl=v_copy(v->vals);
        V *r=v_dict(k,vl); v_free(k); v_free(vl); return r;
    }
    case T_DATETIME: return v_datetime(v->j);
    case T_SUBPROCESS: return v_ref(v);
    case T_TABLE: {
        V *kc = v_copy(v->keys);
        V *vl = v_copy(v->vals);
        return v_table_own(kc, vl);
    }
    default: return v_ref(v);
    }
}
#define DICT_HT_EMPTY 0xFFFFFFFFu
#define DICT_HT_MIN   8
static void dict_ht_rebuild(V *d) {
    uint32_t cap = 16;
    W(cap < (uint32_t)d->n * 2,cap <<= 1)
    d->_ht = x_realloc(d->_ht, cap * sizeof(uint32_t), "dict_ht_rebuild");
    d->_ht_cap = cap;
    memset(d->_ht, 0xFF, cap * sizeof(uint32_t));
    uint32_t mask = cap - 1;
    for (int64_t i = 0; i < d->n; i++) {
        if (d->keys->L[i]->t != T_STR) continue;
        uint32_t slot = fnv1a(d->keys->L[i]->s) & mask;
        W(d->_ht[slot] != DICT_HT_EMPTY,slot = (slot + 1) & mask)
        d->_ht[slot] = (uint32_t)i;
    }
}
void v_dict_set(V *d, const char *key, V *val) {
    Pv(d->t != T_DICT)
    uint32_t h = fnv1a(key);
    if (d->_ht) {
        uint32_t mask = d->_ht_cap - 1, slot = h & mask;
        W(d->_ht[slot] != DICT_HT_EMPTY,{
            uint32_t idx = d->_ht[slot];
            if (d->keys->L[idx]->t == T_STR && strcmp(d->keys->L[idx]->s, key) == 0) {
                v_free(d->vals->L[idx]);
                d->vals->L[idx] = v_ref(val);
                return;
            }
            slot = (slot + 1) & mask;
        })
        d->keys->L = x_realloc(d->keys->L, (d->n + 1) * sizeof(V*), "v_dict_set");
        d->vals->L = x_realloc(d->vals->L, (d->n + 1) * sizeof(V*), "v_dict_set");
        d->keys->L[d->n] = v_str(key);
        d->vals->L[d->n] = v_ref(val);
        uint32_t new_idx = (uint32_t)d->n;
        d->n++; d->keys->n++; d->vals->n++;
        if (d->n * 4 > (int64_t)d->_ht_cap * 3) {
            dict_ht_rebuild(d);
        } else {
            d->_ht[slot] = new_idx;
        }
        return;
    }
    for (int64_t i = 0; i < d->n; i++) {
        if (d->keys->L[i]->t == T_STR && strcmp(d->keys->L[i]->s, key) == 0) {
            v_free(d->vals->L[i]);
            d->vals->L[i] = v_ref(val);
            return;
        }
    }
    d->keys->L = x_realloc(d->keys->L, (d->n + 1) * sizeof(V*), "v_dict_set");
    d->vals->L = x_realloc(d->vals->L, (d->n + 1) * sizeof(V*), "v_dict_set");
    d->keys->L[d->n] = v_str(key);
    d->vals->L[d->n] = v_ref(val);
    d->n++; d->keys->n++; d->vals->n++;
    if (d->n > DICT_HT_MIN) dict_ht_rebuild(d);
}
V *v_dict_get(V *d, const char *key) {
    P(d->t != T_DICT,NULL)
    if (d->_ht) {
        uint32_t mask = d->_ht_cap - 1, slot = fnv1a(key) & mask;
        W(d->_ht[slot] != DICT_HT_EMPTY,{
            uint32_t idx = d->_ht[slot];
            if (d->keys->L[idx]->t == T_STR && strcmp(d->keys->L[idx]->s, key) == 0)
                return d->vals->L[idx];
            slot = (slot + 1) & mask;
        })
        return NULL;
    }
    for (int64_t i = 0; i < d->n; i++)
        if (d->keys->L[i]->t == T_STR && strcmp(d->keys->L[i]->s, key) == 0)
            return d->vals->L[i];
    return NULL;
}
void v_serialize(V *v, FILE *fp) {
    if(!v) { fputc(T_NIL, fp); return; }
    fputc(v->t, fp);
    switch(v->t) {
    case T_BOOL:  fputc(v->b, fp); break;
    case T_INT:   fwrite(&v->j, 8, 1, fp); break;
    case T_CHAR:  fputc((unsigned char)v->j, fp); break;
    case T_FLOAT: fwrite(&v->f, 8, 1, fp); break;
    case T_STR: {
        int64_t len = strlen(v->s);
        fwrite(&len, 8, 1, fp);
        fwrite(v->s, 1, len, fp);
        break;
    }
    case T_DATE:
    case T_TIME:
    case T_DATETIME:
        fwrite(&v->j, 8, 1, fp);
        break;
    case T_IVEC: {
        fwrite(&v->n, 8, 1, fp);
        fwrite(v->J, 8, v->n, fp);
        break;
    }
    case T_FVEC: {
        fwrite(&v->n, 8, 1, fp);
        fwrite(v->F, 8, v->n, fp);
        break;
    }
    case T_BVEC: {
        fwrite(&v->n, 8, 1, fp);
        fwrite(v->B, 1, v->n, fp);
        break;
    }
    case T_CVEC: {
        fwrite(&v->n, 8, 1, fp);
        fwrite(v->B, 1, v->n, fp);
        break;
    }
    case T_IMAT:
    case T_FMAT:
    case T_BMAT:
    case T_CMAT: {
        int64_t cols = mat_cols(v);
        fwrite(&v->n, 8, 1, fp);
        fwrite(&cols, 8, 1, fp);
        if (v->t == T_IMAT) fwrite(v->J, 8, (size_t)(v->n * cols), fp);
        else if (v->t == T_FMAT) fwrite(v->F, 8, (size_t)(v->n * cols), fp);
        else fwrite(v->B, 1, (size_t)(v->n * cols), fp);
        break;
    }
    case T_LIST: {
        fwrite(&v->n, 8, 1, fp);
        for(int64_t i=0; i<v->n; i++) v_serialize(v->L[i], fp);
        break;
    }
    case T_DICT:
        v_serialize(v->keys, fp);
        v_serialize(v->vals, fp);
        break;
    case T_TABLE:
        v_serialize(v->keys, fp);
        v_serialize(v->vals, fp);
        break;
    }
}
static int deser_read_i64(FILE *fp, int64_t *out) {
    return fread(out, 8, 1, fp) == 1;
}
static int deser_len_ok(int64_t len) {
    return len >= 0 && len <= SHAKTI_DESER_MAX_LEN;
}
static int deser_count_ok(int64_t n) {
    return n >= 0 && n <= SHAKTI_DESER_MAX_LEN;
}
static int deser_mat_dims_ok(int64_t rows, int64_t cols, int64_t *cells_out) {
    if (rows < 0 || cols < 0 || rows > SHAKTI_DESER_MAX_LEN || cols > SHAKTI_DESER_MAX_LEN) return 0;
    if (rows == 0 || cols == 0) { *cells_out = 0; return 1; }
    if (rows > INT64_MAX / cols) return 0;
    *cells_out = rows * cols;
    return *cells_out <= SHAKTI_DESER_MAX_LEN;
}
static V *v_deserialize_depth(FILE *fp, int depth);

static V *v_deserialize_depth(FILE *fp, int depth) {
    int t = fgetc(fp);
    P(t == EOF || t == T_NIL, v_nil())
    P(depth >= SHAKTI_DESER_MAX_DEPTH, v_err("deserialize: nesting too deep"))
    switch (t) {
    case T_BOOL: {
        int b = fgetc(fp);
        P(b == EOF, v_err("deserialize: truncated bool"))
        return v_bool(b);
    }
    case T_INT: {
        int64_t j;
        P(!deser_read_i64(fp, &j), v_err("deserialize: truncated int"))
        return v_int(j);
    }
    case T_CHAR: {
        int b = fgetc(fp);
        P(b == EOF, v_err("deserialize: truncated char"))
        return v_char((unsigned char)b);
    }
    case T_FLOAT: {
        double f;
        P(fread(&f, 8, 1, fp) != 1, v_err("deserialize: truncated float"))
        return v_float(f);
    }
    case T_STR: {
        int64_t len;
        P(!deser_read_i64(fp, &len) || !deser_len_ok(len), v_err("deserialize: bad string length"))
        char *s = malloc((size_t)len + 1);
        P(!s, v_err("deserialize: oom"))
        if (len > 0 && fread(s, 1, (size_t)len, fp) != (size_t)len) {
            free(s);
            return v_err("deserialize: truncated string");
        }
        s[len] = 0;
        V *r = v_str(s);
        free(s);
        return r;
    }
    case T_DATE: {
        int64_t j;
        P(!deser_read_i64(fp, &j), v_err("deserialize: truncated date"))
        return v_date(j);
    }
    case T_TIME: {
        int64_t j;
        P(!deser_read_i64(fp, &j), v_err("deserialize: truncated time"))
        return v_time(j);
    }
    case T_DATETIME: {
        int64_t j;
        P(!deser_read_i64(fp, &j), v_err("deserialize: truncated datetime"))
        return v_datetime(j);
    }
    case T_IVEC: {
        int64_t n;
        P(!deser_read_i64(fp, &n) || !deser_count_ok(n), v_err("deserialize: bad ivec length"))
        V *r = v_ivec(n);
        if (n > 0 && fread(r->J, 8, (size_t)n, fp) != (size_t)n) { v_free(r); return v_err("deserialize: truncated ivec"); }
        return r;
    }
    case T_FVEC: {
        int64_t n;
        P(!deser_read_i64(fp, &n) || !deser_count_ok(n), v_err("deserialize: bad fvec length"))
        V *r = v_fvec(n);
        if (n > 0 && fread(r->F, 8, (size_t)n, fp) != (size_t)n) { v_free(r); return v_err("deserialize: truncated fvec"); }
        return r;
    }
    case T_BVEC: {
        int64_t n;
        P(!deser_read_i64(fp, &n) || !deser_count_ok(n), v_err("deserialize: bad bvec length"))
        V *r = v_bvec(n);
        if (n > 0 && fread(r->B, 1, (size_t)n, fp) != (size_t)n) { v_free(r); return v_err("deserialize: truncated bvec"); }
        return r;
    }
    case T_CVEC: {
        int64_t n;
        P(!deser_read_i64(fp, &n) || !deser_count_ok(n), v_err("deserialize: bad cvec length"))
        V *r = v_cvec(n);
        if (n > 0 && fread(r->B, 1, (size_t)n, fp) != (size_t)n) { v_free(r); return v_err("deserialize: truncated cvec"); }
        return r;
    }
    case T_IMAT: {
        int64_t rows, cols, cells;
        P(!deser_read_i64(fp, &rows) || !deser_read_i64(fp, &cols) || !deser_mat_dims_ok(rows, cols, &cells),
          v_err("deserialize: bad imat shape"))
        V *r = v_imat(rows, cols);
        if (cells > 0 && fread(r->J, 8, (size_t)cells, fp) != (size_t)cells) { v_free(r); return v_err("deserialize: truncated imat"); }
        return r;
    }
    case T_FMAT: {
        int64_t rows, cols, cells;
        P(!deser_read_i64(fp, &rows) || !deser_read_i64(fp, &cols) || !deser_mat_dims_ok(rows, cols, &cells),
          v_err("deserialize: bad fmat shape"))
        V *r = v_fmat(rows, cols);
        if (cells > 0 && fread(r->F, 8, (size_t)cells, fp) != (size_t)cells) { v_free(r); return v_err("deserialize: truncated fmat"); }
        return r;
    }
    case T_BMAT: {
        int64_t rows, cols, cells;
        P(!deser_read_i64(fp, &rows) || !deser_read_i64(fp, &cols) || !deser_mat_dims_ok(rows, cols, &cells),
          v_err("deserialize: bad bmat shape"))
        V *r = v_bmat(rows, cols);
        if (cells > 0 && fread(r->B, 1, (size_t)cells, fp) != (size_t)cells) { v_free(r); return v_err("deserialize: truncated bmat"); }
        return r;
    }
    case T_CMAT: {
        int64_t rows, cols, cells;
        P(!deser_read_i64(fp, &rows) || !deser_read_i64(fp, &cols) || !deser_mat_dims_ok(rows, cols, &cells),
          v_err("deserialize: bad cmat shape"))
        V *r = v_cmat(rows, cols);
        if (cells > 0 && fread(r->B, 1, (size_t)cells, fp) != (size_t)cells) { v_free(r); return v_err("deserialize: truncated cmat"); }
        return r;
    }
    case T_LIST: {
        int64_t n;
        P(!deser_read_i64(fp, &n) || !deser_count_ok(n), v_err("deserialize: bad list length"))
        V *r = v_list(n);
        for (int64_t i = 0; i < n; i++) {
            r->L[i] = v_deserialize_depth(fp, depth + 1);
            if (r->L[i]->t == T_ERR) { V *err = r->L[i]; v_free(r); return err; }
        }
        return r;
    }
    case T_DICT: {
        V *k = v_deserialize_depth(fp, depth + 1);
        if (k->t == T_ERR) return k;
        V *v = v_deserialize_depth(fp, depth + 1);
        if (v->t == T_ERR) { v_free(k); return v; }
        V *r = v_dict(k, v);
        v_free(k);
        v_free(v);
        return r;
    }
    case T_TABLE: {
        V *k = v_deserialize_depth(fp, depth + 1);
        if (k->t == T_ERR) return k;
        V *v = v_deserialize_depth(fp, depth + 1);
        if (v->t == T_ERR) { v_free(k); return v; }
        V *r = v_table(k, v);
        v_free(k);
        v_free(v);
        return r;
    }
    }
    return v_nil();
}
V *v_deserialize(FILE *fp) {
    return v_deserialize_depth(fp, 0);
}
static void table_cell_buf(V *col, int64_t r, char *buf, size_t cap) {
    buf[0] = 0;
    if (!col || r < 0) return;
    if (col->t == T_IVEC && r < col->n) snprintf(buf, cap, "%lld", (long long)col->J[r]);
    else if (col->t == T_FVEC && r < col->n) snprintf(buf, cap, "%g", col->F[r]);
    else if (col->t == T_CVEC && r < col->n) snprintf(buf, cap, "0x%02x", (int)col->B[r]);
    else if (col->t == T_BVEC && r < col->n) snprintf(buf, cap, "%s", col->B[r] ? "True" : "False");
    else if (col->t == T_LIST && r < col->n) {
        V *el = col->L[r];
        if (el) { char *s = v_to_str(el); snprintf(buf, cap, "%s", s ? s : ""); free(s); }
        else snprintf(buf, cap, "None");
    } else if (is_mat_t(col->t) && r < col->n) {
        V *rw = v_mat_row(col, r);
        char *s = v_to_str(rw);
        snprintf(buf, cap, "%s", s ? s : "");
        free(s);
        v_free(rw);
    }
}
static void print_val_depth(V *v, FILE *fp, int repr_mode, int depth) {
    if (depth >= SHAKTI_PRINT_MAX_DEPTH) { fprintf(fp, "..."); return; }
    if(!v) { fprintf(fp, "None"); return; }
    switch(v->t) {
    case T_NIL:  fprintf(fp, "None"); break;
    case T_BOOL: fprintf(fp, "%s", v->b ? "True" : "False"); break;
    case T_INT:  fprintf(fp, "%lld", (long long)v->j); break;
    case T_CHAR: fprintf(fp, "0x%02x", (int)(unsigned char)v->j); break;
    case T_DATETIME: {
        char buf[32];
        shakti_format_datetime_ms(v->j, buf, sizeof buf);
        if(repr_mode) fprintf(fp, "\"%s\"", buf);
        else fprintf(fp, "%s", buf);
        break;
    }
    case T_DATE: {
        char buf[16];
        shakti_format_date_ms(v->j, buf, sizeof buf);
        if(repr_mode) fprintf(fp, "date(\"%s\")", buf);
        else fprintf(fp, "%s", buf);
        break;
    }
    case T_TIME: {
        char buf[20];
        shakti_format_time_ms(v->j, buf, sizeof buf);
        if(repr_mode) fprintf(fp, "time_ms(%lld)", (long long)v->j);
        else fprintf(fp, "%s", buf);
        break;
    }
    case T_FLOAT:{
        if(v->f == (int64_t)v->f && v->f < 1e15 && v->f > -1e15)
            fprintf(fp, "%.1f", v->f);
        else fprintf(fp, "%g", v->f);
        break;
    }
    case T_STR:
        if(repr_mode) fprintf(fp, "\"%s\"", v->s);
        else fprintf(fp, "%s", v->s);
        break;
    case T_ERR: fprintf(fp, "Error: %s", v->s); break;
    case T_IVEC:
        fprintf(fp, "[");
        for(int64_t i=0;i<v->n;i++) { if(i) fprintf(fp,", "); fprintf(fp,"%lld",(long long)v->J[i]); }
        fprintf(fp, "]"); break;
    case T_FVEC:
        fprintf(fp, "[");
        for(int64_t i=0;i<v->n;i++) {
            if(i) fprintf(fp,", ");
            double d = v->F[i];
            if(d == (int64_t)d && d < 1e15 && d > -1e15) fprintf(fp,"%.1f",d);
            else fprintf(fp,"%g",d);
        }
        fprintf(fp, "]"); break;
    case T_BVEC:
        fprintf(fp, "[");
        for(int64_t i=0;i<v->n;i++) { if(i) fprintf(fp,", "); fprintf(fp,"%s",v->B[i]?"True":"False"); }
        fprintf(fp, "]"); break;
    case T_CVEC:
        fprintf(fp, "[");
        for(int64_t i=0;i<v->n;i++) { if(i) fprintf(fp,", "); fprintf(fp,"0x%02x",(int)v->B[i]); }
        fprintf(fp, "]"); break;
    case T_IMAT:
    case T_FMAT:
    case T_BMAT:
    case T_CMAT:
        print_mat_val(v, fp, repr_mode);
        break;
    case T_LIST: {
        /* Fast path: homogeneous int/float lists avoid per-element recursion. */
        int all_int = v->n > 0, all_float = v->n > 0;
        for (int64_t i = 0; i < v->n && (all_int || all_float); i++) {
            if (!v->L[i] || v->L[i]->t != T_INT) all_int = 0;
            if (!v->L[i] || v->L[i]->t != T_FLOAT) all_float = 0;
        }
        fprintf(fp, "[");
        if (all_int) {
            for (int64_t i = 0; i < v->n; i++) {
                if (i) fprintf(fp, ", ");
                fprintf(fp, "%lld", (long long)v->L[i]->j);
            }
        } else if (all_float) {
            for (int64_t i = 0; i < v->n; i++) {
                if (i) fprintf(fp, ", ");
                double d = v->L[i]->f;
                if (d == (int64_t)d && d < 1e15 && d > -1e15) fprintf(fp, "%.1f", d);
                else fprintf(fp, "%g", d);
            }
        } else {
            for (int64_t i = 0; i < v->n; i++) {
                if (i) fprintf(fp, ", ");
                print_val_depth(v->L[i], fp, 1, depth + 1);
            }
        }
        fprintf(fp, "]");
        break;
    }
    case T_DICT:
        fprintf(fp, "{");
        for(int64_t i=0;i<v->n;i++) {
            if(i) fprintf(fp,", ");
            print_val_depth(v->keys->L[i],fp,1,depth+1);
            fprintf(fp,": ");
            print_val_depth(v->vals->L[i],fp,1,depth+1);
        }
        fprintf(fp, "}"); break;
    case T_TABLE: {
        V *cols = v->keys, *data = v->vals;
        int nc = cols->n;
        int64_t nr = v->n;
        int *widths = calloc(nc, sizeof(int));
        for(int c=0;c<nc;c++) {
            int w = strlen(cols->L[c]->s);
            V *col = data->L[c];
            for(int64_t r=0;r<nr && r<20;r++) {
                char buf[64];
                table_cell_buf(col, r, buf, sizeof buf);
                int l=strlen(buf); if(l>w) w=l;
            }
            widths[c]=w;
        }
        for(int c=0;c<nc;c++) fprintf(fp, "%-*s  ", widths[c], cols->L[c]->s);
        fprintf(fp,"\n");
        for(int c=0;c<nc;c++) { for(int i=0;i<widths[c];i++) fputc('-',fp); fprintf(fp,"  "); }
        fprintf(fp,"\n");
        for(int64_t r=0;r<nr && r<20;r++) {
            for(int c=0;c<nc;c++) {
                V *col=data->L[c]; char buf[64];
                table_cell_buf(col, r, buf, sizeof buf);
                fprintf(fp,"%-*s  ",widths[c],buf);
            }
            fprintf(fp,"\n");
        }
        if(nr>20) fprintf(fp,"... %lld more rows\n",(long long)(nr-20));
        free(widths); break;
    }
    case T_FN: fprintf(fp, "<fn>"); break;
    case T_INPUT: fprintf(fp, "<input>"); break;
    case T_SUBPROCESS: fprintf(fp, "<proc>"); break;
    }
}
void print_val(V *v, FILE *fp, int repr_mode) {
    print_val_depth(v, fp, repr_mode, 0);
}
void v_print(V *v, int nl) { print_val(v, stdout, 0); if(nl) putchar('\n'); }

/* Fast path: format a homogeneous int list / ivec without OPEN_MEMSTREAM. */
static char *v_repr_int_seq(int64_t n, int64_t (*at)(void *, int64_t), void *ctx) {
    /* Worst case: each int is 20 digits + ", " (2) + brackets. */
    size_t cap = (size_t)n * 22 + 3;
    if (n == 0) cap = 3;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;
    buf[pos++] = '[';
    for (int64_t i = 0; i < n; i++) {
        if (i) {
            buf[pos++] = ',';
            buf[pos++] = ' ';
        }
        int w = snprintf(buf + pos, cap - pos, "%lld", (long long)at(ctx, i));
        if (w < 0 || (size_t)w >= cap - pos) {
            free(buf);
            return NULL;
        }
        pos += (size_t)w;
    }
    if (pos + 1 >= cap) {
        free(buf);
        return NULL;
    }
    buf[pos++] = ']';
    buf[pos] = 0;
    return buf;
}
static int64_t repr_ivec_at(void *ctx, int64_t i) { return ((V *)ctx)->J[i]; }
static int64_t repr_list_int_at(void *ctx, int64_t i) { return ((V *)ctx)->L[i]->j; }

char *v_repr(V *v) {
    if (v && v->t == T_IVEC) {
        char *r = v_repr_int_seq(v->n, repr_ivec_at, v);
        if (r) return r;
    }
    if (v && v->t == T_LIST && v->n > 0) {
        int all_int = 1;
        for (int64_t i = 0; i < v->n; i++) {
            if (!v->L[i] || v->L[i]->t != T_INT) { all_int = 0; break; }
        }
        if (all_int) {
            char *r = v_repr_int_seq(v->n, repr_list_int_at, v);
            if (r) return r;
        }
    }
    char *buf = NULL; size_t sz = 0;
    FILE *fp = OPEN_MEMSTREAM(&buf, &sz);
    print_val(v, fp, 1);
    CLOSE_MEMSTREAM(fp, &buf, &sz);
    return buf;
}
char *v_to_str(V *v) {
    char *buf = NULL; size_t sz = 0;
    FILE *fp = OPEN_MEMSTREAM(&buf, &sz);
    print_val(v, fp, 0);
    CLOSE_MEMSTREAM(fp, &buf, &sz);
    return buf;
}
