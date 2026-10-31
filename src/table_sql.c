#include "shakti.h"
#include "mat_simd.h"
#include "vec_kernels.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifndef SQL_DENSE_CAP
#define SQL_DENSE_CAP 1000000
#endif
#ifndef SQL_RADIX_MIN_ROWS
#define SQL_RADIX_MIN_ROWS 100000
#endif
#ifndef SQL_OMP_GROUP_MIN
#define SQL_OMP_GROUP_MIN 50000
#endif
enum {
    COL_NAME = 0,
    COL_COUNT = 1,
    COL_SUM = 2,
    COL_AVG = 3,
    COL_MIN = 4,
    COL_MAX = 5,
};
typedef struct {
    int kind;
    char name[128];
    char src[128];
} ColSpec;
static int is_agg_kw(ss){static const char*kws[]={"count","sum","avg","min","max",0};int k;for(k=0;kws[k];k++)if(!strcmp(s,kws[k]))return 1;return 0;}
static int tbl_col_idx(V*tbl,const g0*name){int64_t k;P(!tbl||tbl->t!=T_TABLE||!name,-1)for(k=0;k<tbl->keys->n;k++)if(!strcmp(tbl->keys->L[k]->s,name))return(int)k;return-1;}
static inline V*tbl_col(V*tbl,int idx){return(!tbl||tbl->t!=T_TABLE||idx<0||idx>=tbl->keys->n)?NULL:tbl->vals->L[idx];}
static inline double cell_float(V*col,int64_t row){
    if(!col) return 0.;
    if(col->t==T_INT) return (double)col->j;
    if(col->t==T_FLOAT) return col->f;
    if(col->t==T_BOOL) return col->j?1.:0.;
    if(col->t==T_IVEC) return row<col->n?(double)col->J[row]:0.;
    if(col->t==T_FVEC) return row<col->n?col->F[row]:0.;
    if(col->t==T_BVEC) return row<col->n?col->B[row]?1.:0.:0.;
    if(col->t==T_IMAT&&row<col->n) return (double)col->J[mat_idx(col,row,0)];
    if(col->t==T_FMAT&&row<col->n) return col->F[mat_idx(col,row,0)];
    return 0.;
}
static void fmt_f64_bits(char*buf,size_t cap,double x){
 union{double d;uint64_t u;}u;u.d=x;
 snprintf(buf,cap,"%016llx",(unsigned long long)u.u);}
static int cell_key(V*col,int64_t row,char*buf,size_t cap){
 int n;
 if(!col||!buf||!cap)return -1;
 buf[0]=0;
 if(col->t==T_STR)n=snprintf(buf,cap,"%s",col->s?col->s:"");
 else if(col->t==T_LIST&&row<col->n&&col->L[row]){char*t=v_to_str(col->L[row]);n=snprintf(buf,cap,"%s",t?t:"");free(t);}
 else if(col->t==T_INT)n=snprintf(buf,cap,"%lld",(long long)col->j);
 else if(col->t==T_FLOAT){fmt_f64_bits(buf,cap,col->f);n=(int)strlen(buf);}
 else if(col->t==T_IVEC&&row<col->n)n=snprintf(buf,cap,"%lld",(long long)col->J[row]);
 else if(col->t==T_FVEC&&row<col->n){fmt_f64_bits(buf,cap,col->F[row]);n=(int)strlen(buf);}
 else if(col->t==T_BVEC&&row<col->n)n=snprintf(buf,cap,"%s",col->B[row]?"true":"false");
 else if(col->t==T_CVEC&&row<col->n)n=snprintf(buf,cap,"%u",(unsigned)col->B[row]);
 else if(col->t==T_IMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);n=snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_FMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);n=snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_BMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);n=snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_CMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);n=snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_BOOL)n=snprintf(buf,cap,"%s",col->j?"true":"false");
 else n=0;
 if(n<0||(size_t)n>=cap){buf[0]=0;return -1;}
 return 0;}
static V*where_mask(V*tbl,V*where){
 if(!where||where->t==T_NIL){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=1;return all;}
 if(where->t==T_BVEC){P(where->n!=tbl->n,v_err("where: mask length mismatch"))return v_copy(where);}
 if(where->t==T_BOOL){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=where->j?1:0;return all;}
 if(where->t==T_INT){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=where->j?1:0;return all;}
 if(where->t==T_ERR)return v_ref(where); /* an error value in where is the statement's result */
 return v_err("where: need boolean mask");}
static V*tbl_filter_mask(V*tbl,V*mask){
 P(tbl->t!=T_TABLE||mask->t!=T_BVEC,v_err("bad filter"))
    int64_t nr = tbl->n, count = 0;
    const unsigned char *B = mask->B;
    int64_t i;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:count) if(nr >= 1000000)
#endif
    for (i = 0; i < nr; i++) {
        if (B[i]) count++;
    }
    /* Identity mask: skip column compress. */
    if (count == nr) {
        return v_ref(tbl);
    }
    int nc = (int)tbl->keys->n;
    V *new_data = v_list(nc);
    int has_list = 0;
    for (int c0 = 0; c0 < nc; c0++) {
        if (tbl->vals->L[c0] && tbl->vals->L[c0]->t == T_LIST) has_list = 1;
    }
#ifdef _OPENMP
    #pragma omp parallel for if(nr >= 1000000 && !has_list)
#endif
    for (int c = 0; c < nc; c++) {
        V *col = tbl->vals->L[c];
        if (col->t == T_IVEC) {
            V *nc2 = v_ivec(count);
            int64_t j = 0;
            const int64_t *src = col->J;
            int64_t *dst = nc2->J;
            j = mat_compress_i64_masked(dst, j, src, B, nr);
            new_data->L[c] = nc2;
        } else if (col->t == T_FVEC) {
            V *nc2 = v_fvec(count);
            int64_t j = 0;
            const double *src = col->F;
            double *dst = nc2->F;
            j = mat_compress_f64_masked(dst, j, src, B, nr);
            new_data->L[c] = nc2;
        } else if (col->t == T_BVEC || col->t == T_CVEC) {
            V *nc2 = col->t == T_CVEC ? v_cvec(count) : v_bvec(count);
            int64_t j = 0;
            const unsigned char *src = col->B;
            unsigned char *dst = nc2->B;
            for (int64_t k = 0; k < nr; k++) {
                if (B[k]) dst[j++] = src[k];
            }
            new_data->L[c] = nc2;
        } else if (col->t == T_LIST) {
            V *nc2 = v_list(count);
            int64_t j = 0;
            V **src = col->L;
            V **dst = nc2->L;
            for (int64_t k = 0; k < nr; k++) {
                if (B[k]) dst[j++] = v_ref(src[k]);
            }
            new_data->L[c] = nc2;
        } else if (col->t == T_IMAT || col->t == T_FMAT || col->t == T_BMAT || col->t == T_CMAT) {
            int64_t cols = mat_cols(col);
            if (col->t == T_IMAT) {
                V *nc2 = v_imat(count, cols);
                mat_filter_imat_rows(nc2->J, col->J, B, nr, cols);
                new_data->L[c] = nc2;
            } else if (col->t == T_FMAT) {
                V *nc2 = v_fmat(count, cols);
                mat_filter_fmat_rows(nc2->F, col->F, B, nr, cols);
                new_data->L[c] = nc2;
            } else if (col->t == T_CMAT) {
                V *nc2 = v_cmat(count, cols);
                mat_filter_bmat_rows(nc2->B, col->B, B, nr, cols);
                new_data->L[c] = nc2;
            } else {
                V *nc2 = v_bmat(count, cols);
                mat_filter_bmat_rows(nc2->B, col->B, B, nr, cols);
                new_data->L[c] = nc2;
            }
        } else {
            new_data->L[c] = v_ref(col);
        }
    }
    return v_table_own(v_copy(tbl->keys), new_data);
}
static V *tbl_project_names(V *tbl, V *names) {
    if (names->t == T_STR) {
        int idx = tbl_col_idx(tbl, names->s);
        if (idx < 0) {
            return v_errf("select: unknown column '%s'", names->s);
        }
        V *keys = v_list(1);
        keys->L[0] = v_ref(tbl->keys->L[idx]);
        V *data = v_list(1);
        data->L[0] = v_ref(tbl->vals->L[idx]);
        return v_table_own(keys, data);
    }
    if (names->t != T_LIST) {
        return v_err("select: columns must be names");
    }
    V *keys = v_list(names->n);
    V *data = v_list(names->n);
    for (int64_t i = 0; i < names->n; i++) {
        if (names->L[i]->t != T_STR) {
            v_free(keys);
            v_free(data);
            return v_err("select: column names must be strings");
        }
        int idx = tbl_col_idx(tbl, names->L[i]->s);
        if (idx < 0) {
            v_free(keys);
            v_free(data);
            return v_errf("select: unknown column '%s'", names->L[i]->s);
        }
        keys->L[i] = v_ref(tbl->keys->L[idx]);
        data->L[i] = v_ref(tbl->vals->L[idx]);
    }
    return v_table_own(keys, data);
}
static int parse_col_specs(V *cols, ColSpec *specs, int max_specs) {
    if (!cols || cols->t == T_NIL) {
        return 0;
    }
    if (cols->t == T_STR) {
        if (is_agg_kw(cols->s)) {
            return -1;
        }
        specs[0].kind = COL_NAME;
        snprintf(specs[0].name, sizeof specs[0].name, "%s", cols->s);
        specs[0].src[0] = 0;
        return 1;
    }
    if (cols->t != T_LIST) {
        return -1;
    }
    int n = 0;
    for (int64_t i = 0; i < cols->n && n < max_specs; ) {
        V *item = cols->L[i];
        if (item->t != T_STR) {
            return -1;
        }
        const char *s = item->s;
        if (!strcmp(s, "count")) {
            specs[n].kind = COL_COUNT;
            snprintf(specs[n].name, sizeof specs[n].name, "count");
            specs[n].src[0] = 0;
            if (i + 1 < cols->n && cols->L[i + 1]->t == T_STR && !is_agg_kw(cols->L[i + 1]->s)) {
                snprintf(specs[n].src, sizeof specs[n].src, "%s", cols->L[i + 1]->s);
                snprintf(specs[n].name, sizeof specs[n].name, "%s", cols->L[i + 1]->s);
                i += 2;
            } else {
                i += 1;
            }
            n++;
            continue;
        }
        if (!strcmp(s, "sum") || !strcmp(s, "avg") || !strcmp(s, "min") || !strcmp(s, "max")) {
            if (i + 1 >= cols->n || cols->L[i + 1]->t != T_STR || is_agg_kw(cols->L[i + 1]->s)) {
                return -1;
            }
            specs[n].kind = !strcmp(s, "sum") ? COL_SUM :
                             !strcmp(s, "avg") ? COL_AVG :
                             !strcmp(s, "min") ? COL_MIN : COL_MAX;
            snprintf(specs[n].src, sizeof specs[n].src, "%s", cols->L[i + 1]->s);
            snprintf(specs[n].name, sizeof specs[n].name, "%s", cols->L[i + 1]->s);
            i += 2;
            n++;
            continue;
        }
        specs[n].kind = COL_NAME;
        snprintf(specs[n].name, sizeof specs[n].name, "%s", s);
        specs[n].src[0] = 0;
        n++;
        i++;
    }
    /* Rename colliding aggregate output names (e.g. min price, max price). */
    {
        int mark[64];
        memset(mark, 0, sizeof mark);
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (!strcmp(specs[i].name, specs[j].name)) {
                    mark[i] = mark[j] = 1;
                }
            }
        }
        for (int i = 0; i < n; i++) {
            if (!mark[i] || specs[i].kind == COL_NAME) continue;
            if (specs[i].kind == COL_COUNT && !specs[i].src[0]) continue;
            const char *pfx = specs[i].kind == COL_SUM ? "sum" :
                              specs[i].kind == COL_AVG ? "avg" :
                              specs[i].kind == COL_MIN ? "min" :
                              specs[i].kind == COL_MAX ? "max" : "count";
            char tmp[128];
            snprintf(tmp, sizeof tmp, "%s_%s", pfx, specs[i].src[0] ? specs[i].src : "x");
            snprintf(specs[i].name, sizeof specs[i].name, "%s", tmp);
        }
    }
    return n;
}
static V*cell_as_v(V*col,int64_t row){
 return col_get(col, row);}
