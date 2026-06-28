#include "shakti.h"
#include "mat_simd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#if defined(__AVX512F__) && defined(__AVX512BW__)
#include <immintrin.h>
#endif
static inline double sql_to_float(V*v){return !v?0.:v->t==T_INT?(double)v->j:v->t==T_FLOAT?v->f:v->t==T_BOOL?v->b?1.:0.:0.;}
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
static inline double cell_float(V*col,int64_t row){return !col?0.:col->t==T_IVEC?row<col->n?(double)col->J[row]:0.:col->t==T_FVEC?row<col->n?col->F[row]:0.:col->t==T_BVEC?row<col->n?col->B[row]?1.:0.:0.:col->t==T_IMAT&&row<col->n?(double)col->J[mat_idx(col,row,0)]:col->t==T_FMAT&&row<col->n?col->F[mat_idx(col,row,0)]:0.;}
static void cell_key(V*col,int64_t row,char*buf,size_t cap){
 if(!col||!buf||!cap)return;
 buf[0]=0;
 if(col->t==T_STR)snprintf(buf,cap,"%s",col->s);
 else if(col->t==T_LIST&&row<col->n&&col->L[row]){char*t=v_to_str(col->L[row]);snprintf(buf,cap,"%s",t?t:"");free(t);}
 else if(col->t==T_INT)snprintf(buf,cap,"%lld",(long long)col->j);
 else if(col->t==T_FLOAT)snprintf(buf,cap,"%g",col->f);
 else if(col->t==T_IVEC&&row<col->n)snprintf(buf,cap,"%lld",(long long)col->J[row]);
 else if(col->t==T_FVEC&&row<col->n)snprintf(buf,cap,"%g",col->F[row]);
 else if(col->t==T_IMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_FMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_BMAT&&row<col->n){V*rw=v_mat_row(col,row);char*t=v_to_str(rw);snprintf(buf,cap,"%s",t?t:"");free(t);v_free(rw);}
 else if(col->t==T_BOOL&&row<col->n)snprintf(buf,cap,"%s",col->B[row]?"true":"false");}
