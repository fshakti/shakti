#include "shakti.h"
#include "input.h"
#include "mat_simd.h"
#if defined __has_include
#if __has_include("shakti_version.h")
#include "shakti_version.h"
#endif
#endif
#ifndef SHAKTI_PKG_VERSION
#define SHAKTI_PKG_VERSION "0.8.2"
#endif
#if defined(_WIN32) && defined(_MSC_VER)
#include <io.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#else
#include <unistd.h>
#endif
#include <time.h>

#ifdef _WIN32
FILE *win_open_memstream(char **ptr, size_t *sizeloc) {
    if(ptr) *ptr = NULL;
    if(sizeloc) *sizeloc = 0;
    return tmpfile();
}
void win_close_memstream(FILE *fp, char **ptr, size_t *sizeloc) {
    Pv(!fp)
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(sz+1);
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    if(ptr) *ptr = buf;
    if(sizeloc) *sizeloc = sz;
    fclose(fp);
}
#endif
#ifdef _WIN32
#define SHAKTI_TIMEGM(tm) _mkgmtime(tm)
#else
#define SHAKTI_TIMEGM(tm) timegm(tm)
#endif
Node *fn_ast[MAX_FN];
int   fn_ast_n = 0;
int   g_returning  = 0;
int   g_breaking   = 0;
int   g_continuing = 0;
int   g_error      = 0;
V    *g_retval     = NULL;
V    *g_error_val  = NULL;
char  g_lib_path[4096] = "";
char  g_script_dir[4096] = ".";
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h ? h : 1;
}
static V *v_alloc(int t) {
    V *v = calloc(1, sizeof(V));
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
V *v_str(const char *s)  { V *v=v_alloc(T_STR);  v->s=strdup(s); return v; }
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
V *v_err(const char *s)  { V *v=v_alloc(T_ERR);  v->s=strdup(s); return v; }
V *v_errf(const char *fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap,fmt);
    vsnprintf(buf,sizeof(buf),fmt,ap);
    va_end(ap);
    return v_err(buf);
}
V *v_ivec(int64_t n) {
    V *v=v_alloc(T_IVEC); v->n=n;
    v->_ht_cap = n > 0 ? (int)n : 0;
    v->J = malloc((size_t)(n > 0 ? n : 1) * sizeof(int64_t));
    return v;
}
V *v_fvec(int64_t n) {
    V *v=v_alloc(T_FVEC); v->n=n;
    v->F = calloc(n?n:1, sizeof(double));
    return v;
}
V *v_bvec(int64_t n) {
    V *v=v_alloc(T_BVEC); v->n=n;
    v->B = calloc(n?n:1, 1);
    return v;
}
V *v_imat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_IMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz = rows * (cols > 0 ? cols : 1);
    v->J = calloc((size_t)sz, sizeof(int64_t));
    return v;
}
V *v_fmat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_FMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz = rows * (cols > 0 ? cols : 1);
    v->F = calloc((size_t)sz, sizeof(double));
    return v;
}
V *v_bmat(int64_t rows, int64_t cols) {
    V *v = v_alloc(T_BMAT);
    v->n = rows;
    v->_ht_cap = (uint32_t)(cols > 0 ? cols : 0);
    int64_t sz = rows * (cols > 0 ? cols : 1);
    v->B = calloc((size_t)sz, 1);
    return v;
}
static int is_mat_t(int t) { return t == T_IMAT || t == T_FMAT || t == T_BMAT; }
static void mat_cell_format(V *v, int64_t r, int64_t c, char *buf, size_t cap) {
    if (v->t == T_IMAT)
        snprintf(buf, cap, "%lld", (long long)v->J[mat_idx(v, r, c)]);
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
    V *rv = v_bvec(cols);
    memcpy(rv->B, m->B + mat_idx(m, r, 0), (size_t)cols);
    return rv;
}
static V *try_promote_matrix(V **elems, int nch) {
    if (nch <= 0) return NULL;
    int64_t cols = -1;
    int all_int = 1, all_num = 1, all_bool = 1;
    for (int i = 0; i < nch; i++) {
        V *row = elems[i];
        int64_t row_len = 0;
        if (row->t == T_IVEC) {
            row_len = row->n;
        } else if (row->t == T_FVEC) {
            row_len = row->n; all_int = 0;
        } else if (row->t == T_BVEC) {
            row_len = row->n; all_int = 0; all_num = 0;
        } else if (row->t == T_LIST) {
            row_len = row->n;
            for (int64_t j = 0; j < row->n; j++) {
                if (elems[i]->L[j]->t != T_INT) all_int = 0;
                if (elems[i]->L[j]->t != T_INT && elems[i]->L[j]->t != T_FLOAT) all_num = 0;
                if (elems[i]->L[j]->t != T_BOOL) all_bool = 0;
            }
        } else return NULL;
        if (cols < 0) cols = row_len;
        else if (cols != row_len) return NULL;
    }
    if (all_int) {
        V *r = v_imat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_IVEC) memcpy(r->J + mat_idx(r, i, 0), row->J, (size_t)cols * 8);
            else for (int64_t j = 0; j < cols; j++) r->J[mat_idx(r, i, j)] = row->L[j]->j;
        }
        return r;
    }
    if (all_num) {
        V *r = v_fmat(nch, cols);
        for (int i = 0; i < nch; i++) {
            V *row = elems[i];
            if (row->t == T_IVEC) for (int64_t j = 0; j < cols; j++) r->F[mat_idx(r, i, j)] = (double)row->J[j];
            else if (row->t == T_FVEC) memcpy(r->F + mat_idx(r, i, 0), row->F, (size_t)cols * 8);
            else for (int64_t j = 0; j < cols; j++) {
                V *e = row->L[j];
                r->F[mat_idx(r, i, j)] = e->t == T_INT ? (double)e->j : e->f;
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
static V *mat_matmul(V *a, V *b) {
    P(!is_mat_t(a->t) || !is_mat_t(b->t), v_err("matmul requires numeric matrices"))
    P(a->t == T_BMAT || b->t == T_BMAT, v_err("matmul not supported for matrix[bool]"))
    P(mat_cols(a) != b->n, v_err("matmul shape mismatch"))
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
    V *v = calloc(1, sizeof(V)); v->t = T_LIST; v->rc = 1; v->n = n;
    v->_ht_cap = n > 0 ? (int)n : 0;
    v->L = calloc(n > 0 ? (size_t)n : 1, sizeof(V*));
    return v;
}
void v_list_append(V *v, V *item) {
    Pv(v->t != T_LIST)
    if (v->n >= v->_ht_cap) {
        int cap = v->_ht_cap ? v->_ht_cap * 2 : 8;
        v->L = realloc(v->L, (size_t)cap * sizeof(V*));
        v->_ht_cap = cap;
    }
    v->L[v->n++] = v_ref(item);
}
V *v_dict(V *keys, V *vals) {
    V *v=v_alloc(T_DICT); v->n=keys->n;
    v->keys=v_ref(keys); v->vals=v_ref(vals);
    return v;
}
V *v_table(V *cols, V *data) {
    V *v=v_alloc(T_TABLE); v->n = (data->n>0 && data->L[0]) ? data->L[0]->n : 0;
    v->keys=v_ref(cols); v->vals=v_ref(data);
    return v;
}
V *v_fn(V *params, V *defaults, Node *body_ast, Env *closure) {
    V *v=v_alloc(T_FN);
    v->params = v_ref(params);
    v->defaults = defaults ? v_ref(defaults) : NULL;
    int idx = fn_ast_store(body_ast);
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
    struct tm tmv = {};
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
    struct tm tmv = {};
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
    switch(v->t) {
    case T_STR: case T_ERR: free(v->s); break;
    case T_IVEC: free(v->J); break;
    case T_FVEC: free(v->F); break;
    case T_BVEC: free(v->B); break;
    case T_IMAT: free(v->J); break;
    case T_FMAT: free(v->F); break;
    case T_BMAT: free(v->B); break;
    case T_LIST:
        for(int64_t i=0;i<v->n;i++) v_free(v->L[i]);
        free(v->L); break;
    case T_DICT: case T_TABLE:
        free(v->_ht); v_free(v->keys); v_free(v->vals); break;
    case T_FN:
        v_free(v->params);
        if(v->defaults) v_free(v->defaults);
        if(v->closure) env_free(v->closure);
        break;
    case T_INPUT:
        free(v->s);
        break;
    default: break;
    }
    free(v);
}
int fn_ast_store(Node *n) {
    if(fn_ast_n >= MAX_FN) { fprintf(stderr,"too many functions\n"); exit(1); }
    fn_ast[fn_ast_n] = n;
    return fn_ast_n++;
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
        "matrix[int]", "matrix[float]", "matrix[bool]"
    };
    P(t >= 0 && t <= T_BMAT, names[t])
    return "unknown";
}
V *v_copy(V *v) {
    P(!v,v_nil())
    switch(v->t) {
    case T_NIL:   return v_nil();
    case T_BOOL:  return v_bool(v->b);
    case T_INT:   return v_int(v->j);
    case T_FLOAT: return v_float(v->f);
    case T_STR:   return v_str(v->s);
    case T_ERR:   return v_err(v->s);
    case T_DATE:  return v_date(v->j);
    case T_TIME:  return v_time(v->j);
    case T_IVEC:  { V *r=v_ivec(v->n); memcpy(r->J,v->J,v->n*8); return r; }
    case T_FVEC:  { V *r=v_fvec(v->n); memcpy(r->F,v->F,v->n*8); return r; }
    case T_BVEC:  { V *r=v_bvec(v->n); memcpy(r->B,v->B,v->n); return r; }
    case T_IMAT:  { V *r=v_imat(v->n, mat_cols(v)); memcpy(r->J,v->J,(size_t)v->n*mat_cols(v)*8); return r; }
    case T_FMAT:  { V *r=v_fmat(v->n, mat_cols(v)); memcpy(r->F,v->F,(size_t)v->n*mat_cols(v)*8); return r; }
    case T_BMAT:  { V *r=v_bmat(v->n, mat_cols(v)); memcpy(r->B,v->B,(size_t)v->n*mat_cols(v)); return r; }
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
    case T_TABLE: {
        V *kc = v_copy(v->keys);
        V *vl = v_copy(v->vals);
        return v_table(kc, vl);
    }
    default: return v_ref(v);
    }
}
#define DICT_HT_EMPTY 0xFFFFFFFFu
#define DICT_HT_MIN   8
static void dict_ht_rebuild(V *d) {
    uint32_t cap = 16;
    W(cap < (uint32_t)d->n * 2,cap <<= 1)
    d->_ht = realloc(d->_ht, cap * sizeof(uint32_t));
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
        d->keys->L = realloc(d->keys->L, (d->n + 1) * sizeof(V*));
        d->vals->L = realloc(d->vals->L, (d->n + 1) * sizeof(V*));
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
    d->keys->L = realloc(d->keys->L, (d->n + 1) * sizeof(V*));
    d->vals->L = realloc(d->vals->L, (d->n + 1) * sizeof(V*));
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
Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->rc = 1; e->cap = 16; e->len = 0;
    e->names  = calloc(16, sizeof(char*));
    e->vals   = calloc(16, sizeof(V*));
    e->hashes = calloc(16, sizeof(uint32_t));
    e->parent = parent;
    if(parent) parent->rc++;
    return e;
}
void env_set(Env *e, const char *name, V *val) {
    uint32_t h = fnv1a(name);
    for(int i=0; i<e->len; i++) {
        if(e->hashes[i] == h && strcmp(e->names[i], name)==0) {
            v_free(e->vals[i]);
            e->vals[i] = v_ref(val);
            return;
        }
    }
    if(e->len >= e->cap) {
        e->cap *= 2;
        e->names  = realloc(e->names,  e->cap * sizeof(char*));
        e->vals   = realloc(e->vals,   e->cap * sizeof(V*));
        e->hashes = realloc(e->hashes, e->cap * sizeof(uint32_t));
    }
    e->names[e->len]  = strdup(name);
    e->vals[e->len]   = v_ref(val);
    e->hashes[e->len] = h;
    e->len++;
}
void env_set_local(Env *e, const char *name, V *val) {
    env_set(e, name, val);
}
int env_update(Env *e, const char *name, V *val) {
    uint32_t h = fnv1a(name);
    for(; e; e=e->parent)
        for(int i=0; i<e->len; i++)
            if(e->hashes[i] == h && strcmp(e->names[i], name)==0) {
                if (e->vals[i] != val) {
                    v_free(e->vals[i]);
                    e->vals[i] = v_ref(val);
                }
                return 1;
            }
    return 0;
}
V *env_get(Env *e, const char *name) {
    uint32_t h = fnv1a(name);
    for(; e; e=e->parent)
        for(int i=0; i<e->len; i++)
            if(e->hashes[i] == h && strcmp(e->names[i], name)==0)
                return e->vals[i];
    return NULL;
}
void env_ref(Env *e) { if(e) e->rc++; }
void env_free(Env *e) {
    Pv(!e)
    Pv(--e->rc > 0)
    i(e->len,{free(e->names[i]); v_free(e->vals[i]);})
    free(e->names); free(e->vals); free(e->hashes);
    if(e->parent) env_free(e->parent);
    free(e);
}
void v_serialize(V *v, FILE *fp) {
    if(!v) { fputc(T_NIL, fp); return; }
    fputc(v->t, fp);
    switch(v->t) {
    case T_BOOL:  fputc(v->b, fp); break;
    case T_INT:   fwrite(&v->j, 8, 1, fp); break;
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
    case T_IMAT:
    case T_FMAT:
    case T_BMAT: {
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
V *v_deserialize(FILE *fp) {
    int t = fgetc(fp);
    P(t == EOF || t == T_NIL,v_nil())
    switch(t) {
    case T_BOOL:  return v_bool(fgetc(fp));
    case T_INT:   { int64_t j; fread(&j, 8, 1, fp); return v_int(j); }
    case T_FLOAT: { double f; fread(&f, 8, 1, fp); return v_float(f); }
    case T_STR: {
        int64_t len; fread(&len, 8, 1, fp);
        char *s = malloc(len+1); fread(s, 1, len, fp); s[len]=0;
        V *r = v_str(s); free(s); return r;
    }
    case T_DATE: {
        int64_t j; fread(&j, 8, 1, fp); return v_date(j);
    }
    case T_TIME: {
        int64_t j; fread(&j, 8, 1, fp); return v_time(j);
    }
    case T_DATETIME: {
        int64_t j; fread(&j, 8, 1, fp); return v_datetime(j);
    }
    case T_IVEC: {
        int64_t n; fread(&n, 8, 1, fp);
        V *r = v_ivec(n); fread(r->J, 8, n, fp); return r;
    }
    case T_FVEC: {
        int64_t n; fread(&n, 8, 1, fp);
        V *r = v_fvec(n); fread(r->F, 8, n, fp); return r;
    }
    case T_BVEC: {
        int64_t n; fread(&n, 8, 1, fp);
        V *r = v_bvec(n); fread(r->B, 1, n, fp); return r;
    }
    case T_IMAT: {
        int64_t rows, cols; fread(&rows, 8, 1, fp); fread(&cols, 8, 1, fp);
        V *r = v_imat(rows, cols);
        if (rows * cols > 0) fread(r->J, 8, (size_t)(rows * cols), fp);
        return r;
    }
    case T_FMAT: {
        int64_t rows, cols; fread(&rows, 8, 1, fp); fread(&cols, 8, 1, fp);
        V *r = v_fmat(rows, cols);
        if (rows * cols > 0) fread(r->F, 8, (size_t)(rows * cols), fp);
        return r;
    }
    case T_BMAT: {
        int64_t rows, cols; fread(&rows, 8, 1, fp); fread(&cols, 8, 1, fp);
        V *r = v_bmat(rows, cols);
        if (rows * cols > 0) fread(r->B, 1, (size_t)(rows * cols), fp);
        return r;
    }
    case T_LIST: {
        int64_t n; fread(&n, 8, 1, fp);
        V *r = v_list(n);
        for(int64_t i=0; i<n; i++) r->L[i] = v_deserialize(fp);
        return r;
    }
    case T_DICT: {
        V *k = v_deserialize(fp), *v = v_deserialize(fp);
        V *r = v_dict(k,v); v_free(k); v_free(v); return r;
    }
    case T_TABLE: {
        V *k = v_deserialize(fp), *v = v_deserialize(fp);
        V *r = v_table(k,v); v_free(k); v_free(v); return r;
    }
    }
    return v_nil();
}
int env_save(Env *e, const char *path) {
    FILE *fp = fopen(path, "wb");
    P(!fp,0)
    fwrite("KAIO_MCP", 1, 8, fp);
    fwrite(&e->len, 4, 1, fp);
    i(e->len,{
        int nlen = strlen(e->names[i]);
        fwrite(&nlen, 4, 1, fp);
        fwrite(e->names[i], 1, nlen, fp);
        v_serialize(e->vals[i], fp);
    })
    fclose(fp);
    return 1;
}
int env_load(Env *e, const char *path) {
    FILE *fp = fopen(path, "rb");
    P(!fp,0)
    char hdr[8]; fread(hdr, 1, 8, fp);
    if(memcmp(hdr, "KAIO_MCP", 8)) { fclose(fp); return 0; }
    int count; fread(&count, 4, 1, fp);
    i(count,{
        int nlen; fread(&nlen, 4, 1, fp);
        char *name = malloc(nlen+1); fread(name, 1, nlen, fp); name[nlen]=0;
        V *val = v_deserialize(fp);
        env_set(e, name, val);
        v_free(val); free(name);
    })
    fclose(fp);
    return 1;
}
static void print_val(V *v, FILE *fp, int repr_mode) {
    if(!v) { fprintf(fp, "None"); return; }
    switch(v->t) {
    case T_NIL:  fprintf(fp, "None"); break;
    case T_BOOL: fprintf(fp, "%s", v->b ? "True" : "False"); break;
    case T_INT:  fprintf(fp, "%lld", (long long)v->j); break;
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
    case T_IMAT:
    case T_FMAT:
    case T_BMAT:
        print_mat_val(v, fp, repr_mode);
        break;
    case T_LIST:
        fprintf(fp, "[");
        for(int64_t i=0;i<v->n;i++) { if(i) fprintf(fp,", "); print_val(v->L[i],fp,1); }
        fprintf(fp, "]"); break;
    case T_DICT:
        fprintf(fp, "{");
        for(int64_t i=0;i<v->n;i++) {
            if(i) fprintf(fp,", ");
            print_val(v->keys->L[i],fp,1);
            fprintf(fp,": ");
            print_val(v->vals->L[i],fp,1);
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
                if(col->t==T_IVEC) snprintf(buf,64,"%lld",(long long)col->J[r]);
                else if(col->t==T_FVEC) snprintf(buf,64,"%g",col->F[r]);
                else if(col->t==T_LIST) {
                    V *el=col->L[r];
                    if(el) {
                        char *s=v_to_str(el);
                        snprintf(buf,64,"%s",s);
                        free(s);
                    } else snprintf(buf,64,"None");
                }
                else buf[0]=0;
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
                if(col->t==T_IVEC) snprintf(buf,64,"%lld",(long long)col->J[r]);
                else if(col->t==T_FVEC) snprintf(buf,64,"%g",col->F[r]);
                else if(col->t==T_LIST) {
                    V *el=col->L[r];
                    if(el) {
                        char *s = v_to_str(el);
                        snprintf(buf,64,"%s",s);
                        free(s);
                    } else snprintf(buf,64,"None");
                }
                else buf[0]=0;
                fprintf(fp,"%-*s  ",widths[c],buf);
            }
            fprintf(fp,"\n");
        }
        if(nr>20) fprintf(fp,"... %lld more rows\n",(long long)(nr-20));
        free(widths); break;
    }
    case T_FN: fprintf(fp, "<fn>"); break;
    case T_INPUT: fprintf(fp, "<input>"); break;
    }
}
void v_print(V *v, int nl) { print_val(v, stdout, 0); if(nl) putchar('\n'); }
char *v_repr(V *v) {
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
static int is_id_start(char c) { return isalpha(c) || c=='_'; }
static int is_id_char(char c)  { return isalnum(c) || c=='_'; }
void lex_init(Lexer *l, const char *src) {
    memset(l, 0, sizeof(*l));
    l->src = src; l->len = strlen(src);
    l->indent_stack[0] = 0; l->indent_top = 0;
    l->at_line_start = 1;
}
static void skip_comment(Lexer *l) {
    W(l->pos < l->len && l->src[l->pos] != '\n',l->pos++)
}
static Token make_tok(int type) { Token t = {0}; t.type = type; return t; }

/* Update noun_pos after emitting a token (kparser-style left context). */
static void lex_note_token(Lexer *l, Token t) {
    switch (t.type) {
    case T_INT_: case T_FLOAT_: case T_STR_: case T_FSTR_: case T_DATETIME_:
    case T_TRUE_: case T_FALSE_: case T_NONE_: case T_NAME_:
    case T_RPAREN_: case T_RBRACKET_: case T_RBRACE_:
        l->noun_pos = 1;
        break;
    default:
        l->noun_pos = 0;
        break;
    }
}

static int lex_src_is_digit_start(const Lexer *l, size_t p) {
    if (p >= l->len) return 0;
    if (isdigit((unsigned char)l->src[p])) return 1;
    return l->src[p] == '.' && p + 1 < l->len && isdigit((unsigned char)l->src[p + 1]);
}

static int lex_peek_is_signed_literal(Lexer *l) {
    if (!l->has_peek)
        lex_peek(l);
    if (l->peek.type != T_MINUS_)
        return 0;
    size_t p = l->pos;
    while (p < l->len && l->src[p] == ' ') p++;
    return lex_src_is_digit_start(l, p);
}

static Token lex_fstring(Lexer *l) {
    const char *s = l->src;
    int p = l->pos;
    char q = s[p]; p++;
    Token t = {T_FSTR_};
    int qi = 0;
    while(p < l->len && s[p] != q) {
        if(s[p]=='{' && p+1<l->len && s[p+1]=='{') {
            t.sval[qi++] = '{'; p += 2;
        } else if(s[p]=='}' && p+1<l->len && s[p+1]=='}') {
            t.sval[qi++] = '}'; p += 2;
        } else if(s[p]=='{') {
            t.sval[qi++] = '{'; p++;
            int depth = 1;
            while(p < l->len && depth > 0) {
                if(s[p]=='{') depth++;
                else if(s[p]=='}') { depth--; if(depth==0) break; }
                t.sval[qi++] = s[p]; p++;
            }
            if(p < l->len) { t.sval[qi++] = '}'; p++; }
        } else if(s[p]=='\\' && p+1<l->len) {
            p++;
            switch(s[p]) {
            case 'n': t.sval[qi++]='\n'; break;
            case 't': t.sval[qi++]='\t'; break;
            case '\\': t.sval[qi++]='\\'; break;
            case '\'': t.sval[qi++]='\''; break;
            case '"': t.sval[qi++]='"'; break;
            default: t.sval[qi++]=s[p]; break;
            }
            p++;
        } else {
            t.sval[qi++] = s[p]; p++;
        }
    }
    t.sval[qi] = 0;
    if(p < l->len) p++;
    l->pos = p;
    return t;
}
static Token lex_raw(Lexer *l) {
    const char *s = l->src;
    int p = l->pos;
    if(l->pending_dedents > 0) {
        l->pending_dedents--;
        return make_tok(T_DEDENT_);
    }
    if(l->emit_newline) {
        l->emit_newline = 0;
        return make_tok(T_NEWLINE_);
    }
    if(l->at_line_start) {
        int indent = 0;
        while(p < l->len && s[p]==' ') { indent++; p++; }
        if(p < l->len && s[p]=='\t') {
            while(p < l->len && s[p]=='\t') { indent+=4; p++; }
            while(p < l->len && s[p]==' ') { indent++; p++; }
        }
        l->pos = p;
        l->at_line_start = 0;
        if(p >= l->len || s[p]=='\n' || s[p]=='#') {
            if(p < l->len && s[p]=='#') skip_comment(l);
            if(l->pos < l->len && s[l->pos]=='\n') { l->pos++; l->at_line_start=1; }
            return lex_raw(l);
        }
        P(l->paren_depth > 0,lex_raw(l))
        int cur = l->indent_stack[l->indent_top];
        if(indent > cur) {
            l->indent_stack[++l->indent_top] = indent;
            return make_tok(T_INDENT_);
        } else if(indent < cur) {
            while(l->indent_top > 0 && l->indent_stack[l->indent_top] > indent) {
                l->indent_top--;
                l->pending_dedents++;
            }
            l->pending_dedents--;
            return make_tok(T_DEDENT_);
        }
    }
    W(p < l->len && s[p]==' ',p++)
    l->pos = p;
    if(p >= l->len) {
        if(l->indent_top > 0) { l->indent_top--; l->pending_dedents = l->indent_top; return make_tok(T_DEDENT_); }
        return make_tok(T_EOF_);
    }
    char c = s[p];
    if(c == '\n') {
        l->pos = p+1;
        l->at_line_start = 1;
        P(l->paren_depth > 0,lex_raw(l))
        return make_tok(T_NEWLINE_);
    }
    if(c == '#') { skip_comment(l); return lex_raw(l); }
    if((c=='"'||c=='\'') && p+2<l->len && s[p+1]==c && s[p+2]==c) {
        Token t = {T_STR_}; int qi = 0;
        char q = c; p += 3;
        while(p+2 < l->len && !(s[p]==q && s[p+1]==q && s[p+2]==q)) {
            if(s[p]=='\\' && p+1<l->len) {
                p++;
                switch(s[p]) {
                case 'n': t.sval[qi++]='\n'; break;
                case 't': t.sval[qi++]='\t'; break;
                case '\\': t.sval[qi++]='\\'; break;
                default: t.sval[qi++]=s[p]; break;
                }
            } else t.sval[qi++] = s[p];
            p++;
        }
        t.sval[qi] = 0;
        if(p+2 < l->len) p += 3;
        l->pos = p;
        return t;
    }
    if(c == '"' || c == '\'') {
        Token t = {T_STR_}; int qi = 0;
        char q = c; p++;
        while(p < l->len && s[p] != q) {
            if(s[p]=='\\' && p+1<l->len) {
                p++;
                switch(s[p]) {
                case 'n': t.sval[qi++]='\n'; break;
                case 't': t.sval[qi++]='\t'; break;
                case '\\': t.sval[qi++]='\\'; break;
                case '\'': t.sval[qi++]='\''; break;
                case '"': t.sval[qi++]='"'; break;
                case 'r': t.sval[qi++]='\r'; break;
                case '0': t.sval[qi++]='\0'; break;
                default: t.sval[qi++]='\\'; t.sval[qi++]=s[p]; break;
                }
            } else t.sval[qi++] = s[p];
            p++;
        }
        t.sval[qi] = 0;
        if(p < l->len) p++;
        l->pos = p;
        return t;
    }
    if(isdigit((unsigned char)c) && p + 23 <= l->len) {
        char tmp[32];
        memcpy(tmp, s + p, 23);
        tmp[23] = 0;
        int64_t ms;
        if(shakti_parse_datetime_ms(tmp, &ms)) {
            Token t = {T_DATETIME_};
            t.ival = ms;
            t.line = l->line;
            l->pos = p + 23;
            return t;
        }
    }
    /* Number (optional leading sign when not in noun context: -1, not a-1) */
    {
        int neg_lit = 0;
        if (c == '-' && !l->noun_pos && p + 1 < l->len && lex_src_is_digit_start(l, (size_t)(p + 1))) {
            neg_lit = 1;
            p++;
            c = s[p];
        }
        if (isdigit((unsigned char)c) || (c == '.' && p + 1 < l->len && isdigit((unsigned char)s[p + 1]))) {
        Token t = {T_INT_};
        int start = p;
        int is_float = 0;
        if(c=='0' && p+1<l->len && (s[p+1]=='x'||s[p+1]=='X')) {
            p += 2;
            W(p<l->len && isxdigit(s[p]),p++)
            t.ival = strtoll(s+start, NULL, 16);
            if (neg_lit) t.ival = -t.ival;
        } else {
            W(p<l->len && (isdigit(s[p])||s[p]=='_'),p++)
            if(p<l->len && s[p]=='.') { is_float=1; p++; W(p<l->len && isdigit(s[p]),p++) }
            if(p<l->len && (s[p]=='e'||s[p]=='E')) {
                is_float=1; p++;
                if(p<l->len && (s[p]=='+'||s[p]=='-')) p++;
                W(p<l->len && isdigit(s[p]),p++)
            }
            if(is_float) {
                t.type=T_FLOAT_;
                t.fval=strtod(s+start,NULL);
                if (neg_lit) t.fval = -t.fval;
            } else {
                t.ival = strtoll(s+start, NULL, 10);
                if (neg_lit) t.ival = -t.ival;
            }
        }
        l->pos = p;
        return t;
        }
    }
    if(is_id_start(c)) {
        if((c=='f'||c=='F') && p+1<l->len && (s[p+1]=='"'||s[p+1]=='\'')) {
            l->pos = p+1;
            return lex_fstring(l);
        }
        if((c=='r'||c=='R') && p+1<l->len && (s[p+1]=='"'||s[p+1]=='\'')) {
            Token t = {T_STR_}; int qi = 0;
            p++; char q = s[p]; p++;
            while(p < l->len && s[p] != q) { t.sval[qi++] = s[p]; p++; }
            t.sval[qi] = 0;
            if(p < l->len) p++;
            l->pos = p;
            return t;
        }
        if((c=='b'||c=='B') && p+1<l->len && (s[p+1]=='"'||s[p+1]=='\'')) {
            p++;
        }
        Token t = {T_NAME_};
        int start = p;
        W(p<l->len && is_id_char(s[p]),p++)
        int n = p - start;
        if(n >= (int)sizeof(t.sval)) n = sizeof(t.sval)-1;
        memcpy(t.sval, s+start, n); t.sval[n]=0;
        l->pos = p;
        if(!strcmp(t.sval,"def"))       t.type=T_DEF_;
        else if(!strcmp(t.sval,"return"))   t.type=T_RETURN_;
        else if(!strcmp(t.sval,"if"))       t.type=T_IF_;
        else if(!strcmp(t.sval,"elif"))     t.type=T_ELIF_;
        else if(!strcmp(t.sval,"else"))     t.type=T_ELSE_;
        else if(!strcmp(t.sval,"while"))    t.type=T_WHILE_;
        else if(!strcmp(t.sval,"for"))      t.type=T_FOR_;
        else if(!strcmp(t.sval,"in"))       t.type=T_IN_;
        else if(!strcmp(t.sval,"break"))    t.type=T_BREAK_;
        else if(!strcmp(t.sval,"continue")) t.type=T_CONTINUE_;
        else if(!strcmp(t.sval,"and"))      t.type=T_AND_;
        else if(!strcmp(t.sval,"or"))       t.type=T_OR_;
        else if(!strcmp(t.sval,"not"))      t.type=T_NOT_;
        else if(!strcmp(t.sval,"True"))     t.type=T_TRUE_;
        else if(!strcmp(t.sval,"False"))    t.type=T_FALSE_;
        else if(!strcmp(t.sval,"None"))     t.type=T_NONE_;
        else if(!strcmp(t.sval,"import"))   t.type=T_IMPORT_;
        else if(!strcmp(t.sval,"try"))      t.type=T_TRY_;
        else if(!strcmp(t.sval,"except"))   t.type=T_EXCEPT_;
        else if(!strcmp(t.sval,"finally"))  t.type=T_FINALLY_;
        else if(!strcmp(t.sval,"as"))       t.type=T_AS_;
        else if(!strcmp(t.sval,"lambda"))   t.type=T_LAMBDA_;
        else if(!strcmp(t.sval,"pass"))     t.type=T_PASS_;
        else if(!strcmp(t.sval,"class"))    t.type=T_CLASS_;
        else if(!strcmp(t.sval,"global"))   t.type=T_GLOBAL_;
        else if(!strcmp(t.sval,"del"))      t.type=T_DEL_;
        else if(!strcmp(t.sval,"raise"))    t.type=T_RAISE_;
        else if(!strcmp(t.sval,"with"))     t.type=T_WITH_;
        else if(!strcmp(t.sval,"yield"))    t.type=T_YIELD_;
        else if(!strcmp(t.sval,"select"))   t.type=T_SELECT_;
        else if(!strcmp(t.sval,"update"))   t.type=T_UPDATE_;
        else if(!strcmp(t.sval,"delete"))   t.type=T_DELETE_;
        else if(!strcmp(t.sval,"by"))       t.type=T_BY_;
        else if(!strcmp(t.sval,"from"))     t.type=T_FROM_;
        else if(!strcmp(t.sval,"where"))    t.type=T_WHERE_;
        else if(!strcmp(t.sval,"create"))   t.type=T_CREATE_;
        else if(!strcmp(t.sval,"insert"))   t.type=T_INSERT_;
        else if(!strcmp(t.sval,"into"))     t.type=T_INTO_;
        else if(!strcmp(t.sval,"values"))   t.type=T_VALUES_;
        else if(!strcmp(t.sval,"join"))     t.type=T_JOIN_;
        else if(!strcmp(t.sval,"on"))       t.type=T_ON_;
        return t;
    }
    l->pos = p+1;
    switch(c) {
    case '+': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_PLUSEQ_); } return make_tok(T_PLUS_);
    case '-': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_MINUSEQ_); } return make_tok(T_MINUS_);
    case '*':
        if(p+1<l->len && s[p+1]=='*') { l->pos=p+2; return make_tok(T_DSTAR_); }
        if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_STAREQ_); }
        return make_tok(T_STAR_);
    case '/':
        if(p+1<l->len && s[p+1]=='/') { l->pos=p+2; return make_tok(T_DSLASH_); }
        if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_SLASHEQ_); }
        return make_tok(T_SLASH_);
    case '%': return make_tok(T_PERCENT_);
    case '=': return make_tok(T_EQ_);
    case '!': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_NE_); } break;
    case '<': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_LE_); } return make_tok(T_LT_);
    case '>': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_GE_); } return make_tok(T_GT_);
    case '(': l->paren_depth++; return make_tok(T_LPAREN_);
    case ')': l->paren_depth--; return make_tok(T_RPAREN_);
    case '[': l->paren_depth++; return make_tok(T_LBRACKET_);
    case ']': l->paren_depth--; return make_tok(T_RBRACKET_);
    case '{': l->paren_depth++; return make_tok(T_LBRACE_);
    case '}': l->paren_depth--; return make_tok(T_RBRACE_);
    case ',': return make_tok(T_COMMA_);
    case ':': return make_tok(T_COLON_);
    case '.': return make_tok(T_DOT_);
    case ';': return make_tok(T_SEMI_);
    case '@': return make_tok(T_AT_);
    }
    l->pos = p+1;
    return make_tok(T_EOF_);
}
Token lex_next(Lexer *l) {
    Token t;
    if(l->has_peek) { l->has_peek=0; t = l->peek; }
    else t = lex_raw(l);
    lex_note_token(l, t);
    return t;
}
Token lex_peek(Lexer *l) {
    if(!l->has_peek) { l->peek = lex_raw(l); l->has_peek=1; }
    return l->peek;
}
Node *node_new(int type) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    return n;
}
void node_add(Node *n, Node *child) {
    void *p = realloc(n->ch, (size_t)(n->nch + 1) * sizeof(Node *));
    if (!p) {
        fprintf(stderr, "node_add: out of memory\n");
        exit(1);
    }
    n->ch = p;
    n->ch[n->nch++] = child;
}
void node_free(Node *n) {
    Pv(!n)
    free(n->sval);
    i(n->nch,node_free(n->ch[i]))
    free(n->ch);
    free(n);
}
static Node *parse_expr(Lexer *l);
static Node *parse_stmt(Lexer *l);
static Node *parse_block(Lexer *l);
static Node *parse_or(Lexer *l);
static Node *parse_ternary(Lexer *l);
static Node *parse_query(Lexer *l);
static Node *parse_create_table(Lexer *l);
static Node *parse_insert(Lexer *l);
static Node *parse_join(Lexer *l, Node *left);
static Node *parse_atom(Lexer *l);