static int compare_v(const V *a, const V *b) {
    if (!a || a->t == T_NIL) {
        return (!b || b->t == T_NIL) ? 0 : -1;
    }
    if (!b || b->t == T_NIL) {
        return 1;
    }
    if (a->t == T_INT && b->t == T_INT) {
        return (a->j > b->j) - (a->j < b->j);
    }
    if (a->t == T_FLOAT && b->t == T_FLOAT) {
        int an = isnan(a->f), bn = isnan(b->f);
        if (an || bn)
            return an - bn;
        return (a->f > b->f) - (a->f < b->f);
    }
    if (a->t == T_BOOL && b->t == T_BOOL) {
        return (a->j > b->j) - (a->j < b->j);
    }
    if (a->t == T_STR && b->t == T_STR) {
        return strcmp(a->s ? a->s : "", b->s ? b->s : "");
    }
    char *sa = v_to_str((V *)a);
    char *sb = v_to_str((V *)b);
    int c = strcmp(sa ? sa : "", sb ? sb : "");
    free(sa);
    free(sb);
    return c;
}
static int composite_key(V *tbl, const int *by_idx, int nby, int64_t row, char *buf, size_t cap) {
    size_t pos = 0;
    for (int i = 0; i < nby; i++) {
        char part[256];
        if (cell_key(tbl_col(tbl, by_idx[i]), row, part, sizeof part) != 0)
            return -1;
        size_t plen = strlen(part);
        if (plen > 255 || pos + 1 + plen >= cap)
            return -1;
        buf[pos++] = (char)plen;
        memcpy(buf + pos, part, plen);
        pos += plen;
    }
    if (pos < cap) buf[pos] = 0;
    return (int)pos;
}
static inline uint32_t sql_hash_key(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}
static inline uint64_t sql_hash_iparts(const int64_t *p, int n) {
    uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < n; i++) {
        uint64_t x = (uint64_t)p[i];
        h ^= x;
        h *= 1099511628211ull;
    }
    return h ? h : 1ull;
}
#define GH_EMPTY (-1)
typedef struct {
    char key[512];
    int key_len;
    int64_t iparts[32];
    int use_int;
    int nby;
    int64_t nrows;
    int64_t name_row;
    V **by_cell;
    V *name_val[64];
    double sum[64];
    int64_t isum[64];   /* sum of an int column (sum_is_int), wrapped to int64 */
    int32_t icarry[64]; /* its overflows: the true sum is isum + icarry * 2^64 */
    double minv[64];
    double maxv[64];
    int have_mm[64];
} GhSlot;
typedef struct {
    GhSlot *slots;
    int *map;
    int cap;
    int nslots;
} GhTab;
static inline uint32_t gh_hash_key(const char *key, size_t n){return sql_hash_key(key, n);}
static int gh_key_eq(const GhSlot *s, const char *key, size_t n) {
    return s->key_len == (int)n && (n == 0 || memcmp(s->key, key, n) == 0);
}
static inline uint32_t gh_hash_iparts(const int64_t *p, int n) {
    return (uint32_t)sql_hash_iparts(p, n);
}
static int gh_init(GhTab *t, int cap) {
    if (cap < 16) {
        cap = 16;
    }
    int n = 1;
    W(n < cap,n *= 2)
    t->cap = n;
    t->nslots = 0;
    t->slots = calloc((size_t)n, sizeof(GhSlot));
    t->map = malloc((size_t)n * sizeof(int));
    if (!t->slots || !t->map) {
        free(t->slots);
        free(t->map);
        t->slots = NULL;
        t->map = NULL;
        t->cap = 0;
        return -1;
    }
    for (int i = 0; i < n; i++) {
        t->map[i] = GH_EMPTY;
    }
    return 0;
}
static void gh_free(GhTab *t) {
    for (int i = 0; i < t->nslots; i++) {
        GhSlot *s = &t->slots[i];
        if (s->by_cell) {
            for (int b = 0; b < s->nby; b++) {
                if (s->by_cell[b]) {
                    v_free(s->by_cell[b]);
                }
            }
            free(s->by_cell);
        }
        for (int j = 0; j < 64; j++) {
            if (s->name_val[j]) {
                v_free(s->name_val[j]);
            }
        }
    }
    free(t->slots);
    free(t->map);
    t->slots = NULL;
    t->map = NULL;
    t->cap = 0;
    t->nslots = 0;
}
static int gh_resize(GhTab *t);
static inline int agg_needs_sum(int kind) {
    return kind == COL_SUM || kind == COL_AVG;
}
/* sum of an int column is an int, added as ints (as sum of a list[int] is); avg stays a float.
 * Each total wraps to int64 and counts its overflows, so the true sum is known whatever the order
 * of the additions. When a group's final sum doesn't fit (a non-zero count), build_group_result
 * sets g_isum_ovf and tbl_group_select adds the select again with g_isum_off set, as floats, as
 * sum of a list[int] whose sum doesn't fit gives a float. */
static int g_isum_ovf, g_isum_off;
static inline int sum_is_int(int kind, int col_t) {
    return !g_isum_off && kind == COL_SUM && col_t == T_IVEC;
}
static inline void int_add(int64_t *sum, int32_t *carry, int64_t b) {
    if (__builtin_add_overflow(*sum, b, sum)) *carry += b < 0 ? -1 : 1;
}
static inline int agg_needs_min(int kind) {
    return kind == COL_MIN;
}
static inline int agg_needs_max(int kind) {
    return kind == COL_MAX;
}
static int gh_slot_init_aggs(GhSlot *s, V *tbl, const int *by_idx, int nby,
                             int64_t row, ColSpec *specs, int nspecs, int use_int,
                             const int64_t *iparts, V **sp_col) {
    memset(s, 0, sizeof(*s));
    s->nby = nby;
    s->use_int = use_int;
    s->name_row = row;
    s->nrows = 1;
    if (nby > 0) {
        s->by_cell = calloc((size_t)nby, sizeof(V *));
        if (!s->by_cell) return -1;
    }
    if (use_int) {
        memcpy(s->iparts, iparts, (size_t)nby * sizeof(int64_t));
        for (int b = 0; b < nby; b++) {
            s->by_cell[b] = v_int(iparts[b]);
        }
    } else {
        for (int b = 0; b < nby; b++) {
            s->by_cell[b] = cell_as_v(tbl_col(tbl, by_idx[b]), row);
        }
    }
    V *last_col = NULL;
    double last_x = 0;
    for (int sp = 0; sp < nspecs; sp++) {
        if (specs[sp].kind == COL_NAME) {
            int idx = tbl_col_idx(tbl, specs[sp].name);
            if (idx >= 0) {
                s->name_val[sp] = cell_as_v(tbl_col(tbl, idx), row);
            }
        } else if (specs[sp].kind != COL_COUNT) {
            V *col = sp_col[sp];
            double x = col == last_col && col ? last_x : cell_float(col, row);
            last_col = col;
            last_x = x;
            int kind = specs[sp].kind;
            if (col && sum_is_int(kind, col->t)) {
                s->isum[sp] = col->J[row];
                s->icarry[sp] = 0;
            } else if (agg_needs_sum(kind)) {
                s->sum[sp] = x;
            } else if (agg_needs_min(kind)) {
                s->minv[sp] = x;
                s->have_mm[sp] = 1;
            } else if (agg_needs_max(kind)) {
                s->maxv[sp] = x;
                s->have_mm[sp] = 1;
            }
        }
    }
    return 0;
}
static void gh_slot_accum(GhSlot *s, int64_t row,
                          ColSpec *specs, V **sp_col, const int *col_t,
                          const int *agg_sp, int n_agg) {
    s->nrows++;
    V *last_col = NULL;
    double last_x = 0;
    for (int k = 0; k < n_agg; k++) {
        int sp = agg_sp[k];
        V *col = sp_col[sp];
        double x;
        if (col == last_col && col) {
            x = last_x;
        } else if (col) {
            int ct = col_t[sp];
            if (ct == T_IVEC) x = (double)col->J[row];
            else if (ct == T_FVEC) x = col->F[row];
            else x = cell_float(col, row);
        } else {
            x = 0;
        }
        last_col = col;
        last_x = x;
        int kind = specs[sp].kind;
        if (col && sum_is_int(kind, col_t[sp])) {
            int_add(&s->isum[sp], &s->icarry[sp], col->J[row]);
        } else if (agg_needs_sum(kind)) {
            s->sum[sp] += x;
        } else if (agg_needs_min(kind) && x < s->minv[sp]) {
            s->minv[sp] = x;
        } else if (agg_needs_max(kind) && x > s->maxv[sp]) {
            s->maxv[sp] = x;
        }
    }
}
static int gh_find_or_insert_str(GhTab *t, const char *key, size_t key_len, V *tbl, const int *by_idx, int nby,
                                 int64_t row, ColSpec *specs, int nspecs,
                                 V **sp_col, const int *col_t, const int *agg_sp, int n_agg) {
    if (!t->slots || !t->map) return -1;
    if (t->nslots * 10 > t->cap * 7) {
        if (gh_resize(t) != 0) return -1;
    }
    uint32_t h = gh_hash_key(key, key_len);
    int cap = t->cap;
    int i = (int)(h & (uint32_t)(cap - 1));
    for (;;) {
        int si = t->map[i];
        if (si == GH_EMPTY) {
            GhSlot *s = &t->slots[t->nslots];
            if (gh_slot_init_aggs(s, tbl, by_idx, nby, row, specs, nspecs, 0, NULL, sp_col) != 0)
                return -1;
            if (key_len >= sizeof s->key) key_len = sizeof s->key - 1;
            memcpy(s->key, key, key_len);
            s->key[key_len] = 0;
            s->key_len = (int)key_len;
            si = t->nslots++;
            t->map[i] = si;
            return si;
        }
        if (!t->slots[si].use_int && gh_key_eq(&t->slots[si], key, key_len)) {
            gh_slot_accum(&t->slots[si], row, specs, sp_col, col_t, agg_sp, n_agg);
            return si;
        }
        i = (i + 1) & (cap - 1);
    }
}
static int gh_find_or_insert_int(GhTab *t, const int64_t *iparts, int nby, V *tbl,
                                 const int *by_idx, int64_t row, ColSpec *specs, int nspecs,
                                 V **sp_col, const int *col_t, const int *agg_sp, int n_agg) {
    if (!t->slots || !t->map) return -1;
    if (t->nslots * 10 > t->cap * 7) {
        if (gh_resize(t) != 0) return -1;
    }
    uint32_t h = gh_hash_iparts(iparts, nby);
    int cap = t->cap;
    int i = (int)(h & (uint32_t)(cap - 1));
    for (;;) {
        int si = t->map[i];
        if (si == GH_EMPTY) {
            GhSlot *s = &t->slots[t->nslots];
            if (gh_slot_init_aggs(s, tbl, by_idx, nby, row, specs, nspecs, 1, iparts, sp_col) != 0)
                return -1;
            si = t->nslots++;
            t->map[i] = si;
            return si;
        }
        GhSlot *s = &t->slots[si];
        if (s->use_int && s->nby == nby &&
            !memcmp(s->iparts, iparts, (size_t)nby * sizeof(int64_t))) {
            gh_slot_accum(s, row, specs, sp_col, col_t, agg_sp, n_agg);
            return si;
        }
        i = (i + 1) & (cap - 1);
    }
}
static int gh_resize(GhTab *t) {
    GhTab old = *t;
    if (gh_init(t, old.cap * 2) != 0) {
        *t = old;
        return -1;
    }
    for (int i = 0; i < old.nslots; i++) {
        GhSlot *s = &old.slots[i];
        uint32_t h = s->use_int ? gh_hash_iparts(s->iparts, s->nby) : gh_hash_key(s->key, (size_t)s->key_len);
        int cap = t->cap;
        int j = (int)(h & (uint32_t)(cap - 1));
        for (;;) {
            if (t->map[j] == GH_EMPTY) {
                t->map[j] = t->nslots;
                t->slots[t->nslots++] = *s;
                memset(s, 0, sizeof(*s));
                break;
            }
            j = (j + 1) & (cap - 1);
        }
    }
    free(old.slots);
    free(old.map);
    return 0;
}
static void gh_merge_slot(GhTab *dst, GhSlot *src, ColSpec *specs, int nspecs) {
    if (dst->nslots * 10 > dst->cap * 7) {
        if (gh_resize(dst) != 0) return;
    }
    uint32_t h = src->use_int ? gh_hash_iparts(src->iparts, src->nby) : gh_hash_key(src->key, (size_t)src->key_len);
    int cap = dst->cap;
    int i = (int)(h & (uint32_t)(cap - 1));
    for (;;) {
        int si = dst->map[i];
        if (si == GH_EMPTY) {
            si = dst->nslots++;
            dst->slots[si] = *src;
            memset(src, 0, sizeof(*src));
            dst->map[i] = si;
            return;
        }
        GhSlot *d = &dst->slots[si];
        int same = 0;
        if (src->use_int && d->use_int && d->nby == src->nby &&
            !memcmp(d->iparts, src->iparts, (size_t)src->nby * sizeof(int64_t))) {
            same = 1;
        } else if (!src->use_int && !d->use_int && d->key_len == src->key_len &&
                   (src->key_len == 0 || memcmp(d->key, src->key, (size_t)src->key_len) == 0)) {
            same = 1;
        }
        if (same) {
            d->nrows += src->nrows;
            for (int sp = 0; sp < nspecs; sp++) {
                if (specs[sp].kind == COL_COUNT || specs[sp].kind == COL_NAME) continue;
                int kind = specs[sp].kind;
                if (agg_needs_sum(kind)) {
                    d->sum[sp] += src->sum[sp];
                    d->icarry[sp] += src->icarry[sp];
                    int_add(&d->isum[sp], &d->icarry[sp], src->isum[sp]);
                } else if (src->have_mm[sp]) {
                    if (!d->have_mm[sp]) {
                        if (agg_needs_min(kind)) d->minv[sp] = src->minv[sp];
                        else d->maxv[sp] = src->maxv[sp];
                        d->have_mm[sp] = 1;
                    } else if (agg_needs_min(kind)) {
                        if (src->minv[sp] < d->minv[sp]) d->minv[sp] = src->minv[sp];
                    } else {
                        if (src->maxv[sp] > d->maxv[sp]) d->maxv[sp] = src->maxv[sp];
                    }
                }
            }
            /* Free duplicate slot resources owned by src */
            if (src->by_cell) {
                for (int b = 0; b < src->nby; b++) if (src->by_cell[b]) v_free(src->by_cell[b]);
                free(src->by_cell);
                src->by_cell = NULL;
            }
            for (int j = 0; j < 64; j++) {
                if (src->name_val[j]) {
                    v_free(src->name_val[j]);
                    src->name_val[j] = NULL;
                }
            }
            return;
        }
        i = (i + 1) & (cap - 1);
    }
}
static int gh_sort_nby;
static int gh_sort_cmp(const void *a, const void *b) {
    const GhSlot *sa = (const GhSlot *)a;
    const GhSlot *sb = (const GhSlot *)b;
    if (sa->use_int && sb->use_int) {
        for (int i = 0; i < gh_sort_nby; i++) {
            if (sa->iparts[i] < sb->iparts[i]) return -1;
            if (sa->iparts[i] > sb->iparts[i]) return 1;
        }
        return 0;
    }
    for (int i = 0; i < gh_sort_nby; i++) {
        int c = compare_v(sa->by_cell[i], sb->by_cell[i]);
        if (c) {
            return c;
        }
    }
    return 0;
}
static V*slot_agg_value(GhSlot*s,ColSpec*spec,int sp){
 P(spec->kind==COL_COUNT,v_int(s->nrows))
 P(spec->kind==COL_NAME,s->name_val[sp]?v_ref(s->name_val[sp]):v_nil())
 P(spec->kind==COL_SUM,v_float(s->sum[sp]))
 P(spec->kind==COL_AVG,v_float(s->nrows>0?s->sum[sp]/(double)s->nrows:0.0))
 P(spec->kind==COL_MIN,s->have_mm[sp]?v_float(s->minv[sp]):v_nil())
 return s->have_mm[sp]?v_float(s->maxv[sp]):v_nil();}