static V*where_mask(V*tbl,V*where){
 if(!where||where->t==T_NIL){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=1;return all;}
 if(where->t==T_BVEC){P(where->n!=tbl->n,v_err("where: mask length mismatch"))return v_copy(where);}
 if(where->t==T_BOOL){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=where->b?1:0;return all;}
 if(where->t==T_INT){V*all=v_bvec(tbl->n);for(int64_t i=0;i<tbl->n;i++)all->B[i]=where->j?1:0;return all;}
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
    int nc = (int)tbl->keys->n;
    V *new_data = v_list(nc);
#ifdef _OPENMP
    #pragma omp parallel for if(nr >= 1000000)
#endif
    for (int c = 0; c < nc; c++) {
        V *col = tbl->vals->L[c];
        if (col->t == T_IVEC) {
            V *nc2 = v_ivec(count);
            int64_t j = 0;
            const int64_t *src = col->J;
            int64_t *dst = nc2->J;
#if defined(__AVX512F__) && defined(__AVX512BW__)
            int64_t k = 0;
            for (; k + 8 <= nr; k += 8) {
                __mmask8 m = 0;
                for (int b = 0; b < 8; b++) {
                    if (B[k + b]) m |= (1 << b);
                }
                if (m) {
                    __m512i v = _mm512_loadu_si512((void*)&src[k]);
                    _mm512_mask_compressstoreu_epi64(&dst[j], m, v);
                    j += __builtin_popcount(m);
                }
            }
            for (; k < nr; k++) {
                if (B[k]) dst[j++] = src[k];
            }
#else
            for (int64_t k = 0; k < nr; k++) {
                if (B[k]) dst[j++] = src[k];
            }
#endif
            new_data->L[c] = nc2;
        } else if (col->t == T_FVEC) {
            V *nc2 = v_fvec(count);
            int64_t j = 0;
            const double *src = col->F;
            double *dst = nc2->F;
#if defined(__AVX512F__) && defined(__AVX512BW__)
            int64_t k = 0;
            for (; k + 8 <= nr; k += 8) {
                __mmask8 m = 0;
                for (int b = 0; b < 8; b++) {
                    if (B[k + b]) m |= (1 << b);
                }
                if (m) {
                    __m512d v = _mm512_loadu_pd(&src[k]);
                    _mm512_mask_compressstoreu_pd(&dst[j], m, v);
                    j += __builtin_popcount(m);
                }
            }
            for (; k < nr; k++) {
                if (B[k]) dst[j++] = src[k];
            }
#else
            for (int64_t k = 0; k < nr; k++) {
                if (B[k]) dst[j++] = src[k];
            }
#endif
            new_data->L[c] = nc2;
        } else if (col->t == T_BVEC) {
            V *nc2 = v_bvec(count);
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
        } else if (col->t == T_IMAT || col->t == T_FMAT || col->t == T_BMAT) {
            int64_t cols = mat_cols(col);
            if (col->t == T_IMAT) {
                V *nc2 = v_imat(count, cols);
                mat_filter_imat_rows(nc2->J, col->J, B, nr, cols);
                new_data->L[c] = nc2;
            } else if (col->t == T_FMAT) {
                V *nc2 = v_fmat(count, cols);
                mat_filter_fmat_rows(nc2->F, col->F, B, nr, cols);
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
    return v_table(v_copy(tbl->keys), new_data);
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
        data->L[0] = tbl->vals->L[idx];
        tbl->vals->L[idx] = v_ivec(0);
        return v_table(keys, data);
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
        data->L[i] = tbl->vals->L[idx];
        tbl->vals->L[idx] = v_ivec(0);
    }
    return v_table(keys, data);
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
    return n;
}
static V*cell_as_v(V*col,int64_t row){
 P(!col,v_nil())
 P(col->t==T_IVEC&&row<col->n,v_int(col->J[row]))
 P(col->t==T_FVEC&&row<col->n,v_float(col->F[row]))
 P(col->t==T_BVEC&&row<col->n,v_bool(col->B[row]))
 P(col->t==T_IMAT&&row<col->n,v_mat_row(col,row))
 P(col->t==T_FMAT&&row<col->n,v_mat_row(col,row))
 P(col->t==T_BMAT&&row<col->n,v_mat_row(col,row))
 P(col->t==T_LIST&&row<col->n,v_ref(col->L[row]))
 P(col->t==T_STR,v_str(col->s))
 P(col->t==T_INT,v_int(col->j))
 P(col->t==T_FLOAT,v_float(col->f))
 return v_nil();}
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
        return (a->f > b->f) - (a->f < b->f);
    }
    if (a->t == T_BOOL && b->t == T_BOOL) {
        return (a->b > b->b) - (a->b < b->b);
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
static void composite_key(V *tbl, const int *by_idx, int nby, int64_t row, char *buf, size_t cap) {
    size_t pos = 0;
    buf[0] = 0;
    for (int i = 0; i < nby; i++) {
        char part[256];
        cell_key(tbl_col(tbl, by_idx[i]), row, part, sizeof part);
        size_t plen = strlen(part);
        if (pos + plen + 2 >= cap) {
            return;
        }
        if (i > 0) {
            buf[pos++] = '\1';
        }
        memcpy(buf + pos, part, plen);
        pos += plen;
    }
    buf[pos] = 0;
}
static inline uint32_t sql_hash_key(const char*s){uint32_t h=2166136261u;const char*p=s;W(p&&*p,h^=(unsigned char)*p,h*=16777619u,p++)return h?h:1u;}
#define GH_EMPTY (-1)
typedef struct {
    char key[512];
    int nby;
    int64_t nrows;
    V **by_cell;
    V *name_val[64];
    double sum[64];
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
static inline uint32_t gh_hash_key(const char*key){return sql_hash_key(key);}
static void gh_init(GhTab *t, int cap) {
    if (cap < 16) {
        cap = 16;
    }
    int n = 1;
    W(n < cap,n *= 2)
    t->cap = n;
    t->nslots = 0;
    t->slots = calloc((size_t)n, sizeof(GhSlot));
    t->map = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        t->map[i] = GH_EMPTY;
    }
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
static void gh_resize(GhTab *t);
static int gh_find_or_insert(GhTab *t, const char *key, V *tbl, const int *by_idx, int nby,
                              int64_t row, ColSpec *specs, int nspecs) {
    if (t->nslots * 10 > t->cap * 7) {
        gh_resize(t);
    }
    uint32_t h = gh_hash_key(key);
    int cap = t->cap;
    int i = (int)(h & (uint32_t)(cap - 1));
    for (;;) {
        int si = t->map[i];
        if (si == GH_EMPTY) {
            si = t->nslots++;
            GhSlot *s = &t->slots[si];
            memset(s, 0, sizeof(*s));
            snprintf(s->key, sizeof s->key, "%s", key);
            s->nby = nby;
            s->by_cell = calloc((size_t)nby, sizeof(V *));
            for (int b = 0; b < nby; b++) {
                s->by_cell[b] = cell_as_v(tbl_col(tbl, by_idx[b]), row);
            }
            for (int sp = 0; sp < nspecs; sp++) {
                if (specs[sp].kind == COL_NAME) {
                    int idx = tbl_col_idx(tbl, specs[sp].name);
                    if (idx < 0) {
                        return -1;
                    }
                    s->name_val[sp] = cell_as_v(tbl_col(tbl, idx), row);
                } else if (specs[sp].kind != COL_COUNT) {
                    int idx = specs[sp].src[0] ? tbl_col_idx(tbl, specs[sp].src) : -1;
                    V *col = idx >= 0 ? tbl_col(tbl, idx) : NULL;
                    double x = cell_float(col, row);
                    s->sum[sp] = x;
                    s->minv[sp] = s->maxv[sp] = x;
                    s->have_mm[sp] = 1;
                }
            }
            s->nrows = 1;
            t->map[i] = si;
            return si;
        }
        if (!strcmp(t->slots[si].key, key)) {
            GhSlot *s = &t->slots[si];
            s->nrows++;
            for (int sp = 0; sp < nspecs; sp++) {
                if (specs[sp].kind == COL_COUNT) {
                    continue;
                }
                int idx = specs[sp].src[0] ? tbl_col_idx(tbl, specs[sp].src) :
                          specs[sp].kind == COL_NAME ? tbl_col_idx(tbl, specs[sp].name) : -1;
                V *col = idx >= 0 ? tbl_col(tbl, idx) : NULL;
                double x = cell_float(col, row);
                if (specs[sp].kind == COL_NAME) {
                    continue;
                }
                s->sum[sp] += x;
                if (!s->have_mm[sp]) {
                    s->minv[sp] = s->maxv[sp] = x;
                    s->have_mm[sp] = 1;
                } else {
                    if (x < s->minv[sp]) {
                        s->minv[sp] = x;
                    }
                    if (x > s->maxv[sp]) {
                        s->maxv[sp] = x;
                    }
                }
            }
            return si;
        }
        i = (i + 1) & (cap - 1);
    }
}
static void gh_resize(GhTab *t) {
    GhTab old = *t;
    gh_init(t, old.cap * 2);
    for (int i = 0; i < old.nslots; i++) {
        GhSlot *s = &old.slots[i];
        uint32_t h = gh_hash_key(s->key);
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
}
static int gh_sort_nby;
static int gh_sort_cmp(const void *a, const void *b) {
    const GhSlot *sa = (const GhSlot *)a;
    const GhSlot *sb = (const GhSlot *)b;
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
static V *tbl_group_select(V *tbl, ColSpec *specs, int nspecs, V *by) {
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
    GhTab tab;
    // Fast path: nby == 1 and group column is T_IVEC
    V *bcol = tbl_col(tbl, by_idx[0]);
    if (nby == 1 && bcol && bcol->t == T_IVEC) {
        int64_t max_val = -1;
        int64_t min_val = INT64_MAX;
        for (int64_t r = 0; r < tbl->n; r++) {
            if (bcol->J[r] > max_val) max_val = bcol->J[r];
            if (bcol->J[r] < min_val) min_val = bcol->J[r];
        }
        if (min_val >= 0 && max_val < 1000000) {
            int slots_cap = (int)max_val + 1;
            int *s_nrows = calloc((size_t)slots_cap, sizeof(int));
            int *s_name_row = malloc((size_t)slots_cap * sizeof(int));
            for(int i=0; i<slots_cap; i++) s_name_row[i] = -1;
            int name_idx[64];
            int agg_sp[64];
            int col_t[64];
            V *sp_col[64];
            int n_agg = 0;
            for (int sp = 0; sp < nspecs; sp++) {
                int idx = specs[sp].src[0] ? tbl_col_idx(tbl, specs[sp].src) : -1;
                sp_col[sp] = idx >= 0 ? tbl_col(tbl, idx) : NULL;
                name_idx[sp] = specs[sp].kind == COL_NAME ? tbl_col_idx(tbl, specs[sp].name) : -1;
                col_t[sp] = sp_col[sp] ? sp_col[sp]->t : 0;
                if (specs[sp].kind != COL_COUNT && specs[sp].kind != COL_NAME) {
                    agg_sp[n_agg++] = sp;
                }
            }
            double *s_sum[64] = {0};
            double *s_minv[64] = {0};
            double *s_maxv[64] = {0};
            int *s_have_mm[64] = {0};
            for (int k = 0; k < n_agg; k++) {
                s_sum[k] = calloc((size_t)slots_cap, sizeof(double));
                s_minv[k] = malloc((size_t)slots_cap * sizeof(double));
                s_maxv[k] = malloc((size_t)slots_cap * sizeof(double));
                s_have_mm[k] = calloc((size_t)slots_cap, sizeof(int));
            }
            int64_t nr = tbl->n;
            i(nr, {
                int64_t key_val = bcol->J[i];
                if (s_nrows[key_val] == 0) {
                    s_name_row[key_val] = (int)i;
                }
                s_nrows[key_val]++;
                for (int k = 0; k < n_agg; k++) {
                    int sp = agg_sp[k];
                    V *col = sp_col[sp];
                    double x = 0;
                    if (col) {
                        int ct = col_t[sp];
                        if (ct == T_IVEC) x = (double)col->J[i];
                        else if (ct == T_FVEC) x = col->F[i];
                        else x = cell_float(col, i);
                    }
                    s_sum[k][key_val] += x;
                    if (!s_have_mm[k][key_val]) {
                        s_minv[k][key_val] = s_maxv[k][key_val] = x;
                        s_have_mm[k][key_val] = 1;
                    } else {
                        if (x < s_minv[k][key_val]) s_minv[k][key_val] = x;
                        if (x > s_maxv[k][key_val]) s_maxv[k][key_val] = x;
                    }
                }
            })
            int nactive = 0;
            for (int i = 0; i < slots_cap; i++) {
                if (s_nrows[i] > 0) nactive++;
            }
            tab.nslots = nactive;
            tab.slots = malloc((size_t)nactive * sizeof(GhSlot));
            int aidx = 0;
            for (int i = 0; i < slots_cap; i++) {
                if (s_nrows[i] > 0) {
                    GhSlot *s = &tab.slots[aidx++];
                    memset(s, 0, sizeof(GhSlot));
                    s->nrows = s_nrows[i];
                    s->nby = 1;
                    s->by_cell = calloc(1, sizeof(V*));
                    s->by_cell[0] = v_int(i);
                    for (int sp = 0; sp < nspecs; sp++) {
                        if (name_idx[sp] >= 0) {
                            s->name_val[sp] = cell_as_v(tbl_col(tbl, name_idx[sp]), s_name_row[i]);
                        }
                    }
                    for (int k = 0; k < n_agg; k++) {
                        int sp = agg_sp[k];
                        s->sum[sp] = s_sum[k][i];
                        s->minv[sp] = s_minv[k][i];
                        s->maxv[sp] = s_maxv[k][i];
                        s->have_mm[sp] = s_have_mm[k][i];
                    }
                }
            }
            free(s_nrows);
            free(s_name_row);
            for (int k = 0; k < n_agg; k++) {
                free(s_sum[k]);
                free(s_minv[k]);
                free(s_maxv[k]);
                free(s_have_mm[k]);
            }
            tab.map = NULL; 
            /* Already sorted since we iterated 0..slots_cap-1 */
            goto build_table;
        }
    }
    gh_init(&tab, 256);
    char key[512];
    for (int64_t row = 0; row < tbl->n; row++) {
        composite_key(tbl, by_idx, nby, row, key, sizeof key);
        if (gh_find_or_insert(&tab, key, tbl, by_idx, nby, row, specs, nspecs) < 0) {
            gh_free(&tab);
            return v_errf("select: unknown column in projection");
        }
    }
    gh_sort_nby = nby;
    qsort(tab.slots, (size_t)tab.nslots, sizeof(GhSlot), gh_sort_cmp);
build_table:
    int include_by[32];
    for (int i = 0; i < nby; i++) {
        include_by[i] = 1;
    }
    for (int s = 0; s < nspecs; s++) {
        if (specs[s].kind != COL_NAME) {
            continue;
        }
        for (int i = 0; i < nby; i++) {
            if (!strcmp(specs[s].name, by_names[i])) {
                include_by[i] = 0;
            }
        }
    }
    int out_cols = 0;
    for (int i = 0; i < nby; i++) {
        if (include_by[i]) {
            out_cols++;
        }
    }
    out_cols += nspecs;
    if (nspecs == 0) {
        out_cols = nby;
    }
    V *keys = v_list(out_cols);
    V *data = v_list(out_cols);
    int c = 0;
    if (nspecs == 0) {
        for (int i = 0; i < nby; i++) {
            keys->L[c] = v_ref(tbl->keys->L[by_idx[i]]);
            data->L[c] = v_list(tab.nslots);
            c++;
        }
    } else {
        for (int i = 0; i < nby; i++) {
            if (include_by[i]) {
                keys->L[c] = v_ref(tbl->keys->L[by_idx[i]]);
                data->L[c] = v_list(tab.nslots);
                c++;
            }
        }
        for (int s = 0; s < nspecs; s++) {
            keys->L[c] = v_str(specs[s].kind == COL_COUNT && specs[s].src[0] == 0 ? "count" : specs[s].name);
            data->L[c] = v_list(tab.nslots);
            c++;
        }
    }
    for (int g = 0; g < tab.nslots; g++) {
        GhSlot *slot = &tab.slots[g];
        int col = 0;
        if (nspecs == 0) {
            for (int i = 0; i < nby; i++) {
                data->L[col]->L[g] = slot->by_cell[i] ? v_ref(slot->by_cell[i]) : v_nil();
                col++;
            }
        } else {
            for (int i = 0; i < nby; i++) {
                if (include_by[i]) {
                    data->L[col]->L[g] = slot->by_cell[i] ? v_ref(slot->by_cell[i]) : v_nil();
                    col++;
                }
            }
            for (int s = 0; s < nspecs; s++) {
                data->L[col]->L[g] = slot_agg_value(slot, &specs[s], s);
                col++;
            }
        }
    }
    if (tab.map) gh_free(&tab);
    else {
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
    return v_table(keys, data);
}
V *table_sql_select(V *from, V *cols, V *by, V *where) {
    if (!from || from->t == T_ERR) {
        return from ? v_ref(from) : v_err("select: missing table");
    }
    if (from->t != T_TABLE) {
        return v_err("select: need table");
    }
    V *mask = where_mask(from, where);
    if (mask->t == T_ERR) {
        return mask;
    }
    V *filtered = tbl_filter_mask(from, mask);
    v_free(mask);
    if (filtered->t == T_ERR) {
        return filtered;
    }
    const char *by_names[32];
    int nby = parse_by_names(by, by_names, 32);
    if (nby < 0) {
        v_free(filtered);
        return v_err("select: bad by column list");
    }
    ColSpec specs[64];
    int nspecs = parse_col_specs(cols, specs, 64);
    if (nspecs < 0) {
        v_free(filtered);
        return v_err("select: bad column list");
    }
    V *result;
    if (nby > 0) {
        result = tbl_group_select(filtered, specs, nspecs, by);
    } else if (nspecs == 0) {
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
            v_free(filtered);
            return v_err("select: aggregates require by");
        }
        V *names = v_list(nspecs);
        for (int i = 0; i < nspecs; i++) {
            names->L[i] = v_str(specs[i].name);
        }
        result = tbl_project_names(filtered, names);
        v_free(names);
    }
    v_free(filtered);
    return result;
}
static V *merge_update_col(V *old_col, V *new_col, V *mask) {
    int64_t n = mask->n;
    if (new_col->t == T_INT || new_col->t == T_FLOAT || new_col->t == T_BOOL || new_col->t == T_STR) {
        if (old_col->t == T_IVEC) {
            V *out = v_copy(old_col);
            for (int64_t i = 0; i < n && i < out->n; i++) {
                if (mask->B[i]) {
                    out->J[i] = new_col->t == T_INT ? new_col->j :
                                new_col->t == T_BOOL ? new_col->b :
                                (int64_t)sql_to_float(new_col);
                }
            }
            return out;
        }
        if (old_col->t == T_FVEC) {
            V *out = v_copy(old_col);
            double x = new_col->t == T_FLOAT ? new_col->f :
                       new_col->t == T_INT ? (double)new_col->j :
                       new_col->t == T_BOOL ? (double)new_col->b : 0.0;
            for (int64_t i = 0; i < n && i < out->n; i++) {
                if (mask->B[i]) {
                    out->F[i] = x;
                }
            }
            return out;
        }
        if (old_col->t == T_LIST && new_col->t == T_STR) {
            V *out = v_copy(old_col);
            for (int64_t i = 0; i < n && i < out->n; i++) {
                if (mask->B[i]) {
                    v_free(out->L[i]);
                    out->L[i] = v_str(new_col->s);
                }
            }
            return out;
        }
    }
    if (new_col->n != old_col->n) {
        return v_err("update: column length mismatch");
    }
    if (old_col->t == T_IVEC && new_col->t == T_IVEC) {
        V *out = v_copy(old_col);
        for (int64_t i = 0; i < n && i < out->n; i++) {
            if (mask->B[i]) {
                out->J[i] = new_col->J[i];
            }
        }
        return out;
    }
    if ((old_col->t == T_IVEC || old_col->t == T_FVEC) && (new_col->t == T_IVEC || new_col->t == T_FVEC)) {
        V *out = v_copy(old_col);
        if (out->t == T_IVEC) {
            for (int64_t i = 0; i < n && i < out->n; i++) {
                if (mask->B[i]) {
                    out->J[i] = (int64_t)cell_float(new_col, i);
                }
            }
        } else {
            for (int64_t i = 0; i < n && i < out->n; i++) {
                if (mask->B[i]) {
                    out->F[i] = cell_float(new_col, i);
                }
            }
        }
        return out;
    }
    if (old_col->t == T_FVEC && new_col->t == T_FVEC) {
        V *out = v_copy(old_col);
        for (int64_t i = 0; i < n && i < out->n; i++) {
            if (mask->B[i]) {
                out->F[i] = new_col->F[i];
            }
        }
        return out;
    }
    if (old_col->t == T_LIST && new_col->t == T_LIST) {
        V *out = v_copy(old_col);
        for (int64_t i = 0; i < n && i < out->n; i++) {
            if (mask->B[i]) {
                v_free(out->L[i]);
                out->L[i] = v_ref(new_col->L[i]);
            }
        }
        return out;
    }
    return v_copy(old_col);
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
        V *merged = merge_update_col(from->vals->L[idx], new_col, mask);
        if (merged->t == T_ERR) {
            v_free(mask);
            v_free(new_data);
            return merged;
        }
        v_free(new_data->L[idx]);
        new_data->L[idx] = merged;
    }
    v_free(mask);
    return v_table(v_copy(from->keys), new_data);
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
        } else if (col->t == T_IMAT) {
            empty_data->L[c] = v_imat(0, mat_cols(col));
        } else if (col->t == T_FMAT) {
            empty_data->L[c] = v_fmat(0, mat_cols(col));
        } else if (col->t == T_BMAT) {
            empty_data->L[c] = v_bmat(0, mat_cols(col));
        } else if (col->t == T_LIST) {
            empty_data->L[c] = v_list(0);
        } else {
            empty_data->L[c] = v_ref(col);
        }
    }
    return v_table(v_copy(from->keys), empty_data);
}
static inline V*empty_column_like(V*sample){
    if(!sample)return v_list(0);
    if(sample->t==T_IVEC)return v_ivec(0);
    if(sample->t==T_FVEC)return v_fvec(0);
    if(sample->t==T_BVEC)return v_bvec(0);
    if(sample->t==T_IMAT)return v_imat(0, mat_cols(sample));
    if(sample->t==T_FMAT)return v_fmat(0, mat_cols(sample));
    if(sample->t==T_BMAT)return v_bmat(0, mat_cols(sample));
    return v_list(0);}
static int mat_append_row(V *out, V *col, V *cell) {
    int64_t cols = mat_cols(col);
    if (col->t == T_IMAT) {
        if (cell->t == T_IVEC && cell->n == cols) {
            memcpy(out->J + mat_idx(out, col->n, 0), cell->J, (size_t)cols * 8);
            return 1;
        }
        if (cell->t == T_LIST && cell->n == cols) {
            for (int64_t j = 0; j < cols; j++) out->J[mat_idx(out, col->n, j)] = cell->L[j]->j;
            return 1;
        }
    } else if (col->t == T_FMAT) {
        if (cell->t == T_FVEC && cell->n == cols) {
            memcpy(out->F + mat_idx(out, col->n, 0), cell->F, (size_t)cols * 8);
            return 1;
        }
        if (cell->t == T_LIST && cell->n == cols) {
            for (int64_t j = 0; j < cols; j++)
                out->F[mat_idx(out, col->n, j)] = cell->L[j]->t == T_INT ? (double)cell->L[j]->j : cell->L[j]->f;
            return 1;
        }
    } else if (col->t == T_BMAT) {
        if (cell->t == T_BVEC && cell->n == cols) {
            memcpy(out->B + mat_idx(out, col->n, 0), cell->B, (size_t)cols);
            return 1;
        }
        if (cell->t == T_LIST && cell->n == cols) {
            for (int64_t j = 0; j < cols; j++) out->B[mat_idx(out, col->n, j)] = cell->L[j]->b ? 1 : 0;
            return 1;
        }
    }
    return 0;
}
static V *append_cell(V *col, V *cell) {
    if (col->t == T_IVEC) {
        int64_t val = cell->t == T_INT ? cell->j : (int64_t)cell_float(cell, 0);
        if (col->rc == 1) {
            int cap = col->_ht_cap >= (int)col->n ? col->_ht_cap : (int)col->n;
            if (col->n >= cap) {
                cap = cap ? cap * 2 : 8;
                col->J = realloc(col->J, (size_t)cap * sizeof(int64_t));
                col->_ht_cap = cap;
            }
            col->J[col->n++] = val;
            return col;
        }
        V *out = v_ivec(col->n + 1);
        memcpy(out->J, col->J, (size_t)col->n * sizeof(int64_t));
        out->J[col->n] = val;
        return out;
    }
    if (col->t == T_FVEC) {
        V *out = v_fvec(col->n + 1);
        memcpy(out->F, col->F, (size_t)col->n * sizeof(double));
        out->F[col->n] = cell_float(cell, 0);
        return out;
    }
    if (col->t == T_BVEC) {
        V *out = v_bvec(col->n + 1);
        memcpy(out->B, col->B, (size_t)col->n);
        out->B[col->n] = (cell->t == T_BOOL && cell->b) || (cell->t == T_INT && cell->j);
        return out;
    }
    if (col->t == T_IMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_imat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->J, col->J, (size_t)col->n * cols * 8);
        if (!mat_append_row(out, col, cell)) return col;
        return out;
    }
    if (col->t == T_FMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_fmat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->F, col->F, (size_t)col->n * cols * 8);
        if (!mat_append_row(out, col, cell)) return col;
        return out;
    }
    if (col->t == T_BMAT) {
        int64_t cols = mat_cols(col);
        V *out = v_bmat(col->n + 1, cols);
        if (col->n > 0) memcpy(out->B, col->B, (size_t)col->n * cols);
        if (!mat_append_row(out, col, cell)) return col;
        return out;
    }
    if (col->t == T_LIST) {
        if (col->rc == 1) {
            if (col->n >= col->_ht_cap) {
                int cap = col->_ht_cap ? col->_ht_cap * 2 : 8;
                col->L = realloc(col->L, (size_t)cap * sizeof(V*));
                col->_ht_cap = cap;
            }
            if (cell->t == T_STR) {
                col->L[col->n++] = v_str(cell->s);
            } else {
                col->L[col->n++] = v_ref(cell);
            }
            return col;
        }
        V *out = v_list(col->n + 1);
        for (int64_t i = 0; i < col->n; i++) {
            out->L[i] = v_ref(col->L[i]);
        }
        if (cell->t == T_STR) {
            out->L[col->n] = v_str(cell->s);
        } else {
            out->L[col->n] = v_ref(cell);
        }
        return out;
    }
    return col;
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
            v_list_append(keys, v_ref(cols->keys->L[i]));
            V *def = cols->vals->L[i];
            if (!def || def->t == T_NIL) {
                v_list_append(data, v_ivec(0));
            } else if (def->t == T_INT) {
                v_list_append(data, v_ivec(0));
            } else if (def->t == T_FLOAT) {
                v_list_append(data, v_fvec(0));
            } else if (def->t == T_STR) {
                v_list_append(data, v_list(0));
            } else if (def->t == T_LIST) {
                v_list_append(data, v_list(0));
            } else {
                v_list_append(data, empty_column_like(def));
            }
        }
        return v_table(keys, data);
    }
    for (int64_t i = 0; i < cols->n; i++) {
        if (cols->L[i]->t != T_STR) {
            v_free(keys);
            v_free(data);
            return v_err("create table: column names must be strings");
        }
        v_list_append(keys, v_ref(cols->L[i]));
        v_list_append(data, v_ivec(0));
    }
    return v_table(keys, data);
}
V *table_sql_insert(V *table, V *cols, V *vals) {
    if (!table || table->t == T_ERR) {
        return table ? v_ref(table) : v_err("insert: missing table");
    }
    if (table->t != T_TABLE) {
        return v_err("insert: need table");
    }
    if (!vals || vals->t != T_LIST) {
        return v_err("insert: need values list");
    }
    int use_all_cols = !cols || cols->t == T_NIL || (cols->t == T_LIST && cols->n == 0);
    int64_t ncols = use_all_cols ? table->keys->n : cols->n;
    if (vals->n != ncols) {
        return v_err("insert: column/value count mismatch");
    }
    if (table->rc == 1) {
        for (int64_t i = 0; i < ncols; i++) {
            const char *name = use_all_cols ? table->keys->L[i]->s : cols->L[i]->s;
            int idx = tbl_col_idx(table, name);
            if (idx < 0) {
                return v_errf("insert: unknown column '%s'", name);
            }
            V *col = table->vals->L[idx];
            V *next = append_cell(col, vals->L[i]);
            if (next != col) {
                v_free(table->vals->L[idx]);
                table->vals->L[idx] = next;
            }
        }
        if (table->vals->n > 0 && table->vals->L[0]) {
            table->n = table->vals->L[0]->n;
        }
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
            return v_errf("insert: unknown column '%s'", name);
        }
        V *col = new_data->L[idx];
        V *next = append_cell(col, vals->L[i]);
        if (next != col) {
            v_free(new_data->L[idx]);
            new_data->L[idx] = next;
        }
    }
    return v_table(v_copy(table->keys), new_data);
}
V *table_sql_join(V *left, V *right, V *on_col) {
    (void)left;
    (void)right;
    (void)on_col;
    return v_err("join not implemented");
}
