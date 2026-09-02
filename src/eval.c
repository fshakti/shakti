/* shakti/src/eval.c — tree-walk evaluator */
#include "shakti_internal.h"
#include "input.h"
#ifdef SHAKTI_HAVE_IEFS
#include "iefs_format.h"
#include "iefs_io.h"
#include "iefs_map.h"
#endif
#include <stdlib.h>

static int g_call_depth;
static int g_call_depth_limit = -1;

/* Raise g_error and free assign temps when materializing a mapped column fails. */
static V *assign_map_oom(V *a, V *b, V *c, V *d) {
    g_error = 1;
    if (g_error_val) { v_free(g_error_val); g_error_val = NULL; }
    g_error_val = v_err("iefs: out of memory materializing mapped column");
    if (a) v_free(a);
    if (b) v_free(b);
    if (c) v_free(c);
    if (d) v_free(d);
    return v_nil();
}
static int fstr_fmt_valid(const char *spec) {
    const char *p = spec;
    if (!p || !*p || strlen(spec) > 32) return 0;
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    switch (*p) {
    case 'e': case 'E': case 'f': case 'F':
    case 'g': case 'G': case 'a': case 'A':
        p++;
        break;
    default:
        return 0;
    }
    return *p == '\0';
}
static V *eval_slice(V *obj, V *start_v, V *stop_v, V *step_v) {
    int64_t len = 0;
    if(obj->t==T_STR) len = strlen(obj->s);
    else if(obj->t==T_IVEC||obj->t==T_FVEC||obj->t==T_BVEC||obj->t==T_CVEC||obj->t==T_LIST) len = obj->n;
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
    if(obj->t==T_BVEC) {
        V *r=v_bvec(count); int64_t j=0;
        if(step>0) { for(int64_t i=start;i<stop;i+=step) r->B[j++]=obj->B[i]; }
        else { for(int64_t i=start;i>stop;i+=step) r->B[j++]=obj->B[i]; }
        return r;
    }
    if(obj->t==T_CVEC) {
        V *r=v_cvec(count); int64_t j=0;
        if(step>0) { for(int64_t i=start;i<stop;i+=step) r->B[j++]=obj->B[i]; }
        else { for(int64_t i=start;i>stop;i+=step) r->B[j++]=obj->B[i]; }
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
            : obj->t == T_CMAT ? (V *)v_cmat(count, cols)
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
/* INSERT VALUES must stay a heterogeneous T_LIST. Generic N_LIST eval promotes
 * all-int / all-num literals to ivec/fvec; table_sql_insert then reads vals->L[i]
 * and segfaults (union alias with J/F). */
static V *eval_insert_values(Node *n, Env *e) {
    P(!n || n->type != N_LIST,v_err("insert: need values list"))
    V *r = v_list(n->nch);
    for (int i = 0; i < n->nch; i++) {
        V *el = eval(n->ch[i], e);
        if (g_error || !el || el->t == T_ERR) {
            r->n = i;
            v_free(r);
            return el ? el : v_err("insert: value error");
        }
        r->L[i] = el;
    }
    return r;
}
static V *eval_update_cols(Node *n, V *tbl, Env *e) {
    P(!n || n->type == N_NONE,v_dict_empty())
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
            v_list_append_own(keys, v_str(item->ch[0]->sval));
            v_list_append_own(vals, eval(item->ch[1], inner));
        } else {
            env_free(inner);
            v_free(keys);
            v_free(vals);
            return v_err("update: expected col: expr");
        }
    })
    env_free(inner);
    return v_dict_own(keys, vals);
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
        v_list_append_own(keys, v_str(col->sval));
        if(col->nch > 0 && col->ch[0]->type != N_NONE)
            v_list_append_own(vals, eval(col->ch[0], e));
        else
            v_list_append_own(vals, v_nil());
    })
    return v_dict_own(keys, vals);
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
    v_list_append_own(acc, v_str(s));
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