static int parse_by_names(V *by, const char **names, int max_names) {
    if (!by || by->t == T_NIL) {
        return 0;
    }
    if (by->t == T_STR) {
        if (!by->s[0]) {
            return 0;
        }
        names[0] = by->s;
        return 1;
    }
    if (by->t != T_LIST) {
        return -1;
    }
    if (by->n > max_names) {
        return -1;
    }
    for (int64_t i = 0; i < by->n; i++) {
        if (by->L[i]->t != T_STR) {
            return -1;
        }
        names[i] = by->L[i]->s;
    }
    return (int)by->n;
}
/* min and max of a column that isn't a list[float] are the builtins min and max of each group's
   items: an int column gives ints, a bool, char, generic or matrix column what min and max give
   for it. So are sum and avg of a generic, char or matrix column, which the float accumulators
   can't add (they read a matrix row's first item).
   They are made once the groups are (build_group_result), not by the float accumulators. */
static int agg_by_builtin(V *tbl, const ColSpec *spec) {
    V *src = spec->src[0] ? tbl_col(tbl, tbl_col_idx(tbl, spec->src)) : NULL;
    if (!src) return 0;
    if (spec->kind == COL_MIN || spec->kind == COL_MAX) return src->t != T_FVEC;
    return agg_needs_sum(spec->kind) && (src->t == T_LIST || src->t == T_CVEC || is_mat_t(src->t));
}
static void prep_agg_cols(V *tbl, ColSpec *specs, int nspecs,
                          V **sp_col, int *col_t, int *name_idx, int *agg_sp, int *n_agg) {
    *n_agg = 0;
    for (int sp = 0; sp < nspecs; sp++) {
        int idx = specs[sp].src[0] ? tbl_col_idx(tbl, specs[sp].src) : -1;
        sp_col[sp] = idx >= 0 ? tbl_col(tbl, idx) : NULL;
        name_idx[sp] = specs[sp].kind == COL_NAME ? tbl_col_idx(tbl, specs[sp].name) : -1;
        col_t[sp] = sp_col[sp] ? sp_col[sp]->t : 0;
        if (specs[sp].kind != COL_COUNT && specs[sp].kind != COL_NAME && !agg_by_builtin(tbl, &specs[sp])) {
            agg_sp[(*n_agg)++] = sp;
        }
    }
}
static inline double row_measure(V *col, int ct, int64_t row) {
    if (!col) return 0;
    if (ct == T_IVEC) return (double)col->J[row];
    if (ct == T_FVEC) return col->F[row];
    return cell_float(col, row);
}
static inline void dense_accum_row(int key_val, int64_t row, int64_t *s_nrows, int64_t *s_name_row,
                            int *nactive, ColSpec *specs, int n_agg,
                            const int *agg_sp, V **sp_col, const int *col_t,
                            double **s_sum, int64_t **s_isum, int32_t **s_icarry,
                            double **s_minv, double **s_maxv,
                            int **s_have_mm) {
    if (s_nrows[key_val] == 0) {
        s_name_row[key_val] = row;
        (*nactive)++;
    }
    s_nrows[key_val]++;
    V *last_col = NULL;
    double last_x = 0;
    for (int k = 0; k < n_agg; k++) {
        int sp = agg_sp[k];
        V *col = sp_col[sp];
        double x = col == last_col && col ? last_x : row_measure(col, col_t[sp], row);
        last_col = col;
        last_x = x;
        int kind = specs[sp].kind;
        if (s_isum[k]) {
            int_add(&s_isum[k][key_val], &s_icarry[k][key_val], col->J[row]);
        } else if (agg_needs_sum(kind)) {
            s_sum[k][key_val] += x;
        } else if (!s_have_mm[k][key_val]) {
            if (agg_needs_min(kind)) s_minv[k][key_val] = x;
            else s_maxv[k][key_val] = x;
            s_have_mm[k][key_val] = 1;
        } else if (agg_needs_min(kind)) {
            if (x < s_minv[k][key_val]) s_minv[k][key_val] = x;
        } else {
            if (x > s_maxv[k][key_val]) s_maxv[k][key_val] = x;
        }
    }
}
static int dense_to_slots(GhTab *tab, int slots_cap, int nby, const int64_t *dims,
                           int64_t *s_nrows, int64_t *s_name_row, V *tbl, const int *name_idx, int nspecs,
                           int nactive, ColSpec *specs, int n_agg, const int *agg_sp,
                           double **s_sum, int64_t **s_isum, int32_t **s_icarry,
                           double **s_minv, double **s_maxv,
                           int **s_have_mm) {
    tab->nslots = 0;
    tab->slots = NULL;
    tab->map = NULL;
    if (nby <= 0 || nby > 32 || nactive <= 0 || !dims) return 0;
    tab->nslots = nactive;
    tab->slots = malloc((size_t)nactive * sizeof(GhSlot));
    if (!tab->slots) { tab->nslots = 0; return -1; }
    int aidx = 0;
    for (int i = 0; i < slots_cap; i++) {
        if (s_nrows[i] <= 0) continue;
        GhSlot *s = &tab->slots[aidx++];
        memset(s, 0, sizeof(GhSlot));
        s->nrows = s_nrows[i];
        s->nby = nby;
        s->use_int = 1;
        s->name_row = s_name_row[i];
        s->by_cell = calloc((size_t)nby, sizeof(V *));
        if (!s->by_cell) {
            int done = aidx - 1;
            for (int k = 0; k < done; k++) {
                GhSlot *p = &tab->slots[k];
                if (p->by_cell) {
                    for (int b = 0; b < p->nby; b++) if (p->by_cell[b]) v_free(p->by_cell[b]);
                    free(p->by_cell);
                }
                for (int j = 0; j < 64; j++) if (p->name_val[j]) v_free(p->name_val[j]);
            }
            free(tab->slots);
            tab->slots = NULL;
            tab->nslots = 0;
            return -1;
        }
        int64_t flat = i;
        for (int b = nby - 1; b >= 0; b--) {
            if (dims[b] <= 0) {
                s->iparts[b] = 0;
                flat = 0;
            } else {
                s->iparts[b] = flat % dims[b];
                flat /= dims[b];
            }
            s->by_cell[b] = v_int(s->iparts[b]);
        }
        for (int sp = 0; sp < nspecs; sp++) {
            if (name_idx[sp] >= 0) {
                s->name_val[sp] = cell_as_v(tbl_col(tbl, name_idx[sp]), s_name_row[i]);
            }
        }
        for (int k = 0; k < n_agg; k++) {
            int sp = agg_sp[k];
            int kind = specs[sp].kind;
            if (s_isum[k]) {
                s->isum[sp] = s_isum[k][i];
                s->icarry[sp] = s_icarry[k][i];
            } else if (agg_needs_sum(kind)) {
                s->sum[sp] = s_sum[k][i];
            } else {
                if (agg_needs_min(kind)) s->minv[sp] = s_minv[k][i];
                else s->maxv[sp] = s_maxv[k][i];
                s->have_mm[sp] = s_have_mm[k][i];
            }
        }
    }
    return 0;
}
/* LSD radix argsort of indices by uint64 keys (stable). */
static int radix_argsort_u64(uint64_t *keys, int64_t *idx, int64_t n) {
    if (n <= 1) return 0;
    int64_t *tmp = malloc((size_t)n * sizeof(int64_t));
    uint64_t *ktmp = malloc((size_t)n * sizeof(uint64_t));
    if (!tmp || !ktmp) { free(tmp); free(ktmp); return -1; }
    int64_t counts[256];
    for (int pass = 0; pass < 8; pass++) {
        memset(counts, 0, sizeof counts);
        int shift = pass * 8;
        for (int64_t i = 0; i < n; i++) {
            counts[(keys[i] >> shift) & 0xff]++;
        }
        int64_t sum = 0;
        for (int c = 0; c < 256; c++) {
            int64_t v = counts[c];
            counts[c] = sum;
            sum += v;
        }
        for (int64_t i = 0; i < n; i++) {
            int b = (int)((keys[i] >> shift) & 0xff);
            int64_t dest = counts[b]++;
            tmp[dest] = idx[i];
            ktmp[dest] = keys[i];
        }
        memcpy(idx, tmp, (size_t)n * sizeof(int64_t));
        memcpy(keys, ktmp, (size_t)n * sizeof(uint64_t));
    }
    free(tmp);
    free(ktmp);
    return 0;
}
static int run_reduce_into_slot(GhSlot *s, V **bcols, int nby, int64_t *idx, int64_t start, int64_t len,
                                 V *tbl, ColSpec *specs, int nspecs, const int *name_idx,
                                 int n_agg, const int *agg_sp, V **sp_col, const int *col_t) {
    int64_t first = idx[start];
    memset(s, 0, sizeof(*s));
    s->nby = nby;
    s->use_int = 1;
    s->nrows = len;
    s->name_row = first;
    if (nby > 0) {
        s->by_cell = calloc((size_t)nby, sizeof(V *));
        if (!s->by_cell) return -1;
    }
    for (int b = 0; b < nby; b++) {
        s->iparts[b] = bcols[b]->J[first];
        s->by_cell[b] = v_int(s->iparts[b]);
    }
    for (int sp = 0; sp < nspecs; sp++) {
        if (name_idx[sp] >= 0) {
            s->name_val[sp] = cell_as_v(tbl_col(tbl, name_idx[sp]), first);
        }
    }
    for (int k = 0; k < n_agg; k++) {
        int sp = agg_sp[k];
        V *col = sp_col[sp];
        int ct = col_t[sp];
        int kind = specs[sp].kind;
        int need_sum = agg_needs_sum(kind);
        int need_min = agg_needs_min(kind);
        int need_max = agg_needs_max(kind);
        double sum = 0, mn = 0, mx = 0;
        int have = 0;
        if (col && sum_is_int(kind, ct)) {
            int64_t isum = 0;
            int32_t carry = 0;
            for (int64_t i = 0; i < len; i++) int_add(&isum, &carry, col->J[idx[start + i]]);
            s->isum[sp] = isum;
            s->icarry[sp] = carry;
            continue;
        }
        /* Contiguous gather + SIMD reduce for large float runs. */
        int fast = 0;
        if (col && ct == T_FVEC && len >= 64) {
            double *tmp = malloc((size_t)len * sizeof(double));
            if (tmp) {
                for (int64_t i = 0; i < len; i++) tmp[i] = col->F[idx[start + i]];
                if (need_sum) sum = shakti_sum_f64(tmp, len);
                if (need_min) mn = shakti_min_f64(tmp, len);
                if (need_max) mx = shakti_max_f64(tmp, len);
                have = need_min || need_max;
                free(tmp);
                fast = 1;
            }
        } else if (col && ct == T_IVEC && len >= 64 && (need_min || need_max)) {
            int64_t *tmp = malloc((size_t)len * sizeof(int64_t));
            if (tmp) {
                for (int64_t i = 0; i < len; i++) tmp[i] = col->J[idx[start + i]];
                if (need_min) mn = (double)shakti_min_i64(tmp, len);
                if (need_max) mx = (double)shakti_max_i64(tmp, len);
                have = 1;
                free(tmp);
                fast = 1;
            }
        }
        if (!fast) {
            for (int64_t i = 0; i < len; i++) {
                double x = row_measure(col, ct, idx[start + i]);
                if (need_sum) sum += x;
                if ((need_min || need_max) && !have) {
                    if (need_min) mn = x;
                    else mx = x;
                    have = 1;
                } else if (need_min) {
                    if (x < mn) mn = x;
                } else if (need_max) {
                    if (x > mx) mx = x;
                }
            }
        }
        if (need_sum) s->sum[sp] = sum;
        if (need_min) s->minv[sp] = mn;
        if (need_max) s->maxv[sp] = mx;
        if (need_min || need_max) s->have_mm[sp] = have;
    }
    return 0;
}
static int try_dense_ivec_group(V *tbl, V **bcols, int nby, ColSpec *specs, int nspecs,
                                GhTab *tab, const unsigned char *MB) {
    int64_t nr = tbl->n;
    if (nr <= 0 || nby <= 0 || nby > 32) return 0;
    int64_t mins[32], maxs[32], dims[32];
    for (int b = 0; b < nby; b++) {
        mins[b] = INT64_MAX;
        maxs[b] = -1;
    }
    for (int64_t r = 0; r < nr; r++) {
        if (MB && !MB[r]) continue;
        for (int b = 0; b < nby; b++) {
            int64_t v = bcols[b]->J[r];
            if (v < mins[b]) mins[b] = v;
            if (v > maxs[b]) maxs[b] = v;
        }
    }
    int64_t product = 1;
    for (int b = 0; b < nby; b++) {
        if (mins[b] < 0 || maxs[b] >= SQL_DENSE_CAP) return 0;
        dims[b] = maxs[b] + 1;
        if (dims[b] <= 0) return 0;
        if (product > SQL_DENSE_CAP / dims[b]) return 0;
        product *= dims[b];
    }
    if (product > SQL_DENSE_CAP) return 0;
    int slots_cap = (int)product;
    int64_t *s_nrows = calloc((size_t)slots_cap, sizeof(int64_t));
    int64_t *s_name_row = malloc((size_t)slots_cap * sizeof(int64_t));
    if (!s_nrows || !s_name_row) { free(s_nrows); free(s_name_row); return -1; }
    for (int i = 0; i < slots_cap; i++) s_name_row[i] = -1;
    int name_idx[64], agg_sp[64], col_t[64];
    V *sp_col[64];
    int n_agg = 0;
    prep_agg_cols(tbl, specs, nspecs, sp_col, col_t, name_idx, agg_sp, &n_agg);
    double *s_sum[64] = {0}, *s_minv[64] = {0}, *s_maxv[64] = {0};
    int64_t *s_isum[64] = {0};
    int32_t *s_icarry[64] = {0};
    int *s_have_mm[64] = {0};
    for (int k = 0; k < n_agg; k++) {
        int kind = specs[agg_sp[k]].kind;
        if (sum_is_int(kind, col_t[agg_sp[k]])) {
            s_isum[k] = calloc((size_t)slots_cap, sizeof(int64_t));
            s_icarry[k] = calloc((size_t)slots_cap, sizeof(int32_t));
            if (!s_icarry[k]) { free(s_isum[k]); s_isum[k] = NULL; }
        } else if (agg_needs_sum(kind)) {
            s_sum[k] = calloc((size_t)slots_cap, sizeof(double));
        } else {
            if (agg_needs_min(kind)) s_minv[k] = malloc((size_t)slots_cap * sizeof(double));
            else s_maxv[k] = malloc((size_t)slots_cap * sizeof(double));
            s_have_mm[k] = calloc((size_t)slots_cap, sizeof(int));
        }
        if ((agg_needs_sum(kind) && !s_sum[k] && !s_isum[k]) ||
            (!agg_needs_sum(kind) && ((!s_minv[k] && !s_maxv[k]) || !s_have_mm[k]))) {
            for (int j = 0; j <= k; j++) {
                free(s_sum[j]); free(s_isum[j]); free(s_icarry[j]);
                free(s_minv[j]); free(s_maxv[j]); free(s_have_mm[j]);
            }
            free(s_nrows);
            free(s_name_row);
            return -1;
        }
    }
    int nactive = 0;
    /* Tight inlined scans for the common 1- and 2-key cases. */
    if (nby == 1) {
        const int64_t *k0 = bcols[0]->J;
        if (n_agg == 0) {
            for (int64_t r = 0; r < nr; r++) {
                if (MB && !MB[r]) continue;
                int key_val = (int)k0[r];
                if (s_nrows[key_val]++ == 0) {
                    s_name_row[key_val] = r;
                    nactive++;
                }
            }
        } else if (n_agg == 1) {
            int sp = agg_sp[0];
            int kind = specs[sp].kind;
            V *col = sp_col[sp];
            int ct = col_t[sp];
            if (s_isum[0]) {
                int64_t *isum = s_isum[0];
                int32_t *carry = s_icarry[0];
                for (int64_t r = 0; r < nr; r++) {
                    if (MB && !MB[r]) continue;
                    int key_val = (int)k0[r];
                    if (s_nrows[key_val]++ == 0) {
                        s_name_row[key_val] = r;
                        nactive++;
                    }
                    int_add(&isum[key_val], &carry[key_val], col->J[r]);
                }
            } else if (agg_needs_sum(kind)) {
                double *sum = s_sum[0];
                for (int64_t r = 0; r < nr; r++) {
                    if (MB && !MB[r]) continue;
                    int key_val = (int)k0[r];
                    if (s_nrows[key_val]++ == 0) {
                        s_name_row[key_val] = r;
                        nactive++;
                    }
                    sum[key_val] += row_measure(col, ct, r);
                }
            } else if (agg_needs_min(kind)) {
                double *minv = s_minv[0];
                int *have = s_have_mm[0];
                for (int64_t r = 0; r < nr; r++) {
                    if (MB && !MB[r]) continue;
                    int key_val = (int)k0[r];
                    if (s_nrows[key_val]++ == 0) {
                        s_name_row[key_val] = r;
                        nactive++;
                    }
                    double x = row_measure(col, ct, r);
                    if (!have[key_val] || x < minv[key_val]) minv[key_val] = x;
                    have[key_val] = 1;
                }
            } else {
                double *maxv = s_maxv[0];
                int *have = s_have_mm[0];
                for (int64_t r = 0; r < nr; r++) {
                    if (MB && !MB[r]) continue;
                    int key_val = (int)k0[r];
                    if (s_nrows[key_val]++ == 0) {
                        s_name_row[key_val] = r;
                        nactive++;
                    }
                    double x = row_measure(col, ct, r);
                    if (!have[key_val] || x > maxv[key_val]) maxv[key_val] = x;
                    have[key_val] = 1;
                }
            }
        } else {
            for (int64_t r = 0; r < nr; r++) {
                if (MB && !MB[r]) continue;
                int key_val = (int)k0[r];
                dense_accum_row(key_val, r, s_nrows, s_name_row, &nactive, specs,
                                n_agg, agg_sp, sp_col, col_t, s_sum, s_isum, s_icarry,
                                s_minv, s_maxv, s_have_mm);
            }
        }
    } else if (nby == 2) {
        const int64_t *k0 = bcols[0]->J;
        const int64_t *k1 = bcols[1]->J;
        int64_t d1 = dims[1];
        for (int64_t r = 0; r < nr; r++) {
            if (MB && !MB[r]) continue;
            int key_val = (int)(k0[r] * d1 + k1[r]);
            dense_accum_row(key_val, r, s_nrows, s_name_row, &nactive, specs,
                            n_agg, agg_sp, sp_col, col_t, s_sum, s_isum, s_icarry,
                            s_minv, s_maxv, s_have_mm);
        }
    } else {
        for (int64_t r = 0; r < nr; r++) {
            if (MB && !MB[r]) continue;
            int64_t flat = 0;
            for (int b = 0; b < nby; b++) {
                flat = flat * dims[b] + bcols[b]->J[r];
            }
            dense_accum_row((int)flat, r, s_nrows, s_name_row, &nactive, specs,
                            n_agg, agg_sp, sp_col, col_t, s_sum, s_isum, s_icarry,
                            s_minv, s_maxv, s_have_mm);
        }
    }
    int dense_rc = dense_to_slots(tab, slots_cap, nby, dims, s_nrows, s_name_row, tbl, name_idx, nspecs,
                   nactive, specs, n_agg, agg_sp, s_sum, s_isum, s_icarry,
                   s_minv, s_maxv, s_have_mm);
    free(s_nrows);
    free(s_name_row);
    for (int k = 0; k < n_agg; k++) {
        free(s_sum[k]);
        free(s_isum[k]);
        free(s_icarry[k]);
        free(s_minv[k]);
        free(s_maxv[k]);
        free(s_have_mm[k]);
    }
    return dense_rc;
}
static int try_radix_ivec_group(V *tbl, V **bcols, int nby, ColSpec *specs, int nspecs,
                                GhTab *tab, const unsigned char *MB) {
    int64_t nr = tbl->n;
    if (nr < SQL_RADIX_MIN_ROWS) return 0;
    /* Pack keys into uint64 when domains fit; else hash-mix (equality via parts scan). */
    int64_t mins[32], maxs[32], dims[32];
    for (int b = 0; b < nby; b++) {
        mins[b] = INT64_MAX;
        maxs[b] = INT64_MIN;
    }
    for (int64_t r = 0; r < nr; r++) {
        if (MB && !MB[r]) continue;
        for (int b = 0; b < nby; b++) {
            int64_t v = bcols[b]->J[r];
            if (v < mins[b]) mins[b] = v;
            if (v > maxs[b]) maxs[b] = v;
        }
    }
    int can_pack = 1;
    uint64_t product = 1;
    for (int b = 0; b < nby; b++) {
        if (mins[b] < 0 || mins[b] == INT64_MAX || maxs[b] - mins[b] == INT64_MAX) {
            can_pack = 0;
            break;
        }
        dims[b] = maxs[b] - mins[b] + 1;
        if (dims[b] <= 0 || product > UINT64_MAX / (uint64_t)dims[b]) {
            can_pack = 0;
            break;
        }
        product *= (uint64_t)dims[b];
    }
    if (!can_pack) return 0; /* hash-sorted keys are not equality-safe for run grouping */
    /* Compact to masked rows so radix never sees filtered-out keys. */
    int64_t nkeep = 0;
    if (MB) {
        for (int64_t r = 0; r < nr; r++) if (MB[r]) nkeep++;
    } else {
        nkeep = nr;
    }
    if (nkeep == 0) {
        tab->nslots = 0;
        tab->slots = NULL;
        tab->map = NULL;
        return 1;
    }
    uint64_t *keys = malloc((size_t)nkeep * sizeof(uint64_t));
    int64_t *idx = malloc((size_t)nkeep * sizeof(int64_t));
    if (!keys || !idx) { free(keys); free(idx); return -1; }
    int64_t w = 0;
    for (int64_t r = 0; r < nr; r++) {
        if (MB && !MB[r]) continue;
        idx[w] = r;
        uint64_t flat = 0;
        for (int b = 0; b < nby; b++) {
            flat = flat * (uint64_t)dims[b] + (uint64_t)(bcols[b]->J[r] - mins[b]);
        }
        keys[w] = flat;
        w++;
    }
    if (radix_argsort_u64(keys, idx, nkeep) != 0) {
        free(keys);
        free(idx);
        return -1;
    }
    int name_idx[64], agg_sp[64], col_t[64];
    V *sp_col[64];
    int n_agg = 0;
    prep_agg_cols(tbl, specs, nspecs, sp_col, col_t, name_idx, agg_sp, &n_agg);
    /* Count groups */
    int ngrp = 0;
    for (int64_t i = 0; i < nkeep; ) {
        int64_t j = i + 1;
        while (j < nkeep && keys[j] == keys[i]) j++;
        ngrp++;
        i = j;
    }
    tab->nslots = ngrp;
    tab->slots = malloc((size_t)ngrp * sizeof(GhSlot));
    tab->map = NULL;
    if (!tab->slots) {
        tab->nslots = 0;
        free(keys);
        free(idx);
        return -1;
    }
    int g = 0;
    for (int64_t i = 0; i < nkeep; ) {
        int64_t j = i + 1;
        while (j < nkeep && keys[j] == keys[i]) j++;
        if (run_reduce_into_slot(&tab->slots[g], bcols, nby, idx, i, j - i, tbl, specs, nspecs,
                                 name_idx, n_agg, agg_sp, sp_col, col_t) != 0) {
            tab->nslots = g + 1;
            free(keys);
            free(idx);
            return -1;
        }
        for (int b = 0; b < nby; b++) {
            tab->slots[g].iparts[b] = bcols[b]->J[idx[i]];
            if (tab->slots[g].by_cell[b]) {
                v_free(tab->slots[g].by_cell[b]);
                tab->slots[g].by_cell[b] = v_int(tab->slots[g].iparts[b]);
            }
        }
        g++;
        i = j;
    }
    free(keys);
    free(idx);
    return 1;
}
static int typed_int_hash_group(V *tbl, V **bcols, int nby, const int *by_idx,
                                 ColSpec *specs, int nspecs, GhTab *tab,
                                 const unsigned char *MB) {
    int64_t nr = tbl->n;
    int name_idx[64], agg_sp[64], col_t[64];
    V *sp_col[64];
    int n_agg = 0;
    prep_agg_cols(tbl, specs, nspecs, sp_col, col_t, name_idx, agg_sp, &n_agg);
#ifdef _OPENMP
    int nt = 1;
        /* V* refcounts are not atomic; build groups serially. */
        if (0 && nr >= SQL_OMP_GROUP_MIN) {
        nt = omp_get_max_threads();
        if (nt > 16) nt = 16;
        if (nt < 1) nt = 1;
    }
    if (nt > 1) {
        GhTab *locals = calloc((size_t)nt, sizeof(GhTab));
        for (int t = 0; t < nt; t++) {
            if (gh_init(&locals[t], 256) != 0) { free(locals); return -1; }
        }
        #pragma omp parallel num_threads(nt)
        {
            int tid = omp_get_thread_num();
            GhTab *lt = &locals[tid];
            #pragma omp for schedule(static)
            for (int64_t row = 0; row < nr; row++) {
                if (MB && !MB[row]) continue;
                int64_t parts[32];
                for (int b = 0; b < nby; b++) parts[b] = bcols[b]->J[row];
                gh_find_or_insert_int(lt, parts, nby, tbl, by_idx, row, specs, nspecs,
                                      sp_col, col_t, agg_sp, n_agg);
            }
        }
        if (gh_init(tab, 256) != 0) { free(locals); return -1; }
        for (int t = 0; t < nt; t++) {
            for (int i = 0; i < locals[t].nslots; i++) {
                gh_merge_slot(tab, &locals[t].slots[i], specs, nspecs);
            }
            free(locals[t].slots);
            free(locals[t].map);
        }
        free(locals);
        return 0;
    }
#endif
    if (gh_init(tab, 256) != 0) return -1;
    for (int64_t row = 0; row < nr; row++) {
        if (MB && !MB[row]) continue;
        int64_t parts[32];
        for (int b = 0; b < nby; b++) parts[b] = bcols[b]->J[row];
        if (gh_find_or_insert_int(tab, parts, nby, tbl, by_idx, row, specs, nspecs,
                                  sp_col, col_t, agg_sp, n_agg) < 0)
            return -1;
    }
    return 0;
}
/* Each row's group (-1 for a row `where` drops). The string-key pass records the slot it put each
   row in (rowslot, before the slots were sorted); int-key groups are in key order, so a binary
   search finds a row's, or a table by key when the keys span a small range. */
