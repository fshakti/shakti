/* each / adverb application, split out of the tree-walk evaluator. */
#include "shakti_internal.h"

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
        for (int64_t i = 0; i < n; i++) { r->B[i] = items[i]->j ? 1 : 0; v_free(items[i]); }
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
        for (int64_t i = 0; i < n; i++) { r->B[i] = items[i]->j ? 1 : 0; v_free(items[i]); }
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
    int ok = c && c->t == T_BOOL && c->j;
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
V *eval_each(Node *n, Env *e) {
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