static int each_is_seq(int t) {
    return t == T_LIST || t == T_IVEC || t == T_FVEC || t == T_BVEC || t == T_CVEC || t == T_STR;
}
static int each_is_container(V *v) {
    if (!v) return 0;
    return each_is_seq(v->t) || is_mat_t(v->t) || v->t == T_DICT || v->t == T_TABLE;
}
static V *each_invoke(V *fn, V **args, int nargs, Env *e) {
    V *al = v_list(nargs);
    for (int i = 0; i < nargs; i++) al->L[i] = v_ref(args[i]);
    V *r = builtin_call("__invoke__", (V*[]){fn, al}, 2, NULL, NULL, 0, e);
    v_free(al);
    return r;
}
static V *each_get_seq(V *v, int64_t i) {
    if (v->t == T_IVEC) return v_int(v->J[i]);
    if (v->t == T_FVEC) return v_float(v->F[i]);
    if (v->t == T_BVEC) return v_bool(v->B[i] != 0);
    if (v->t == T_CVEC) return v_char(v->B[i]);
    if (v->t == T_LIST) return v_ref(v->L[i]);
    if (v->t == T_STR) {
        char buf[2] = {v->s[i], 0};
        return v_str(buf);
    }
    return v_err("each: bad sequence");
}
static int64_t each_seq_len(V *v) {
    if (v->t == T_STR) return (int64_t)strlen(v->s);
    return v->n;
}
static V *each_get_mat(V *m, int64_t r, int64_t c) {
    if (m->t == T_IMAT) return v_int(m->J[mat_idx(m, r, c)]);
    if (m->t == T_FMAT) return v_float(m->F[mat_idx(m, r, c)]);
    if (m->t == T_BMAT) return v_bool(m->B[mat_idx(m, r, c)] != 0);
    if (m->t == T_CMAT) return v_char(m->B[mat_idx(m, r, c)]);
    return v_err("each: bad matrix");
}
static V *each_pack_seq(V **items, int64_t n, int prefer_str) {
    if (n == 0) return prefer_str ? v_str("") : v_list(0);
    int all_int = 1, all_u8 = 1, all_num = 1, all_bool = 1, all_char = prefer_str;
    for (int64_t i = 0; i < n; i++) {
        V *x = items[i];
        if (x->t != T_INT && x->t != T_CHAR) all_int = 0;
        if (x->t != T_CHAR) all_u8 = 0;
        if (x->t != T_INT && x->t != T_FLOAT && x->t != T_CHAR) all_num = 0;
        if (x->t != T_BOOL) all_bool = 0;
        if (!(x->t == T_STR && x->s && strlen(x->s) == 1)) all_char = 0;
    }
    if (prefer_str && all_char) {
        char *s = malloc((size_t)n + 1);
        if (!s) {
            for (int64_t i = 0; i < n; i++) v_free(items[i]);
            return v_err("out of memory");
        }
        for (int64_t i = 0; i < n; i++) {
            s[i] = items[i]->s[0];
            v_free(items[i]);
        }
        s[n] = 0;
        return v_str_take(s);
    }
    if (all_u8) {
        V *r = v_cvec(n);
        for (int64_t i = 0; i < n; i++) { r->B[i] = (unsigned char)items[i]->j; v_free(items[i]); }
        return r;
    }
    if (all_int) {
        V *r = v_ivec(n);
        for (int64_t i = 0; i < n; i++) { r->J[i] = items[i]->j; v_free(items[i]); }
        return r;
    }
    if (all_num) {
        V *r = v_fvec(n);
        for (int64_t i = 0; i < n; i++) {
            r->F[i] = items[i]->t == T_FLOAT ? items[i]->f : (double)items[i]->j;
            v_free(items[i]);
        }
        return r;
    }
    if (all_bool) {
        V *r = v_bvec(n);
        for (int64_t i = 0; i < n; i++) { r->B[i] = items[i]->b ? 1 : 0; v_free(items[i]); }
        return r;
    }
    V *r = v_list(n);
    for (int64_t i = 0; i < n; i++) r->L[i] = items[i];
    return r;
}
static V *each_empty_seq_like(V *v) {
    if (v->t == T_STR) return v_str("");
    if (v->t == T_IVEC) return v_ivec(0);
    if (v->t == T_FVEC) return v_fvec(0);
    if (v->t == T_BVEC) return v_bvec(0);
    if (v->t == T_CVEC) return v_cvec(0);
    return v_list(0);
}
static V *each_empty_mat_like(V *v) {
    int64_t cols = mat_cols(v);
    if (v->t == T_FMAT) return v_fmat(v->n, cols);
    if (v->t == T_BMAT) return v_bmat(v->n, cols);
    if (v->t == T_CMAT) return v_cmat(v->n, cols);
    return v_imat(v->n, cols);
}
static V *each_pack_mat(V **items, int64_t rows, int64_t cols) {
    if (!items || rows <= 0 || cols <= 0) return v_imat(rows > 0 ? rows : 0, cols > 0 ? cols : 0);
    if (cols > 0 && rows > INT64_MAX / cols) return v_err("each: matrix too large");
    int64_t n = rows * cols;
    if (n <= 0) return v_imat(rows, cols);
    int all_int = 1, all_u8 = 1, all_num = 1, all_bool = 1;
    for (int64_t i = 0; i < n; i++) {
        if (!items[i]) {
            for (int64_t j = 0; j < n; j++) {
                if (items[j]) { v_free(items[j]); items[j] = NULL; }
            }
            return v_err("each: null matrix element");
        }
        if (items[i]->t != T_INT && items[i]->t != T_CHAR) all_int = 0;
        if (items[i]->t != T_CHAR) all_u8 = 0;
        if (items[i]->t != T_INT && items[i]->t != T_FLOAT && items[i]->t != T_CHAR) all_num = 0;
        if (items[i]->t != T_BOOL) all_bool = 0;
    }
    if (all_u8) {
        V *r = v_cmat(rows, cols);
        for (int64_t i = 0; i < n; i++) { r->B[i] = (unsigned char)items[i]->j; v_free(items[i]); }
        return r;
    }
    if (all_int) {
        V *r = v_imat(rows, cols);
        for (int64_t i = 0; i < n; i++) { r->J[i] = items[i]->j; v_free(items[i]); }
        return r;
    }
    if (all_num) {
        V *r = v_fmat(rows, cols);
        for (int64_t i = 0; i < n; i++) {
            r->F[i] = items[i]->t == T_FLOAT ? items[i]->f : (double)items[i]->j;
            v_free(items[i]);
        }
        return r;
    }
    if (all_bool) {
        V *r = v_bmat(rows, cols);
        for (int64_t i = 0; i < n; i++) { r->B[i] = items[i]->b ? 1 : 0; v_free(items[i]); }
        return r;
    }
    /* Heterogeneous matrix → list of row lists, preserving shape. */
    V *rows_l = v_list(rows);
    for (int64_t r = 0; r < rows; r++) {
        V *row = v_list(cols);
        for (int64_t c = 0; c < cols; c++)
            row->L[c] = items[r * cols + c];
        rows_l->L[r] = row;
    }
    return rows_l;
}
static void each_free_items(V **items, int64_t n) {
    if (!items) return;
    for (int64_t i = 0; i < n; i++) if (items[i]) v_free(items[i]);
    free(items);
}
static int each_dict_key_eq(V *a, V *b) {
    V *c = vec_cmp(a, b, OP_EQ);
    int ok = c && c->t == T_BOOL && c->b;
    v_free(c);
    return ok;
}
static int each_dict_find_key(V *d, V *key) {
    for (int64_t i = 0; i < d->n; i++)
        if (each_dict_key_eq(d->keys->L[i], key)) return (int)i;
    return -1;
}
static int each_table_find_col(V *t, const char *name) {
    for (int64_t i = 0; i < t->keys->n; i++)
        if (t->keys->L[i]->t == T_STR && !strcmp(t->keys->L[i]->s, name)) return (int)i;
    return -1;
}
static V *each_col_get(V *col, int64_t i) {
    if (col->t == T_IVEC) return v_int(col->J[i]);
    if (col->t == T_FVEC) return v_float(col->F[i]);
    if (col->t == T_BVEC) return v_bool(col->B[i] != 0);
    if (col->t == T_CVEC) return v_char(col->B[i]);
    if (col->t == T_LIST) return v_ref(col->L[i]);
    return v_err("each: unsupported table column type");
}
static V *each_unary(V *fn, V *xs, Env *e) {
    if (xs->t == T_INPUT)
        return v_err("each: input streams are not supported");
    if (!each_is_container(xs))
        return each_invoke(fn, (V*[]){xs}, 1, e);
    if (each_is_seq(xs->t)) {
        int64_t n = each_seq_len(xs);
        if (n == 0) return each_empty_seq_like(xs);
        V **items = calloc((size_t)n, sizeof(V*));
        if (!items) return v_err("out of memory");
        for (int64_t i = 0; i < n; i++) {
            V *el = each_get_seq(xs, i);
            if (el->t == T_ERR) { each_free_items(items, i); return el; }
            V *r = each_invoke(fn, (V*[]){el}, 1, e);
            v_free(el);
            if (!r || r->t == T_ERR || g_error) {
                each_free_items(items, i);
                return r ? r : v_nil();
            }
            items[i] = r;
        }
        V *out = each_pack_seq(items, n, xs->t == T_STR);
        free(items);
        return out;
    }
    if (is_mat_t(xs->t)) {
        int64_t rows = xs->n, cols = mat_cols(xs), n = rows * cols;
        if (rows <= 0 || cols <= 0 || n <= 0) return each_empty_mat_like(xs);
        V **items = calloc((size_t)n, sizeof(V*));
        if (!items) return v_err("out of memory");
        for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < cols; c++) {
            int64_t i = r * cols + c;
            V *el = each_get_mat(xs, r, c);
            V *rv = each_invoke(fn, (V*[]){el}, 1, e);
            v_free(el);
            if (!rv || rv->t == T_ERR || g_error) {
                each_free_items(items, i);
                return rv ? rv : v_nil();
            }
            items[i] = rv;
        }
        V *out = each_pack_mat(items, rows, cols);
        free(items);
        return out;
    }
    if (xs->t == T_DICT) {
        int64_t n = xs->n;
        V *vals = v_list(n);
        for (int64_t i = 0; i < n; i++) {
            V *rv = each_invoke(fn, (V*[]){xs->vals->L[i]}, 1, e);
            if (!rv || rv->t == T_ERR || g_error) {
                for (int64_t j = 0; j < i; j++) v_free(vals->L[j]);
                free(vals->L); free(vals);
                return rv ? rv : v_nil();
            }
            vals->L[i] = rv;
        }
        V *r = v_dict(xs->keys, vals);
        v_free(vals);
        return r;
    }
    if (xs->t == T_TABLE) {
        int64_t nc = xs->keys->n, nr = xs->n;
        V *new_data = v_list(nc);
        for (int64_t c = 0; c < nc; c++) {
            V *col = xs->vals->L[c];
            V **items = calloc(nr ? (size_t)nr : 1, sizeof(V*));
            if (!items) {
                v_free(new_data);
                return v_err("out of memory");
            }
            for (int64_t r = 0; r < nr; r++) {
                V *el = each_col_get(col, r);
                if (el->t == T_ERR) {
                    each_free_items(items, r);
                    v_free(new_data);
                    return el;
                }
                V *rv = each_invoke(fn, (V*[]){el}, 1, e);
                v_free(el);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, r);
                    v_free(new_data);
                    return rv ? rv : v_nil();
                }
                items[r] = rv;
            }
            new_data->L[c] = nr == 0 ? each_empty_seq_like(col)
                                     : each_pack_seq(items, nr, 0);
            free(items);
            if (new_data->L[c]->t == T_ERR) {
                V *err = new_data->L[c];
                new_data->L[c] = NULL;
                for (int64_t j = 0; j < c; j++) v_free(new_data->L[j]);
                free(new_data->L); free(new_data);
                return err;
            }
        }
        V *r = v_table(xs->keys, new_data);
        v_free(new_data);
        return r;
    }
    return v_errf("each: unsupported type %s", type_name(xs->t));
}
static V *each_dyadic(V *fn, V *xs, V *ys, Env *e) {
    if (xs->t == T_INPUT || ys->t == T_INPUT)
        return v_err("each: input streams are not supported");
    int xc = each_is_container(xs), yc = each_is_container(ys);
    if (!xc && !yc)
        return each_invoke(fn, (V*[]){xs, ys}, 2, e);
    if (xc && !yc) {
        /* Scalar on right: broadcast. Map with a unary wrapper via invoke2. */
        if (each_is_seq(xs->t)) {
            int64_t n = each_seq_len(xs);
            if (n == 0) return each_empty_seq_like(xs);
            V **items = calloc((size_t)n, sizeof(V*));
            if (!items) return v_err("out of memory");
            for (int64_t i = 0; i < n; i++) {
                V *el = each_get_seq(xs, i);
                V *rv = each_invoke(fn, (V*[]){el, ys}, 2, e);
                v_free(el);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, i);
                    return rv ? rv : v_nil();
                }
                items[i] = rv;
            }
            V *out = each_pack_seq(items, n, xs->t == T_STR);
            free(items);
            return out;
        }
        if (is_mat_t(xs->t)) {
            int64_t rows = xs->n, cols = mat_cols(xs), n = rows * cols;
            if (rows <= 0 || cols <= 0 || n <= 0) return each_empty_mat_like(xs);
            V **items = calloc((size_t)n, sizeof(V*));
            if (!items) return v_err("out of memory");
            for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < cols; c++) {
                int64_t i = r * cols + c;
                V *el = each_get_mat(xs, r, c);
                V *rv = each_invoke(fn, (V*[]){el, ys}, 2, e);
                v_free(el);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, i);
                    return rv ? rv : v_nil();
                }
                items[i] = rv;
            }
            V *out = each_pack_mat(items, rows, cols);
            free(items);
            return out;
        }
        if (xs->t == T_DICT) {
            int64_t n = xs->n;
            V *vals = v_list(n);
            for (int64_t i = 0; i < n; i++) {
                V *rv = each_invoke(fn, (V*[]){xs->vals->L[i], ys}, 2, e);
                if (!rv || rv->t == T_ERR || g_error) {
                    for (int64_t j = 0; j < i; j++) v_free(vals->L[j]);
                    free(vals->L); free(vals);
                    return rv ? rv : v_nil();
                }
                vals->L[i] = rv;
            }
            V *r = v_dict(xs->keys, vals);
            v_free(vals);
            return r;
        }
        if (xs->t == T_TABLE) {
            int64_t nc = xs->keys->n, nr = xs->n;
            V *new_data = v_list(nc);
            for (int64_t c = 0; c < nc; c++) {
                V *col = xs->vals->L[c];
                V **items = calloc(nr ? (size_t)nr : 1, sizeof(V*));
                if (!items) { v_free(new_data); return v_err("out of memory"); }
                for (int64_t r = 0; r < nr; r++) {
                    V *el = each_col_get(col, r);
                    V *rv = each_invoke(fn, (V*[]){el, ys}, 2, e);
                    v_free(el);
                    if (!rv || rv->t == T_ERR || g_error) {
                        each_free_items(items, r);
                        v_free(new_data);
                        return rv ? rv : v_nil();
                    }
                    items[r] = rv;
                }
                new_data->L[c] = nr == 0 ? each_empty_seq_like(col)
                                         : each_pack_seq(items, nr, 0);
                free(items);
            }
            V *r = v_table(xs->keys, new_data);
            v_free(new_data);
            return r;
        }
    }
    if (!xc && yc) {
        /* Scalar on left. */
        if (each_is_seq(ys->t)) {
            int64_t n = each_seq_len(ys);
            if (n == 0) return each_empty_seq_like(ys);
            V **items = calloc((size_t)n, sizeof(V*));
            if (!items) return v_err("out of memory");
            for (int64_t i = 0; i < n; i++) {
                V *el = each_get_seq(ys, i);
                V *rv = each_invoke(fn, (V*[]){xs, el}, 2, e);
                v_free(el);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, i);
                    return rv ? rv : v_nil();
                }
                items[i] = rv;
            }
            V *out = each_pack_seq(items, n, ys->t == T_STR);
            free(items);
            return out;
        }
        if (is_mat_t(ys->t)) {
            int64_t rows = ys->n, cols = mat_cols(ys), n = rows * cols;
            if (rows <= 0 || cols <= 0 || n <= 0) return each_empty_mat_like(ys);
            V **items = calloc((size_t)n, sizeof(V*));
            if (!items) return v_err("out of memory");
            for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < cols; c++) {
                int64_t i = r * cols + c;
                V *el = each_get_mat(ys, r, c);
                V *rv = each_invoke(fn, (V*[]){xs, el}, 2, e);
                v_free(el);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, i);
                    return rv ? rv : v_nil();
                }
                items[i] = rv;
            }
            V *out = each_pack_mat(items, rows, cols);
            free(items);
            return out;
        }
        if (ys->t == T_DICT) {
            int64_t n = ys->n;
            V *vals = v_list(n);
            for (int64_t i = 0; i < n; i++) {
                V *rv = each_invoke(fn, (V*[]){xs, ys->vals->L[i]}, 2, e);
                if (!rv || rv->t == T_ERR || g_error) {
                    for (int64_t j = 0; j < i; j++) v_free(vals->L[j]);
                    free(vals->L); free(vals);
                    return rv ? rv : v_nil();
                }
                vals->L[i] = rv;
            }
            V *r = v_dict(ys->keys, vals);
            v_free(vals);
            return r;
        }
        if (ys->t == T_TABLE) {
            int64_t nc = ys->keys->n, nr = ys->n;
            V *new_data = v_list(nc);
            for (int64_t c = 0; c < nc; c++) {
                V *col = ys->vals->L[c];
                V **items = calloc(nr ? (size_t)nr : 1, sizeof(V*));
                if (!items) { v_free(new_data); return v_err("out of memory"); }
                for (int64_t r = 0; r < nr; r++) {
                    V *el = each_col_get(col, r);
                    V *rv = each_invoke(fn, (V*[]){xs, el}, 2, e);
                    v_free(el);
                    if (!rv || rv->t == T_ERR || g_error) {
                        each_free_items(items, r);
                        v_free(new_data);
                        return rv ? rv : v_nil();
                    }
                    items[r] = rv;
                }
                new_data->L[c] = nr == 0 ? each_empty_seq_like(col)
                                         : each_pack_seq(items, nr, 0);
                free(items);
            }
            V *r = v_table(ys->keys, new_data);
            v_free(new_data);
            return r;
        }
    }
    /* Both containers. */
    if (each_is_seq(xs->t) && each_is_seq(ys->t)) {
        int64_t nx = each_seq_len(xs), ny = each_seq_len(ys);
        if (nx != ny) return v_err("each: length mismatch");
        if (nx == 0) return each_empty_seq_like(xs);
        V **items = calloc((size_t)nx, sizeof(V*));
        if (!items) return v_err("out of memory");
        for (int64_t i = 0; i < nx; i++) {
            V *a = each_get_seq(xs, i);
            V *b = each_get_seq(ys, i);
            V *rv = each_invoke(fn, (V*[]){a, b}, 2, e);
            v_free(a); v_free(b);
            if (!rv || rv->t == T_ERR || g_error) {
                each_free_items(items, i);
                return rv ? rv : v_nil();
            }
            items[i] = rv;
        }
        V *out = each_pack_seq(items, nx, xs->t == T_STR && ys->t == T_STR);
        free(items);
        return out;
    }
    if (is_mat_t(xs->t) && is_mat_t(ys->t)) {
        if (xs->n != ys->n || mat_cols(xs) != mat_cols(ys))
            return v_err("each: matrix shape mismatch");
        int64_t rows = xs->n, cols = mat_cols(xs), n = rows * cols;
        if (rows <= 0 || cols <= 0 || n <= 0) return each_empty_mat_like(xs);
        V **items = calloc((size_t)n, sizeof(V*));
        if (!items) return v_err("out of memory");
        for (int64_t r = 0; r < rows; r++) for (int64_t c = 0; c < cols; c++) {
            int64_t i = r * cols + c;
            V *a = each_get_mat(xs, r, c);
            V *b = each_get_mat(ys, r, c);
            V *rv = each_invoke(fn, (V*[]){a, b}, 2, e);
            v_free(a); v_free(b);
            if (!rv || rv->t == T_ERR || g_error) {
                each_free_items(items, i);
                return rv ? rv : v_nil();
            }
            items[i] = rv;
        }
        V *out = each_pack_mat(items, rows, cols);
        free(items);
        return out;
    }
    if (xs->t == T_DICT && ys->t == T_DICT) {
        if (xs->n != ys->n) return v_err("each: dictionary key set mismatch");
        V *vals = v_list(xs->n);
        for (int64_t i = 0; i < xs->n; i++) {
            int j = each_dict_find_key(ys, xs->keys->L[i]);
            if (j < 0) {
                for (int64_t k = 0; k < i; k++) v_free(vals->L[k]);
                free(vals->L); free(vals);
                return v_err("each: dictionary key set mismatch");
            }
            V *rv = each_invoke(fn, (V*[]){xs->vals->L[i], ys->vals->L[j]}, 2, e);
            if (!rv || rv->t == T_ERR || g_error) {
                for (int64_t k = 0; k < i; k++) v_free(vals->L[k]);
                free(vals->L); free(vals);
                return rv ? rv : v_nil();
            }
            vals->L[i] = rv;
        }
        V *r = v_dict(xs->keys, vals);
        v_free(vals);
        return r;
    }
    if (xs->t == T_TABLE && ys->t == T_TABLE) {
        if (xs->keys->n != ys->keys->n || xs->n != ys->n)
            return v_err("each: table schema or shape mismatch");
        int64_t nc = xs->keys->n, nr = xs->n;
        for (int64_t c = 0; c < nc; c++) {
            if (xs->keys->L[c]->t != T_STR ||
                each_table_find_col(ys, xs->keys->L[c]->s) < 0)
                return v_err("each: table schema or shape mismatch");
        }
        V *new_data = v_list(nc);
        for (int64_t c = 0; c < nc; c++) {
            int yc = each_table_find_col(ys, xs->keys->L[c]->s);
            V *colx = xs->vals->L[c], *coly = ys->vals->L[yc];
            V **items = calloc(nr ? (size_t)nr : 1, sizeof(V*));
            if (!items) { v_free(new_data); return v_err("out of memory"); }
            for (int64_t r = 0; r < nr; r++) {
                V *a = each_col_get(colx, r);
                V *b = each_col_get(coly, r);
                V *rv = each_invoke(fn, (V*[]){a, b}, 2, e);
                v_free(a); v_free(b);
                if (!rv || rv->t == T_ERR || g_error) {
                    each_free_items(items, r);
                    v_free(new_data);
                    return rv ? rv : v_nil();
                }
                items[r] = rv;
            }
            new_data->L[c] = nr == 0 ? each_empty_seq_like(colx)
                                     : each_pack_seq(items, nr, 0);
            free(items);
        }
        V *r = v_table(xs->keys, new_data);
        v_free(new_data);
        return r;
    }
    return v_errf("each: cannot pair %s with %s", type_name(xs->t), type_name(ys->t));
}
static V *eval_each(Node *n, Env *e) {
    V *fn = eval(n->ch[0], e);
    P(fn->t == T_ERR, fn)
    if (fn->t != T_FN) {
        V *err = v_errf("each: left of '@' must be callable (got %s); use f@ xs, xs f@ ys, or mmul",
                        type_name(fn->t));
        v_free(fn);
        return err;
    }
    if (n->nch == 2) {
        V *xs = eval(n->ch[1], e);
        if (xs->t == T_ERR) { v_free(fn); return xs; }
        V *r = each_unary(fn, xs, e);
        v_free(fn); v_free(xs);
        return r;
    }
    if (n->nch >= 3) {
        V *xs = eval(n->ch[1], e);
        if (xs->t == T_ERR) { v_free(fn); return xs; }
        V *ys = eval(n->ch[2], e);
        if (ys->t == T_ERR) { v_free(fn); v_free(xs); return ys; }
        V *r = each_dyadic(fn, xs, ys, e);
        v_free(fn); v_free(xs); v_free(ys);
        return r;
    }
    v_free(fn);
    return v_err("each: bad arity");
}