static int64_t *group_ids(V *tbl, GhTab *tab, const int *by_idx, int nby, const unsigned char *MB,
                          const int64_t *rowslot) {
    int64_t nr = tbl->n, ng = tab->nslots;
    int64_t *gid = malloc((size_t)(nr ? nr : 1) * sizeof(int64_t));
    if (!gid) return NULL;
    for (int64_t r = 0; r < nr; r++) gid[r] = -1;
    if (rowslot) {
        int64_t *pos = malloc((size_t)(ng ? ng : 1) * sizeof(int64_t));
        if (!pos) { free(gid); return NULL; }
        for (int64_t g = 0; g < ng; g++) pos[rowslot[tab->slots[g].name_row]] = g;
        for (int64_t r = 0; r < nr; r++) if (rowslot[r] >= 0) gid[r] = pos[rowslot[r]];
        free(pos);
        return gid;
    }
    V *bcols[32];
    for (int b = 0; b < nby; b++) bcols[b] = tbl_col(tbl, by_idx[b]);
    if (ng == 0) return gid;
    /* keys over small ranges: index the groups by key */
    int64_t lo[32], hi[32];
    uint64_t cells = 1;
    for (int b = 0; b < nby; b++) {
        lo[b] = hi[b] = tab->slots[0].iparts[b];
        for (int64_t g = 1; g < ng; g++) {
            int64_t x = tab->slots[g].iparts[b];
            if (x < lo[b]) lo[b] = x;
            if (x > hi[b]) hi[b] = x;
        }
        uint64_t w = (uint64_t)hi[b] - (uint64_t)lo[b] + 1;
        cells = w && cells <= SQL_DENSE_CAP / w ? cells * w : SQL_DENSE_CAP + 1;
    }
    if (cells <= SQL_DENSE_CAP) {
        int64_t *at = malloc((size_t)cells * sizeof(int64_t));
        if (!at) { free(gid); return NULL; }
        for (int64_t g = 0; g < ng; g++) {
            uint64_t f = 0;
            for (int b = 0; b < nby; b++)
                f = f * ((uint64_t)hi[b] - (uint64_t)lo[b] + 1) + ((uint64_t)tab->slots[g].iparts[b] - (uint64_t)lo[b]);
            at[f] = g;
        }
        for (int64_t r = 0; r < nr; r++) {
            if (MB && !MB[r]) continue;
            uint64_t f = 0;
            for (int b = 0; b < nby; b++)
                f = f * ((uint64_t)hi[b] - (uint64_t)lo[b] + 1) + ((uint64_t)bcols[b]->J[r] - (uint64_t)lo[b]);
            gid[r] = at[f];
        }
        free(at);
        return gid;
    }
    for (int64_t r = 0; r < nr; r++) {
        if (MB && !MB[r]) continue;
        int64_t left = 0, right = ng - 1;
        while (left <= right) {
            int64_t mid = left + (right - left) / 2;
            int c = 0;
            for (int b = 0; b < nby && !c; b++) {
                int64_t x = bcols[b]->J[r], y = tab->slots[mid].iparts[b];
                c = (x > y) - (x < y);
            }
            if (!c) { gid[r] = mid; break; }
            if (c < 0) right = mid - 1; else left = mid + 1;
        }
    }
    return gid;
}
/* the items of col at the given rows, of col's own kind */
static V *col_take(V *col, const int64_t *rows, int64_t n) {
    V *r;
    switch (col->t) {
    case T_BVEC: r = v_bvec(n); for (int64_t i = 0; i < n; i++) r->B[i] = col->B[rows[i]]; return r;
    case T_CVEC: r = v_cvec(n); for (int64_t i = 0; i < n; i++) r->B[i] = col->B[rows[i]]; return r;
    case T_BMAT: case T_CMAT: {
        int64_t nc = mat_cols(col);
        r = col->t == T_BMAT ? v_bmat(n, nc) : v_cmat(n, nc);
        for (int64_t i = 0; i < n; i++)
            if (nc) memcpy(r->B + i * nc, col->B + rows[i] * nc, (size_t)nc);
        return r;
    }
    case T_IMAT: {
        int64_t nc = mat_cols(col);
        r = v_imat(n, nc);
        for (int64_t i = 0; i < n; i++)
            if (nc) memcpy(r->J + i * nc, col->J + rows[i] * nc, (size_t)nc * sizeof(int64_t));
        return r;
    }
    case T_FMAT: {
        int64_t nc = mat_cols(col);
        r = v_fmat(n, nc);
        for (int64_t i = 0; i < n; i++)
            if (nc) memcpy(r->F + i * nc, col->F + rows[i] * nc, (size_t)nc * sizeof(double));
        return r;
    }
    default: r = v_list(n); for (int64_t i = 0; i < n; i++) r->L[i] = col_get(col, rows[i]); return r;
    }
}
/* Each group's rows, in row order: group g's run from rows[g ? end[g - 1] : 0] to just before
   rows[end[g]]. Made from gid the first time a column is gathered by group, and kept for the
   select's other columns. */