static int is_jux_arg_token(int tt) {
    return tt == T_NAME_ || tt == T_INT_ || tt == T_FLOAT_ || tt == T_DATETIME_
        || tt == T_STR_ || tt == T_LPAREN_ || tt == T_LBRACKET_ || tt == T_MINUS_;
}

static int is_jux_arg_start(Lexer *l) {
    Token pk = lex_peek(l);
    if (pk.type == T_MINUS_)
        return lex_peek_is_signed_literal(l);
    return is_jux_arg_token(pk.type);
}

static int is_jux_callee(Node *n) {
    return n && (n->type == N_NAME || n->type == N_DOT || n->type == N_CALL);
}

static Node *parse_jux_arg(Lexer *l) {
    return parse_atom(l);
}

static void expect(Lexer *l, int type) {
    Token t = lex_next(l);
    if(t.type != type) {
        fprintf(stderr, "parse error: expected token %d, got %d", type, t.type);
        if(t.type == T_NAME_ || t.type == T_STR_) fprintf(stderr, " (%s)", t.sval);
        fprintf(stderr, "\n");
    }
}
static Node *parse_atom(Lexer *l) {
    Token t = lex_next(l);
    Node *n;
    switch(t.type) {
    case T_INT_:
        n = node_new(N_INT); n->ival = t.ival; return n;
    case T_DATETIME_:
        n = node_new(N_DATETIME); n->ival = t.ival; return n;
    case T_FLOAT_:
        n = node_new(N_FLOAT); n->fval = t.fval; return n;
    case T_STR_:
        n = node_new(N_STR); n->sval = strdup(t.sval); return n;
    case T_FSTR_:
        n = node_new(N_FSTRING); n->sval = strdup(t.sval); return n;
    case T_TRUE_:
        n = node_new(N_BOOL); n->ival = 1; return n;
    case T_FALSE_:
        n = node_new(N_BOOL); n->ival = 0; return n;
    case T_NONE_:
        return node_new(N_NONE);
    case T_NAME_:
        n = node_new(N_NAME); n->sval = strdup(t.sval); return n;
    case T_LPAREN_: {
        if(lex_peek(l).type == T_RPAREN_) { lex_next(l); return node_new(N_LIST); }
        n = parse_expr(l);
        if(lex_peek(l).type == T_COMMA_) {
            Node *tup = node_new(N_LIST);
            node_add(tup, n);
            W(lex_peek(l).type == T_COMMA_,{
                lex_next(l);
                if(lex_peek(l).type == T_RPAREN_) break;
                node_add(tup, parse_expr(l));
            })
            expect(l, T_RPAREN_);
            return tup;
        }
        expect(l, T_RPAREN_);
        return n;
    }
    case T_LBRACKET_: {
        n = node_new(N_LIST);
        if(lex_peek(l).type != T_RBRACKET_) {
            node_add(n, parse_expr(l));
            while(lex_peek(l).type != T_RBRACKET_ && lex_peek(l).type != T_EOF_) {
                if(lex_peek(l).type == T_COMMA_) lex_next(l);
                if(lex_peek(l).type == T_RBRACKET_) break;
                node_add(n, parse_expr(l));
            }
        }
        expect(l, T_RBRACKET_);
        return n;
    }
    case T_LBRACE_: {
        n = node_new(N_DICT);
        if(lex_peek(l).type != T_RBRACE_) {
            Node *k = parse_expr(l); expect(l, T_COLON_); Node *v = parse_expr(l);
            node_add(n, k); node_add(n, v);
            W(lex_peek(l).type == T_COMMA_,{
                lex_next(l); if(lex_peek(l).type==T_RBRACE_) break;
                k = parse_expr(l); expect(l, T_COLON_); v = parse_expr(l);
                node_add(n, k); node_add(n, v);
            })
        }
        expect(l, T_RBRACE_);
        return n;
    }
    case T_MINUS_: {
        n = node_new(N_UNOP); n->op = OP_NEG;
        node_add(n, parse_atom(l));
        return n;
    }
    case T_NOT_: {
        n = node_new(N_UNOP); n->op = OP_NOT;
        node_add(n, parse_atom(l));
        return n;
    }
    case T_LAMBDA_: {
        Node *params = node_new(N_LIST);
        Node *defaults = node_new(N_LIST);
        Node *body = node_new(N_RETURN);
        if(lex_peek(l).type != T_COLON_) {
            Token p = lex_next(l);
            Node *pn = node_new(N_NAME); pn->sval = strdup(p.sval);
            node_add(params, pn);
            if(lex_peek(l).type == T_COLON_) {
                lex_next(l);
                Node *expr = parse_ternary(l);
                if(lex_peek(l).type == T_COLON_) {
                    lex_next(l);
                    node_add(defaults, expr);
                    node_add(body, parse_ternary(l));
                } else {
                    node_add(defaults, node_new(N_NONE));
                    node_add(body, expr);
                }
            } else {
                node_add(defaults, node_new(N_NONE));
            }
        }
        if(body->nch == 0) {
            expect(l, T_COLON_);
            node_add(body, parse_ternary(l));
        }
        n = node_new(N_LAMBDA);
        node_add(n, params);
        node_add(n, body);
        node_add(n, defaults);
        return n;
    }
    case T_PASS_:
        return node_new(N_PASS);
    case T_SELECT_: case T_UPDATE_: case T_DELETE_:
        l->has_peek = 1; l->peek = t;
        return parse_query(l);
    default:
        fprintf(stderr, "parse error: unexpected token %d ('%s')\n", t.type, t.sval);
        return node_new(N_NONE);
    }
}
static Node *parse_postfix(Lexer *l) {
    Node *n = parse_atom(l);
    /* q/k-style numeric vectors: 1 2 3 or 1 -2 3 → N_LIST */
    if((n->type == N_INT || n->type == N_FLOAT)) {
        Token pk0 = lex_peek(l);
        if(pk0.type == T_INT_ || pk0.type == T_FLOAT_
            || (pk0.type == T_MINUS_ && lex_peek_is_signed_literal(l))) {
            Node *vec = node_new(N_LIST);
            node_add(vec, n);
            while((pk0 = lex_peek(l)).type == T_INT_ || pk0.type == T_FLOAT_
                  || (pk0.type == T_MINUS_ && lex_peek_is_signed_literal(l)))
                node_add(vec, parse_jux_arg(l));
            n = vec;
        }
    }
    for(;;) {
        Token pk = lex_peek(l);
        if(pk.type == T_LPAREN_) {
            lex_next(l);
            Node *call = node_new(N_CALL);
            node_add(call, n);
            if(lex_peek(l).type != T_RPAREN_) {
                Node *arg = parse_expr(l);
                if(arg->type == N_NAME && lex_peek(l).type == T_COLON_) {
                    lex_next(l);
                    Node *kw = node_new(N_KWARG);
                    kw->sval = strdup(arg->sval);
                    node_free(arg);
                    node_add(kw, parse_expr(l));
                    node_add(call, kw);
                } else {
                    node_add(call, arg);
                }
                W(lex_peek(l).type == T_COMMA_,{
                    lex_next(l);
                    if(lex_peek(l).type == T_RPAREN_) break;
                    arg = parse_expr(l);
                    if(arg->type == N_NAME && lex_peek(l).type == T_COLON_) {
                        lex_next(l);
                        Node *kw = node_new(N_KWARG);
                        kw->sval = strdup(arg->sval);
                        node_free(arg);
                        node_add(kw, parse_expr(l));
                        node_add(call, kw);
                    } else {
                        node_add(call, arg);
                    }
                })
            }
            expect(l, T_RPAREN_);
            n = call;
        } else if(pk.type == T_LBRACKET_) {
            lex_next(l);
            if(lex_peek(l).type == T_COLON_) {
                Node *sl = node_new(N_SLICE);
                node_add(sl, n);
                node_add(sl, node_new(N_NONE));
                lex_next(l);
                if(lex_peek(l).type == T_RBRACKET_) {
                    node_add(sl, node_new(N_NONE));
                } else if(lex_peek(l).type == T_COLON_) {
                    node_add(sl, node_new(N_NONE));
                } else {
                    node_add(sl, parse_expr(l));
                }
                if(lex_peek(l).type == T_COLON_) {
                    lex_next(l);
                    if(lex_peek(l).type == T_RBRACKET_) node_add(sl, node_new(N_NONE));
                    else node_add(sl, parse_expr(l));
                }
                expect(l, T_RBRACKET_);
                n = sl;
            } else {
                Node *first = parse_expr(l);
                if(lex_peek(l).type == T_COLON_) {
                    Node *sl = node_new(N_SLICE);
                    node_add(sl, n);
                    node_add(sl, first);
                    lex_next(l);
                    if(lex_peek(l).type == T_RBRACKET_) {
                        node_add(sl, node_new(N_NONE));
                    } else if(lex_peek(l).type == T_COLON_) {
                        node_add(sl, node_new(N_NONE));
                    } else {
                        node_add(sl, parse_expr(l));
                    }
                    if(lex_peek(l).type == T_COLON_) {
                        lex_next(l);
                        if(lex_peek(l).type == T_RBRACKET_) node_add(sl, node_new(N_NONE));
                        else node_add(sl, parse_expr(l));
                    }
                    expect(l, T_RBRACKET_);
                    n = sl;
                } else {
                    Node *idx = node_new(N_INDEX);
                    node_add(idx, n);
                    node_add(idx, first);
                    W(lex_peek(l).type == T_COMMA_,{
                        lex_next(l);
                        node_add(idx, parse_expr(l));
                    })
                    expect(l, T_RBRACKET_);
                    n = idx;
                }
            }
        } else if(is_jux_callee(n) && is_jux_arg_start(l)) {
            Node *call = node_new(N_CALL);
            node_add(call, n);
            while(is_jux_arg_start(l))
                node_add(call, parse_jux_arg(l));
            n = call;
        } else if(pk.type == T_DOT_) {
            lex_next(l);
            Token name = lex_next(l);
            Node *dot = node_new(N_DOT);
            dot->sval = strdup(name.sval);
            node_add(dot, n);
            n = dot;
        } else break;
    }
    return n;
}
static Node *parse_power(Lexer *l) {
    Node *n = parse_postfix(l);
    if(lex_peek(l).type == T_DSTAR_) {
        lex_next(l);
        Node *r = node_new(N_BINOP); r->op = OP_POW;
        node_add(r, n); node_add(r, parse_power(l));
        return r;
    }
    return n;
}
static Node *parse_matmul(Lexer *l) {
    Node *n = parse_power(l);
    W(lex_peek(l).type == T_AT_, {
        lex_next(l);
        Node *r = node_new(N_BINOP);
        r->op = OP_MATMUL;
        node_add(r, n);
        node_add(r, parse_matmul(l));
        n = r;
    })
    return n;
}
static Node *parse_mul(Lexer *l) {
    Node *n = parse_matmul(l);
    W(lex_peek(l).type==T_STAR_ || lex_peek(l).type==T_SLASH_ ||
      lex_peek(l).type==T_PERCENT_ || lex_peek(l).type==T_DSLASH_,{
        Token op = lex_next(l);
        int o = op.type==T_STAR_?OP_MUL : op.type==T_SLASH_?OP_DIV : op.type==T_DSLASH_?OP_FLOORDIV : OP_MOD;
        Node *r = node_new(N_BINOP); r->op = o;
        node_add(r, n); node_add(r, parse_power(l));
        n = r;
    })
    return n;
}
static Node *parse_add(Lexer *l) {
    Node *n = parse_mul(l);
    W(lex_peek(l).type==T_PLUS_ || lex_peek(l).type==T_MINUS_,{
        Token op = lex_next(l);
        int o = op.type==T_PLUS_?OP_ADD:OP_SUB;
        Node *r = node_new(N_BINOP); r->op = o;
        node_add(r, n); node_add(r, parse_mul(l));
        n = r;
    })
    return n;
}
static Node *parse_cmp(Lexer *l) {
    Node *n = parse_add(l);
    Token pk = lex_peek(l);
    if(pk.type==T_NOT_) {
        lex_next(l);
        if(lex_peek(l).type == T_IN_) {
            lex_next(l);
            Node *r = node_new(N_CMP); r->op = OP_NOT_IN;
            node_add(r, n); node_add(r, parse_add(l));
            return r;
        }
        fprintf(stderr, "parse error: expected 'in' after 'not'\n");
        return n;
    }
    if(pk.type==T_EQ_||pk.type==T_NE_||pk.type==T_LT_||pk.type==T_GT_||
       pk.type==T_LE_||pk.type==T_GE_||pk.type==T_IN_) {
        Token op = lex_next(l);
        int o;
        switch(op.type) {
        case T_EQ_: o=OP_EQ; break; case T_NE_: o=OP_NE; break;
        case T_LT_: o=OP_LT; break; case T_GT_: o=OP_GT; break;
        case T_LE_: o=OP_LE; break; case T_GE_: o=OP_GE; break;
        case T_IN_: o=OP_IN; break;
        default: o=OP_EQ;
        }
        Node *r = node_new(N_CMP); r->op = o;
        node_add(r, n); node_add(r, parse_add(l));
        return r;
    }
    return n;
}
static Node *parse_not(Lexer *l) {
    if(lex_peek(l).type == T_NOT_) {
        lex_next(l);
        Node *n = node_new(N_UNOP); n->op = OP_NOT;
        node_add(n, parse_not(l));
        return n;
    }
    return parse_cmp(l);
}
static Node *parse_and(Lexer *l) {
    Node *n = parse_not(l);
    W(lex_peek(l).type == T_AND_,{
        lex_next(l);
        Node *r = node_new(N_BINOP); r->op = OP_AND;
        node_add(r, n); node_add(r, parse_not(l));
        n = r;
    })
    return n;
}
static Node *parse_or(Lexer *l) {
    Node *n = parse_and(l);
    W(lex_peek(l).type == T_OR_,{
        lex_next(l);
        Node *r = node_new(N_BINOP); r->op = OP_OR;
        node_add(r, n); node_add(r, parse_and(l));
        n = r;
    })
    return n;
}
static Node *parse_ternary(Lexer *l) {
    Node *n = parse_or(l);
    if(lex_peek(l).type == T_IF_) {
        lex_next(l);
        Node *cond = parse_or(l);
        expect(l, T_ELSE_);
        Node *alt = parse_ternary(l);
        Node *r = node_new(N_IF);
        node_add(r, cond);
        node_add(r, n);
        Node *true_node = node_new(N_BOOL); true_node->ival = 1;
        node_add(r, true_node);
        node_add(r, alt);
        return r;
    }
    return n;
}
static int is_sql_agg_kw(const char *s) {
    static const char *kws[] = {"count", "sum", "avg", "min", "max", NULL};
    for (int k = 0; kws[k]; k++) {
        if (!strcmp(s, kws[k])) {
            return 1;
        }
    }
    return 0;
}
static void parse_select_col(Lexer *l, Node *cols) {
    if (lex_peek(l).type != T_NAME_) {
        node_add(cols, parse_expr(l));
        return;
    }
    Token nm = lex_next(l);
    Node *name = node_new(N_NAME);
    name->sval = strdup(nm.sval);
    if (is_sql_agg_kw(nm.sval) && lex_peek(l).type == T_NAME_) {
        node_add(cols, name);
        Token nm2 = lex_next(l);
        Node *name2 = node_new(N_NAME);
        name2->sval = strdup(nm2.sval);
        node_add(cols, name2);
        return;
    }
    node_add(cols, name);
}
static Node *parse_by_cols(Lexer *l) {
    Node *n = node_new(N_LIST);
    while (lex_peek(l).type == T_NAME_) {
        Token nm = lex_next(l);
        Node *name = node_new(N_NAME);
        name->sval = strdup(nm.sval);
        node_add(n, name);
        if (lex_peek(l).type == T_COMMA_) {
            lex_next(l);
            continue;
        }
        break;
    }
    if (n->nch == 0) {
        node_free(n);
        return node_new(N_NONE);
    }
    if (n->nch == 1) {
        Node *single = n->ch[0];
        n->ch[0] = NULL;
        n->nch = 0;
        node_free(n);
        return single;
    }
    return n;
}
static Node *parse_query(Lexer *l) {
    Token kw = lex_next(l);
    Node *n = node_new(kw.type == T_SELECT_ ? N_SELECT : (kw.type == T_UPDATE_ ? N_UPDATE : N_DELETE));
    Node *cols = node_new(N_LIST);
    Node *by   = node_new(N_NONE);
    Node *from = node_new(N_NONE);
    Node *where = node_new(N_NONE);
    if (kw.type == T_SELECT_ || kw.type == T_UPDATE_ || kw.type == T_DELETE_) {
        while (lex_peek(l).type != T_FROM_ && lex_peek(l).type != T_WHERE_ &&
               lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_) {
            if (lex_peek(l).type == T_BY_) {
                if (kw.type == T_SELECT_ || kw.type == T_UPDATE_ || kw.type == T_DELETE_) {
                    lex_next(l);
                    node_free(by);
                    by = parse_by_cols(l);
                    continue;
                }
            }
            if (kw.type == T_UPDATE_ && lex_peek(l).type == T_NAME_) {
                Token nm = lex_next(l);
                if (lex_peek(l).type == T_COLON_) {
                    lex_next(l);
                    Node *as = node_new(N_ASSIGN);
                    Node *ln = node_new(N_NAME);
                    ln->sval = strdup(nm.sval);
                    node_add(as, ln);
                    node_add(as, parse_expr(l));
                    node_add(cols, as);
                    if (lex_peek(l).type == T_COMMA_) lex_next(l);
                    continue;
                }
                l->has_peek = 1;
                l->peek = nm;
            }
            if (kw.type == T_SELECT_) {
                parse_select_col(l, cols);
            } else {
                node_add(cols, parse_expr(l));
            }
            if (lex_peek(l).type == T_COMMA_) lex_next(l);
        }
    }
    if (lex_peek(l).type == T_FROM_) {
        lex_next(l);
        node_free(from);
        from = parse_expr(l);
    }
    if (lex_peek(l).type == T_WHERE_) {
        lex_next(l);
        node_free(where);
        where = parse_expr(l);
    }
    node_add(n, from);
    node_add(n, cols);
    node_add(n, by);
    node_add(n, where);
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return n;
}
static Node *parse_create_table(Lexer *l) {
    lex_next(l);
    Token kwt = lex_next(l);
    if(kwt.type != T_NAME_ || strcmp(kwt.sval, "table")) {
        fprintf(stderr, "parse error: expected 'table' after create\n");
    }
    Token name = lex_next(l);
    Node *n = node_new(N_CREATE_TABLE);
    n->sval = strdup(name.sval);
    Node *cols = node_new(N_LIST);
    expect(l, T_LPAREN_);
    while(lex_peek(l).type != T_RPAREN_ && lex_peek(l).type != T_EOF_) {
        Token cn = lex_next(l);
        Node *col = node_new(N_KWARG);
        col->sval = strdup(cn.sval);
        if(lex_peek(l).type == T_COLON_) {
            lex_next(l);
            node_add(col, parse_expr(l));
        } else {
            node_add(col, node_new(N_NONE));
        }
        node_add(cols, col);
        if(lex_peek(l).type == T_COMMA_) lex_next(l);
    }
    expect(l, T_RPAREN_);
    node_add(n, cols);
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return n;
}
static Node *parse_insert(Lexer *l) {
    lex_next(l);
    expect(l, T_INTO_);
    Token table = lex_next(l);
    Node *n = node_new(N_INSERT);
    n->sval = strdup(table.sval);
    Node *cols = node_new(N_LIST);
    if(lex_peek(l).type == T_LPAREN_) {
        lex_next(l);
        while(lex_peek(l).type != T_RPAREN_ && lex_peek(l).type != T_EOF_) {
            Token c = lex_next(l);
            node_add(cols, node_new(N_NAME));
            cols->ch[cols->nch-1]->sval = strdup(c.sval);
            if(lex_peek(l).type == T_COMMA_) lex_next(l);
        }
        expect(l, T_RPAREN_);
    }
    expect(l, T_VALUES_);
    Node *vals = node_new(N_LIST);
    expect(l, T_LPAREN_);
    while(lex_peek(l).type != T_RPAREN_ && lex_peek(l).type != T_EOF_) {
        node_add(vals, parse_expr(l));
        if(lex_peek(l).type == T_COMMA_) lex_next(l);
    }
    expect(l, T_RPAREN_);
    node_add(n, cols);
    node_add(n, vals);
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return n;
}
static Node *parse_join(Lexer *l, Node *left) {
    lex_next(l);
    Node *right = parse_expr(l);
    expect(l, T_ON_);
    Token key = lex_next(l);
    Node *n = node_new(N_JOIN);
    n->sval = strdup(key.sval);
    node_add(n, left);
    node_add(n, right);
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return n;
}
static Node *parse_expr(Lexer *l) {
    return parse_ternary(l);
}
static Node *parse_block(Lexer *l) {
    expect(l, T_INDENT_);
    Node *block = node_new(N_BLOCK);
    while(lex_peek(l).type != T_DEDENT_ && lex_peek(l).type != T_EOF_) {
        Node *s = parse_stmt(l);
        if(s) node_add(block, s);
    }
    if(lex_peek(l).type == T_DEDENT_) lex_next(l);
    return block;
}
static Node *parse_if(Lexer *l) {
    Node *n = node_new(N_IF);
    Node *cond = parse_expr(l);
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *body = parse_block(l);
    node_add(n, cond);
    node_add(n, body);
    while(lex_peek(l).type == T_ELIF_) {
        lex_next(l);
        Node *econd = parse_expr(l);
        expect(l, T_COLON_);
        expect(l, T_NEWLINE_);
        Node *ebody = parse_block(l);
        node_add(n, econd);
        node_add(n, ebody);
    }
    if(lex_peek(l).type == T_ELSE_) {
        lex_next(l);
        expect(l, T_COLON_);
        expect(l, T_NEWLINE_);
        Node *ebody = parse_block(l);
        node_add(n, node_new(N_BOOL));
        n->ch[n->nch-1]->ival = 1;
        node_add(n, ebody);
    }
    return n;
}
static Node *parse_while(Lexer *l) {
    Node *n = node_new(N_WHILE);
    Node *cond = parse_expr(l);
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *body = parse_block(l);
    node_add(n, cond);
    node_add(n, body);
    return n;
}
static Node *parse_for(Lexer *l) {
    Node *n = node_new(N_FOR);
    Token var1 = lex_next(l);
    Node *vars;
    if(lex_peek(l).type == T_COMMA_) {
        vars = node_new(N_LIST);
        Node *v1 = node_new(N_NAME); v1->sval = strdup(var1.sval);
        node_add(vars, v1);
        while(lex_peek(l).type == T_COMMA_) {
            lex_next(l);
            Token v = lex_next(l);
            Node *vn = node_new(N_NAME); vn->sval = strdup(v.sval);
            node_add(vars, vn);
        }
    } else {
        vars = node_new(N_NAME); vars->sval = strdup(var1.sval);
    }
    expect(l, T_IN_);
    Node *iter = parse_expr(l);
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *body = parse_block(l);
    node_add(n, vars);
    node_add(n, iter);
    node_add(n, body);
    return n;
}
static Node *parse_def(Lexer *l) {
    Token name = lex_next(l);
    expect(l, T_LPAREN_);
    Node *params = node_new(N_LIST);
    Node *defaults = node_new(N_LIST);
    if(lex_peek(l).type != T_RPAREN_) {
        if(lex_peek(l).type == T_STAR_) { lex_next(l); }
        if(lex_peek(l).type == T_DSTAR_) { lex_next(l); }
        Token p = lex_next(l);
        Node *pn = node_new(N_NAME); pn->sval = strdup(p.sval);
        node_add(params, pn);
        if(lex_peek(l).type == T_COLON_) {
            lex_next(l); node_add(defaults, parse_expr(l));
        } else {
            node_add(defaults, node_new(N_NONE));
        }
        while(lex_peek(l).type == T_COMMA_) {
            lex_next(l);
            if(lex_peek(l).type == T_RPAREN_) break;
            if(lex_peek(l).type == T_STAR_) { lex_next(l); }
            if(lex_peek(l).type == T_DSTAR_) { lex_next(l); }
            p = lex_next(l);
            pn = node_new(N_NAME); pn->sval = strdup(p.sval);
            node_add(params, pn);
            if(lex_peek(l).type == T_COLON_) {
                lex_next(l); node_add(defaults, parse_expr(l));
            } else {
                node_add(defaults, node_new(N_NONE));
            }
        }
    }
    expect(l, T_RPAREN_);
    if(lex_peek(l).type == T_MINUS_) {
        lex_next(l);
        if(lex_peek(l).type == T_GT_) { lex_next(l); parse_expr(l);  }
    }
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *body = parse_block(l);
    Node *n = node_new(N_DEF);
    n->sval = strdup(name.sval);
    node_add(n, params);
    node_add(n, body);
    node_add(n, defaults);
    return n;
}
static Node *parse_try(Lexer *l) {
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *try_body = parse_block(l);
    Node *n = node_new(N_TRY);
    node_add(n, try_body);
    if(lex_peek(l).type == T_EXCEPT_) {
        lex_next(l);
        if(lex_peek(l).type == T_NAME_) {
            lex_next(l);
        }
        if(lex_peek(l).type == T_AS_) {
            lex_next(l);
            Token ename = lex_next(l);
            n->sval = strdup(ename.sval);
        }
        expect(l, T_COLON_);
        expect(l, T_NEWLINE_);
        Node *except_body = parse_block(l);
        node_add(n, except_body);
    } else {
        node_add(n, node_new(N_PASS));
    }
    if(lex_peek(l).type == T_ELSE_) {
        lex_next(l); expect(l, T_COLON_); expect(l, T_NEWLINE_);
        Node *else_body = parse_block(l);
        node_add(n, else_body);
    }
    if(lex_peek(l).type == T_FINALLY_) {
        lex_next(l); expect(l, T_COLON_); expect(l, T_NEWLINE_);
        Node *finally_body = parse_block(l);
        node_add(n, finally_body);
    }
    return n;
}
static Node *parse_class(Lexer *l) {
    Token name = lex_next(l);
    if(lex_peek(l).type == T_LPAREN_) {
        lex_next(l);
        W(lex_peek(l).type != T_RPAREN_ && lex_peek(l).type != T_EOF_,lex_next(l))
        expect(l, T_RPAREN_);
    }
    expect(l, T_COLON_);
    expect(l, T_NEWLINE_);
    Node *body = parse_block(l);
    Node *n = node_new(N_CLASS);
    n->sval = strdup(name.sval);
    node_add(n, body);
    return n;
}
static Node *parse_stmt(Lexer *l) {
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    Token pk = lex_peek(l);
    P(pk.type == T_EOF_ || pk.type == T_DEDENT_,NULL)
    P(pk.type == T_IF_,(lex_next(l),parse_if(l)))
    P(pk.type == T_WHILE_,(lex_next(l),parse_while(l)))
    P(pk.type == T_FOR_,(lex_next(l),parse_for(l)))
    P(pk.type == T_DEF_,(lex_next(l),parse_def(l)))
    P(pk.type == T_TRY_,(lex_next(l),parse_try(l)))
    P(pk.type == T_CLASS_,(lex_next(l),parse_class(l)))
    P(pk.type == T_PASS_,(lex_next(l),({W(lex_peek(l).type==T_NEWLINE_,lex_next(l)); node_new(N_PASS);})))
    P(pk.type == T_CREATE_,parse_create_table(l))
    P(pk.type == T_INSERT_,parse_insert(l))
    if(pk.type == T_RETURN_) {
        lex_next(l);
        Node *n = node_new(N_RETURN);
        if(lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_ && lex_peek(l).type != T_DEDENT_)
            node_add(n, parse_expr(l));
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    if(pk.type == T_BREAK_) {
        lex_next(l);
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return node_new(N_BREAK);
    }
    if(pk.type == T_CONTINUE_) {
        lex_next(l);
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return node_new(N_CONTINUE);
    }
    if(pk.type == T_RAISE_) {
        lex_next(l);
        Node *n = node_new(N_RAISE);
        if(lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_) {
            node_add(n, parse_expr(l));
        }
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    if(pk.type == T_DEL_) {
        lex_next(l);
        Node *n = node_new(N_DEL);
        node_add(n, parse_expr(l));
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    if(pk.type == T_GLOBAL_) {
        lex_next(l);
        Node *n = node_new(N_GLOBAL);
        Token nm = lex_next(l);
        n->sval = strdup(nm.sval);
        W(lex_peek(l).type == T_COMMA_,{lex_next(l); lex_next(l);})
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    if(pk.type == T_WITH_) {
        lex_next(l);
        Node *n = node_new(N_WITH);
        node_add(n, parse_expr(l));
        if(lex_peek(l).type == T_AS_) {
            lex_next(l);
            Token vname = lex_next(l);
            n->sval = strdup(vname.sval);
        }
        expect(l, T_COLON_);
        expect(l, T_NEWLINE_);
        Node *body = parse_block(l);
        node_add(n, body);
        return n;
    }
    if(pk.type == T_IMPORT_) {
        lex_next(l);
        Node *n = node_new(N_IMPORT);
        Token path = lex_next(l);
        n->sval = strdup(path.sval);
        W(lex_peek(l).type == T_DOT_,{
            lex_next(l);
            Token sub = lex_next(l);
            char buf[4096];
            snprintf(buf, sizeof(buf), "%s.%s", n->sval, sub.sval);
            free(n->sval);
            n->sval = strdup(buf);
        })
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    Node *expr = parse_expr(l);
    if(lex_peek(l).type == T_JOIN_) {
        return parse_join(l, expr);
    }
    pk = lex_peek(l);
    if(pk.type == T_COMMA_ && expr->type == N_NAME) {
        Node *targets = node_new(N_LIST);
        node_add(targets, expr);
        W(lex_peek(l).type == T_COMMA_,{
            lex_next(l);
            if(lex_peek(l).type == T_COLON_) break;
            node_add(targets, parse_expr(l));
        })
        if(lex_peek(l).type == T_COLON_) {
            lex_next(l);
            Node *val = parse_expr(l);
            Node *n = node_new(N_ASSIGN);
            node_add(n, targets);
            node_add(n, val);
            W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
            return n;
        }
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return targets;
    }
    if(pk.type == T_COLON_) {
        lex_next(l);
        Node *val = parse_expr(l);
        Node *n = node_new(N_ASSIGN);
        node_add(n, expr);
        node_add(n, val);
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    if(pk.type == T_PLUSEQ_ || pk.type == T_MINUSEQ_ || pk.type == T_STAREQ_ || pk.type == T_SLASHEQ_) {
        Token op = lex_next(l);
        int o = op.type==T_PLUSEQ_?OP_ADD : op.type==T_MINUSEQ_?OP_SUB : op.type==T_STAREQ_?OP_MUL : OP_DIV;
        Node *val = parse_expr(l);
        Node *n = node_new(N_AUGASSIGN); n->op = o;
        node_add(n, expr);
        node_add(n, val);
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return n;
    }
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return expr;
}
Node *parse(const char *src) {
    Lexer l;
    lex_init(&l, src);
    Node *prog = node_new(N_BLOCK);
    W(lex_peek(&l).type != T_EOF_,{
        Node *s = parse_stmt(&l);
        if(s) node_add(prog, s);
    })
    return prog;
}

static const char *node_op_name(int op) {
    switch (op) {
    case OP_ADD: return "+"; case OP_SUB: return "-"; case OP_MUL: return "*";
    case OP_DIV: return "/"; case OP_MOD: return "%"; case OP_POW: return "**";
    case OP_NEG: return "neg"; case OP_NOT: return "not";
    default: return "?";
    }
}

static void node_sprint_rec(Node *n, FILE *fp) {
    if (!n) { fputs("nil", fp); return; }
    switch (n->type) {
    case N_INT: fprintf(fp, "%lld", (long long)n->ival); return;
    case N_FLOAT: fprintf(fp, "%g", n->fval); return;
    case N_STR: fprintf(fp, "\"%s\"", n->sval ? n->sval : ""); return;
    case N_BOOL: fputs(n->ival ? "True" : "False", fp); return;
    case N_NONE: fputs("None", fp); return;
    case N_NAME: fprintf(fp, "`%s", n->sval ? n->sval : ""); return;
    case N_UNOP:
        fprintf(fp, "(%s ", node_op_name(n->op));
        node_sprint_rec(n->nch > 0 ? n->ch[0] : NULL, fp);
        fputc(')', fp);
        return;
    case N_BINOP:
        fprintf(fp, "(%s ", node_op_name(n->op));
        node_sprint_rec(n->nch > 0 ? n->ch[0] : NULL, fp);
        fputc(' ', fp);
        node_sprint_rec(n->nch > 1 ? n->ch[1] : NULL, fp);
        fputc(')', fp);
        return;
    case N_CALL:
        fputs("(call ", fp);
        for (int i = 0; i < n->nch; i++) {
            if (i) fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(')', fp);
        return;
    case N_LIST:
        fputs("[", fp);
        for (int i = 0; i < n->nch; i++) {
            if (i) fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(']', fp);
        return;
    default:
        fprintf(fp, "(n%d", n->type);
        for (int i = 0; i < n->nch; i++) {
            fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(')', fp);
        return;
    }
}

void node_sprint(Node *n, FILE *fp) {
    node_sprint_rec(n, fp);
}

static int is_truthy(V *v) {
    P(!v,0)
    switch(v->t) {
    case T_NIL:  return 0;
    case T_BOOL: return v->b;
    case T_INT:  return v->j != 0;
    case T_FLOAT:return v->f != 0.0;
    case T_STR:  return v->s[0] != 0;
    case T_IVEC: case T_FVEC: case T_BVEC: case T_LIST: return v->n > 0;
    case T_IMAT: case T_FMAT: case T_BMAT: return v->n > 0 && mat_cols(v) > 0;
    case T_DATETIME: return v->j != 0;
    case T_DATE: return v->j != 0;
    case T_TIME: return v->j != 0;
    case T_ERR:  return 0;
    default: return 1;
    }
}
static int v_elem_truthy(V *v, int64_t i) {
    switch (v->t) {
    case T_BVEC: return v->B[i] != 0;
    case T_IVEC: return v->J[i] != 0;
    case T_FVEC: return v->F[i] != 0.0;
    default: return is_truthy(v);
    }
}
/* Element-wise logical and/or for vector operands (scalars broadcast). Scalar
 * short-circuit truthiness collapses a whole vector to one bool and returns the
 * other operand wholesale, so `mask_a and mask_b` must be combined per element
 * here instead — otherwise SQL `where A and B` silently ignores A. */
static V *vec_logic(V *a, V *b, int is_and) {
    int av = (a->t == T_BVEC || a->t == T_IVEC || a->t == T_FVEC);
    int bv = (b->t == T_BVEC || b->t == T_IVEC || b->t == T_FVEC);
    if (av && bv && a->n != b->n) return v_err("and/or: vector length mismatch");
    int64_t n = av ? a->n : b->n;
    V *r = v_bvec(n);
    int as = av ? 0 : is_truthy(a);
    int bs = bv ? 0 : is_truthy(b);
    for (int64_t i = 0; i < n; i++) {
        int ai = av ? v_elem_truthy(a, i) : as;
        int bi = bv ? v_elem_truthy(b, i) : bs;
        r->B[i] = (unsigned char)(is_and ? (ai && bi) : (ai || bi));
    }
    return r;
}
static double to_float(V *v) {
    P(!v,0)
    P(v->t==T_INT,(double)v->j)
    P(v->t==T_FLOAT,v->f)
    P(v->t==T_BOOL,(double)v->b)
    return 0;
}
static V *mat_binop(V *a, V *b, int op) {
    if (!is_mat_t(a->t) && !is_mat_t(b->t)) return NULL;
    if (a->t == T_BMAT || b->t == T_BMAT)
        return v_err("arithmetic not supported on matrix[bool]");
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT)) {
        int64_t rows = a->n, cols = mat_cols(a), ne = rows * cols;
        int out_t = (a->t == T_FMAT || b->t == T_FLOAT) ? T_FMAT : T_IMAT;
        V *r = out_t == T_FMAT ? (V *)v_fmat(rows, cols) : (V *)v_imat(rows, cols);
        double y = to_float(b);
        if (out_t == T_FMAT && a->t == T_FMAT)
            mat_fmat_binop_scalar(r->F, a->F, y, ne, op);
        else if (out_t == T_IMAT && a->t == T_IMAT) {
            int64_t yi = b->t == T_INT ? b->j : (int64_t)y;
            mat_imat_binop_scalar(r->J, a->J, yi, ne, op);
        } else {
            for (int64_t i = 0; i < ne; i++) {
                double x = a->t == T_IMAT ? (double)a->J[i] : a->F[i];
                switch (op) {
                case OP_ADD: r->F[i] = x + y; break;
                case OP_SUB: r->F[i] = x - y; break;
                case OP_MUL: r->F[i] = x * y; break;
                case OP_DIV: r->F[i] = y != 0 ? x / y : 0; break;
                case OP_FLOORDIV: r->F[i] = y != 0 ? floor(x / y) : 0; break;
                case OP_MOD: r->F[i] = y != 0 ? fmod(x, y) : 0; break;
                case OP_POW: r->F[i] = pow(x, y); break;
                default: break;
                }
            }
        }
        return r;
    }
    if ((a->t == T_INT || a->t == T_FLOAT) && is_mat_t(b->t)) {
        V *r = mat_binop(b, a, op);
        if (!r || r->t == T_ERR) return r;
        if (op == OP_SUB || op == OP_DIV || op == OP_FLOORDIV || op == OP_MOD) {
            int64_t rows = b->n, cols = mat_cols(b), ne = rows * cols;
            if (b->t == T_FMAT) {
                V *out = v_fmat(rows, cols);
                double x = to_float(a);
                mat_fmat_binop_scalar_rev(out->F, x, b->F, ne, op);
                v_free(r);
                return out;
            }
            V *out = v_imat(rows, cols);
            int64_t x = a->j;
            mat_imat_binop_scalar_rev(out->J, x, b->J, ne, op);
            v_free(r);
            return out;
        }
        return r;
    }
    if (!is_mat_t(a->t) || !is_mat_t(b->t))
        return v_errf("unsupported operand types for op %d: %s and %s", op, type_name(a->t), type_name(b->t));
    if (a->n != b->n || mat_cols(a) != mat_cols(b))
        return v_err("matrix shape mismatch");
    int64_t ne = a->n * mat_cols(a);
    int out_t = (a->t == T_FMAT || b->t == T_FMAT) ? T_FMAT : T_IMAT;
    if (op == OP_DIV || op == OP_POW) out_t = T_FMAT;
    V *r = out_t == T_FMAT ? (V *)v_fmat(a->n, mat_cols(a)) : (V *)v_imat(a->n, mat_cols(a));
    if (out_t == T_FMAT && a->t == T_FMAT && b->t == T_FMAT)
        mat_fmat_binop_mm(r->F, a->F, b->F, ne, op);
    else if (out_t == T_IMAT && a->t == T_IMAT && b->t == T_IMAT)
        mat_imat_binop_mm(r->J, a->J, b->J, ne, op);
    else {
        for (int64_t i = 0; i < ne; i++) {
            if (out_t == T_FMAT) {
                double x = a->t == T_IMAT ? (double)a->J[i] : a->F[i];
                double y = b->t == T_IMAT ? (double)b->J[i] : b->F[i];
                switch (op) {
                case OP_ADD: r->F[i] = x + y; break;
                case OP_SUB: r->F[i] = x - y; break;
                case OP_MUL: r->F[i] = x * y; break;
                case OP_DIV: r->F[i] = y != 0 ? x / y : 0; break;
                case OP_FLOORDIV: r->F[i] = y != 0 ? floor(x / y) : 0; break;
                case OP_MOD: r->F[i] = y != 0 ? fmod(x, y) : 0; break;
                case OP_POW: r->F[i] = pow(x, y); break;
                default: break;
                }
            } else {
                int64_t x = a->J[i], y = b->J[i];
                switch (op) {
                case OP_ADD: r->J[i] = x + y; break;
                case OP_SUB: r->J[i] = x - y; break;
                case OP_MUL: r->J[i] = x * y; break;
                case OP_FLOORDIV: r->J[i] = y ? x / y : 0; break;
                case OP_MOD: r->J[i] = y ? x % y : 0; break;
                default: break;
                }
            }
        }
    }
    return r;
}
static V *vec_binop(V *a, V *b, int op) {
    if (is_mat_t(a->t) || is_mat_t(b->t)) {
        V *r = mat_binop(a, b, op);
        if (r) return r;
    }
    if (a->t == T_IVEC && b->t == T_IVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n < b->n ? a->n : b->n;
        V *r = v_ivec(n);
        const int64_t *aj = a->J, *bj = b->J;
        int64_t *rj = r->J;
        switch (op) {
        case OP_ADD:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] + bj[i];
            break;
        case OP_SUB:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] - bj[i];
            break;
        case OP_MUL:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] * bj[i];
            break;
        case OP_FLOORDIV:
            for (int64_t i = 0; i < n; i++) rj[i] = bj[i] ? aj[i] / bj[i] : 0;
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) rj[i] = bj[i] ? aj[i] % bj[i] : 0;
            break;
        default: break;
        }
        return r;
    }
    if (a->t == T_IVEC && b->t == T_INT && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n, y = b->j;
        V *r = v_ivec(n);
        const int64_t *aj = a->J;
        int64_t *rj = r->J;
        switch (op) {
        case OP_ADD:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] + y;
            break;
        case OP_SUB:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] - y;
            break;
        case OP_MUL:
            for (int64_t i = 0; i < n; i++) rj[i] = aj[i] * y;
            break;
        case OP_FLOORDIV:
            for (int64_t i = 0; i < n; i++) rj[i] = y ? aj[i] / y : 0;
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) rj[i] = y ? aj[i] % y : 0;
            break;
        default: break;
        }
        return r;
    }
    if (a->t == T_INT && b->t == T_IVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = b->n, x = a->j;
        V *r = v_ivec(n);
        const int64_t *bj = b->J;
        int64_t *rj = r->J;
        switch (op) {
        case OP_ADD:
            for (int64_t i = 0; i < n; i++) rj[i] = x + bj[i];
            break;
        case OP_SUB:
            for (int64_t i = 0; i < n; i++) rj[i] = x - bj[i];
            break;
        case OP_MUL:
            for (int64_t i = 0; i < n; i++) rj[i] = x * bj[i];
            break;
        case OP_FLOORDIV:
            for (int64_t i = 0; i < n; i++) rj[i] = bj[i] ? x / bj[i] : 0;
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) rj[i] = bj[i] ? x % bj[i] : 0;
            break;
        default: break;
        }
        return r;
    }
    if((a->t==T_INT||a->t==T_FLOAT) && (b->t==T_INT||b->t==T_FLOAT)) {
        int use_int = (a->t==T_INT && b->t==T_INT && op!=OP_DIV && op!=OP_POW);
        if(use_int) {
            int64_t x=a->j, y=b->j;
            switch(op) {
            case OP_ADD: return v_int(x+y); case OP_SUB: return v_int(x-y);
            case OP_MUL: return v_int(x*y);
            case OP_FLOORDIV: return y?v_int(x/y):v_err("division by zero");
            case OP_MOD: return y?v_int(x%y):v_err("modulo by zero");
            default: break;
            }
        }
        double x=to_float(a), y=to_float(b);
        switch(op) {
        case OP_ADD: return v_float(x+y); case OP_SUB: return v_float(x-y);
        case OP_MUL: return v_float(x*y);
        case OP_DIV: return y!=0?v_float(x/y):v_err("division by zero");
        case OP_FLOORDIV: return y!=0?v_float(floor(x/y)):v_err("division by zero");
        case OP_MOD: return y!=0?v_float(fmod(x,y)):v_err("modulo by zero");
        case OP_POW: return v_float(pow(x,y));
        default: break;
        }
    }
    if(a->t==T_STR && b->t==T_STR && op==OP_ADD) {
        size_t la=strlen(a->s), lb=strlen(b->s);
        char *r = malloc(la+lb+1);
        memcpy(r, a->s, la); memcpy(r+la, b->s, lb); r[la+lb]=0;
        V *v = v_str(r); free(r); return v;
    }
    if(a->t==T_STR && b->t==T_INT && op==OP_MUL) {
        int64_t n = b->j; if(n<0) n=0;
        size_t slen = strlen(a->s);
        char *r = malloc(slen*n+1); r[0]=0;
        for(int64_t i=0;i<n;i++) memcpy(r+i*slen, a->s, slen);
        r[slen*n]=0;
        V *v = v_str(r); free(r); return v;
    }
    P(a->t==T_INT && b->t==T_STR && op==OP_MUL,vec_binop(b, a, op))
    #define VEC_BIN(AT,BT,AJ,BJ) \
    if(a->t==AT && b->t==BT) { \
        int64_t n=a->n<b->n?a->n:b->n; \
        int ui=(AT==T_IVEC&&BT==T_IVEC&&op!=OP_DIV&&op!=OP_POW); \
        if(ui){ V*r=v_ivec(n); for(int64_t i=0;i<n;i++){int64_t x=AJ[i],y=BJ[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:r->J[i]=y?x/y:0;break; \
            case OP_MOD:r->J[i]=y?x%y:0;break;default:break;}} return r; } \
        V*r=v_fvec(n); for(int64_t i=0;i<n;i++){double x=AT==T_IVEC?(double)a->J[i]:a->F[i], \
            y=BT==T_IVEC?(double)b->J[i]:b->F[i]; \
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break; \
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:r->F[i]=y!=0?x/y:0;break; \
            case OP_FLOORDIV:r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:r->F[i]=y!=0?fmod(x,y):0;break; \
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return r; }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_IVEC||b->t==T_FVEC)) {
        VEC_BIN(a->t, b->t, a->J, b->J)
    }
    if((a->t==T_INT||a->t==T_FLOAT) && (b->t==T_IVEC||b->t==T_FVEC)) {
        int64_t n=b->n;
        int ui=(a->t==T_INT&&b->t==T_IVEC&&op!=OP_DIV&&op!=OP_POW);
        if(ui){ V*r=v_ivec(n); int64_t x=a->j; for(int64_t i=0;i<n;i++){int64_t y=b->J[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:r->J[i]=y?x/y:0;break; \
            case OP_MOD:r->J[i]=y?x%y:0;break;default:break;}} return r; }
        V*r=v_fvec(n); double x=to_float(a);
        for(int64_t i=0;i<n;i++){double y=b->t==T_IVEC?(double)b->J[i]:b->F[i];
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break;
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:r->F[i]=y!=0?x/y:0;break;
            case OP_FLOORDIV:r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:r->F[i]=y!=0?fmod(x,y):0;break;
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return r;
    }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_INT||b->t==T_FLOAT)) {
        int64_t n=a->n;
        int ui=(a->t==T_IVEC&&b->t==T_INT&&op!=OP_DIV&&op!=OP_POW);
        if(ui){ V*r=v_ivec(n); int64_t y=b->j; for(int64_t i=0;i<n;i++){int64_t x=a->J[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:r->J[i]=y?x/y:0;break; \
            case OP_MOD:r->J[i]=y?x%y:0;break;default:break;}} return r; }
        V*r=v_fvec(n); double y=to_float(b);
        for(int64_t i=0;i<n;i++){double x=a->t==T_IVEC?(double)a->J[i]:a->F[i];
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break;
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:r->F[i]=y!=0?x/y:0;break;
            case OP_FLOORDIV:r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:r->F[i]=y!=0?fmod(x,y):0;break;
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return r;
    }
    if(a->t==T_LIST && b->t==T_LIST && op==OP_ADD) {
        V *r = v_list(a->n + b->n);
        for(int64_t i=0;i<a->n;i++) r->L[i] = v_ref(a->L[i]);
        for(int64_t i=0;i<b->n;i++) r->L[a->n+i] = v_ref(b->L[i]);
        return r;
    }
    return v_errf("unsupported operand types for op %d: %s and %s", op, type_name(a->t), type_name(b->t));
    #undef VEC_BIN
}
V *vec_cmp(V *a, V *b, int op) {
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_BOOL) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_BOOL)) {
        double x = a->t==T_BOOL?(double)a->b:to_float(a);
        double y = b->t==T_BOOL?(double)b->b:to_float(b);
        int r;
        switch(op) {
        case OP_EQ: r=(x==y); break; case OP_NE: r=(x!=y); break;
        case OP_LT: r=(x<y); break;  case OP_GT: r=(x>y); break;
        case OP_LE: r=(x<=y); break; case OP_GE: r=(x>=y); break;
        default: r=0;
        }
        return v_bool(r);
    }
    if(a->t==T_STR && b->t==T_STR) {
        int c = strcmp(a->s, b->s);
        switch(op) {
        case OP_EQ: return v_bool(c==0); case OP_NE: return v_bool(c!=0);
        case OP_LT: return v_bool(c<0);  case OP_GT: return v_bool(c>0);
        case OP_LE: return v_bool(c<=0); case OP_GE: return v_bool(c>=0);
        default: return v_bool(0);
        }
    }
    if(a->t==T_DATETIME && b->t==T_DATETIME) {
        int64_t x = a->j, y = b->j;
        switch(op) {
        case OP_EQ: return v_bool(x==y); case OP_NE: return v_bool(x!=y);
        case OP_LT: return v_bool(x<y);  case OP_GT: return v_bool(x>y);
        case OP_LE: return v_bool(x<=y); case OP_GE: return v_bool(x>=y);
        default: return v_bool(0);
        }
    }
    if(a->t==T_DATE && b->t==T_DATE) {
        int64_t x = a->j, y = b->j;
        switch(op) {
        case OP_EQ: return v_bool(x==y); case OP_NE: return v_bool(x!=y);
        case OP_LT: return v_bool(x<y);  case OP_GT: return v_bool(x>y);
        case OP_LE: return v_bool(x<=y); case OP_GE: return v_bool(x>=y);
        default: return v_bool(0);
        }
    }
    if(a->t==T_TIME && b->t==T_TIME) {
        int64_t x = a->j, y = b->j;
        switch(op) {
        case OP_EQ: return v_bool(x==y); case OP_NE: return v_bool(x!=y);
        case OP_LT: return v_bool(x<y);  case OP_GT: return v_bool(x>y);
        case OP_LE: return v_bool(x<=y); case OP_GE: return v_bool(x>=y);
        default: return v_bool(0);
        }
    }
    if(a->t==T_NIL || b->t==T_NIL) {
        int both_nil = (a->t==T_NIL && b->t==T_NIL);
        P(op==OP_EQ,v_bool(both_nil))
        P(op==OP_NE,v_bool(!both_nil))
        return v_bool(0);
    }
    if (a->t == T_IVEC && b->t == T_INT) {
        int64_t n = a->n, y = b->j;
        V *r = v_bvec(n);
        const int64_t *aj = a->J;
        for (int64_t i = 0; i < n; i++) {
            int64_t x = aj[i];
            switch (op) {
            case OP_EQ: r->B[i] = (x == y); break;
            case OP_NE: r->B[i] = (x != y); break;
            case OP_LT: r->B[i] = (x < y); break;
            case OP_GT: r->B[i] = (x > y); break;
            case OP_LE: r->B[i] = (x <= y); break;
            case OP_GE: r->B[i] = (x >= y); break;
            default: break;
            }
        }
        return r;
    }
    if (a->t == T_INT && b->t == T_IVEC) {
        int64_t n = b->n, x = a->j;
        V *r = v_bvec(n);
        const int64_t *bj = b->J;
        for (int64_t i = 0; i < n; i++) {
            int64_t y = bj[i];
            switch (op) {
            case OP_EQ: r->B[i] = (x == y); break;
            case OP_NE: r->B[i] = (x != y); break;
            case OP_LT: r->B[i] = (x < y); break;
            case OP_GT: r->B[i] = (x > y); break;
            case OP_LE: r->B[i] = (x <= y); break;
            case OP_GE: r->B[i] = (x >= y); break;
            default: break;
            }
        }
        return r;
    }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_INT||b->t==T_FLOAT)) {
        int64_t n=a->n; V *r=v_bvec(n); double y=to_float(b);
        for(int64_t i=0;i<n;i++) {
            double x = a->t==T_IVEC?(double)a->J[i]:a->F[i];
            switch(op) {
            case OP_EQ: r->B[i]=(x==y); break; case OP_NE: r->B[i]=(x!=y); break;
            case OP_LT: r->B[i]=(x<y); break;  case OP_GT: r->B[i]=(x>y); break;
            case OP_LE: r->B[i]=(x<=y); break;  case OP_GE: r->B[i]=(x>=y); break;
            default: break;
            }
        }
        return r;
    }
    if((a->t==T_INT||a->t==T_FLOAT) && (b->t==T_IVEC||b->t==T_FVEC)) {
        int64_t n=b->n; V *r=v_bvec(n); double x=to_float(a);
        for(int64_t i=0;i<n;i++) {
            double y = b->t==T_IVEC?(double)b->J[i]:b->F[i];
            switch(op) {
            case OP_EQ: r->B[i]=(x==y); break; case OP_NE: r->B[i]=(x!=y); break;
            case OP_LT: r->B[i]=(x<y); break;  case OP_GT: r->B[i]=(x>y); break;
            case OP_LE: r->B[i]=(x<=y); break;  case OP_GE: r->B[i]=(x>=y); break;
            default: break;
            }
        }
        return r;
    }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_IVEC||b->t==T_FVEC) && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        for(int64_t i=0;i<a->n;i++) {
            double x = a->t==T_IVEC?(double)a->J[i]:a->F[i];
            double y = b->t==T_IVEC?(double)b->J[i]:b->F[i];
            P(x != y,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
    }
    if(a->t==T_LIST && (b->t==T_LIST||b->t==T_IVEC||b->t==T_FVEC) && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        for(int64_t i=0;i<a->n;i++) {
            V *belem;
            if(b->t==T_LIST) belem = b->L[i];
            else if(b->t==T_IVEC) belem = v_int(b->J[i]);
            else belem = v_float(b->F[i]);
            V *c = vec_cmp(a->L[i], belem, OP_EQ);
            int eq = c->t==T_BOOL && c->b;
            v_free(c); if(b->t != T_LIST) v_free(belem);
            P(!eq,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
    }
    if((a->t==T_IVEC||a->t==T_FVEC) && b->t==T_LIST && (op==OP_EQ||op==OP_NE)) {
        return vec_cmp(b, a, op);
    }
    if(a->t==T_LIST && b->t==T_STR) {
        int64_t n = a->n;
        V *r = v_bvec(n);
        for(int64_t i = 0; i < n; i++) {
            V *c = vec_cmp(a->L[i], b, op);
            r->B[i] = (c->t == T_BOOL && c->b) ? 1 : 0;
            v_free(c);
        }
        return r;
    }
    if(a->t==T_STR && b->t==T_LIST) {
        int64_t n = b->n;
        V *r = v_bvec(n);
        for(int64_t i = 0; i < n; i++) {
            V *c = vec_cmp(a, b->L[i], op);
            r->B[i] = (c->t == T_BOOL && c->b) ? 1 : 0;
            v_free(c);
        }
        return r;
    }
    if(a->t==T_DICT && b->t==T_DICT && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        for(int64_t i=0;i<a->n;i++) {
            V *ak = a->keys->L[i], *av = a->vals->L[i];
            int found = 0;
            for(int64_t j=0;j<b->n;j++) {
                V *kc = vec_cmp(ak, b->keys->L[j], OP_EQ);
                int keq = kc->t==T_BOOL && kc->b;
                v_free(kc);
                if(!keq) continue;
                V *vc = vec_cmp(av, b->vals->L[j], OP_EQ);
                int veq = vc->t==T_BOOL && vc->b;
                v_free(vc);
                P(!veq,v_bool(op==OP_NE))
                found = 1;
                break;
            }
            P(!found,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
    }
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT || b->t == T_BOOL)) {
        int64_t ne = a->n * mat_cols(a);
        V *r = v_bmat(a->n, mat_cols(a));
        double y = b->t == T_BOOL ? (double)b->b : to_float(b);
        if (a->t == T_FMAT)
            mat_fmat_cmp_bmat_scalar(r->B, a->F, y, ne, op);
        else if (a->t == T_IMAT)
            mat_imat_cmp_bmat_scalar(r->B, a->J, y, ne, op);
        else {
            for (int64_t i = 0; i < ne; i++) {
                double x = (double)a->B[i];
                int cmp = 0;
                switch (op) {
                case OP_EQ: cmp = (x == y); break;
                case OP_NE: cmp = (x != y); break;
                case OP_LT: cmp = (x < y); break;
                case OP_GT: cmp = (x > y); break;
                case OP_LE: cmp = (x <= y); break;
                case OP_GE: cmp = (x >= y); break;
                default: break;
                }
                r->B[i] = cmp ? 1 : 0;
            }
        }
        return r;
    }
    if ((a->t == T_INT || a->t == T_FLOAT || a->t == T_BOOL) && is_mat_t(b->t))
        return vec_cmp(b, a, op);
    if (is_mat_t(a->t) && is_mat_t(b->t) && (op == OP_EQ || op == OP_NE)) {
        P(a->n != b->n || mat_cols(a) != mat_cols(b), v_bool(op == OP_NE))
        int64_t ne = a->n * mat_cols(a);
        for (int64_t i = 0; i < ne; i++) {
            double x = a->t == T_IMAT ? (double)a->J[i] : (a->t == T_FMAT ? a->F[i] : (double)a->B[i]);
            double y = b->t == T_IMAT ? (double)b->J[i] : (b->t == T_FMAT ? b->F[i] : (double)b->B[i]);
            P(x != y, v_bool(op == OP_NE))
        }
        return v_bool(op == OP_EQ);
    }
    if (is_mat_t(a->t) && is_mat_t(b->t) && a->n == b->n && mat_cols(a) == mat_cols(b)) {
        int64_t ne = a->n * mat_cols(a);
        V *r = v_bmat(a->n, mat_cols(a));
        if (a->t == T_FMAT && b->t == T_FMAT)
            mat_fmat_cmp_bmat_mm(r->B, a->F, b->F, ne, op);
        else if (a->t == T_IMAT && b->t == T_IMAT)
            mat_imat_cmp_bmat_mm(r->B, a->J, b->J, ne, op);
        else {
            for (int64_t i = 0; i < ne; i++) {
                double x = a->t == T_IMAT ? (double)a->J[i] : (a->t == T_FMAT ? a->F[i] : (double)a->B[i]);
                double y = b->t == T_IMAT ? (double)b->J[i] : (b->t == T_FMAT ? b->F[i] : (double)b->B[i]);
                int cmp = 0;
                switch (op) {
                case OP_EQ: cmp = (x == y); break;
                case OP_NE: cmp = (x != y); break;
                case OP_LT: cmp = (x < y); break;
                case OP_GT: cmp = (x > y); break;
                case OP_LE: cmp = (x <= y); break;
                case OP_GE: cmp = (x >= y); break;
                default: break;
                }
                r->B[i] = cmp ? 1 : 0;
            }
        }
        return r;
    }
    return v_errf("cannot compare types %s and %s", type_name(a->t), type_name(b->t));
}
static V *table_filter(V *tbl, V *mask) {
    P(tbl->t != T_TABLE || mask->t != T_BVEC,v_err("bad filter"))
    int64_t nr = tbl->n, count=0;
    for(int64_t i=0;i<nr && i<mask->n;i++) if(mask->B[i]) count++;
    int nc = tbl->keys->n;
    V *new_data = v_list(nc);
    for(int c=0;c<nc;c++) {
        V *col = tbl->vals->L[c];
        if(col->t == T_IVEC) {
            V *nc2 = v_ivec(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->J[j++]=col->J[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_FVEC) {
            V *nc2 = v_fvec(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->F[j++]=col->F[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_LIST) {
            V *nc2 = v_list(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->L[j++]=v_ref(col->L[i]);
            new_data->L[c] = nc2;
        } else if(col->t == T_IMAT || col->t == T_FMAT || col->t == T_BMAT) {
            int64_t cols = mat_cols(col);
            if(col->t == T_IMAT) {
                V *nc2 = v_imat(count, cols);
                mat_filter_imat_rows(nc2->J, col->J, mask->B, nr, cols);
                new_data->L[c] = nc2;
            } else if(col->t == T_FMAT) {
                V *nc2 = v_fmat(count, cols);
                mat_filter_fmat_rows(nc2->F, col->F, mask->B, nr, cols);
                new_data->L[c] = nc2;
            } else {
                V *nc2 = v_bmat(count, cols);
                mat_filter_bmat_rows(nc2->B, col->B, mask->B, nr, cols);
                new_data->L[c] = nc2;
            }
        } else {
            new_data->L[c] = v_ref(col);
        }
    }
    return v_table(tbl->keys, new_data);
}
static V *eval_slice(V *obj, V *start_v, V *stop_v, V *step_v) {
    int64_t len = 0;
    if(obj->t==T_STR) len = strlen(obj->s);
    else if(obj->t==T_IVEC||obj->t==T_FVEC||obj->t==T_BVEC||obj->t==T_LIST) len = obj->n;
    else if(is_mat_t(obj->t)) len = obj->n;
    else return v_err("object is not sliceable");
    int64_t step  = step_v->t==T_NIL  ? 1 : step_v->j;
    P(step == 0,v_err("slice step cannot be zero"))
    int64_t start = start_v->t==T_NIL ? (step>0 ? 0 : len-1) : start_v->j;
    int64_t stop  = stop_v->t==T_NIL  ? (step>0 ? len : -len-1) : stop_v->j;
    if (start < 0) start += len;
    if (start < 0) start = step > 0 ? 0 : -1;
    if (stop < 0) stop += len;
    if (stop < 0) stop = step > 0 ? 0 : -1;
    if(start >= len) start = step>0 ? len : len-1;
    if(stop > len) stop = len;
    int64_t count = 0;
    if(step > 0) { for(int64_t i=start;i<stop;i+=step) count++; }
    else { for(int64_t i=start;i>stop;i+=step) count++; }
    if(obj->t==T_STR) {
        char *r = malloc(count+1);
        int64_t j=0;
        if(step > 0) { for(int64_t i=start;i<stop;i+=step) r[j++]=obj->s[i]; }
        else { for(int64_t i=start;i>stop;i+=step) r[j++]=obj->s[i]; }
        r[j]=0;
        V *v = v_str(r); free(r); return v;
    }
    if(obj->t==T_IVEC) {
        V *r=v_ivec(count); int64_t j=0;
        if(step>0) { for(int64_t i=start;i<stop;i+=step) r->J[j++]=obj->J[i]; }
        else { for(int64_t i=start;i>stop;i+=step) r->J[j++]=obj->J[i]; }
        return r;
    }
    if(obj->t==T_FVEC) {
        V *r=v_fvec(count); int64_t j=0;
        if(step>0) { for(int64_t i=start;i<stop;i+=step) r->F[j++]=obj->F[i]; }
        else { for(int64_t i=start;i>stop;i+=step) r->F[j++]=obj->F[i]; }
        return r;
    }
    if(obj->t==T_LIST) {
        V *r=v_list(count); int64_t j=0;
        if(step>0) { for(int64_t i=start;i<stop;i+=step) r->L[j++]=v_ref(obj->L[i]); }
        else { for(int64_t i=start;i>stop;i+=step) r->L[j++]=v_ref(obj->L[i]); }
        return r;
    }
    if(is_mat_t(obj->t)) {
        int64_t cols = mat_cols(obj);
        V *r = obj->t == T_IMAT ? (V *)v_imat(count, cols)
            : (obj->t == T_FMAT ? (V *)v_fmat(count, cols) : (V *)v_bmat(count, cols));
        int64_t j = 0;
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step, j++) {
                if (obj->t == T_IMAT)
                    memcpy(r->J + mat_idx(r, j, 0), obj->J + mat_idx(obj, i, 0), (size_t)cols * 8);
                else if (obj->t == T_FMAT)
                    memcpy(r->F + mat_idx(r, j, 0), obj->F + mat_idx(obj, i, 0), (size_t)cols * 8);
                else
                    memcpy(r->B + mat_idx(r, j, 0), obj->B + mat_idx(obj, i, 0), (size_t)cols);
            }
        } else {
            for (int64_t i = start; i > stop; i += step, j++) {
                if (obj->t == T_IMAT)
                    memcpy(r->J + mat_idx(r, j, 0), obj->J + mat_idx(obj, i, 0), (size_t)cols * 8);
                else if (obj->t == T_FMAT)
                    memcpy(r->F + mat_idx(r, j, 0), obj->F + mat_idx(obj, i, 0), (size_t)cols * 8);
                else
                    memcpy(r->B + mat_idx(r, j, 0), obj->B + mat_idx(obj, i, 0), (size_t)cols);
            }
        }
        return r;
    }
    return v_nil();
}
static void for_set_vars(Node *vars, V *item, Env *e) {
    if(vars->type == N_LIST) {
        if(item->t == T_LIST) {
            for(int j=0; j<vars->nch && j<item->n; j++)
                env_set(e, vars->ch[j]->sval, item->L[j]);
        }
    } else {
        env_set(e, vars->sval, item);
    }
}
static const char *SHAKTI_SQL_FLAG = "__shakti_sql__";
static int is_sql_import(const char *name) {
    P(!name || !name[0],0)
    if (!strcmp(name, "sql")) return 1;
    const char *dot = strrchr(name, '.');
    return dot && !strcmp(dot + 1, "sql");
}
static int shakti_sql_enabled(Env *e) {
    V *v = env_get(e, SHAKTI_SQL_FLAG);
    return v && v->t == T_BOOL && v->b;
}
static V *require_sql(Env *e) {
    P(!shakti_sql_enabled(e),v_err("SQL requires: import sql"))
    return NULL;
}
static V *do_import(const char *name, Env *e) {
    P(!name || !name[0],v_err("import requires a module name"))
    char path[8192];
    FILE *f = NULL;
    f = fopen(name, "r");
    if(!f) { snprintf(path,sizeof(path),"%s.ie",name); f=fopen(path,"r"); }
    if(!f) { snprintf(path,sizeof(path),"%s/%s",g_script_dir,name); f=fopen(path,"r"); }
    if(!f) { snprintf(path,sizeof(path),"%s/%s.ie",g_script_dir,name); f=fopen(path,"r"); }
    if(!f && g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s",g_lib_path,name); f=fopen(path,"r"); }
    if(!f && g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s.ie",g_lib_path,name); f=fopen(path,"r"); }
    if(!f) {
        const char *env = getenv("SHAKTI_LIB");
        if(env) {
            snprintf(path,sizeof(path),"%s/%s",env,name); f=fopen(path,"r");
            if(!f) { snprintf(path,sizeof(path),"%s/%s.ie",env,name); f=fopen(path,"r"); }
        }
    }
    if(!f) {
        char dotpath[8192];
        strncpy(dotpath, name, sizeof(dotpath)-1);
        for(char *p=dotpath; *p; p++) if(*p=='.') *p='/';
        if(g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s.ie",g_lib_path,dotpath); f=fopen(path,"r"); }
        if(!f) { snprintf(path,sizeof(path),"%s.ie",dotpath); f=fopen(path,"r"); }
    }
    P(!f,v_errf("cannot import '%s'", name))
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz+2);
    fread(buf, 1, sz, f); buf[sz]='\n'; buf[sz+1]=0;
    fclose(f);
    Env *mod_env = env_new(e);
    Node *prog = parse(buf);
    V *r = eval(prog, mod_env);
    v_free(r);
    free(buf);
    V *mk = v_list(mod_env->len), *mv = v_list(mod_env->len);
    i(mod_env->len,{
        mk->L[i] = v_str(mod_env->names[i]);
        mv->L[i] = v_ref(mod_env->vals[i]);
    })
    V *mod_dict = v_dict(mk, mv);
    v_free(mk); v_free(mv);
    char *dot = strchr(name, '.');
    if(dot) {
        char parent[256];
        int plen = dot - name;
        memcpy(parent, name, plen); parent[plen] = 0;
        const char *child = dot + 1;
        V *existing = env_get(e, parent);
        if(existing && existing->t == T_DICT) {
            v_dict_set(existing, child, mod_dict);
        } else {
            V *nk = v_list(1), *nv = v_list(1);
            nk->L[0] = v_str(child);
            nv->L[0] = v_ref(mod_dict);
            V *ns = v_dict(nk, nv);
            env_set(e, parent, ns);
            v_free(nk); v_free(nv); v_free(ns);
        }
    } else {
        env_set(e, name, mod_dict);
    }
    if (is_sql_import(name))
        env_set(e, SHAKTI_SQL_FLAG, v_bool(1));
    v_free(mod_dict);
    env_free(mod_env);
    return v_nil();
}
static V *eval_select_name(Node *n, Env *e) {
    P(!n,v_nil())
    P(n->type == N_NAME,v_str(n->sval))
    if(n->type == N_LIST) {
        V *r = v_list(n->nch);
        i(n->nch,r->L[i] = eval_select_name(n->ch[i], e))
        return r;
    }
    return eval(n, e);
}
static V *eval_select_cols(Node *n, Env *e) {
    P(!n,v_nil())
    P(n->type != N_LIST,eval_select_name(n, e))
    V *r = v_list(n->nch);
    i(n->nch,r->L[i] = eval_select_name(n->ch[i], e))
    return r;
}
static V *eval_with_table_columns(V *tbl, Node *expr, Env *e) {
    P(!expr || expr->type == N_NONE,v_nil())
    Env *inner = env_new(e);
    if(tbl && tbl->t == T_TABLE) {
        V *cn = tbl->keys, *dv = tbl->vals;
        for(int64_t i = 0; i < cn->n; i++)
            env_set(inner, cn->L[i]->s, dv->L[i]);
    }
    V *r = eval(expr, inner);
    env_free(inner);
    return r;
}
static V *eval_name_list(Node *n) {
    P(!n || n->type == N_NONE,v_nil())
    P(n->type == N_NAME,v_str(n->sval))
    P(n->type != N_LIST,v_err("expected column name list"))
    V *r = v_list(n->nch);
    for(int i = 0; i < n->nch; i++) {
        if(n->ch[i]->type != N_NAME)
            { v_free(r); return v_err("expected column name"); }
        r->L[i] = v_str(n->ch[i]->sval);
    }
    return r;
}
static V *eval_update_cols(Node *n, V *tbl, Env *e) {
    P(!n || n->type == N_NONE,v_dict(v_list(0), v_list(0)))
    Env *inner = env_new(e);
    if(tbl && tbl->t == T_TABLE) {
        V *cn = tbl->keys, *dv = tbl->vals;
        for(int64_t i = 0; i < cn->n; i++)
            env_set(inner, cn->L[i]->s, dv->L[i]);
    }
    V *keys = v_list(0), *vals = v_list(0);
    Node **items = n->type == N_LIST ? n->ch : &n;
    int nitems = n->type == N_LIST ? n->nch : 1;
    i(nitems,{
        Node *item = items[i];
        if(item->type == N_ASSIGN && item->nch >= 2 && item->ch[0]->type == N_NAME) {
            v_list_append(keys, v_str(item->ch[0]->sval));
            v_list_append(vals, eval(item->ch[1], inner));
        } else {
            env_free(inner);
            v_free(keys);
            v_free(vals);
            return v_err("update: expected col: expr");
        }
    })
    env_free(inner);
    V *r = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    return r;
}
static V *eval_create_schema(Node *n, Env *e) {
    P(!n || n->type != N_LIST,v_err("create table: bad schema"))
    V *keys = v_list(0), *vals = v_list(0);
    i(n->nch,{
        Node *col = n->ch[i];
        if(col->type != N_KWARG) {
            v_free(keys); v_free(vals);
            return v_err("create table: bad column");
        }
        v_list_append(keys, v_str(col->sval));
        if(col->nch > 0 && col->ch[0]->type != N_NONE)
            v_list_append(vals, eval(col->ch[0], e));
        else
            v_list_append(vals, v_nil());
    })
    V *r = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    return r;
}
static int select_sym_is_agg_kw(const char *s) {
    static const char *kws[] = {"count", "sum", "avg", "min", "max", NULL};
    for (int k = 0; kws[k]; k++) {
        if (!strcmp(s, kws[k])) {
            return 1;
        }
    }
    return 0;
}
static void select_strlist_add_unique(V *acc, const char *s) {
    if (!acc || acc->t != T_LIST || !s) {
        return;
    }
    for (int64_t i = 0; i < acc->n; i++) {
        V *v = acc->L[i];
        if (v && v->t == T_STR && !strcmp(v->s, s)) {
            return;
        }
    }
    v_list_append(acc, v_str(s));
}
static void select_collect_syms(Node *n, V *acc) {
    if (!n) {
        return;
    }
    switch (n->type) {
    case N_NAME:
        if (!select_sym_is_agg_kw(n->sval)) {
            select_strlist_add_unique(acc, n->sval);
        }
        return;
    case N_CALL:
        for (int i = 1; i < n->nch; i++) {
            select_collect_syms(n->ch[i], acc);
        }
        return;
    case N_LIST:
        for (int i = 0; i < n->nch; i++) {
            select_collect_syms(n->ch[i], acc);
        }
        return;
    case N_DOT:
        select_collect_syms(n->ch[0], acc);
        return;
    default:
        for (int i = 0; i < n->nch; i++) {
            select_collect_syms(n->ch[i], acc);
        }
        return;
    }
}
static V *select_load_projection(Node *sel) {
    V *acc = v_list(0);
    if (!sel || sel->type != N_SELECT) {
        v_free(acc);
        return NULL;
    }
    if (sel->ch[1]) {
        select_collect_syms(sel->ch[1], acc);
    }
    if (sel->ch[2] && sel->ch[2]->type != N_NONE) {
        select_collect_syms(sel->ch[2], acc);
    }
    if (sel->ch[3] && sel->ch[3]->type != N_NONE) {
        select_collect_syms(sel->ch[3], acc);
    }
    if (acc->n == 0) {
        v_free(acc);
        return NULL;
    }
    return acc;
}
V *eval(Node *n, Env *e) {
    P(!n,v_nil())
    P(g_returning || g_breaking || g_continuing || g_error,v_nil())
    switch(n->type) {
    case N_INT:  return v_int(n->ival);
    case N_FLOAT:return v_float(n->fval);
    case N_STR:  return v_str(n->sval);
    case N_BOOL: return v_bool(n->ival);
    case N_NONE: return v_nil();
    case N_PASS: return v_nil();
    case N_DATETIME: return v_datetime(n->ival);
    case N_SELECT: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *from0 = eval(n->ch[0], e);
        V *from;
        if (from0->t == T_STR) {
            V *proj = select_load_projection(n);
            from = table_load(from0->s, proj);
            if (proj) {
                v_free(proj);
            }
            v_free(from0);
            P(from->t == T_ERR,from)
        } else {
            from = from0;
        }
        V *cols = eval_select_cols(n->ch[1], e);
        V *by = eval_select_name(n->ch[2], e);
        V *where = eval_with_table_columns(from, n->ch[3], e);
        V *r = table_sql_select(from, cols, by, where);
        v_free(from);
        v_free(cols);
        v_free(by);
        v_free(where);
        return r;
    }
    case N_UPDATE: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *from = eval(n->ch[0], e);
        P(from->t == T_ERR,from)
        V *assignments = eval_update_cols(n->ch[1], from, e);
        P(assignments->t == T_ERR,(v_free(from),assignments))
        V *where = eval_with_table_columns(from, n->ch[3], e);
        V *r = table_sql_update(from, assignments, where);
        if(r && r->t != T_ERR && n->ch[0] && n->ch[0]->type == N_NAME)
            env_update(e, n->ch[0]->sval, r);
        v_free(from); v_free(assignments); v_free(where);
        return r;
    }
    case N_DELETE: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *from = eval(n->ch[0], e);
        P(from->t == T_ERR,from)
        V *cols = eval_select_cols(n->ch[1], e);
        V *where = eval_with_table_columns(from, n->ch[3], e);
        V *r = table_sql_delete(from, cols, where);
        if(r && r->t != T_ERR && n->ch[0] && n->ch[0]->type == N_NAME)
            env_update(e, n->ch[0]->sval, r);
        v_free(from); v_free(cols); v_free(where);
        return r;
    }
    case N_CREATE_TABLE: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *schema = eval_create_schema(n->ch[0], e);
        P(schema->t == T_ERR,schema)
        V *name_v = v_str(n->sval);
        V *r = table_sql_create_table(name_v, schema);
        env_set(e, n->sval, r);
        v_free(schema); v_free(name_v);
        return r;
    }
    case N_INSERT: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *existing = env_get(e, n->sval);
        P(!existing,v_errf("insert: table '%s' not found", n->sval))
        V *cols = eval_name_list(n->ch[0]);
        P(cols->t == T_ERR,(cols))
        V *vals = eval(n->ch[1], e);
        V *r = table_sql_insert(existing, cols, vals);
        if (r && r->t != T_ERR && r != existing) env_update(e, n->sval, r);
        v_free(cols); v_free(vals);
        return r;
    }
    case N_JOIN: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *left = eval(n->ch[0], e);
        V *right = eval(n->ch[1], e);
        V *on_col = v_str(n->sval);
        V *r = table_sql_join(left, right, on_col);
        v_free(left); v_free(right); v_free(on_col);
        return r;
    }
    case N_NAME: {
        V *v = env_get(e, n->sval);
        P(v,v_ref(v))
        if(is_builtin(n->sval)) {
            V *f = v_alloc(T_FN);
            f->s = strdup(n->sval);
            f->n = -1;
            return f;
        }
        return v_errf("name '%s' is not defined", n->sval);
    }
    case N_FSTRING: {
        const char *s = n->sval;
        char *result = malloc(8192);
        int rlen = 0; result[0] = 0;
        int i = 0, slen = strlen(s);
        while(i < slen) {
            if(s[i] == '{' && i+1 < slen && s[i+1] != '{') {
                i++;
                int start = i, depth = 1;
                while(i < slen && depth > 0) {
                    if(s[i]=='{') depth++;
                    else if(s[i]=='}') { depth--; if(depth==0) break; }
                    i++;
                }
                int elen = i - start;
                char *raw = malloc(elen+1);
                memcpy(raw, s+start, elen); raw[elen]=0;
                char *fmt_spec = NULL;
                int bd=0;
                for(int k=elen-1;k>=0;k--){
                    if(raw[k]==']'||raw[k]==')'||raw[k]=='}')bd++;
                    else if(raw[k]=='['||raw[k]=='('||raw[k]=='{')bd--;
                    else if(raw[k]==':'&&bd==0){fmt_spec=raw+k+1;raw[k]=0;break;}
                }
                char *expr = malloc(strlen(raw)+2);
                sprintf(expr,"%s\n",raw);
                Node *ast = parse(expr);
                V *val = eval(ast, e);
                char *vs;
                if(fmt_spec && *fmt_spec) {
                    char fmt[64]; snprintf(fmt,sizeof(fmt),"%%%s",fmt_spec);
                    char buf[256];
                    if(val->t==T_FLOAT) snprintf(buf,sizeof(buf),fmt,val->f);
                    else if(val->t==T_INT) snprintf(buf,sizeof(buf),fmt,(double)val->j);
                    else { vs=v_to_str(val); snprintf(buf,sizeof(buf),"%s",vs); free(vs); }
                    vs=strdup(buf);
                } else vs = v_to_str(val);
                int vslen = strlen(vs);
                result = realloc(result, rlen+vslen+256);
                memcpy(result+rlen, vs, vslen);
                rlen += vslen;
                free(vs); v_free(val); free(expr); free(raw);
                if(i < slen) i++;
            } else {
                result = realloc(result, rlen+2);
                result[rlen++] = s[i++];
            }
        }
        result[rlen] = 0;
        V *r = v_str(result);
        free(result);
        return r;
    }
    case N_LIST: {
        int nch = n->nch;
        V **elems = calloc(nch?nch:1, sizeof(V*));
        int all_int=1, all_num=1, all_bool=1;
        for(int i=0;i<nch;i++) {
            elems[i] = eval(n->ch[i], e);
            if(g_error) { for(int j=0;j<=i;j++) v_free(elems[j]); free(elems); return v_nil(); }
            if(elems[i]->t != T_INT) all_int = 0;
            if(elems[i]->t != T_INT && elems[i]->t != T_FLOAT) all_num = 0;
            if(elems[i]->t != T_BOOL) all_bool = 0;
        }
        if(nch > 0 && all_int) {
            V *r = v_ivec(nch);
            i(nch,{r->J[i]=elems[i]->j; v_free(elems[i]);})
            free(elems); return r;
        }
        if(nch > 0 && all_num) {
            V *r = v_fvec(nch);
            i(nch,{r->F[i]=to_float(elems[i]); v_free(elems[i]);})
            free(elems); return r;
        }
        if(nch > 0 && all_bool) {
            V *r = v_bvec(nch);
            i(nch,{r->B[i]=elems[i]->b?1:0; v_free(elems[i]);})
            free(elems); return r;
        }
        {
            V *mat = try_promote_matrix(elems, nch);
            if (mat) {
                for (int i = 0; i < nch; i++) v_free(elems[i]);
                free(elems);
                return mat;
            }
        }
        V *r = v_list(nch);
        i(nch,r->L[i] = elems[i])
        free(elems);
        return r;
    }
    case N_DICT: {
        int np = n->nch / 2;
        V *keys = v_list(np);
        V *vals = v_list(np);
        i(np,{
            keys->L[i] = eval(n->ch[i*2], e);
            vals->L[i] = eval(n->ch[i*2+1], e);
        })
        V *r = v_dict(keys, vals);
        v_free(keys); v_free(vals);
        return r;
    }
    case N_BINOP: {
        if(n->op == OP_AND) {
            V *a = eval(n->ch[0], e);
            P(a->t==T_ERR,a)
            if(a->t==T_BVEC||a->t==T_IVEC||a->t==T_FVEC){
                V *b = eval(n->ch[1], e);
                if(b->t==T_ERR){ v_free(a); return b; }
                V *r = vec_logic(a, b, 1);
                v_free(a); v_free(b);
                return r;
            }
            P(!is_truthy(a),a)
            v_free(a);
            return eval(n->ch[1], e);
        }
        if(n->op == OP_OR) {
            V *a = eval(n->ch[0], e);
            P(a->t==T_ERR,a)
            if(a->t==T_BVEC||a->t==T_IVEC||a->t==T_FVEC){
                V *b = eval(n->ch[1], e);
                if(b->t==T_ERR){ v_free(a); return b; }
                V *r = vec_logic(a, b, 0);
                v_free(a); v_free(b);
                return r;
            }
            P(is_truthy(a),a)
            v_free(a);
            return eval(n->ch[1], e);
        }
        V *a = eval(n->ch[0], e);
        V *b = eval(n->ch[1], e);
        P(a->t==T_ERR,(v_free(b),a))
        P(b->t==T_ERR,(v_free(a),b))
        if (n->op == OP_MATMUL) {
            V *r = mat_matmul(a, b);
            v_free(a); v_free(b);
            return r;
        }
        V *r = vec_binop(a, b, n->op);
        v_free(a); v_free(b);
        return r;
    }
    case N_CMP: {
        V *a = eval(n->ch[0], e);
        V *b = eval(n->ch[1], e);
        P(a->t==T_ERR,(v_free(b),a))
        P(b->t==T_ERR,(v_free(a),b))
        if(n->op == OP_IN || n->op == OP_NOT_IN) {
            int found = 0;
            if(b->t==T_LIST) {
                for(int64_t i=0;i<b->n;i++) {
                    V *c = vec_cmp(a, b->L[i], OP_EQ);
                    if(c->t==T_BOOL && c->b) { found=1; v_free(c); break; }
                    v_free(c);
                }
            } else if(b->t==T_IVEC && a->t==T_INT) {
                for(int64_t i=0;i<b->n;i++) if(b->J[i]==a->j) { found=1; break; }
            } else if(b->t==T_STR && a->t==T_STR) {
                found = strstr(b->s, a->s) != NULL;
            } else if(b->t==T_DICT && a->t==T_STR) {
                found = v_dict_get(b, a->s) != NULL;
            } else if(b->t==T_DICT) {
                for(int64_t i=0;i<b->n;i++) {
                    V *c = vec_cmp(a, b->keys->L[i], OP_EQ);
                    if(c->t==T_BOOL && c->b) { found=1; v_free(c); break; }
                    v_free(c);
                }
            }
            v_free(a); v_free(b);
            return v_bool(n->op == OP_IN ? found : !found);
        }
        V *r = vec_cmp(a, b, n->op);
        v_free(a); v_free(b);
        return r;
    }
    case N_UNOP: {
        V *a = eval(n->ch[0], e);
        if(n->op == OP_NEG) {
            if(a->t==T_INT)  { V *r=v_int(-a->j); v_free(a); return r; }
            if(a->t==T_FLOAT){ V *r=v_float(-a->f); v_free(a); return r; }
            if(a->t==T_IVEC) { V *r=v_ivec(a->n); for(int64_t i=0;i<a->n;i++) r->J[i]=-a->J[i]; v_free(a); return r; }
            if(a->t==T_FVEC) { V *r=v_fvec(a->n); for(int64_t i=0;i<a->n;i++) r->F[i]=-a->F[i]; v_free(a); return r; }
            if(a->t==T_IMAT) { int64_t ne=a->n*mat_cols(a); V *r=v_imat(a->n,mat_cols(a)); for(int64_t i=0;i<ne;i++) r->J[i]=-a->J[i]; v_free(a); return r; }
            if(a->t==T_FMAT) { int64_t ne=a->n*mat_cols(a); V *r=v_fmat(a->n,mat_cols(a)); for(int64_t i=0;i<ne;i++) r->F[i]=-a->F[i]; v_free(a); return r; }
        }
        if(n->op == OP_NOT) { int r = !is_truthy(a); v_free(a); return v_bool(r); }
        v_free(a);
        return v_err("bad unop");
    }
    case N_ASSIGN: {
        Node *target = n->ch[0];
        V *val = eval(n->ch[1], e);
        if(g_error) { v_free(val); return v_nil(); }
        if(target->type == N_LIST && (val->t==T_LIST||val->t==T_IVEC||val->t==T_FVEC)) {
            for(int i=0; i<target->nch && i<val->n; i++) {
                if(target->ch[i]->type == N_NAME) {
                    V *elem;
                    if(val->t==T_LIST) elem=val->L[i];
                    else if(val->t==T_IVEC) elem=v_int(val->J[i]);
                    else elem=v_float(val->F[i]);
                    env_set(e, target->ch[i]->sval, elem);
                    if(val->t!=T_LIST) v_free(elem);
                }
            }
            v_free(val);
            return v_nil();
        }
        if(target->type == N_NAME) {
            env_set(e, target->sval, val);
            V *r = v_ref(val); v_free(val); return r;
        }
        if(target->type == N_INDEX) {
            V *obj = eval(target->ch[0], e);
            if (is_mat_t(obj->t) && target->nch >= 3) {
                V *idx0 = eval(target->ch[1], e);
                V *idx1 = eval(target->ch[2], e);
                if (idx0->t == T_INT && idx1->t == T_INT) {
                    int64_t r = idx0->j, c = idx1->j;
                    if (r < 0) r += obj->n;
                    if (c < 0) c += mat_cols(obj);
                    if (r >= 0 && r < obj->n && c >= 0 && c < mat_cols(obj)) {
                        if (obj->t == T_IMAT && val->t == T_INT)
                            obj->J[mat_idx(obj, r, c)] = val->j;
                        else if (obj->t == T_FMAT && (val->t == T_FLOAT || val->t == T_INT))
                            obj->F[mat_idx(obj, r, c)] = to_float(val);
                        else if (obj->t == T_BMAT && val->t == T_BOOL)
                            obj->B[mat_idx(obj, r, c)] = val->b ? 1 : 0;
                    }
                }
                v_free(obj); v_free(idx0); v_free(idx1); v_free(val);
                return v_nil();
            }
            V *idx = eval(target->ch[1], e);
            if(obj->t==T_DICT) {
                if(idx->t==T_STR) {
                    v_dict_set(obj, idx->s, val);
                } else if(idx->t==T_INT) {
                    int64_t i = idx->j;
                    if(i<0) i+=obj->n;
                    if(i>=0 && i<obj->n) {
                        v_free(obj->vals->L[i]);
                        obj->vals->L[i] = v_ref(val);
                    }
                }
                v_free(obj); v_free(idx); v_free(val);
                return v_nil();
            }
            if(idx->t==T_INT) {
                int64_t i = idx->j;
                if(i < 0) i += obj->n;
                if(i >= 0 && i < obj->n) {
                    if(obj->t==T_LIST) { v_free(obj->L[i]); obj->L[i] = v_ref(val); }
                    else if(obj->t==T_IVEC && val->t==T_INT) obj->J[i] = val->j;
                    else if(obj->t==T_FVEC && (val->t==T_FLOAT||val->t==T_INT))
                        obj->F[i] = val->t==T_FLOAT ? val->f : (double)val->j;
                    else if(is_mat_t(obj->t) && target->nch == 2) {
                        int64_t cols = mat_cols(obj);
                        if(obj->t==T_IMAT && val->t==T_IVEC && val->n==cols)
                            memcpy(obj->J + mat_idx(obj, i, 0), val->J, (size_t)cols * 8);
                        else if(obj->t==T_FMAT && val->t==T_FVEC && val->n==cols)
                            memcpy(obj->F + mat_idx(obj, i, 0), val->F, (size_t)cols * 8);
                        else if(obj->t==T_BMAT && val->t==T_BVEC && val->n==cols)
                            memcpy(obj->B + mat_idx(obj, i, 0), val->B, (size_t)cols);
                    }
                }
            }
            v_free(obj); v_free(idx); v_free(val);
            return v_nil();
        }
        if(target->type == N_DOT) {
            V *obj = eval(target->ch[0], e);
            if(obj->t == T_DICT) {
                v_dict_set(obj, target->sval, val);
            } else if(obj->t == T_TABLE) {
                int found = -1;
                for(int i=0; i<obj->keys->n; i++) {
                    if(!strcmp(obj->keys->L[i]->s, target->sval)) { found=i; break; }
                }
                if(found >= 0) {
                    v_free(obj->vals->L[found]);
                    obj->vals->L[found] = v_ref(val);
                } else {
                    V *k = v_str(target->sval);
                    v_list_append(obj->keys, k);
                    v_list_append(obj->vals, val);
                    v_free(k);
                }
            }
            v_free(obj); v_free(val);
            return v_nil();
        }
        v_free(val);
        return v_err("bad assignment target");
    }
    case N_AUGASSIGN: {
        Node *target = n->ch[0];
        if(target->type == N_NAME) {
            V *cur = env_get(e, target->sval);
            P(!cur,v_errf("name '%s' is not defined", target->sval))
            V *delta = eval(n->ch[1], e);
            V *newval = vec_binop(cur, delta, n->op);
            env_set(e, target->sval, newval);
            v_free(delta);
            V *r = v_ref(newval); v_free(newval); return r;
        }
        if(target->type == N_INDEX) {
            V *obj = eval(target->ch[0], e);
            V *idx = eval(target->ch[1], e);
            V *delta = eval(n->ch[1], e);
            if(obj->t==T_DICT && idx->t==T_STR) {
                V *cur = v_dict_get(obj, idx->s);
                if(cur) {
                    V *newval = vec_binop(cur, delta, n->op);
                    v_dict_set(obj, idx->s, newval);
                    v_free(newval);
                }
            }
            v_free(obj); v_free(idx); v_free(delta);
            return v_nil();
        }
        return v_err("bad augmented assignment target");
    }
    case N_INDEX: {
        V *obj = eval(n->ch[0], e);
        P(n->nch < 2, obj)
        if (obj->t == T_ERR) return obj;
        if(obj->t == T_FN) {
            V *al = v_list(n->nch - 1);
            for(int i=1; i<n->nch; i++) al->L[i-1] = eval(n->ch[i], e);
            V *r = builtin_call("__invoke__", (V*[]){obj, al}, 2, NULL, NULL, 0, e);
            v_free(obj); v_free(al); return r;
        }
        for(int i=1; i<n->nch; i++) {
            if (obj->t == T_ERR) break;
            V *idx = eval(n->ch[i], e);
            V *next = NULL;
            if(obj->t == T_TABLE && idx->t == T_BVEC) {
                next = table_filter(obj, idx);
            } else if(obj->t == T_TABLE && idx->t == T_STR) {
                V *cols = obj->keys;
                for(int64_t j=0;j<cols->n;j++) {
                    if(strcmp(cols->L[j]->s, idx->s)==0) {
                        next = v_ref(obj->vals->L[j]); break;
                    }
                }
                if(!next) next = v_errf("table has no column '%s'", idx->s);
            } else if(is_mat_t(obj->t) && idx->t==T_INT) {
                int64_t j = idx->j;
                if(j < 0) j += obj->n;
                if(j >= 0 && j < obj->n) next = v_mat_row(obj, j);
                else next = v_err("index out of range");
            } else if((obj->t==T_IVEC||obj->t==T_FVEC||obj->t==T_LIST) && idx->t==T_INT) {
                int64_t j = idx->j;
                if(j < 0) j += obj->n;
                if(j >= 0 && j < obj->n) {
                    if(obj->t==T_IVEC) next = v_int(obj->J[j]);
                    else if(obj->t==T_FVEC) next = v_float(obj->F[j]);
                    else next = v_ref(obj->L[j]);
                } else next = v_err("index out of range");
            } else if(obj->t==T_DICT) {
                if(idx->t==T_STR) {
                    V *found = v_dict_get(obj, idx->s);
                    next = found ? v_ref(found) : v_nil();
                } else {
                    next = v_nil();
                    for(int64_t j=0;j<obj->n;j++) {
                        V *c = vec_cmp(idx, obj->keys->L[j], OP_EQ);
                        if(c->t==T_BOOL && c->b) { v_free(next); next = v_ref(obj->vals->L[j]); v_free(c); break; }
                        v_free(c);
                    }
                }
            } else if(obj->t==T_STR && idx->t==T_INT) {
                int64_t j = idx->j;
                int64_t slen = strlen(obj->s);
                if(j < 0) j += slen;
                if(j >= 0 && j < slen) {
                    char buf[2] = {obj->s[j], 0};
                    next = v_str(buf);
                } else next = v_err("string index out of range");
            } else {
                next = v_errf("cannot index type %s with type %s", type_name(obj->t), type_name(idx->t));
            }
            v_free(obj); v_free(idx);
            obj = next;
            if(obj->t == T_ERR) break;
        }
        return obj;
    }
    case N_SLICE: {
        V *obj = eval(n->ch[0], e);
        V *start_v = eval(n->ch[1], e);
        V *stop_v = eval(n->ch[2], e);
        V *step_v = n->nch > 3 ? eval(n->ch[3], e) : v_int(1);
        V *r = eval_slice(obj, start_v, stop_v, step_v);
        v_free(obj); v_free(start_v); v_free(stop_v); v_free(step_v);
        return r;
    }
    case N_DOT: {
        V *obj = eval(n->ch[0], e);
        if(obj->t == T_TABLE) {
            V *cols = obj->keys;
            for(int64_t i=0;i<cols->n;i++) {
                if(strcmp(cols->L[i]->s, n->sval)==0) {
                    V *r = v_ref(obj->vals->L[i]); v_free(obj); return r;
                }
            }
            v_free(obj);
            return v_errf("table has no column '%s'", n->sval);
        }
        if(obj->t == T_DICT) {
            V *found = v_dict_get(obj, n->sval);
            if(found) { V *r = v_ref(found); v_free(obj); return r; }
        }
        v_free(obj);
        return v_errf("attribute '%s' not found", n->sval);
    }
    case N_CALL: {
        Node *fn_node = n->ch[0];
        if(fn_node->type == N_DOT) {
            V *obj = eval(fn_node->ch[0], e);
            P(obj->t == T_ERR,obj)
            const char *method = fn_node->sval;
            int nargs_total = n->nch - 1;
            V **args = calloc(nargs_total+1, sizeof(V*));
            int nargs = 0;
            for(int i=1; i<n->nch; i++) {
                if(n->ch[i]->type != N_KWARG)
                    args[nargs++] = eval(n->ch[i], e);
            }
            V *attr = NULL;
            if(obj->t == T_DICT) {
                attr = v_dict_get(obj, method);
            }
            if(attr && attr->t == T_FN) {
                Env *call_env = env_new(attr->closure);
                V *params = attr->params;
                for(int i=0; i<params->n && i<nargs; i++)
                    env_set(call_env, params->L[i]->s, args[i]);
                Node *body = fn_ast[(int)attr->j];
                V *result = eval(body, call_env);
                if(g_returning) {
                    g_returning = 0; v_free(result);
                    result = g_retval ? g_retval : v_nil();
                    g_retval = NULL;
                }
                env_free(call_env);
                v_free(obj);
                for(int i=0;i<nargs;i++) v_free(args[i]);
                free(args);
                return result;
            }
            V *r = method_call(obj, method, args, nargs, e);
            v_free(obj);
            for(int i=0;i<nargs;i++) v_free(args[i]);
            free(args);
            return r;
        }
        int nargs_total = n->nch - 1;
        V **args = calloc(nargs_total+1, sizeof(V*));
        V **kwnames = calloc(nargs_total+1, sizeof(V*));
        V **kwvals = calloc(nargs_total+1, sizeof(V*));
        int nargs = 0, nkw = 0;
        int positional_names = 0;
        if (fn_node->type == N_NAME &&
            (!strcmp(fn_node->sval, "dict") || !strcmp(fn_node->sval, "ktable")) &&
            n->nch > 2) {
            positional_names = 1;
            for (int i = 1; i < n->nch; i++) {
                if (n->ch[i]->type != N_NAME) { positional_names = 0; break; }
            }
        }
        for(int i=1; i<n->nch; i++) {
            if(n->ch[i]->type == N_KWARG) {
                kwnames[nkw] = v_str(n->ch[i]->sval);
                kwvals[nkw] = eval(n->ch[i]->ch[0], e);
                nkw++;
            } else if (positional_names) {
                kwnames[nkw] = v_str(n->ch[i]->sval);
                kwvals[nkw] = eval(n->ch[i], e);
                nkw++;
            } else {
                args[nargs++] = eval(n->ch[i], e);
            }
        }
        if(fn_node->type == N_NAME && is_builtin(fn_node->sval)) {
            V *r = builtin_call(fn_node->sval, args, nargs, kwnames, kwvals, nkw, e);
            for(int i=0;i<nargs;i++) v_free(args[i]);
            for(int i=0;i<nkw;i++) { v_free(kwnames[i]); v_free(kwvals[i]); }
            free(args); free(kwnames); free(kwvals);
            return r;
        }
        V *fn = NULL;
        if(fn_node->type == N_NAME) {
            fn = env_get(e, fn_node->sval);
            if(fn) fn = v_ref(fn);
        } else {
            fn = eval(fn_node, e);
        }
        if(!fn || fn->t != T_FN) {
            if(fn) v_free(fn);
            for(int i=0;i<nargs;i++) v_free(args[i]);
            for(int i=0;i<nkw;i++) { v_free(kwnames[i]); v_free(kwvals[i]); }
            free(args); free(kwnames); free(kwvals);
            return v_errf("'%s' is not callable", fn_node->sval ? fn_node->sval : "?");
        }
        Env *call_env = env_new(fn->closure);
        V *params = fn->params;
        for(int i=0; i<params->n; i++) {
            if(i < nargs) {
                env_set(call_env, params->L[i]->s, args[i]);
            } else if(fn->defaults && i < fn->defaults->n && fn->defaults->L[i]->t != T_NIL) {
                env_set(call_env, params->L[i]->s, fn->defaults->L[i]);
            }
        }
        for(int i=0;i<nkw;i++)
            env_set(call_env, kwnames[i]->s, kwvals[i]);
        Node *body = fn_ast[(int)fn->j];
        V *result = eval(body, call_env);
        if(g_returning) {
            g_returning = 0;
            v_free(result);
            result = g_retval ? g_retval : v_nil();
            g_retval = NULL;
        }
        env_free(call_env);
        v_free(fn);
        for(int i=0;i<nargs;i++) v_free(args[i]);
        for(int i=0;i<nkw;i++) { v_free(kwnames[i]); v_free(kwvals[i]); }
        free(args); free(kwnames); free(kwvals);
        return result;
    }
    case N_IF: {
        for(int i=0; i<n->nch; i+=2) {
            V *cond = eval(n->ch[i], e);
            int t = is_truthy(cond);
            v_free(cond);
            P(t,eval(n->ch[i+1], e))
        }
        return v_nil();
    }
    case N_WHILE: {
        V *r = v_nil();
        for(;;) {
            V *cond = eval(n->ch[0], e);
            int t = is_truthy(cond); v_free(cond);
            if(!t) break;
            v_free(r);
            r = eval(n->ch[1], e);
            P(g_returning || g_error,r)
            if(g_breaking) { g_breaking=0; break; }
            if(g_continuing) { g_continuing=0; continue; }
        }
        return r;
    }
    case N_FOR: {
        Node *vars = n->ch[0];
        V *iter = eval(n->ch[1], e);
        V *r = v_nil();
        if(iter->t == T_IVEC) {
            for(int64_t i=0;i<iter->n;i++) {
                V *item = v_int(iter->J[i]);
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_FVEC) {
            for(int64_t i=0;i<iter->n;i++) {
                V *item = v_float(iter->F[i]);
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_LIST) {
            for(int64_t i=0;i<iter->n;i++) {
                for_set_vars(vars, iter->L[i], e);
                v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(is_mat_t(iter->t)) {
            for(int64_t i=0;i<iter->n;i++) {
                V *item = v_mat_row(iter, i);
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_STR) {
            int64_t slen = strlen(iter->s);
            for(int64_t i=0;i<slen;i++) {
                char buf[2] = {iter->s[i], 0};
                V *ch = v_str(buf);
                for_set_vars(vars, ch, e);
                v_free(ch); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_INPUT) {
            for(;;) {
                V *item = input_stream_next(iter);
                if(g_error) { v_free(iter); return item; }
                if(!item || item->t == T_NIL) { v_free(item); break; }
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_DICT) {
            for(int64_t i=0;i<iter->n;i++) {
                if(vars->type == N_LIST && vars->nch >= 2) {
                    env_set(e, vars->ch[0]->sval, iter->keys->L[i]);
                    env_set(e, vars->ch[1]->sval, iter->vals->L[i]);
                } else {
                    for_set_vars(vars, iter->keys->L[i], e);
                }
                v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        }
        v_free(iter);
        return r;
    }
    case N_DEF: {
        V *params = v_list(n->ch[0]->nch);
        V *defaults = v_list(n->ch[0]->nch);
        for(int i=0; i<n->ch[0]->nch; i++) {
            params->L[i] = v_str(n->ch[0]->ch[i]->sval);
            if(n->nch > 2 && i < n->ch[2]->nch && n->ch[2]->ch[i]->type != N_NONE) {
                defaults->L[i] = eval(n->ch[2]->ch[i], e);
            } else {
                defaults->L[i] = v_nil();
            }
        }
        V *fn = v_fn(params, defaults, n->ch[1], e);
        env_set(e, n->sval, fn);
        v_free(params); v_free(defaults); v_free(fn);
        return v_nil();
    }
    case N_LAMBDA: {
        V *params = v_list(n->ch[0]->nch);
        V *defaults = v_list(n->ch[0]->nch);
        for(int i=0; i<n->ch[0]->nch; i++) {
            params->L[i] = v_str(n->ch[0]->ch[i]->sval);
            if(n->nch > 2 && i < n->ch[2]->nch && n->ch[2]->ch[i]->type != N_NONE) {
                defaults->L[i] = eval(n->ch[2]->ch[i], e);
            } else {
                defaults->L[i] = v_nil();
            }
        }
        V *fn = v_fn(params, defaults, n->ch[1], e);
        v_free(params); v_free(defaults);
        return fn;
    }
    case N_RETURN: {
        V *rv;
        if(n->nch > 0) rv = eval(n->ch[0], e);
        else rv = v_nil();
        g_retval = rv;
        g_returning = 1;
        return v_nil();
    }
    case N_BREAK:    { g_breaking = 1; return v_nil(); }
    case N_CONTINUE: { g_continuing = 1; return v_nil(); }
    case N_RAISE: {
        V *val = n->nch > 0 ? eval(n->ch[0], e) : v_err("Exception");
        g_error = 1;
        g_error_val = val->t == T_ERR ? v_ref(val) : v_errf("%s", v_to_str(val));
        v_free(val);
        return v_nil();
    }
    case N_TRY: {
        int prev_error = g_error;
        g_error = 0;
        V *r = eval(n->ch[0], e);
        if(g_error) {
            g_error = 0;
            if(n->sval && g_error_val) {
                env_set(e, n->sval, g_error_val);
            }
            if(g_error_val) { v_free(g_error_val); g_error_val = NULL; }
            v_free(r);
            if(n->nch > 1) r = eval(n->ch[1], e);
            else r = v_nil();
        } else if(r->t == T_ERR) {
            if(n->sval) env_set(e, n->sval, r);
            v_free(r);
            if(n->nch > 1) r = eval(n->ch[1], e);
            else r = v_nil();
        } else {
            if(n->nch > 2) { v_free(r); r = eval(n->ch[2], e); }
        }
        int finally_idx = (n->nch > 2 && n->ch[n->nch-1]->type != N_PASS) ? n->nch-1 : -1;
        if(finally_idx >= 0 && finally_idx > 1) {
            V *f = eval(n->ch[finally_idx], e);
            v_free(f);
        }
        g_error = prev_error;
        return r;
    }
    case N_WITH: {
        V *ctx = eval(n->ch[0], e);
        if(n->sval) env_set(e, n->sval, ctx);
        V *r = eval(n->ch[1], e);
        v_free(ctx);
        return r;
    }
    case N_DEL: return v_nil();
    case N_GLOBAL: return v_nil();
    case N_CLASS: {
        Env *cls_env = env_new(e);
        V *r = eval(n->ch[0], cls_env);
        v_free(r);
        V *keys = v_list(cls_env->len);
        V *vals = v_list(cls_env->len);
        i(cls_env->len,{
            keys->L[i] = v_str(cls_env->names[i]);
            vals->L[i] = v_ref(cls_env->vals[i]);
        })
        V *cls = v_dict(keys, vals);
        env_set(e, n->sval, cls);
        v_free(keys); v_free(vals);
        env_free(cls_env);
        return v_nil();
    }
    case N_BLOCK: {
        V *r = v_nil();
        i(n->nch,{
            v_free(r);
            r = eval(n->ch[i], e);
            P(g_returning || g_breaking || g_continuing || g_error,r)
        })
        return r;
    }
    case N_IMPORT: return do_import(n->sval, e);
    default:
        return v_errf("unknown node type %d", n->type);
    }
}
static int shakti_stmt_silent(Node *s) {
    P(!s,1)
    switch (s->type) {
    case N_ASSIGN:
    case N_AUGASSIGN:
    case N_DEL:
    case N_IMPORT:
    case N_PASS:
    case N_GLOBAL:
    case N_DEF:
    case N_CLASS:
    case N_BREAK:
    case N_CONTINUE:
    case N_RETURN:
    case N_RAISE:
        return 1;
    default:
        return 0;
    }
}
static int shakti_prog_silent_last(Node *prog) {
    P(!prog || prog->type != N_BLOCK || prog->nch <= 0,0)
    return shakti_stmt_silent(prog->ch[prog->nch - 1]);
}

static int needs_more(const char *line) {
    size_t len = strlen(line);
    P(len == 0,0)
    P(line[len-1] == ':',1)
    P(line[len-1] == '\\',1)
    int parens=0, brackets=0, braces=0;
    for(size_t i=0;i<len;i++) {
        if(line[i]=='(') parens++; else if(line[i]==')') parens--;
        if(line[i]=='[') brackets++; else if(line[i]==']') brackets--;
        if(line[i]=='{') braces++; else if(line[i]=='}') braces--;
    }
    return (parens>0 || brackets>0 || braces>0);
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <termios.h>
#include <unistd.h>
#ifdef SHAKTI_HAVE_SYNTH
#include "synth.h"
#include <sys/select.h>
#endif
#define SHAKTI_HL 1
#else
#define SHAKTI_HL 0
#endif
#define HL_RST  "\033[0m"
#define HL_KW   "\033[1;34m"
#define HL_BI   "\033[36m"
#define HL_STR  "\033[32m"
#define HL_NUM  "\033[33m"
#define HL_CMT  "\033[90m"
#define HL_CON  "\033[35m"
#define HL_DEC  "\033[33m"
#define HL_QRY  "\033[1;33m"
static const char *HL_KWS[] = {
    "def","return","if","elif","else","while","for","in",
    "break","continue","and","or","not","import",
    "try","except","finally","as","lambda","pass",
    "class","global","del","raise","with","yield",NULL
};
static const char *HL_QRYS[] = {
    "select", "update", "delete", "from", "where", "by",
    "create", "table", "insert", "into", "values", NULL
};
static const char *HL_CONS[] = {"True","False","None",NULL};
static const char *HL_BIS[] = {
    "print","len","range","type","int","float","str","list","bool",
    "sum","avg","min","max","abs","sqrt",
    "sort","reverse","zip","enumerate","map","filter",
    "table","columns","shape","head","tail",
    "append","pop","keys","values",
    "load","save","input","repr","clock",
    "read","write","readlines",
    "listdir","walk","stat",
    "path_join","path_exists","path_isdir","path_isfile",
    "path_basename","path_dirname","path_splitext",
    "getcwd","mkdir","getenv","machine","sh",
    "re_findall","re_sub","re_match","re_split",
    "json_loads","json_dumps","json_load","json_dump",
    "sorted","any","all","isinstance","hasattr","getattr",
    "chr","ord","hex","dict","set","next","assert",
    "int64","float64",
    NULL
};
static int hl_in(const char *w, const char **t) {
    for(int i=0;t[i];i++) if(!strcmp(w,t[i])) return 1;
    return 0;
}
static void hl_render(const char *s, int len) {
    int i=0;
    while(i<len) {
        char c=s[i];
        if(c=='#') { printf(HL_CMT); while(i<len)putchar(s[i++]); printf(HL_RST); return; }
        if((c=='f'||c=='F'||c=='r'||c=='R'||c=='b'||c=='B')&&i+1<len&&(s[i+1]=='"'||s[i+1]=='\'')) {
            char q=s[i+1]; printf(HL_STR); putchar(s[i++]); putchar(s[i++]);
            while(i<len&&s[i]!=q){if(s[i]=='\\'&&i+1<len)putchar(s[i++]);putchar(s[i++]);}
            if(i<len)putchar(s[i++]); printf(HL_RST); continue;
        }
        if(c=='"'||c=='\'') {
            char q=c; printf(HL_STR);
            if(i+2<len&&s[i+1]==q&&s[i+2]==q) {
                putchar(s[i++]);putchar(s[i++]);putchar(s[i++]);
                while(i<len){if(i+2<len&&s[i]==q&&s[i+1]==q&&s[i+2]==q){putchar(s[i++]);putchar(s[i++]);putchar(s[i++]);break;}
                if(s[i]=='\\'&&i+1<len)putchar(s[i++]);putchar(s[i++]);}
            } else {
                putchar(s[i++]);
                while(i<len&&s[i]!=q){if(s[i]=='\\'&&i+1<len)putchar(s[i++]);putchar(s[i++]);}
                if(i<len)putchar(s[i++]);
            }
            printf(HL_RST); continue;
        }
        if(isdigit((unsigned char)c)||(c=='.'&&i+1<len&&isdigit((unsigned char)s[i+1]))) {
            printf(HL_NUM);
            for(;;) {
                if(i>=len) break;
                char d=s[i];
                if(isdigit((unsigned char)d)||(d=='.'&&i+1<len&&isdigit((unsigned char)s[i+1]))) {
                    if(d=='0'&&i+1<len&&(s[i+1]=='x'||s[i+1]=='X')){putchar(s[i++]);putchar(s[i++]);while(i<len&&isxdigit((unsigned char)s[i]))putchar(s[i++]);}
                    else{while(i<len&&(isdigit((unsigned char)s[i])||s[i]=='_'))putchar(s[i++]);
                        if(i<len&&s[i]=='.'){putchar(s[i++]);while(i<len&&isdigit((unsigned char)s[i]))putchar(s[i++]);}
                        if(i<len&&(s[i]=='e'||s[i]=='E')){putchar(s[i++]);if(i<len&&(s[i]=='+'||s[i]=='-'))putchar(s[i++]);while(i<len&&isdigit((unsigned char)s[i]))putchar(s[i++]);}}
                } else break;
                int j=i;
                W(j<len&&(s[j]==' '||s[j]=='\t'),j++)
                if(j>=len) break;
                char nc=s[j];
                if(isdigit((unsigned char)nc)||(nc=='.'&&j+1<len&&isdigit((unsigned char)s[j+1]))){
                    W(i<j,putchar(s[i++]))
                    continue;
                }
                break;
            }
            printf(HL_RST); continue;
        }
        if(isalpha(c)||c=='_') {
            int p0=i; while(i<len&&(isalnum(s[i])||s[i]=='_'))i++;
            char w[256]; int wl=i-p0; if(wl>=(int)sizeof(w))wl=sizeof(w)-1;
            memcpy(w,s+p0,wl); w[wl]=0;
            if(hl_in(w,HL_KWS))       printf(HL_KW "%s" HL_RST,w);
            else if(hl_in(w,HL_QRYS))  printf(HL_QRY "%s" HL_RST,w);
            else if(hl_in(w,HL_CONS))  printf(HL_CON "%s" HL_RST,w);
            else if(hl_in(w,HL_BIS))   printf(HL_BI "%s" HL_RST,w);
            else printf("%s",w);
            continue;
        }
        if(c=='@'){printf(HL_DEC);putchar(s[i++]);while(i<len&&(isalnum(s[i])||s[i]=='_'||s[i]=='.'))putchar(s[i++]);printf(HL_RST);continue;}
        putchar(s[i++]);
    }
}
#if SHAKTI_HL
#define HL_HMAX 512
static char *hl_hist[HL_HMAX];
static int   hl_hlen = 0;
static void hl_hadd(const char *s) {
    Pv(!s[0])
    Pv(hl_hlen>0 && !strcmp(hl_hist[hl_hlen-1],s))
    if(hl_hlen>=HL_HMAX){free(hl_hist[0]);memmove(hl_hist,hl_hist+1,(HL_HMAX-1)*sizeof(char*));hl_hlen--;}
    hl_hist[hl_hlen++]=strdup(s);
}
static struct termios hl_orig;
static int hl_raw=0;
static void hl_raw_off(void){if(hl_raw){tcsetattr(STDIN_FILENO,TCSAFLUSH,&hl_orig);hl_raw=0;}}
static void hl_raw_on(void){
    if(!isatty(STDIN_FILENO))return;
    tcgetattr(STDIN_FILENO,&hl_orig);
    struct termios r=hl_orig;
    r.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
    r.c_oflag |= OPOST;
    r.c_cflag |= CS8;
    r.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
    r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&r);
    hl_raw=1;
}
static void hl_redraw(const char *prompt, const char *buf, int len, int pos) {
    int plen=(int)strlen(prompt);
    printf("\r\033[K%s",prompt);
    hl_render(buf,len);
    if(plen+pos>0) printf("\r\033[%dC",plen+pos);
    else printf("\r");
    fflush(stdout);
}
#if defined(SHAKTI_HAVE_SYNTH)
static int hl_read_char(char *c) {
    return input_hub_read_char(c);
}
#else
static int hl_read_char(char *c) {
    return input_hub_read_char(c);
}
#endif
static char *hl_readline(const char *prompt) {
    static char buf[65536];
    int len=0, pos=0, hidx=hl_hlen;
    if(!isatty(STDIN_FILENO)){
        printf("%s",prompt); fflush(stdout);
        if(!fgets(buf,sizeof(buf),stdin))return NULL;
        size_t l=strlen(buf); if(l>0&&buf[l-1]=='\n')buf[l-1]=0;
        return buf;
    }
    hl_raw_on();
    printf("\r%s",prompt); fflush(stdout);
    for(;;) {
        char c; if(!hl_read_char(&c)){hl_raw_off();return NULL;}
        if(c=='\r'||c=='\n'){buf[len]=0;printf("\r\n");fflush(stdout);hl_raw_off();hl_hadd(buf);return buf;}
        if(c==3){len=pos=0;printf("\r\n%s",prompt);fflush(stdout);continue;}
        if(c==4){if(len==0){printf("\r\n");hl_raw_off();return NULL;}continue;}
        if(c==127||c==8){if(pos>0){memmove(buf+pos-1,buf+pos,len-pos);pos--;len--;}}
        else if(c==1){pos=0;}
        else if(c==5){pos=len;}
        else if(c==21){memmove(buf,buf+pos,len-pos);len-=pos;pos=0;}
        else if(c==11){len=pos;}
        else if(c==12){printf("\033[2J\033[H");}
        else if(c==23){
            int e=pos; while(pos>0&&buf[pos-1]==' ')pos--;
            W(pos>0&&buf[pos-1]!=' ',pos--)
            memmove(buf+pos,buf+e,len-e);len-=(e-pos);
        }
        else if(c==9){
            if(len+4<(int)sizeof(buf)){memmove(buf+pos+4,buf+pos,len-pos);memset(buf+pos,' ',4);pos+=4;len+=4;}
        }
        else if(c==27){
            char sq[3]; if(!hl_read_char(&sq[0]))continue;
            if(!hl_read_char(&sq[1]))continue;
            if(sq[0]=='['){
                switch(sq[1]){
                case 'A':if(hidx>0){hidx--;strncpy(buf,hl_hist[hidx],sizeof(buf)-1);len=(int)strlen(buf);pos=len;}break;
                case 'B':if(hidx<hl_hlen-1){hidx++;strncpy(buf,hl_hist[hidx],sizeof(buf)-1);len=(int)strlen(buf);pos=len;}
                         else{hidx=hl_hlen;buf[0]=0;len=pos=0;}break;
                case 'C':if(pos<len)pos++;break;
                case 'D':if(pos>0)pos--;break;
                case 'H':pos=0;break;
                case 'F':pos=len;break;
                case '3':hl_read_char(&sq[2]);if(pos<len){memmove(buf+pos,buf+pos+1,len-pos-1);len--;}break;
                case '1':case '7':hl_read_char(&sq[2]);pos=0;break;
                case '4':case '8':hl_read_char(&sq[2]);pos=len;break;
                }
            }
        }
        else if(c>=32&&len+1<(int)sizeof(buf)){memmove(buf+pos+1,buf+pos,len-pos);buf[pos]=c;pos++;len++;}
        else continue;
        buf[len]=0;
        hl_redraw(prompt,buf,len,pos);
    }
}
#endif
#if !SHAKTI_HL
static char *read_line(const char *prompt) {
    static char buf[65536];
    (void)prompt;
    if (!fgets(buf, (int)sizeof buf, stdin)) return NULL;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return buf;
}
#endif
static FILE *repl_open_runtime_doc(void) {
    char path[4096];
    if (g_lib_path[0]) {
        size_t n = strlen(g_lib_path);
        if (n > 8 && !strcmp(g_lib_path + n - 8, "/src/lib")) {
            memcpy(path, g_lib_path, n - 8);
            path[n - 8] = 0;
            snprintf(path + n - 8, sizeof path - (n - 8), "/docs/RUNTIME_API.md");
            FILE *f = fopen(path, "r");
            if (f) return f;
        }
    }
    const char *doc = getenv("SHAKTI_DOC");
    if (doc && doc[0]) {
        FILE *f = fopen(doc, "r");
        if (f) return f;
    }
    const char *lib = getenv("SHAKTI_LIB");
    if (lib && lib[0]) {
        snprintf(path, sizeof path, "%s/../docs/RUNTIME_API.md", lib);
        FILE *f = fopen(path, "r");
        if (f) return f;
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    {
        char exe[4096];
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (len > 0) {
            exe[len] = 0;
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = 0;
                snprintf(path, sizeof path, "%s/docs/RUNTIME_API.md", exe);
                FILE *f = fopen(path, "r");
                if (f) return f;
            }
        }
    }
#endif
    return fopen("docs/RUNTIME_API.md", "r");
}
static int repl_md_table_sep(const char *s) {
    if (!s || s[0] != '|') return 0;
    for (; *s; s++) {
        if (*s != '|' && *s != '-' && *s != ':' && *s != ' ') return 0;
    }
    return 1;
}
static void repl_md_strip_links(char *s) {
    char *w = s;
    for (char *p = s; *p; ) {
        if (*p == '[') {
            char *close = strchr(p + 1, ']');
            char *open = close ? strchr(close, '(') : NULL;
            char *end = open ? strchr(open, ')') : NULL;
            if (close && open == close + 1 && end) {
                size_t n = (size_t)(close - (p + 1));
                if (n) { memcpy(w, p + 1, n); w += n; }
                p = end + 1;
                continue;
            }
        }
        *w++ = *p++;
    }
    *w = 0;
}
static void repl_md_strip_emphasis(char *s) {
    char *w = s;
    for (char *p = s; *p; p++) {
        if (*p == '*' && p[1] == '*') { p++; continue; }
        if (*p == '`') continue;
        *w++ = *p;
    }
    *w = 0;
}
static void repl_md_format_table(char *s) {
    if (s[0] != '|') return;
    char *p = s;
    if (*p == '|') p++;
    char *w = s;
    int col = 0;
    for (; *p; p++) {
        if (*p == '|') {
            if (col) { *w++ = ' '; *w++ = ' '; }
            col = 1;
            continue;
        }
        *w++ = *p;
    }
    while (w > s && (w[-1] == ' ')) w--;
    *w = 0;
}
static void repl_md_plain_line(char *line) {
    char *p = line;
    while (*p == '#') {
        p++;
        if (*p == ' ') p++;
    }
    if (p != line) memmove(line, p, strlen(p) + 1);
    repl_md_strip_links(line);
    repl_md_strip_emphasis(line);
    repl_md_format_table(line);
}
static void repl_print_runtime_doc(void) {
    FILE *f = repl_open_runtime_doc();
    if (!f) {
        fprintf(stderr, "Cannot open docs/RUNTIME_API.md (set SHAKTI_DOC to override)\n");
        return;
    }
    char line[4096];
    int in_code = 0;
    int prev_blank = 1;
    while (fgets(line, (int)sizeof line, f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
            in_code = !in_code;
            if (!prev_blank) putchar('\n');
            prev_blank = 1;
            continue;
        }
        if (in_code) {
            puts(line);
            prev_blank = (n == 0);
            continue;
        }
        if (repl_md_table_sep(line)) continue;
        repl_md_plain_line(line);
        if (line[0] == 0) {
            if (!prev_blank) putchar('\n');
            prev_blank = 1;
            continue;
        }
        if (!prev_blank) putchar('\n');
        puts(line);
        prev_blank = 0;
    }
    if (!prev_blank) putchar('\n');
    fclose(f);
}
static void run_repl(Env *e) {

#if SHAKTI_HL
    atexit(hl_raw_off);
#endif
    char input[262144];
    for(;;) {
#if SHAKTI_HL
        char *line = hl_readline("> ");
#else
        char *line = read_line("> ");
#endif
        if(!line) break;
        if(strcmp(line, "quit")==0 || strcmp(line, "exit")==0) break;
        if(line[0]==0) continue;
        strcpy(input, line);
        strcat(input, "\n");
        if (strcmp(line, "\\v") == 0) {
            for(int i=0; i<e->len; i++) {
                printf("%-15s", e->names[i]);
                print_val(e->vals[i], stdout, 1);
                printf("\n");
            }
            continue;
        }
        if (strcmp(line, "\\w") == 0) {
            for(int i=0; i<e->len; i++) {
                printf("%s\n", e->names[i]);
            }
            continue;
        }
        if (strcmp(line, "\\d") == 0 || strcmp(line, "\\help") == 0 || strcmp(line, "help") == 0) {
            repl_print_runtime_doc();
            continue;
        }
        if (line[0] == '\\') {
            fprintf(stderr, "Unknown REPL command: %s (try \\d for help)\n", line);
            continue;
        }
        while(needs_more(line)) {
#if SHAKTI_HL
            line = hl_readline("| ");
#else
            line = read_line("| ");
#endif
            if(!line) break;
            if(line[0]==0) break;
            strcat(input, line);
            strcat(input, "\n");
        }
        Node *prog = parse(input);
        if(!prog) continue;
        V *result = eval(prog, e);
        if(g_error) {
            if(g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
            g_error = 0;
        } else if(!shakti_prog_silent_last(prog) && result && result->t != T_NIL && result->t != T_ERR) {
            print_val(result, stdout, 1);
            putchar('\n');
        }
        if(result && result->t == T_ERR) {
            fprintf(stderr, "Error: %s\n", result->s);
        }
        v_free(result);
    }
}
static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz+2);
    fread(buf, 1, sz, f);
    buf[sz] = '\n'; buf[sz+1] = 0;
    fclose(f);
    return buf;
}
#ifndef SHAKTI_NO_MAIN
int shakti_lang_main(int argc, char **argv) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    {
        char exe[4096];
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe)-1);
        if(len > 0) {
            exe[len] = 0;
            char *slash = strrchr(exe, '/');
            if(slash) { *slash = 0; snprintf(g_lib_path, sizeof(g_lib_path), "%s/src/lib", exe); }
        }
    }
#endif
    Env *global = env_new(NULL);
    builtin_register(global);
    int i = 1;
    char *cmd = NULL;
    int interactive = 0;
    int parse_dump = 0;
    int parse_profile = 0;
    int parse_profile_iters = 100000;
    while(i < argc && argv[i][0] == '-') {
        if(!strcmp(argv[i], "-c") && i+1 < argc) {
            cmd = argv[++i];
        } else if(!strcmp(argv[i], "-i")) {
            interactive = 1;
        } else if(!strcmp(argv[i], "--parse-dump")) {
            parse_dump = 1;
        } else if(!strcmp(argv[i], "--parse-profile")) {
            parse_profile = 1;
        } else if(!strcmp(argv[i], "--parse-profile-iters") && i+1 < argc) {
            parse_profile_iters = atoi(argv[++i]);
            if (parse_profile_iters < 1) parse_profile_iters = 1;
        }
        i++;
    }

    if (parse_profile) {
        const char *src = cmd;
        char *file_buf = NULL;
        if (!src && i < argc) {
            file_buf = read_file(argv[i]);
            if (!file_buf) return 1;
            src = file_buf;
        }
        if (!src) src = "x = 1 2 3\ny = abs -1.2\n";
        clock_t t0 = clock();
        for (int k = 0; k < parse_profile_iters; k++) {
            Node *prog = parse(src);
            node_free(prog);
        }
        double sec = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
        printf("parse_profile: %d iters in %.3fs (%.0f parses/sec)\n",
               parse_profile_iters, sec, (double)parse_profile_iters / sec);
        free(file_buf);
        env_free(global);
        return 0;
    }

    if (parse_dump) {
        const char *src = cmd;
        char *file_buf = NULL;
        if (!src && i < argc) {
            file_buf = read_file(argv[i]);
            if (!file_buf) return 1;
            src = file_buf;
        }
        if (!src) {
            fprintf(stderr, "usage: shakti --parse-dump -c 'expr' | script.ie\n");
            env_free(global);
            return 1;
        }
        Node *prog = parse(src);
        if (prog && prog->nch > 0)
            node_sprint(prog->ch[prog->nch - 1], stdout);
        else
            node_sprint(prog, stdout);
        putchar('\n');
        node_free(prog);
        free(file_buf);
        env_free(global);
        return 0;
    }

    if(cmd) {
        Node *prog = parse(cmd);
        V *r = eval(prog, global);
        if(g_error && g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
        if(!shakti_prog_silent_last(prog) && r && r->t != T_NIL && r->t != T_ERR) {
            print_val(r, stdout, 1);
            putchar('\n');
        }
        v_free(r);
        if(interactive) run_repl(global);
    } else if(i < argc) {
        strncpy(g_script_dir, argv[i], sizeof(g_script_dir)-1);
        char *slash = strrchr(g_script_dir, '/');
        if(slash) *slash = 0; else strcpy(g_script_dir, ".");
        char *src = read_file(argv[i]);
        P(!src,1)
        Node *prog = parse(src);
        V *r = eval(prog, global);
        int script_err = g_error || (r && r->t == T_ERR);
        if(g_error && g_error_val) { fprintf(stderr, "Error: %s\n", g_error_val->s); v_free(g_error_val); g_error_val=NULL; }
        if(r && r->t == T_ERR) fprintf(stderr, "Error: %s\n", r->s);
        v_free(r);
        free(src);
        env_free(global);
        return script_err ? 1 : 0;
    } else {
        run_repl(global);
    }
    env_free(global);
    return 0;
}
#endif