/* True if node is an INT expression using only INT literals and `iname`. */
static int int_expr_uses_only(Node *n, const char *iname) {
    if(!n) return 0;
    if(n->type == N_INT) return 1;
    if(n->type == N_NAME) return iname && strcmp(n->sval, iname) == 0;
    if(n->type == N_BINOP && n->nch >= 2 &&
       (n->op == OP_ADD || n->op == OP_SUB || n->op == OP_MUL))
        return int_expr_uses_only(n->ch[0], iname) && int_expr_uses_only(n->ch[1], iname);
    return 0;
}
static int eval_int_expr_i(Node *n, int64_t i, int64_t *out) {
    if(n->type == N_INT) { *out = n->ival; return 1; }
    if(n->type == N_NAME) { *out = i; return 1; }
    if(n->type == N_BINOP && n->nch >= 2) {
        int64_t a, b;
        if(!eval_int_expr_i(n->ch[0], i, &a) || !eval_int_expr_i(n->ch[1], i, &b)) return 0;
        switch(n->op) {
        case OP_ADD: *out = a + b; return 1;
        case OP_SUB: *out = a - b; return 1;
        case OP_MUL: *out = a * b; return 1;
        default: return 0;
        }
    }
    return 0;
}
/* Specialize C-lowered counting loops:
 *   while (i < N):
 *       total += <int expr in i>
 *       i += 1
 * Returns a result V* if handled, or NULL to fall back to generic while.
 */