typedef struct { int64_t *end, *rows; } GroupRows;
static int group_rows(GroupRows *gr, const int64_t *gid, int64_t nr, int64_t ng) {
    if (gr->end) return 0;
    int64_t *end = calloc((size_t)ng + 1, sizeof(int64_t));
    int64_t *rows = malloc((size_t)(nr ? nr : 1) * sizeof(int64_t));
    if (!end || !rows) { free(end); free(rows); return -1; }
    for (int64_t i = 0; i < nr; i++) if (gid[i] >= 0) end[gid[i] + 1]++;
    for (int64_t g = 0; g < ng; g++) end[g + 1] += end[g];
    for (int64_t i = 0; i < nr; i++) if (gid[i] >= 0) rows[end[gid[i]]++] = i;
    gr->end = end;
    gr->rows = rows;
    return 0;
}
/* One result column: each group's min, max, sum or avg of col. An int column's min and max are
   folded here, exactly, as the builtins fold one; any other is gathered by group and given to the
   builtin. */
static V *group_agg_col(V *col, const int64_t *gid, int64_t nr, int64_t ng, int kind, GroupRows *gr) {
    int is_min = kind == COL_MIN;
    if (col->t == T_IVEC && (kind == COL_MIN || kind == COL_MAX)) {
        V *r = v_ivec(ng);
        unsigned char *seen = calloc((size_t)(ng ? ng : 1), 1);
        if (!seen) { v_free(r); return NULL; }
        for (int64_t i = 0; i < nr; i++) {
            int64_t g = gid[i], x = col->J[i];
            if (g < 0) continue;
            if (!seen[g] || (is_min ? x < r->J[g] : x > r->J[g])) r->J[g] = x;
            seen[g] = 1;
        }
        free(seen);
        return r;
    }
    if (group_rows(gr, gid, nr, ng) != 0) return NULL;
    const char *fn = kind == COL_MIN ? "min" : kind == COL_MAX ? "max" : kind == COL_SUM ? "sum" : "avg";
    V *r = v_list(ng);
    for (int64_t g = 0, start = 0; g < ng; g++) {
        V *items = col_take(col, gr->rows + start, gr->end[g] - start);
        r->L[g] = builtin_call(fn, &items, 1, NULL, NULL, 0, NULL);
        v_free(items);
        start = gr->end[g];
    }
    return r;
}
static V *build_group_result(V *tbl, GhTab *tab, ColSpec *specs, int nspecs,
                             const char **by_names, const int *by_idx, int nby,
                             const unsigned char *MB, const int64_t *rowslot) {
    int include_by[32];
    for (int i = 0; i < nby; i++) include_by[i] = 1;
    for (int s = 0; s < nspecs; s++) {
        if (specs[s].kind != COL_NAME) continue;
        for (int i = 0; i < nby; i++) {
            if (!strcmp(specs[s].name, by_names[i])) include_by[i] = 0;
        }
    }
    int out_cols = 0;
    for (int i = 0; i < nby; i++) if (include_by[i]) out_cols++;
    out_cols += nspecs;
    if (nspecs == 0) out_cols = nby;
    V *keys = v_list(out_cols);
    V *data = v_list(out_cols);
    int c = 0;
    int all_int_keys = tab->nslots == 0 || (tab->slots[0].use_int);
    int64_t *gid = NULL;
    GroupRows grows = {NULL, NULL};
    int agg_done[64] = {0}, agg_oom = 0;
    if (nspecs == 0) {
        for (int i = 0; i < nby; i++) {
            keys->L[c] = v_ref(tbl->keys->L[by_idx[i]]);
            data->L[c] = all_int_keys ? v_ivec(tab->nslots) : v_list(tab->nslots);
            c++;
        }
    } else {
        for (int i = 0; i < nby; i++) {
            if (!include_by[i]) continue;
            keys->L[c] = v_ref(tbl->keys->L[by_idx[i]]);
            data->L[c] = all_int_keys ? v_ivec(tab->nslots) : v_list(tab->nslots);
            c++;
        }
        for (int s = 0; s < nspecs; s++) {
            keys->L[c] = v_str(specs[s].kind == COL_COUNT && specs[s].src[0] == 0 ? "count" : specs[s].name);
            V *src = specs[s].src[0] ? tbl_col(tbl, tbl_col_idx(tbl, specs[s].src)) : NULL;
            if (specs[s].kind == COL_COUNT || (src && sum_is_int(specs[s].kind, src->t))) {
                data->L[c] = v_ivec(tab->nslots);
            } else if (specs[s].kind == COL_NAME) {
                data->L[c] = v_list(tab->nslots);
            } else if (agg_by_builtin(tbl, &specs[s])) {
                if (!gid) gid = group_ids(tbl, tab, by_idx, nby, MB, rowslot);
                data->L[c] = gid ? group_agg_col(src, gid, tbl->n, tab->nslots, specs[s].kind, &grows) : NULL;
                if (!data->L[c]) { agg_oom = 1; data->L[c] = v_list(0); }
                agg_done[s] = 1;
            } else {
                data->L[c] = v_fvec(tab->nslots);
            }
            c++;
        }
    }
    for (int g = 0; g < tab->nslots; g++) {
        GhSlot *slot = &tab->slots[g];
        int col = 0;
        if (nspecs == 0) {
            for (int i = 0; i < nby; i++) {
                if (data->L[col]->t == T_IVEC) {
                    data->L[col]->J[g] = slot->use_int ? slot->iparts[i] :
                        (slot->by_cell[i] && slot->by_cell[i]->t == T_INT ? slot->by_cell[i]->j : 0);
                } else {
                    data->L[col]->L[g] = slot->by_cell[i] ? v_ref(slot->by_cell[i]) : v_nil();
                }
                col++;
            }
        } else {
            for (int i = 0; i < nby; i++) {
                if (!include_by[i]) continue;
                if (data->L[col]->t == T_IVEC) {
                    data->L[col]->J[g] = slot->use_int ? slot->iparts[i] :
                        (slot->by_cell[i] && slot->by_cell[i]->t == T_INT ? slot->by_cell[i]->j : 0);
                } else {
                    data->L[col]->L[g] = slot->by_cell[i] ? v_ref(slot->by_cell[i]) : v_nil();
                }
                col++;
            }
            for (int s = 0; s < nspecs; s++) {
                V *colv = data->L[col];
                if (agg_done[s]) {
                    /* filled above */
                } else if (colv->t == T_IVEC) {
                    if (specs[s].kind == COL_SUM && slot->icarry[s]) g_isum_ovf = 1;
                    colv->J[g] = specs[s].kind == COL_SUM ? slot->isum[s] : slot->nrows;
                } else if (colv->t == T_FVEC) {
                    if (specs[s].kind == COL_SUM) colv->F[g] = slot->sum[s];
                    else if (specs[s].kind == COL_AVG)
                        colv->F[g] = slot->nrows > 0 ? slot->sum[s] / (double)slot->nrows : 0.0;
                    else if (specs[s].kind == COL_MIN)
                        colv->F[g] = slot->have_mm[s] ? slot->minv[s] : 0.0;
                    else
                        colv->F[g] = slot->have_mm[s] ? slot->maxv[s] : 0.0;
                } else {
                    colv->L[g] = slot_agg_value(slot, &specs[s], s);
                }
                col++;
            }
        }
    }
    free(gid);
    free(grows.end);
    free(grows.rows);
    if (agg_oom) { v_free(keys); v_free(data); return v_err("select: out of memory"); }
    return v_table_own(keys, data);
}
static V *tbl_group_select_once(V *tbl, ColSpec *specs, int nspecs, V *by, V *row_mask) {
    /* row_mask: optional T_BVEC; when set, only rows with mask[r] participate (no materialize). */
    const unsigned char *MB = (row_mask && row_mask->t == T_BVEC) ? row_mask->B : NULL;
    const char *by_names[32];
    int nby = parse_by_names(by, by_names, 32);
    if (nby < 0) {
        return v_err("select: bad by column list");
    }
    if (nby == 0) {
        return v_err("select: by requires column names");
    }
    int by_idx[32];
    for (int i = 0; i < nby; i++) {
        by_idx[i] = tbl_col_idx(tbl, by_names[i]);
        if (by_idx[i] < 0) {
            return v_errf("select: unknown group column '%s'", by_names[i]);
        }
    }
    /* A name that is no column (nor an aggregate's keyword) is an error, as in a select without by. */
    for (int s = 0; s < nspecs; s++) {
        const char *name = specs[s].kind == COL_NAME ? specs[s].name : specs[s].src;
        if (name[0] && tbl_col_idx(tbl, name) < 0) {
            return v_errf("select: unknown column '%s'", name);
        }
    }
    GhTab tab;
    memset(&tab, 0, sizeof tab);
    int64_t *rowslot = NULL;
    V *bcols[32];
    int all_ivec = 1;
    for (int i = 0; i < nby; i++) {
        bcols[i] = tbl_col(tbl, by_idx[i]);
        if (!bcols[i] || bcols[i]->t != T_IVEC) {
            all_ivec = 0;
            break;
        }
    }
    if (all_ivec && tbl->n > 0) {
        int gr = try_dense_ivec_group(tbl, bcols, nby, specs, nspecs, &tab, MB);
        if (gr < 0) { gh_free(&tab); return v_err("select: out of memory"); }
        if (gr > 0) goto build_table;
        gr = try_radix_ivec_group(tbl, bcols, nby, specs, nspecs, &tab, MB);
        if (gr < 0) { gh_free(&tab); return v_err("select: out of memory"); }
        if (gr > 0) goto build_table;
        if (typed_int_hash_group(tbl, bcols, nby, by_idx, specs, nspecs, &tab, MB) != 0) {
            gh_free(&tab);
            return v_err("select: out of memory");
        }
        gh_sort_nby = nby;
        qsort(tab.slots, (size_t)tab.nslots, sizeof(GhSlot), gh_sort_cmp);
        goto build_table;
    }
    /* String / mixed-key fallback */
    {
        int name_idx[64], agg_sp[64], col_t[64];
        V *sp_col[64];
        int n_agg = 0;
        prep_agg_cols(tbl, specs, nspecs, sp_col, col_t, name_idx, agg_sp, &n_agg);
        if (gh_init(&tab, 256) != 0) return v_err("select: out of memory");
        for (int sp = 0; sp < nspecs && !rowslot; sp++) {
            if (!agg_by_builtin(tbl, &specs[sp])) continue;
            rowslot = malloc((size_t)(tbl->n ? tbl->n : 1) * sizeof(int64_t));
            if (!rowslot) { gh_free(&tab); return v_err("select: out of memory"); }
            for (int64_t row = 0; row < tbl->n; row++) rowslot[row] = -1;
        }
        char key[512];
        for (int64_t row = 0; row < tbl->n; row++) {
            if (MB && !MB[row]) continue;
            int key_len = composite_key(tbl, by_idx, nby, row, key, sizeof key);
            if (key_len < 0) {
                gh_free(&tab);
                free(rowslot);
                return v_err("select: group key too long");
            }
            int si = gh_find_or_insert_str(&tab, key, (size_t)key_len, tbl, by_idx, nby, row, specs, nspecs,
                                           sp_col, col_t, agg_sp, n_agg);
            if (si < 0) {
                gh_free(&tab);
                free(rowslot);
                return v_err("select: out of memory");
            }
            if (rowslot) rowslot[row] = si;
        }
        gh_sort_nby = nby;
        qsort(tab.slots, (size_t)tab.nslots, sizeof(GhSlot), gh_sort_cmp);
    }
build_table:;
    V *result = build_group_result(tbl, &tab, specs, nspecs, by_names, by_idx, nby, MB, rowslot);
    free(rowslot);
    if (tab.map) {
        gh_free(&tab);
    } else {
        for (int i = 0; i < tab.nslots; i++) {
            GhSlot *s = &tab.slots[i];
            if (s->by_cell) {
                for (int b = 0; b < s->nby; b++) if (s->by_cell[b]) v_free(s->by_cell[b]);
                free(s->by_cell);
            }
            for (int j = 0; j < 64; j++) if (s->name_val[j]) v_free(s->name_val[j]);
        }
        free(tab.slots);
    }
    return result;
}
static V *tbl_group_select(V *tbl, ColSpec *specs, int nspecs, V *by, V *row_mask) {
    g_isum_ovf = 0;
    V *r = tbl_group_select_once(tbl, specs, nspecs, by, row_mask);
    if (!g_isum_ovf) return r;
    v_free(r);
    g_isum_off = 1;
    r = tbl_group_select_once(tbl, specs, nspecs, by, row_mask);
    g_isum_off = 0;
    return r;
}
V *table_sql_select(V *from, V *cols, V *by, V *where) {
    if (!from || from->t == T_ERR) {
        return from ? v_ref(from) : v_err("select: missing table");
    }
    if (from->t != T_TABLE) {
        return v_err("select: need table");
    }
    int from_shared = shakti_count(from) > 1 || shakti_count(from->vals) > 1;
    const char *by_names[32];
    int nby = parse_by_names(by, by_names, 32);
    if (nby < 0) {
        return v_err("select: bad by column list");
    }
    ColSpec specs[64];
    int nspecs = parse_col_specs(cols, specs, 64);
    if (nspecs < 0) {
        return v_err("select: bad column list");
    }
    V *mask = NULL;
    if (where && where->t != T_NIL) {
        mask = where_mask(from, where);
        if (mask->t == T_ERR) {
            return mask;
        }
    }
    /* Grouped aggregates: fuse WHERE into the group scan (no full-table materialize). */
    if (nby > 0) {
        V *result = tbl_group_select(from, specs, nspecs, by, mask);
        if (mask) v_free(mask);
        return result;
    }
    /* Project / filter-only: still materialize matching rows. */
    V *filtered = from;
    int free_filtered = 0;
    if (mask) {
        filtered = tbl_filter_mask(from, mask);
        v_free(mask);
        free_filtered = 1;
        if (filtered->t == T_ERR) {
            return filtered;
        }
    }
    V *result;
    if (nspecs == 0) {
        result = v_ref(filtered);
    } else {
        int has_agg = 0;
        for (int i = 0; i < nspecs; i++) {
            if (specs[i].kind != COL_NAME) {
                has_agg = 1;
                break;
            }
        }
        if (has_agg) {
            if (free_filtered) v_free(filtered);
            return v_err("select: aggregates require by");
        }
        V *names = v_list(nspecs);
        for (int i = 0; i < nspecs; i++) {
            names->L[i] = v_str(specs[i].name);
        }
        result = tbl_project_names(filtered, names);
        v_free(names);
    }
    /* No filter, or one that keeps every row, hands out from's own columns: copy
     * them unless from is a temporary table that dies here (a load, a nested
     * select). A column a temporary was built from (x in table(a: x)) is still
     * shared with whatever holds it. */
    if (filtered == from && result->t == T_TABLE && from_shared) {
        V *own = table_copy_cols(result);
        v_free(result);
        result = own;
    }
    if (free_filtered) v_free(filtered);
    return result;
}
V *table_sql_delete(V *from, V *cols, V *where) {
    (void)cols;
    if (!from || from->t == T_ERR) {
        return from ? v_ref(from) : v_err("delete: missing table");
    }
    if (from->t != T_TABLE) {
        return v_err("delete: need table");
    }
    V *mask = where_mask(from, where);
    if (mask->t == T_ERR) {
        return mask;
    }
    V *keep = v_bvec(from->n);
    for (int64_t i = 0; i < from->n && i < mask->n; i++) {
        keep->B[i] = mask->B[i] ? 0 : 1;
    }
    v_free(mask);
    if (where && where->t != T_NIL) {
        V *result = tbl_filter_mask(from, keep);
        v_free(keep);
        if (result == from) {
            /* Every row kept: a new table on from's columns, as update gives (the
             * caller copies them if from has an alias). */
            V *data = v_ref(from->vals);
            v_list_unshare(&data);
            v_free(result);
            result = v_table_own(v_ref(from->keys), data);
        }
        return result;
    }
    v_free(keep);
    V *empty_data = v_list(from->keys->n);
    for (int64_t c = 0; c < from->keys->n; c++) {
        V *col = from->vals->L[c];
        if (col->t == T_IVEC) {
            empty_data->L[c] = v_ivec(0);
        } else if (col->t == T_FVEC) {
            empty_data->L[c] = v_fvec(0);
        } else if (col->t == T_BVEC) {
            empty_data->L[c] = v_bvec(0);
        } else if (col->t == T_CVEC) {
            empty_data->L[c] = v_cvec(0);
        } else if (col->t == T_IMAT) {
            empty_data->L[c] = v_imat(0, mat_cols(col));
        } else if (col->t == T_FMAT) {
            empty_data->L[c] = v_fmat(0, mat_cols(col));
        } else if (col->t == T_BMAT) {
            empty_data->L[c] = v_bmat(0, mat_cols(col));
        } else if (col->t == T_CMAT) {
            empty_data->L[c] = v_cmat(0, mat_cols(col));
        } else if (col->t == T_LIST) {
            empty_data->L[c] = v_list(0);
        } else {
            empty_data->L[c] = v_ref(col);
        }
    }
    return v_table_own(v_copy(from->keys), empty_data);
}
static inline V*empty_column_like(V*sample){
    if(!sample)return v_list(0);
    if(sample->t==T_IVEC)return v_ivec(0);
    if(sample->t==T_FVEC)return v_fvec(0);
    if(sample->t==T_BVEC)return v_bvec(0);
    if(sample->t==T_CVEC)return v_cvec(0);
    if(sample->t==T_IMAT)return v_imat(0, mat_cols(sample));
    if(sample->t==T_FMAT)return v_fmat(0, mat_cols(sample));
    if(sample->t==T_BMAT)return v_bmat(0, mat_cols(sample));
    if(sample->t==T_CMAT)return v_cmat(0, mat_cols(sample));
    return v_list(0);}
/* The kind of item a column holds: int for a list[int] or a matrix[int], and so on. */
static int col_item_t(int t) {
    switch (t) {
    case T_IVEC: case T_IMAT: return T_INT;
    case T_FVEC: case T_FMAT: return T_FLOAT;
    case T_CVEC: case T_CMAT: return T_CHAR;
    case T_BVEC: case T_BMAT: return T_BOOL;
    default: return T_LIST;
    }
}
/* Sets row i of matrix m to cell (fit_err has checked that it fits). */
static void mat_put_row(V *m, int64_t i, V *cell) {
    int64_t cols = mat_cols(m), at = mat_idx(m, i, 0);
    if (cell->t != T_LIST && col_item_t(cell->t) == col_item_t(m->t)) {
        if (m->t == T_IMAT) memcpy(m->J + at, cell->J, (size_t)cols * 8);
        else if (m->t == T_FMAT) memcpy(m->F + at, cell->F, (size_t)cols * 8);
        else memcpy(m->B + at, cell->B, (size_t)cols);
        return;
    }
    /* a list row, or ints or chars into a matrix[float], or chars into a matrix[int] */
    for (int64_t j = 0; j < cols; j++) {
        V *e = cell->t == T_LIST ? cell->L[j] : NULL;
        if (e && e->t == T_FLOAT) { m->F[at + j] = e->f; continue; }
        int64_t x = e ? e->j : cell->t == T_IVEC ? cell->J[j] : cell->B[j];
        if (m->t == T_FMAT) m->F[at + j] = (double)x;
        else if (m->t == T_IMAT) m->J[at + j] = x;
        else m->B[at + j] = (unsigned char)x;
    }
}
/* Sets row i of col to cell, as insert and update put a value in a column (fit_err has checked that
   it fits; a list's item i is the caller's to free first). */