static V *try_fast_counting_while(Node *n, Env *e) {
    if(!n || n->nch < 2) return NULL;
    Node *cond = n->ch[0], *body = n->ch[1];
    if(cond->type != N_BINOP || cond->op != OP_LT || cond->nch < 2) return NULL;
    if(cond->ch[0]->type != N_NAME || cond->ch[1]->type != N_INT) return NULL;
    if(body->type != N_BLOCK || body->nch < 1) return NULL;
    const char *iname = cond->ch[0]->sval;
    int64_t limit = cond->ch[1]->ival;
    Node *last = body->ch[body->nch - 1];
    if(last->type != N_AUGASSIGN || last->op != OP_ADD) return NULL;
    if(last->ch[0]->type != N_NAME || strcmp(last->ch[0]->sval, iname) != 0) return NULL;
    if(last->ch[1]->type != N_INT || last->ch[1]->ival != 1) return NULL;
    for(int s = 0; s < body->nch - 1; s++) {
        Node *stmt = body->ch[s];
        if(stmt->type != N_AUGASSIGN || stmt->op != OP_ADD || stmt->ch[0]->type != N_NAME)
            return NULL;
        if(!int_expr_uses_only(stmt->ch[1], iname)) return NULL;
        int64_t cur;
        if(!env_get_int(e, stmt->ch[0]->sval, &cur)) return NULL;
    }
    int64_t i;
    if(!env_get_int(e, iname, &i)) return NULL;
    while(i < limit) {
        for(int s = 0; s < body->nch - 1; s++) {
            Node *stmt = body->ch[s];
            int64_t delta, cur;
            if(!eval_int_expr_i(stmt->ch[1], i, &delta)) return NULL;
            if(!env_get_int(e, stmt->ch[0]->sval, &cur)) return NULL;
            if(!env_set_int_inplace(e, stmt->ch[0]->sval, cur + delta)) return NULL;
        }
        i += 1;
    }
    if(!env_set_int_inplace(e, iname, i)) return NULL;
    return v_nil();
}