static inline void col_put(V *col, int64_t i, V *cell) {
    switch (col->t) {
    case T_IVEC: col->J[i] = cell->j; break;
    case T_FVEC: col->F[i] = cell->t == T_FLOAT ? cell->f : (double)cell->j; break;
    case T_BVEC: col->B[i] = cell->j ? 1 : 0; break;
    case T_CVEC: col->B[i] = (unsigned char)cell->j; break;
    case T_LIST: col->L[i] = cell->t == T_STR ? v_str(cell->s) : v_ref(cell); break;
    default: mat_put_row(col, i, cell);
    }
}
static V *append_cell(V *col, V *cell) {
    if (col->t == T_IVEC) {
        if (shakti_count(col) == 1) {
            if (col->owner_kind == V_OWNER_MAP_ALIAS) {
                if (v_ensure_writable(col) != 0)
                    return v_err("insert: out of memory");
            }
            int64_t cap64 = (int64_t)col->_ht_cap;
            if (cap64 < col->n) cap64 = col->n;
            if (col->n >= cap64) {
                if (cap64 > (int64_t)(UINT32_MAX / 2))
                    return v_err("insert: too large");
                int64_t new_cap = cap64 ? cap64 * 2 : 8;
                int64_t *nj = realloc(col->J, (size_t)new_cap * sizeof(int64_t));
                if (!nj) return v_err("insert: out of memory");
                col->J = nj;
                col->_ht_cap = (uint32_t)new_cap;
            }
            col_put(col, col->n++, cell);
            return col;
        }
        V *out = v_ivec(col->n + 1);
        memcpy(out->J, col->J, (size_t)col->n * sizeof(int64_t));
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_FVEC) {
        V *out = v_fvec(col->n + 1);
        memcpy(out->F, col->F, (size_t)col->n * sizeof(double));
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_BVEC) {
        V *out = v_bvec(col->n + 1);
        memcpy(out->B, col->B, (size_t)col->n);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_CVEC) {
        V *out = v_cvec(col->n + 1);
        if (col->n > 0) memcpy(out->B, col->B, (size_t)col->n);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_IMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_imat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->J, col->J, (size_t)col->n * cols * 8);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_FMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_fmat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->F, col->F, (size_t)col->n * cols * 8);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_BMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_bmat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->B, col->B, (size_t)col->n * cols);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_CMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_cmat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->B, col->B, (size_t)col->n * cols);
        col_put(out, col->n, cell);
        return out;
    }
    if (col->t == T_LIST) {
        if (shakti_count(col) == 1) {
            if (col->n >= col->_ht_cap) {
                int cap = col->_ht_cap ? col->_ht_cap * 2 : 8;
                V **nl = realloc(col->L, (size_t)cap * sizeof(V*));
                if (!nl) return v_err("insert: out of memory");
                col->L = nl;
                col->_ht_cap = cap;
            }
            col_put(col, col->n++, cell);
            return col;
        }
        V *out = v_list(col->n + 1);
        for (int64_t i = 0; i < col->n; i++) {
            out->L[i] = v_ref(col->L[i]);
        }
        col_put(out, col->n, cell);
        return out;
    }
    return v_err("insert: unsupported column type");
}
/* Whether a column of kind t holds an item of kind xt as it is (for a matrix: as an item of a row):
   a list[int] takes an int or a char, a list[float] a float, an int or a char (an int becomes a
   float, and a char its code), a list[char] a char, a list[bool] a bool, and a list anything. */
static int item_fits(int t, int xt) {
    switch (t) {
    case T_IVEC: case T_IMAT: return xt == T_INT || xt == T_CHAR;
    case T_FVEC: case T_FMAT: return xt == T_FLOAT || xt == T_INT || xt == T_CHAR;
    case T_CVEC: case T_CMAT: return xt == T_CHAR;
    case T_BVEC: case T_BMAT: return xt == T_BOOL;
    default: return 1;
    }
}
/* NULL if col holds cell, else the error of op (insert or update), which names what the column
   holds: a matrix takes a row of its width whose items fit, any other column an item that fits. */
static V *fit_err(const char *op, V *col, V *cell, const char *name) {
    const char *holds = type_name(col_item_t(col->t));
    if (!is_mat_t(col->t)) {
        return item_fits(col->t, cell->t) ? NULL :
            v_errf("%s: column '%s' holds %s, not %s", op, name, holds, type_name(cell->t));
    }
    if (!is_vec_t(cell->t) && cell->t != T_LIST)
        return v_errf("%s: column '%s' holds rows of %s, not %s (a row is a list of %lld)",
                      op, name, holds, type_name(cell->t), (long long)mat_cols(col));
    if (cell->n != mat_cols(col)) return v_errf("%s: matrix width mismatch", op);
    int bad = cell->t != T_LIST && !item_fits(col->t, col_item_t(cell->t)) ? col_item_t(cell->t) : -1;
    for (int64_t j = 0; bad < 0 && cell->t == T_LIST && j < cell->n; j++) {
        int xt = cell->L[j] ? cell->L[j]->t : T_NIL;
        if (!item_fits(col->t, xt)) bad = xt;
    }
    return bad < 0 ? NULL :
        v_errf("%s: column '%s' holds rows of %s, not %s", op, name, holds, type_name(bad));
}
/* Sets each row mask picks in col to row r of v, or to its own row of v if r < 0; v is a column of
   col's own kind and width, and may be col itself when r >= 0 (row r is left as it is). */
static void col_copy_rows(V *col, V *v, int64_t r, V *mask) {
    int64_t n = mask->n < col->n ? mask->n : col->n;
    unsigned char *m = mask->B;
    if (col->t == T_IVEC) {
        int64_t *d = col->J, *x = v->J;
        for (int64_t i = 0; i < n; i++) if (m[i]) d[i] = x[r < 0 ? i : r];
    } else if (col->t == T_FVEC) {
        double *d = col->F, *x = v->F;
        for (int64_t i = 0; i < n; i++) if (m[i]) d[i] = x[r < 0 ? i : r];
    } else if (col->t == T_LIST) {
        for (int64_t i = 0; i < n; i++)
            if (m[i] && i != r) { v_free(col->L[i]); col->L[i] = v_ref(v->L[r < 0 ? i : r]); }
    } else {
        /* a list[bool] or list[char], or a matrix: a row is w items of sz bytes */
        size_t sz = col->t == T_IMAT || col->t == T_FMAT ? 8 : 1, w = is_mat_t(col->t) ? (size_t)mat_cols(col) : 1;
        unsigned char *d = col->t == T_IMAT ? (unsigned char *)col->J : col->t == T_FMAT ? (unsigned char *)col->F : col->B;
        unsigned char *x = col->t == T_IMAT ? (unsigned char *)v->J : col->t == T_FMAT ? (unsigned char *)v->F : v->B;
        for (int64_t i = 0; i < n; i++)
            if (m[i] && i != r) memcpy(d + (size_t)i * w * sz, x + (size_t)(r < 0 ? i : r) * w * sz, w * sz);
    }
}
/* Sets each row mask picks in col to its own row of v, a vector or matrix of another kind whose items
   col holds (ints or chars into a list[float] or matrix[float], chars into a list[int] or
   matrix[int]), each item converted as col_put converts it. */
static void col_convert_rows(V *col, V *v, V *mask) {
    int64_t n = mask->n < col->n ? mask->n : col->n, w = is_mat_t(col->t) ? mat_cols(col) : 1;
    int to_float = col->t == T_FVEC || col->t == T_FMAT, ints = v->t == T_IVEC || v->t == T_IMAT;
    for (int64_t i = 0; i < n; i++) {
        if (!mask->B[i]) continue;
        for (int64_t j = i * w; j < (i + 1) * w; j++) {
            int64_t x = ints ? v->J[j] : v->B[j];
            if (to_float) col->F[j] = (double)x; else col->J[j] = x;
        }
    }
}
/* NULL if col (not a list) holds each row of v, a vector or matrix as long as col, else fit_err's
   error: v's kind, and a matrix's width, say what every row of it is, so an empty v is checked as
   a full one is. A vector is a column for a vector column only (one row for a matrix column). */
static V *fit_col_err(V *col, V *v, const char *name) {
    const char *holds = type_name(col_item_t(col->t));
    int xt = col_item_t(v->t);
    if (!is_mat_t(col->t)) {
        if (is_mat_t(v->t))   /* each row of v is a row, which a vector column doesn't hold */
            xt = v->t == T_IMAT ? T_IVEC : v->t == T_FMAT ? T_FVEC : v->t == T_CMAT ? T_CVEC : T_BVEC;
        return item_fits(col->t, xt) ? NULL :
            v_errf("update: column '%s' holds %s, not %s", name, holds, type_name(xt));
    }
    if (mat_cols(v) != mat_cols(col)) return v_err("update: matrix width mismatch");
    return item_fits(col->t, xt) ? NULL :
        v_errf("update: column '%s' holds rows of %s, not %s", name, holds, type_name(xt));
}
/* col with the rows mask picks set from v: one value for each of them, or, if v is a list, a matrix,
   or a vector (but one row for a matrix column), a column as long as col whose row i goes in row i.
   Each value goes in as insert puts it, or the update is an error that changes nothing. */
static V *merge_update_col(V *col, V *v, V *mask, const char *name) {
    int whole = v->t == T_LIST || is_mat_t(v->t) || (is_vec_t(v->t) && !is_mat_t(col->t));
    if (whole && v->n != col->n) return v_err("update: column length mismatch");
    V *err = NULL;
    if (!whole) {
        err = fit_err("update", col, v, name);
    } else if (col->t == T_LIST) {
        /* a list column holds anything */
    } else if (v->t == T_LIST) {
        for (int64_t i = 0; !err && i < v->n; i++) err = fit_err("update", col, v->L[i], name);
    } else {
        err = fit_col_err(col, v, name);
    }
    if (err) return err;
    V *out = v_copy(col);
    if (v->t == col->t) {
        col_copy_rows(out, v, -1, mask);   /* a column of col's own kind: its rows as they are */
        return out;
    }
    if (whole && (v->t == T_IVEC || v->t == T_CVEC || v->t == T_IMAT || v->t == T_CMAT) &&
        (out->t == T_IVEC || out->t == T_FVEC || out->t == T_IMAT || out->t == T_FMAT)) {
        col_convert_rows(out, v, mask);   /* converted in place, without a value for each row */
        return out;
    }
    for (int64_t i = 0; i < mask->n && i < out->n; i++) {
        if (!mask->B[i]) continue;
        if (!whole && out->t == T_LIST) {   /* one value into a list column: each row its own copy */
            v_free(out->L[i]);
            out->L[i] = v_copy(v);
            continue;
        }
        V *x = whole ? col_get(v, i) : v;
        if (out->t == T_LIST) v_free(out->L[i]);
        col_put(out, i, x);
        if (whole) v_free(x);
        /* one value into a vector or matrix: put in once, then copied to the other rows */
        if (!whole && out->t != T_LIST) { col_copy_rows(out, out, i, mask); break; }
    }
    return out;
}
V *table_sql_update(V *from, V *cols, V *where) {
    if (!from || from->t == T_ERR) {
        return from ? v_ref(from) : v_err("update: missing table");
    }
    if (from->t != T_TABLE) {
        return v_err("update: need table");
    }
    if (!cols || cols->t != T_DICT) {
        return v_err("update: need assignment dict");
    }
    V *mask = where_mask(from, where);
    if (mask->t == T_ERR) {
        return mask;
    }
    V *new_data = v_list(from->keys->n);
    for (int64_t c = 0; c < from->keys->n; c++) {
        new_data->L[c] = v_ref(from->vals->L[c]);
    }
    for (int64_t u = 0; u < cols->keys->n; u++) {
        const char *name = cols->keys->L[u]->s;
        V *new_col = cols->vals->L[u];
        int idx = tbl_col_idx(from, name);
        if (idx < 0) {
            v_free(mask);
            v_free(new_data);
            return v_errf("update: unknown column '%s'", name);
        }
        V *merged = merge_update_col(from->vals->L[idx], new_col, mask, name);
        if (merged->t == T_ERR) {
            v_free(mask);
            v_free(new_data);
            return merged;
        }
        v_free(new_data->L[idx]);
        new_data->L[idx] = merged;
    }
    v_free(mask);
    return v_table_own(v_copy(from->keys), new_data);
}
static void col_truncate(V *col, int64_t n0) {
    if (!col || col->n <= n0) return;
    if (col->t == T_LIST) {
        for (int64_t i = n0; i < col->n; i++) v_free(col->L[i]);
    }
    col->n = n0;
}
V *table_sql_create_table(V *name, V *cols) {
    (void)name;
    if (!cols || (cols->t != T_DICT && cols->t != T_LIST)) {
        return v_err("create table: need schema dict");
    }
    V *keys = v_list(0);
    V *data = v_list(0);
    if (cols->t == T_DICT) {
        for (int64_t i = 0; i < cols->keys->n; i++) {
            v_list_append(keys, cols->keys->L[i]);
            V *def = cols->vals->L[i];
            if (!def || def->t == T_NIL) {
                v_list_append_own(data, v_ivec(0));
            } else if (def->t == T_INT) {
                v_list_append_own(data, v_ivec(0));
            } else if (def->t == T_FLOAT) {
                v_list_append_own(data, v_fvec(0));
            } else if (def->t == T_STR) {
                v_list_append_own(data, v_list(0));
            } else if (def->t == T_LIST) {
                v_list_append_own(data, v_list(0));
            } else {
                v_list_append_own(data, empty_column_like(def));
            }
        }
        return v_table_own(keys, data);
    }
    for (int64_t i = 0; i < cols->n; i++) {
        if (cols->L[i]->t != T_STR) {
            v_free(keys);
            v_free(data);
            return v_err("create table: column names must be strings");
        }
        v_list_append(keys, cols->L[i]);
        v_list_append_own(data, v_ivec(0));
    }
    return v_table_own(keys, data);
}
V *table_sql_insert(V *table, V *cols, V *vals) {
    if (!table || table->t == T_ERR) {
        return table ? v_ref(table) : v_err("insert: missing table");
    }
    if (table->t != T_TABLE) {
        return v_err("insert: need table");
    }
    /* Accept T_LIST, or numeric vectors from N_LIST promotion of VALUES (...). */
    V *vals_list = NULL;
    int free_vals_list = 0;
    if (!vals) {
        return v_err("insert: need values list");
    }
    if (vals->t == T_LIST) {
        vals_list = vals;
    } else if (vals->t == T_IVEC || vals->t == T_FVEC || vals->t == T_BVEC) {
        vals_list = v_list(vals->n);
        free_vals_list = 1;
        for (int64_t i = 0; i < vals->n; i++) {
            if (vals->t == T_IVEC) vals_list->L[i] = v_int(vals->J[i]);
            else if (vals->t == T_FVEC) vals_list->L[i] = v_float(vals->F[i]);
            else vals_list->L[i] = v_bool(vals->B[i]);
        }
    } else {
        return v_err("insert: need values list");
    }
    vals = vals_list;
    int use_all_cols = !cols || cols->t == T_NIL || (cols->t == T_LIST && cols->n == 0);
    int64_t ncols = use_all_cols ? table->keys->n : cols->n;
    if (vals->n != ncols) {
        if (free_vals_list) v_free(vals_list);
        return v_err("insert: column/value count mismatch");
    }
    for (int64_t i = 0; i < ncols; i++) {
        const char *name = use_all_cols ? table->keys->L[i]->s : cols->L[i]->s;
        int idx = tbl_col_idx(table, name);
        if (idx < 0 || !table->keys->L[idx] || table->keys->L[idx]->t != T_STR) {
            if (free_vals_list) v_free(vals_list);
            return v_errf("insert: unknown column '%s'", name ? name : "");
        }
        if (!use_all_cols) {
            for (int64_t k = 0; k < i; k++) {
                if (!strcmp(cols->L[k]->s, name)) {
                    if (free_vals_list) v_free(vals_list);
                    return v_errf("insert: column '%s' named more than once", name);
                }
            }
        }
        V *err = fit_err("insert", table->vals->L[idx], vals_list->L[i], name);
        if (err) {
            if (free_vals_list) v_free(vals_list);
            return err;
        }
    }
    if (shakti_count(table) == 1 && table->vals && shakti_count(table->vals) == 1 && use_all_cols) {
        int64_t *saved_n = malloc((size_t)table->keys->n * sizeof(int64_t));
        if (!saved_n) {
            if (free_vals_list) v_free(vals_list);
            return v_err("insert: out of memory");
        }
        for (int64_t c = 0; c < table->keys->n; c++)
            saved_n[c] = table->vals->L[c] ? table->vals->L[c]->n : 0;
        for (int64_t i = 0; i < ncols; i++) {
            const char *name = table->keys->L[i]->s;
            int idx = tbl_col_idx(table, name);
            V *col = table->vals->L[idx];
            V *next = append_cell(col, vals_list->L[i]);
            if (next && next->t == T_ERR) {
                for (int64_t c = 0; c < table->keys->n; c++)
                    col_truncate(table->vals->L[c], saved_n[c]);
                free(saved_n);
                if (free_vals_list) v_free(vals_list);
                return next;
            }
            if (next != col) {
                v_free(table->vals->L[idx]);
                table->vals->L[idx] = next;
            }
        }
        free(saved_n);
        if (table->vals->n > 0 && table->vals->L[0]) {
            table->n = table->vals->L[0]->n;
        }
        if (free_vals_list) v_free(vals_list);
        return v_ref(table);
    }
    V *new_data = v_list(table->keys->n);
    for (int64_t c = 0; c < table->keys->n; c++) {
        new_data->L[c] = v_ref(table->vals->L[c]);
    }
    for (int64_t i = 0; i < ncols; i++) {
        const char *name = use_all_cols ? table->keys->L[i]->s : cols->L[i]->s;
        int idx = tbl_col_idx(table, name);
        if (idx < 0) {
            v_free(new_data);
            if (free_vals_list) v_free(vals_list);
            return v_errf("insert: unknown column '%s'", name);
        }
        V *col = new_data->L[idx];
        V *next = append_cell(col, vals_list->L[i]);
        if (next && next->t == T_ERR) {
            v_free(new_data);
            if (free_vals_list) v_free(vals_list);
            return next;
        }
        if (next != col) {
            v_free(new_data->L[idx]);
            new_data->L[idx] = next;
        }
    }
    if (free_vals_list) v_free(vals_list);
    return v_table_own(v_copy(table->keys), new_data);
}
V *table_sql_join(V *left, V *right, V *on_col) {
    (void)left;
    (void)right;
    (void)on_col;
    return v_err("join not implemented");
}