static int shakti_call_depth_limit(void) {
    if (g_call_depth_limit < 0) {
        const char *e = getenv("SHAKTI_CALL_MAX_DEPTH");
        int v = e && e[0] ? atoi(e) : 0;
        g_call_depth_limit = v > 0 ? v : SHAKTI_CALL_MAX_DEPTH;
    }
    return g_call_depth_limit;
}

V *eval_fn(Node *body, Env *e) {
    if (g_call_depth >= shakti_call_depth_limit())
        return v_err("recursion limit exceeded");
    g_call_depth++;
    V *r = eval(body, e);
    g_call_depth--;
    return r;
}

V *eval(Node *n, Env *e) {
    P(!n,v_nil())
    P(g_returning || g_breaking || g_continuing || g_error,v_nil())
    switch(n->type) {
    case N_INT:  return v_int(n->ival);
    case N_CHARS: return v_char((unsigned char)n->ival);
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
            size_t plen = strlen(from0->s);
#ifdef SHAKTI_HAVE_IEFS
            if (plen >= 5 && !strcmp(from0->s + plen - 5, ".iefs")) {
                (void)proj;
                from = iefs_store_map(from0->s, IEFS_MAP_PAGES_THP);
            } else
#endif
            {
                from = table_load(from0->s, proj);
            }
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
        V *from0 = eval(n->ch[0], e);
        P(from0->t == T_ERR,from0)
        char *iefs_path = NULL;
        V *from = from0;
#ifdef SHAKTI_HAVE_IEFS
        if (from0->t == T_STR) {
            size_t plen = strlen(from0->s);
            if (plen >= 5 && !strcmp(from0->s + plen - 5, ".iefs")) {
                iefs_path = strdup(from0->s);
                from = iefs_store_map(from0->s, IEFS_MAP_PAGES_THP);
                v_free(from0);
                if (from->t == T_ERR) {
                    free(iefs_path);
                    return from;
                }
            }
        }
#endif
        V *assignments = eval_update_cols(n->ch[1], from, e);
        if (assignments->t == T_ERR) {
            free(iefs_path);
            v_free(from);
            return assignments;
        }
        V *where = eval_with_table_columns(from, n->ch[3], e);
        V *r = table_sql_update(from, assignments, where);
#ifdef SHAKTI_HAVE_IEFS
        if (r && r->t != T_ERR && iefs_path) {
            char err[256];
            err[0] = 0;
            if (iefs_store_write(r, iefs_path, IEFS_IO_AUTO, err, sizeof err) != 0) {
                const char *e_msg = iefs_last_error();
                v_free(r);
                r = v_err(e_msg && e_msg[0] ? e_msg : (err[0] ? err : "iefs save failed"));
            }
        }
#endif
        if(r && r->t != T_ERR && n->ch[0] && n->ch[0]->type == N_NAME)
            env_update(e, n->ch[0]->sval, r);
        free(iefs_path);
        v_free(from); v_free(assignments); v_free(where);
        return r;
    }
    case N_DELETE: {
        V *sql_err = require_sql(e);
        P(sql_err,sql_err)
        V *from0 = eval(n->ch[0], e);
        P(from0->t == T_ERR,from0)
        char *iefs_path = NULL;
        V *from = from0;
#ifdef SHAKTI_HAVE_IEFS
        if (from0->t == T_STR) {
            size_t plen = strlen(from0->s);
            if (plen >= 5 && !strcmp(from0->s + plen - 5, ".iefs")) {
                iefs_path = strdup(from0->s);
                from = iefs_store_map(from0->s, IEFS_MAP_PAGES_THP);
                v_free(from0);
                if (from->t == T_ERR) {
                    free(iefs_path);
                    return from;
                }
            }
        }
#endif
        V *cols = eval_select_cols(n->ch[1], e);
        V *where = eval_with_table_columns(from, n->ch[3], e);
        V *r = table_sql_delete(from, cols, where);
#ifdef SHAKTI_HAVE_IEFS
        if (r && r->t != T_ERR && iefs_path) {
            char err[256];
            err[0] = 0;
            if (iefs_store_write(r, iefs_path, IEFS_IO_AUTO, err, sizeof err) != 0) {
                const char *e_msg = iefs_last_error();
                v_free(r);
                r = v_err(e_msg && e_msg[0] ? e_msg : (err[0] ? err : "iefs save failed"));
            }
        }
#endif
        if(r && r->t != T_ERR && n->ch[0] && n->ch[0]->type == N_NAME)
            env_update(e, n->ch[0]->sval, r);
        free(iefs_path);
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
        V *vals = eval_insert_values(n->ch[1], e);
        P(vals->t == T_ERR,(v_free(cols),vals))
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
    case N_UNION_JOIN: {
        V *left = eval(n->ch[0], e);
        V *right = eval(n->ch[1], e);
        P(left->t==T_ERR,(v_free(right),left))
        P(right->t==T_ERR,(v_free(left),right))
        V *r;
        if(left->t==T_TABLE&&right->t==T_TABLE) r=table_union_join(left,right);
        else if((left->t==T_LIST||left->t==T_IVEC||left->t==T_FVEC)&&
                (right->t==T_LIST||right->t==T_IVEC||right->t==T_FVEC)) r=list_union(left,right);
        else r=v_err("union: requires two tables or two lists/vectors");
        v_free(left); v_free(right);
        return r;
    }
    case N_OUTER_JOIN: {
        V *left = eval(n->ch[0], e);
        V *right = eval(n->ch[1], e);
        P(left->t==T_ERR,(v_free(right),left))
        P(right->t==T_ERR,(v_free(left),right))
        V *r=table_outer_join(left,right);
        v_free(left); v_free(right);
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
        if (!result) return v_err("out of memory");
        int rlen = 0;
        result[0] = 0;
        int i = 0, slen = (int)strlen(s);
        while (i < slen) {
            if (s[i] == '{' && i + 1 < slen && s[i + 1] != '{') {
                char *raw = NULL, *expr = NULL, *vs = NULL;
                i++;
                int start = i, depth = 1;
                while (i < slen && depth > 0) {
                    if (s[i] == '{') depth++;
                    else if (s[i] == '}') {
                        depth--;
                        if (depth == 0) break;
                    }
                    i++;
                }
                int elen = i - start;
                raw = malloc((size_t)elen + 1);
                if (!raw) goto fstr_oom;
                memcpy(raw, s + start, (size_t)elen);
                raw[elen] = 0;
                char *fmt_spec = NULL;
                int bd = 0;
                for (int k = elen - 1; k >= 0; k--) {
                    if (raw[k] == ']' || raw[k] == ')' || raw[k] == '}') bd++;
                    else if (raw[k] == '[' || raw[k] == '(' || raw[k] == '{') bd--;
                    else if (raw[k] == ':' && bd == 0) {
                        fmt_spec = raw + k + 1;
                        raw[k] = 0;
                        break;
                    }
                }
                size_t rawlen = strlen(raw);
                expr = malloc(rawlen + 2);
                if (!expr) {
                    free(raw);
                    goto fstr_oom;
                }
                memcpy(expr, raw, rawlen);
                expr[rawlen] = '\n';
                expr[rawlen + 1] = 0;
                Node *ast = parse(expr);
                V *val = eval(ast, e);
                node_free(ast);
                if (g_error || !val || val->t == T_ERR) {
                    if (val) v_free(val);
                    free(expr);
                    free(raw);
                    free(result);
                    return v_nil();
                }
                if (fmt_spec && *fmt_spec) {
                    if (val->t == T_FLOAT || val->t == T_INT) {
                        if (!fstr_fmt_valid(fmt_spec)) {
                            char spec_copy[40];
                            snprintf(spec_copy, sizeof spec_copy, "%s", fmt_spec);
                            v_free(val);
                            free(expr);
                            free(raw);
                            free(result);
                            return v_errf("f-string: invalid format specifier ':%s'", spec_copy);
                        }
                        char fmt[64];
                        snprintf(fmt, sizeof fmt, "%%%s", fmt_spec);
                        char buf[256];
                        double dv = val->t == T_FLOAT ? val->f : (double)val->j;
                        snprintf(buf, sizeof buf, fmt, dv);
                        vs = strdup(buf);
                    } else {
                        vs = v_to_str(val);
                    }
                } else {
                    vs = v_to_str(val);
                }
                if (!vs) {
                    v_free(val);
                    free(expr);
                    free(raw);
                    goto fstr_oom;
                }
                int vslen = (int)strlen(vs);
                {
                    char *tmp = realloc(result, (size_t)rlen + (size_t)vslen + 256);
                    if (!tmp) {
                        free(vs);
                        v_free(val);
                        free(expr);
                        free(raw);
                        goto fstr_oom;
                    }
                    result = tmp;
                }
                memcpy(result + rlen, vs, (size_t)vslen);
                rlen += vslen;
                free(vs);
                v_free(val);
                free(expr);
                free(raw);
                if (i < slen) i++;
            } else {
                char *tmp = realloc(result, (size_t)rlen + 2);
                if (!tmp) goto fstr_oom;
                result = tmp;
                result[rlen++] = s[i++];
            }
        }
        result[rlen] = 0;
        V *r = v_str(result);
        free(result);
        return r;
    fstr_oom:
        free(result);
        return v_err("out of memory");
    }
    case N_LIST: {
        int nch = n->nch;
        V **elems = calloc(nch?nch:1, sizeof(V*));
        int all_int=1, all_u8=1, all_num=1, all_bool=1;
        for(int i=0;i<nch;i++) {
            elems[i] = eval(n->ch[i], e);
            if(g_error) { for(int j=0;j<=i;j++) v_free(elems[j]); free(elems); return v_nil(); }
            if(elems[i]->t != T_INT && elems[i]->t != T_CHAR) all_int = 0;
            if(elems[i]->t != T_CHAR) all_u8 = 0;
            if(elems[i]->t != T_INT && elems[i]->t != T_FLOAT && elems[i]->t != T_CHAR) all_num = 0;
            if(elems[i]->t != T_BOOL) all_bool = 0;
        }
        if(nch > 0 && all_u8) {
            V *r = v_cvec(nch);
            i(nch,{r->B[i]=(unsigned char)elems[i]->j; v_free(elems[i]);})
            free(elems); return r;
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
        if(n->op == OP_ASOF_COMMA) {
            V *a=eval(n->ch[0],e);
            V *b=eval(n->ch[1],e);
            P(a->t==T_ERR,(v_free(b),a))
            P(b->t==T_ERR,(v_free(a),b))
            V *r=table_comma_join(a,b);
            v_free(a);v_free(b);
            return r;
        }
        if(n->op == OP_AND) {
            V *a = eval(n->ch[0], e);
            P(a->t==T_ERR,a)
            if(a->t==T_BVEC||a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC){
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
            if(a->t==T_BVEC||a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC){
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
            /* Vector membership: ivec in ivec|list[int] → bvec (dense bitset). */
            if (a->t == T_IVEC && (b->t == T_IVEC || b->t == T_LIST)) {
                int64_t bn = b->n;
                int64_t *needles = NULL;
                int64_t nneed = 0;
                if (b->t == T_IVEC) {
                    needles = b->J;
                    nneed = bn;
                } else {
                    needles = malloc((size_t)bn * sizeof(int64_t));
                    if (!needles) {
                        v_free(a); v_free(b);
                        return v_err("out of memory");
                    }
                    for (int64_t i = 0; i < bn; i++) {
                        if (!b->L[i] || b->L[i]->t != T_INT) {
                            free(needles);
                            v_free(a); v_free(b);
                            return v_err("in: list members must be int");
                        }
                        needles[i] = b->L[i]->j;
                    }
                    nneed = bn;
                }
                int64_t bmin = INT64_MAX, bmax = INT64_MIN;
                for (int64_t i = 0; i < nneed; i++) {
                    if (needles[i] < bmin) bmin = needles[i];
                    if (needles[i] > bmax) bmax = needles[i];
                }
                V *r = v_bvec(a->n);
                int invert = (n->op == OP_NOT_IN);
                if (nneed == 0) {
                    for (int64_t i = 0; i < a->n; i++) r->B[i] = invert ? 1 : 0;
                } else if (bmin >= 0 && bmax < 1000000 && bmax >= bmin) {
                    unsigned char *pres = calloc((size_t)bmax + 1, 1);
                    if (!pres) {
                        if (b->t == T_LIST) free(needles);
                        v_free(r); v_free(a); v_free(b);
                        return v_err("out of memory");
                    }
                    for (int64_t i = 0; i < nneed; i++) pres[needles[i]] = 1;
                    for (int64_t i = 0; i < a->n; i++) {
                        int64_t v = a->J[i];
                        int hit = (v >= 0 && v <= bmax && pres[v]);
                        r->B[i] = invert ? !hit : hit;
                    }
                    free(pres);
                } else {
                    /* Fallback: linear scan per element (small needle sets). */
                    for (int64_t i = 0; i < a->n; i++) {
                        int hit = 0;
                        int64_t v = a->J[i];
                        for (int64_t j = 0; j < nneed; j++) {
                            if (needles[j] == v) { hit = 1; break; }
                        }
                        r->B[i] = invert ? !hit : hit;
                    }
                }
                if (b->t == T_LIST) free(needles);
                v_free(a); v_free(b);
                return r;
            }
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
            if(a->t==T_INT || a->t==T_CHAR)  {
                /* -INT64_MIN overflows int64; promote to float. */
                V *r = (a->j==INT64_MIN) ? v_float(-(double)INT64_MIN) : v_int(-a->j);
                v_free(a); return r;
            }
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
        if(target->type == N_LIST && (val->t==T_LIST||val->t==T_IVEC||val->t==T_FVEC||val->t==T_CVEC)) {
            for(int i=0; i<target->nch && i<val->n; i++) {
                if(target->ch[i]->type == N_NAME) {
                    V *elem;
                    if(val->t==T_LIST) elem=val->L[i];
                    else if(val->t==T_IVEC) elem=v_int(val->J[i]);
                    else if(val->t==T_CVEC) elem=v_char(val->B[i]);
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
                if ((idx0->t == T_INT || idx0->t == T_CHAR) && (idx1->t == T_INT || idx1->t == T_CHAR)) {
                    int64_t r = idx0->j, c = idx1->j;
                    if (r < 0) r += obj->n;
                    if (c < 0) c += mat_cols(obj);
                    if (r >= 0 && r < obj->n && c >= 0 && c < mat_cols(obj)) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx0, idx1, val);
                        if (obj->t == T_IMAT && (val->t == T_INT || val->t == T_CHAR))
                            obj->J[mat_idx(obj, r, c)] = val->j;
                        else if (obj->t == T_FMAT && (val->t == T_FLOAT || val->t == T_INT || val->t == T_CHAR))
                            obj->F[mat_idx(obj, r, c)] = to_float(val);
                        else if (obj->t == T_BMAT && val->t == T_BOOL)
                            obj->B[mat_idx(obj, r, c)] = val->b ? 1 : 0;
                        else if (obj->t == T_CMAT && (val->t == T_CHAR || val->t == T_INT))
                            obj->B[mat_idx(obj, r, c)] = (unsigned char)val->j;
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
            if(idx->t==T_INT || idx->t==T_CHAR) {
                int64_t i = idx->j;
                if(i < 0) i += obj->n;
                if(i >= 0 && i < obj->n) {
                    if(obj->t==T_LIST) { v_free(obj->L[i]); obj->L[i] = v_ref(val); }
                    else if(obj->t==T_IVEC && (val->t==T_INT || val->t==T_CHAR)) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx, val, NULL);
                        obj->J[i] = val->j;
                    } else if(obj->t==T_FVEC && (val->t==T_FLOAT||val->t==T_INT||val->t==T_CHAR)) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx, val, NULL);
                        obj->F[i] = val->t==T_FLOAT ? val->f : (double)val->j;
                    } else if(obj->t==T_BVEC && val->t==T_BOOL) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx, val, NULL);
                        obj->B[i] = val->b ? 1 : 0;
                    } else if(obj->t==T_CVEC && (val->t==T_CHAR || val->t==T_INT)) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx, val, NULL);
                        obj->B[i] = (unsigned char)val->j;
                    } else if(is_mat_t(obj->t) && target->nch == 2) {
                        if (v_ensure_writable(obj) != 0)
                            return assign_map_oom(obj, idx, val, NULL);
                        int64_t cols = mat_cols(obj);
                        if(obj->t==T_IMAT && val->t==T_IVEC && val->n==cols)
                            memcpy(obj->J + mat_idx(obj, i, 0), val->J, (size_t)cols * 8);
                        else if(obj->t==T_FMAT && val->t==T_FVEC && val->n==cols)
                            memcpy(obj->F + mat_idx(obj, i, 0), val->F, (size_t)cols * 8);
                        else if(obj->t==T_BMAT && val->t==T_BVEC && val->n==cols)
                            memcpy(obj->B + mat_idx(obj, i, 0), val->B, (size_t)cols);
                        else if(obj->t==T_CMAT && val->t==T_CVEC && val->n==cols)
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
                    if ((val->t==T_IVEC||val->t==T_FVEC||val->t==T_BVEC||val->t==T_CVEC||val->t==T_LIST) &&
                        obj->n > 0 && val->n != obj->n) {
                        v_free(obj); v_free(val);
                        return v_err("table column length mismatch");
                    }
                    v_free(obj->vals->L[found]);
                    obj->vals->L[found] = v_ref(val);
                } else {
                    if ((val->t==T_IVEC||val->t==T_FVEC||val->t==T_BVEC||val->t==T_CVEC||val->t==T_LIST) &&
                        obj->n > 0 && val->n != obj->n) {
                        v_free(obj); v_free(val);
                        return v_err("table column length mismatch");
                    }
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
            /* Fast path: uniquely-owned INT += INT (common in C-lowered loops). */
            if(cur->t == T_INT && cur->rc == 1 && n->ch[1]->type == N_INT && n->op == OP_ADD) {
                cur->j += n->ch[1]->ival;
                return v_ref(cur);
            }
            if(cur->t == T_INT && cur->rc == 1 && n->ch[1]->type == N_NAME && n->op == OP_ADD) {
                V *rhs = env_get(e, n->ch[1]->sval);
                if(rhs && rhs->t == T_INT) {
                    cur->j += rhs->j;
                    return v_ref(cur);
                }
            }
            V *delta = eval(n->ch[1], e);
            if(cur->t == T_INT && cur->rc == 1 && delta->t == T_INT &&
               (n->op == OP_ADD || n->op == OP_SUB || n->op == OP_MUL)) {
                if(n->op == OP_ADD) cur->j += delta->j;
                else if(n->op == OP_SUB) cur->j -= delta->j;
                else cur->j *= delta->j;
                v_free(delta);
                return v_ref(cur);
            }
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
            } else if(is_mat_t(obj->t) && (idx->t==T_INT || idx->t==T_CHAR)) {
                int64_t j = idx->j;
                if(j < 0) j += obj->n;
                if(j >= 0 && j < obj->n) next = v_mat_row(obj, j);
                else next = v_err("index out of range");
            } else if((obj->t==T_IVEC||obj->t==T_FVEC||obj->t==T_BVEC||obj->t==T_CVEC||obj->t==T_LIST) && (idx->t==T_INT||idx->t==T_CHAR)) {
                int64_t j = idx->j;
                if(j < 0) j += obj->n;
                if(j >= 0 && j < obj->n) {
                    if(obj->t==T_IVEC) next = v_int(obj->J[j]);
                    else if(obj->t==T_FVEC) next = v_float(obj->F[j]);
                    else if(obj->t==T_BVEC) next = v_bool(obj->B[j] != 0);
                    else if(obj->t==T_CVEC) next = v_char(obj->B[j]);
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
            } else if(obj->t==T_STR && (idx->t==T_INT||idx->t==T_CHAR)) {
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
        if(obj->t == T_SUBPROCESS) {
            if(strcmp(n->sval, "pid") == 0) { V *r = v_int(obj->n); v_free(obj); return r; }
            if(strcmp(n->sval, "status") == 0) { V *r = subprocess_status(obj); v_free(obj); return r; }
        }
        v_free(obj);
        return v_errf("attribute '%s' not found", n->sval);
    }
    case N_EACH:
        return eval_each(n, e);
    case N_CALL: {
        Node *fn_node = n->ch[0];
        if(fn_node->type == N_LIST && fn_node->ival > 0) {
            if(n->nch==1) {
                switch(fn_node->ival) {
                case T_BVEC: return v_bvec(0);
                case T_CVEC: return v_cvec(0);
                case T_FVEC: return v_fvec(0);
                case T_IVEC: return v_ivec(0);
                case T_BMAT: return v_bmat(0,0);
                case T_CMAT: return v_cmat(0,0);
                case T_FMAT: return v_fmat(0,0);
                case T_IMAT: return v_imat(0,0);
                case T_LIST: return v_list(0);
                default: return v_err("bad typed constructor");
                }
            }
            if(n->nch==2) {
                V *obj = eval(n->ch[1], e);
                if(obj->t != T_CHAR && obj->t != T_INT) {
                    V *r = builtin_call("list",&obj,1,NULL,NULL,0,e);
                    v_free(obj);
                    return r;
                }
                long long x = obj->j;
                v_free(obj);
                if(x < 0) x = 0;
                switch(fn_node->ival) {
                case T_BVEC: return v_bvec(x);
                case T_CVEC: return v_cvec(x);
                case T_FVEC: return v_fvec(x);
                case T_IVEC: return v_ivec(x);
                case T_BMAT: return v_bmat(x,0);
                case T_CMAT: return v_cmat(x,0);
                case T_FMAT: return v_fmat(x,0);
                case T_IMAT: return v_imat(x,0);
                case T_LIST: return v_list(x);
                default: return v_err("bad typed constructor");
                }
            }
            if(n->nch==3) {
                V *o1 = eval(n->ch[1], e);
                V *o2 = eval(n->ch[2], e);
                if((o1->t != T_CHAR && o1->t != T_INT)||(o2->t != T_CHAR && o2->t != T_INT)) {
                    v_free(o1); v_free(o2);
                    return v_err("types");
                }
                long long x = o1->j;
                long long y = o2->j;
                v_free(o1); v_free(o2);
                if(x < 0) x = 0;
                if(y < 0) y = 0;
                switch(fn_node->ival) {
                case T_BVEC:
                case T_CVEC:
                case T_FVEC:
                case T_IVEC:
                case T_LIST: return v_err("too many dimensions");
                case T_BMAT: return v_bmat(x,y);
                case T_CMAT: return v_cmat(x,y);
                case T_FMAT: return v_fmat(x,y);
                case T_IMAT: return v_imat(x,y);
                default: return v_err("bad typed constructor");
                }
            }
            return v_err("too many dimensions");
        }
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
                for(int i=0; i<params->n; i++) {
                    if(i < nargs) {
                        env_set(call_env, params->L[i]->s, args[i]);
                    } else if(attr->defaults && i < attr->defaults->n && attr->defaults->L[i]->t != T_NIL) {
                        env_set(call_env, params->L[i]->s, attr->defaults->L[i]);
                    }
                }
                for(int i=1; i<n->nch; i++) {
                    if(n->ch[i]->type == N_KWARG) {
                        V *kv = eval(n->ch[i]->ch[0], e);
                        env_set(call_env, n->ch[i]->sval, kv);
                        v_free(kv);
                    }
                }
                Node *body = fn_ast[(int)attr->j];
                V *result = eval_fn(body, call_env);
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
            if(obj->t == T_SUBPROCESS) {
                V *r;
                if(!nargs) r = subprocess_next(obj, -1);
                else if(nargs == 1 && args[0]->t == T_FLOAT) r = subprocess_next(obj, args[0]->f);
                else if(nargs == 1 && args[0]->t == T_INT) r = subprocess_next(obj, (double)args[0]->j);
                else if(nargs == 1 && args[0]->t == T_NIL) r = subprocess_next(obj, -1.0);
                else r = v_int(subprocess_send(obj, args, nargs));
                v_free(obj);
                for(int i=0;i<nargs;i++) v_free(args[i]);
                free(args);
                return r;
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
        if (fn && fn->t == T_SUBPROCESS) {
            V *result;
            if (nargs && args[0]->t == T_FLOAT)
                result = subprocess_next(fn, args[0]->f);
            else if (nargs && args[0]->t == T_INT)
                result = subprocess_next(fn, (double)args[0]->j);
            else if (!nargs || args[0]->t == T_NIL)
                result = subprocess_next(fn, -1.0);
            else
                result = v_int(subprocess_send(fn, args, nargs));
            v_free(fn);
            for(int i=0;i<nargs;i++) v_free(args[i]);
            for(int i=0;i<nkw;i++) { v_free(kwnames[i]); v_free(kwvals[i]); }
            free(args); free(kwnames); free(kwvals);
            return result;
        }
        if(!fn || fn->t != T_FN) {
            if(fn) v_free(fn);
            for(int i=0;i<nargs;i++) v_free(args[i]);
            for(int i=0;i<nkw;i++) { v_free(kwnames[i]); v_free(kwvals[i]); }
            free(args); free(kwnames); free(kwvals);
            return v_errf("'%s' is not callable", fn_node->sval ? fn_node->sval : "?");
        }
        Env *call_env = env_acquire(fn->closure);
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
        V *result = eval_fn(body, call_env);
        if(g_returning) {
            g_returning = 0;
            v_free(result);
            result = g_retval ? g_retval : v_nil();
            g_retval = NULL;
        }
        env_release(call_env);
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
        V *fast = try_fast_counting_while(n, e);
        if(fast) return fast;
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
        } else if(iter->t == T_CVEC) {
            for(int64_t i=0;i<iter->n;i++) {
                V *item = v_char(iter->B[i]);
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
                r = eval(n->ch[2], e);
                if(g_returning||g_error) { v_free(iter); return r; }
                if(g_breaking) { g_breaking=0; break; }
                if(g_continuing) { g_continuing=0; }
            }
        } else if(iter->t == T_BVEC) {
            for(int64_t i=0;i<iter->n;i++) {
                V *item = v_bool(iter->B[i] != 0);
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
        } else if(iter->t == T_SUBPROCESS) {
            for(;;) {
                V *item = subprocess_next(iter, 0.0);
                if(g_error) { v_free(iter); return item; }
                if(!item || item->t == T_NIL) { v_free(item); break; }
                for_set_vars(vars, item, e);
                v_free(item); v_free(r);
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
        Node *body = n->ch[1];
        V *fn = v_fn(params, defaults, body, e);
        if(fn->t == T_ERR) {
            g_error = 1;
            if(g_error_val) { v_free(g_error_val); g_error_val = NULL; }
            g_error_val = v_ref(fn);
            v_free(params); v_free(defaults); v_free(fn);
            return v_nil();
        }
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
        Node *body = n->ch[1];
        V *fn = v_fn(params, defaults, body, e);
        v_free(params); v_free(defaults);
        if(fn->t == T_ERR) {
            g_error = 1;
            if(g_error_val) { v_free(g_error_val); g_error_val = NULL; }
            g_error_val = v_ref(fn);
            v_free(fn);
            return v_nil();
        }
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
        if (val->t == T_ERR) {
            g_error_val = v_ref(val);
        } else {
            char *s = v_to_str(val);
            g_error_val = v_errf("%s", s ? s : "");
            free(s);
        }
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
