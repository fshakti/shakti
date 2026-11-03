/* shakti/src/vec_ops.c — vector/matrix binops, compare, table filter */
#include "shakti_internal.h"
#include "mat_simd.h"
#include "i64bin.h"

int is_truthy(V *v) {
    P(!v,0)
    switch(v->t) {
    case T_NIL:  return 0;
    case T_BOOL: return v->j;
    case T_INT:  return v->j != 0;
    case T_CHAR: return v->j != 0;
    case T_FLOAT:return v->f != 0.0;
    case T_STR:  return v->s[0] != 0;
    case T_IVEC: case T_FVEC: case T_BVEC: case T_CVEC: case T_LIST: return v->n > 0;
    case T_IMAT: case T_FMAT: case T_BMAT: case T_CMAT: return v->n > 0 && mat_cols(v) > 0;
    case T_DICT: case T_TABLE: return v->n > 0;   /* entries, rows */
    case T_DATETIME: return v->j != 0;
    case T_DATE: return v->j != 0;
    case T_TIME: return v->j != 0;
    case T_ERR:  return 0;
    case T_SUBPROCESS: return v->j >= 0;
    default: return 1;
    }
}
static int v_elem_truthy(V *v, int64_t i) {
    switch (v->t) {
    case T_BVEC: return v->B[i] != 0;
    case T_CVEC: return v->B[i] != 0;
    case T_IVEC: return v->J[i] != 0;
    case T_FVEC: return v->F[i] != 0.0;
    default: return is_truthy(v);
    }
}
/* Element-wise logical and/or for vector operands (scalars broadcast). Scalar
 * short-circuit truthiness collapses a whole vector to one bool and returns the
 * other operand wholesale, so `mask_a and mask_b` must be combined per element
 * here instead — otherwise SQL `where A and B` silently ignores A. */
V *vec_logic(V *a, V *b, int is_and) {
    int av = (a->t == T_BVEC || a->t == T_IVEC || a->t == T_FVEC || a->t == T_CVEC);
    int bv = (b->t == T_BVEC || b->t == T_IVEC || b->t == T_FVEC || b->t == T_CVEC);
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
double to_float(V *v) {
    P(!v,0)
    P(v->t==T_INT,(double)v->j)
    P(v->t==T_CHAR,(double)v->j)
    P(v->t==T_FLOAT,v->f)
    P(v->t==T_BOOL,(double)v->j)
    return 0;
}
/* / // %: a zero divisor anywhere in a vector or matrix is the scalar's error, not a 0 item. The
 * kernels flag a zero divisor as they go; the error comes after the loop, so the operands' other
 * errors (length, shape, type) come first. */
static int is_div_op(int op) { return op == OP_DIV || op == OP_FLOORDIV || op == OP_MOD; }
static V *div_by_zero(int op) { return v_err(op == OP_MOD ? "modulo by zero" : "division by zero"); }
static int is_zero_num(V *v) {
    return (v->t == T_INT || v->t == T_CHAR) ? v->j == 0 : v->t == T_FLOAT && v->f == 0;
}
/* r, or the error (r freed) when a divisor was zero */
static V *div_result(V *r, int zero, int op) {
    if (!zero) return r;
    v_free(r);
    return div_by_zero(op);
}
static double i64_floordiv_f(int64_t x, int64_t y) {
    if (!y) return 0;
    if (i64_pair_promotes(x, y)) return -(double)INT64_MIN;
    return (double)(x / y);
}
static int i64_span_promotes(const int64_t *xs, const int64_t *ys, int64_t n,
                             int xsc, int64_t x, int ysc, int64_t y) {
    for (int64_t i = 0; i < n; i++) {
        int64_t a = xsc ? x : xs[i];
        int64_t b = ysc ? y : ys[i];
        if (i64_pair_promotes(a, b)) return 1;
    }
    return 0;
}
static void i64_arith_f(double *r, const int64_t *a, int64_t xa, const int64_t *b, int64_t yb, int64_t n, int op) {
    for (int64_t i = 0; i < n; i++) {
        double x = (double)(a ? a[i] : xa), y = (double)(b ? b[i] : yb);
        r[i] = op == OP_ADD ? x + y : op == OP_SUB ? x - y : x * y;
    }
}
/* Exact integer power via squaring. Sets *z = x ** y and returns 1 when y >= 0 and the power fits
 * an int; otherwise returns 0 and the caller falls back to float, so we never emit a rounded
 * integer (pow() on doubles loses precision above 2^53, e.g. 5**27). */
static int i64_pow(int64_t x, int64_t y, int64_t *z) {
    int64_t base = x, exp = y, acc = 1;
    if (y < 0) return 0;
    while (exp > 0) {
        if ((exp & 1) && __builtin_mul_overflow(acc, base, &acc)) return 0;
        exp >>= 1;
        if (exp && __builtin_mul_overflow(base, base, &base)) return 0;
    }
    *z = acc;
    return 1;
}
/* Int ** int item by item, as for two ints: an int vector or matrix shaped like `like` while every
 * power is exact, else (a negative exponent or an overflow) the whole result is float, each item
 * the float of its power. xs/ys are the operands' items (NULL: the scalar x/y). */
static V *i64_pow_items(V *like, const int64_t *xs, int64_t x, const int64_t *ys, int64_t y) {
    int mat = is_mat_t(like->t);
    int64_t ne = mat ? like->n * mat_cols(like) : like->n, i = 0, z;
    V *r = mat ? v_imat(like->n, mat_cols(like)) : v_ivec(like->n);
    while (i < ne && i64_pow(xs ? xs[i] : x, ys ? ys[i] : y, &r->J[i])) i++;
    if (i == ne) return r;
    v_free(r);
    r = mat ? v_fmat(like->n, mat_cols(like)) : v_fvec(like->n);
    for (i = 0; i < ne; i++) {
        int64_t a = xs ? xs[i] : x, b = ys ? ys[i] : y;
        r->F[i] = i64_pow(a, b, &z) ? (double)z : pow((double)a, (double)b);
    }
    return r;
}
V *mat_binop(V *a, V *b, int op) {
    if (!is_mat_t(a->t) && !is_mat_t(b->t)) return NULL;
    if (a->t == T_BMAT || b->t == T_BMAT)
        return v_err("arithmetic not supported on matrix[bool]");
    if (a->t == T_CMAT || b->t == T_CMAT)
        return v_err("arithmetic not supported on matrix[char]");
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT || b->t == T_CHAR)) {
        /* an int or float matrix over a zero number has no other error to give first */
        if (is_div_op(op) && is_zero_num(b)) return div_by_zero(op);
        int64_t rows = a->n, cols = mat_cols(a), ne = rows * cols;
        if (op == OP_POW && a->t == T_IMAT && b->t != T_FLOAT)
            return i64_pow_items(a, a->J, 0, NULL, b->j);
        /* / is float, as for two matrices and for vectors; the int kernel does not divide. */
        int out_t = (a->t == T_FMAT || b->t == T_FLOAT || op == OP_DIV) ? T_FMAT : T_IMAT;
        if (out_t == T_IMAT && op == OP_FLOORDIV && a->t == T_IMAT &&
            i64_span_promotes(a->J, NULL, ne, 0, 0, 1, b->j))
            out_t = T_FMAT;
        V *r = out_t == T_FMAT ? (V *)v_fmat(rows, cols) : (V *)v_imat(rows, cols);
        double y = to_float(b);
        if (out_t == T_FMAT && a->t == T_FMAT)
            mat_fmat_binop_scalar(r->F, a->F, y, ne, op);
        else if (out_t == T_IMAT && a->t == T_IMAT) {
            int64_t yi = b->t == T_INT ? b->j : (int64_t)y;
            if (mat_imat_binop_scalar(r->J, a->J, yi, ne, op)) {
                v_free(r);
                r = v_fmat(rows, cols);
                i64_arith_f(r->F, a->J, 0, NULL, yi, ne, op);
            }
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
    if ((a->t == T_INT || a->t == T_FLOAT || a->t == T_CHAR) && is_mat_t(b->t)) {
        /* x op m is m op x when op commutes; the others are computed here, x on the left. */
        if (op == OP_SUB || op == OP_DIV || op == OP_FLOORDIV || op == OP_MOD || op == OP_POW) {
            int zero = 0;
            int64_t rows = b->n, cols = mat_cols(b), ne = rows * cols;
            if (op == OP_POW && b->t == T_IMAT && a->t != T_FLOAT)
                return i64_pow_items(b, NULL, a->j, b->J, 0);
            if (b->t == T_FMAT || a->t == T_FLOAT || op == OP_DIV) {
                V *out = v_fmat(rows, cols);
                double x = to_float(a);
                if (b->t == T_FMAT) {
                    zero = mat_fmat_binop_scalar_rev(out->F, x, b->F, ne, op);
                } else {
                    for (int64_t i = 0; i < ne; i++) {
                        double yv = (double)b->J[i];
                        zero |= is_div_op(op) && yv == 0;
                        if (op == OP_SUB) out->F[i] = x - yv;
                        else if (op == OP_DIV) out->F[i] = yv != 0 ? x / yv : 0;
                        else if (op == OP_FLOORDIV) out->F[i] = yv != 0 ? floor(x / yv) : 0;
                        else if (op == OP_MOD) out->F[i] = yv != 0 ? fmod(x, yv) : 0;
                        else out->F[i] = pow(x, yv);
                    }
                }
                return div_result(out, zero, op);
            }
            if (op == OP_FLOORDIV && a->t != T_FLOAT &&
                i64_span_promotes(NULL, b->J, ne, 1, a->j, 0, 0)) {
                V *out = v_fmat(rows, cols);
                for (int64_t i = 0; i < ne; i++) {
                    zero |= b->J[i] == 0;
                    out->F[i] = i64_floordiv_f(a->j, b->J[i]);
                }
                return div_result(out, zero, op);
            }
            V *out = v_imat(rows, cols);
            int64_t x = a->j;
            if (mat_imat_binop_scalar_rev(out->J, x, b->J, ne, op)) {
                v_free(out);
                if (is_div_op(op)) return div_by_zero(op);  /* a zero divisor */
                out = v_fmat(rows, cols);                     /* - overflowed */
                i64_arith_f(out->F, NULL, x, b->J, 0, ne, op);
            }
            return out;
        }
        return mat_binop(b, a, op);
    }
    if (!is_mat_t(a->t) || !is_mat_t(b->t))
        return v_errf("unsupported operand types for %s: %s and %s",
                      op == OP_ADD ? "+" : op == OP_SUB ? "-" : op == OP_MUL ? "*" :
                      op == OP_DIV ? "/" : op == OP_FLOORDIV ? "//" : op == OP_MOD ? "%" :
                      op == OP_POW ? "**" : "that operator",
                      type_name(a->t), type_name(b->t));
    if (a->n != b->n || mat_cols(a) != mat_cols(b))
        return v_err("matrix shape mismatch");
    if (op == OP_POW && a->t == T_IMAT && b->t == T_IMAT)
        return i64_pow_items(a, a->J, 0, b->J, 0);
    int64_t ne = a->n * mat_cols(a);
    int out_t = (a->t == T_FMAT || b->t == T_FMAT) ? T_FMAT : T_IMAT;
    if (op == OP_DIV) out_t = T_FMAT;
    if (out_t == T_IMAT && op == OP_FLOORDIV &&
        i64_span_promotes(a->J, b->J, ne, 0, 0, 0, 0))
        out_t = T_FMAT;
    V *r = out_t == T_FMAT ? (V *)v_fmat(a->n, mat_cols(a)) : (V *)v_imat(a->n, mat_cols(a));
    int zero = 0;
    if (out_t == T_FMAT && a->t == T_FMAT && b->t == T_FMAT)
        zero = mat_fmat_binop_mm(r->F, a->F, b->F, ne, op);
    else if (out_t == T_IMAT && a->t == T_IMAT && b->t == T_IMAT) {
        if (mat_imat_binop_mm(r->J, a->J, b->J, ne, op)) {
            if (is_div_op(op))
                zero = 1;  /* a zero divisor */
            else {         /* + - * overflowed */
                int64_t rows = a->n, cols = mat_cols(a);
                v_free(r);
                r = v_fmat(rows, cols);
                i64_arith_f(r->F, a->J, 0, b->J, 0, ne, op);
            }
        }
    }
    else {
        for (int64_t i = 0; i < ne; i++) {
            if (out_t == T_FMAT) {
                double x = a->t == T_IMAT ? (double)a->J[i] : a->F[i];
                double y = b->t == T_IMAT ? (double)b->J[i] : b->F[i];
                switch (op) {
                case OP_ADD: r->F[i] = x + y; break;
                case OP_SUB: r->F[i] = x - y; break;
                case OP_MUL: r->F[i] = x * y; break;
                case OP_DIV: zero |= y == 0; r->F[i] = y != 0 ? x / y : 0; break;
                case OP_FLOORDIV: zero |= y == 0; r->F[i] = y != 0 ? floor(x / y) : 0; break;
                case OP_MOD: zero |= y == 0; r->F[i] = y != 0 ? fmod(x, y) : 0; break;
                case OP_POW: r->F[i] = pow(x, y); break;
                default: break;
                }
            } else {
                int64_t x = a->J[i], y = b->J[i];
                switch (op) {
                case OP_ADD: r->J[i] = x + y; break;
                case OP_SUB: r->J[i] = x - y; break;
                case OP_MUL: r->J[i] = x * y; break;
                case OP_FLOORDIV: zero |= y == 0; r->J[i] = i64_floordiv(x, y); break;
                case OP_MOD: zero |= y == 0; r->J[i] = i64_mod(x, y); break;
                default: break;
                }
            }
        }
    }
    return div_result(r, zero, op);
}
static int is_arith_vec(int t) {
    return t == T_IVEC || t == T_FVEC || t == T_CVEC;
}
V *vec_binop(V *a, V *b, int op) {
    if (is_mat_t(a->t) || is_mat_t(b->t)) {
        V *r = mat_binop(a, b, op);
        if (r) return r;
    }
    /* Scalars broadcast. Two vectors must be the same length. */
    if (is_arith_vec(a->t) && is_arith_vec(b->t) && a->n != b->n)
        return v_err("length mismatch");
    if (op == OP_POW && (a->t == T_IVEC || b->t == T_IVEC) &&
        (a->t == T_IVEC || a->t == T_INT) && (b->t == T_IVEC || b->t == T_INT))
        return i64_pow_items(a->t == T_IVEC ? a : b, a->t == T_IVEC ? a->J : NULL, a->t == T_INT ? a->j : 0,
                             b->t == T_IVEC ? b->J : NULL, b->t == T_INT ? b->j : 0);
    if (a->t == T_IVEC && b->t == T_IVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n;
        V *r = v_ivec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            if (mat_imat_binop_mm(r->J, a->J, b->J, n, op)) {
                v_free(r);
                r = v_fvec(n);
                i64_arith_f(r->F, a->J, 0, b->J, 0, n, op);
            }
            return r;
        }
        const int64_t *aj = a->J, *bj = b->J;
        int64_t *rj = r->J;
        int zero = 0;
        switch (op) {
        case OP_FLOORDIV:
            if (i64_span_promotes(aj, bj, n, 0, 0, 0, 0)) {
                V *fr = v_fvec(n);
                for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; fr->F[i] = i64_floordiv_f(aj[i], bj[i]); }
                v_free(r);
                return div_result(fr, zero, op);
            }
            for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; rj[i] = i64_floordiv(aj[i], bj[i]); }
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; rj[i] = i64_mod(aj[i], bj[i]); }
            break;
        default: break;
        }
        return div_result(r, zero, op);
    }
    if (a->t == T_CVEC && b->t == T_CVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n;
        V *r = v_cvec(n);
        const unsigned char *ac = a->B, *bc = b->B;
        unsigned char *rc = r->B;
        int zero = 0;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] + bc[i]; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] - bc[i]; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] * bc[i]; break;
        case OP_FLOORDIV: for (int64_t i = 0; i < n; i++) { zero |= !bc[i]; rc[i] = bc[i] ? ac[i] / bc[i] : 0; } break;
        case OP_MOD: for (int64_t i = 0; i < n; i++) { zero |= !bc[i]; rc[i] = bc[i] ? ac[i] % bc[i] : 0; } break;
        default: break;
        }
        return div_result(r, zero, op);
    }
    if (a->t == T_IVEC && b->t == T_INT && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n, y = b->j;
        if (!y && is_div_op(op)) return div_by_zero(op);
        V *r = v_ivec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            if (mat_imat_binop_scalar(r->J, a->J, y, n, op)) {
                v_free(r);
                r = v_fvec(n);
                i64_arith_f(r->F, a->J, 0, NULL, y, n, op);
            }
            return r;
        }
        const int64_t *aj = a->J;
        int64_t *rj = r->J;
        switch (op) {
        case OP_FLOORDIV:
            if (i64_span_promotes(aj, NULL, n, 0, 0, 1, y)) {
                V *fr = v_fvec(n);
                for (int64_t i = 0; i < n; i++) fr->F[i] = i64_floordiv_f(aj[i], y);
                v_free(r);
                return fr;
            }
            for (int64_t i = 0; i < n; i++) rj[i] = i64_floordiv(aj[i], y);
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) rj[i] = i64_mod(aj[i], y);
            break;
        default: break;
        }
        return r;
    }
    if (a->t == T_CVEC && (b->t == T_INT || b->t == T_CHAR) && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n, y = b->j;
        if (!y && is_div_op(op)) return div_by_zero(op);
        V *r = v_cvec(n);
        const unsigned char *ac = a->B;
        unsigned char *rc = r->B;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] + (uint64_t)y; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] - (uint64_t)y; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] * (uint64_t)y; break;
        case OP_FLOORDIV: for (int64_t i = 0; i < n; i++) rc[i] = y ? ac[i] / y : 0; break;
        case OP_MOD: for (int64_t i = 0; i < n; i++) rc[i] = y ? ac[i] % y : 0; break;
        default: break;
        }
        return r;
    }
    if (a->t == T_INT && b->t == T_IVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = b->n, x = a->j;
        V *r = v_ivec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            if (mat_imat_binop_scalar_rev(r->J, x, b->J, n, op)) {
                v_free(r);
                r = v_fvec(n);
                i64_arith_f(r->F, NULL, x, b->J, 0, n, op);
            }
            return r;
        }
        const int64_t *bj = b->J;
        int64_t *rj = r->J;
        int zero = 0;
        switch (op) {
        case OP_FLOORDIV:
            if (i64_span_promotes(NULL, bj, n, 1, x, 0, 0)) {
                V *fr = v_fvec(n);
                for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; fr->F[i] = i64_floordiv_f(x, bj[i]); }
                v_free(r);
                return div_result(fr, zero, op);
            }
            for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; rj[i] = i64_floordiv(x, bj[i]); }
            break;
        case OP_MOD:
            for (int64_t i = 0; i < n; i++) { zero |= bj[i] == 0; rj[i] = i64_mod(x, bj[i]); }
            break;
        default: break;
        }
        return div_result(r, zero, op);
    }
    if ((a->t == T_INT || a->t == T_CHAR) && b->t == T_CVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = b->n, x = a->j;
        V *r = v_cvec(n);
        const unsigned char *bc = b->B;
        unsigned char *rc = r->B;
        int zero = 0;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = (uint64_t)x + bc[i]; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = (uint64_t)x - bc[i]; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = (uint64_t)x * bc[i]; break;
        case OP_FLOORDIV: for (int64_t i = 0; i < n; i++) { zero |= !bc[i]; rc[i] = bc[i] ? x / bc[i] : 0; } break;
        case OP_MOD: for (int64_t i = 0; i < n; i++) { zero |= !bc[i]; rc[i] = bc[i] ? x % bc[i] : 0; } break;
        default: break;
        }
        return div_result(r, zero, op);
    }
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_CHAR) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_CHAR)) {
        int use_int = (a->t!=T_FLOAT && b->t!=T_FLOAT && op!=OP_DIV);
        if(use_int) {
            int64_t x=a->j, y=b->j;
            switch(op) {
            case OP_ADD: {
                int64_t z;
                if(__builtin_add_overflow(x, y, &z)) return v_float((double)x+(double)y);
                return v_int(z);
            }
            case OP_SUB: {
                int64_t z;
                if(__builtin_sub_overflow(x, y, &z)) return v_float((double)x-(double)y);
                return v_int(z);
            }
            case OP_MUL: {
                int64_t z;
                if(__builtin_mul_overflow(x, y, &z)) return v_float((double)x*(double)y);
                return v_int(z);
            }
            case OP_FLOORDIV:
                if(!y) return v_err("division by zero");
                /* INT64_MIN / -1 overflows int64 (UB in C); promote to float. */
                if(x==INT64_MIN && y==-1) return v_float(-(double)INT64_MIN);
                return v_int(x/y);
            case OP_MOD:
                if(!y) return v_err("modulo by zero");
                if(x==INT64_MIN && y==-1) return v_int(0);
                return v_int(x%y);
            case OP_POW: {
                int64_t z;
                if(i64_pow(x, y, &z)) return v_int(z);
                return v_float(pow((double)x, (double)y));
            }
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
        size_t need = 0;
        if (__builtin_add_overflow(la, lb, &need) || __builtin_add_overflow(need, (size_t)1, &need))
            return v_err("string concat too large");
        char *r = malloc(need);
        if (!r) return v_err("out of memory");
        memcpy(r, a->s, la); memcpy(r+la, b->s, lb); r[la+lb]=0;
        V *v = v_str(r); free(r); return v;
    }
    if(a->t==T_STR && b->t==T_INT && op==OP_MUL) {
        int64_t n = b->j;
        size_t slen = strlen(a->s);
        if (n <= 0 || slen == 0) return v_str("");
        if ((uint64_t)n > (SIZE_MAX - 1) / slen)
            return v_err("string repeat too large");
        size_t total = slen * (size_t)n;
        char *r = malloc(total + 1);
        if (!r) return v_err("out of memory");
        for (size_t i = 0; i < (size_t)n; i++)
            memcpy(r + i * slen, a->s, slen);
        r[total] = 0;
        V *v = v_str(r);
        free(r);
        return v;
    }
    P(a->t==T_INT && b->t==T_STR && op==OP_MUL,vec_binop(b, a, op))
    if (a->t == T_FVEC && b->t == T_FVEC) {
        int64_t n = a->n;
        V *r = v_fvec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV ||
            op == OP_FLOORDIV || op == OP_MOD || op == OP_POW) {
            int zero = mat_fmat_binop_mm(r->F, a->F, b->F, n, op);
            return div_result(r, zero, op);
        }
    }
    if (a->t == T_FVEC && (b->t == T_INT || b->t == T_FLOAT)) {
        int64_t n = a->n;
        double y = to_float(b);
        if (y == 0 && is_div_op(op)) return div_by_zero(op);
        V *r = v_fvec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV ||
            op == OP_FLOORDIV || op == OP_MOD || op == OP_POW) {
            mat_fmat_binop_scalar(r->F, a->F, y, n, op);
            return r;
        }
    }
    if ((a->t == T_INT || a->t == T_FLOAT) && b->t == T_FVEC) {
        int64_t n = b->n;
        double x = to_float(a);
        V *r = v_fvec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV ||
            op == OP_FLOORDIV || op == OP_MOD || op == OP_POW) {
            int zero = mat_fmat_binop_scalar_rev(r->F, x, b->F, n, op);
            return div_result(r, zero, op);
        }
    }
    #define VEC_BIN(AT,BT,AJ,BJ) \
    if(a->t==AT && b->t==BT) { \
        int64_t n=a->n; \
        int ui=(AT==T_IVEC&&BT==T_IVEC&&op!=OP_DIV&&op!=OP_POW), zero=0; \
        if(ui){ V*r=v_ivec(n); for(int64_t i=0;i<n;i++){int64_t x=AJ[i],y=BJ[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:zero|=!y;r->J[i]=i64_floordiv(x,y);break; \
            case OP_MOD:zero|=!y;r->J[i]=i64_mod(x,y);break;default:break;}} return div_result(r,zero,op); } \
        V*r=v_fvec(n); for(int64_t i=0;i<n;i++){double x=AT==T_IVEC?(double)a->J[i]:a->F[i], \
            y=BT==T_IVEC?(double)b->J[i]:b->F[i]; \
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break; \
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:zero|=y==0;r->F[i]=y!=0?x/y:0;break; \
            case OP_FLOORDIV:zero|=y==0;r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:zero|=y==0;r->F[i]=y!=0?fmod(x,y):0;break; \
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return div_result(r,zero,op); }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_IVEC||b->t==T_FVEC)) {
        VEC_BIN(a->t, b->t, a->J, b->J)
    }
    if((a->t==T_INT||a->t==T_FLOAT) && (b->t==T_IVEC||b->t==T_FVEC)) {
        int64_t n=b->n;
        int ui=(a->t==T_INT&&b->t==T_IVEC&&op!=OP_DIV&&op!=OP_POW), zero=0;
        if(ui){ V*r=v_ivec(n); int64_t x=a->j; for(int64_t i=0;i<n;i++){int64_t y=b->J[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:zero|=!y;r->J[i]=i64_floordiv(x,y);break; \
            case OP_MOD:zero|=!y;r->J[i]=i64_mod(x,y);break;default:break;}} return div_result(r,zero,op); }
        V*r=v_fvec(n); double x=to_float(a);
        for(int64_t i=0;i<n;i++){double y=b->t==T_IVEC?(double)b->J[i]:b->F[i];
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break;
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:zero|=y==0;r->F[i]=y!=0?x/y:0;break;
            case OP_FLOORDIV:zero|=y==0;r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:zero|=y==0;r->F[i]=y!=0?fmod(x,y):0;break;
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return div_result(r,zero,op);
    }
    if((a->t==T_IVEC||a->t==T_FVEC) && (b->t==T_INT||b->t==T_FLOAT)) {
        int64_t n=a->n;
        if(is_div_op(op) && is_zero_num(b)) return div_by_zero(op);
        int ui=(a->t==T_IVEC&&b->t==T_INT&&op!=OP_DIV&&op!=OP_POW);
        if(ui){ V*r=v_ivec(n); int64_t y=b->j; for(int64_t i=0;i<n;i++){int64_t x=a->J[i]; \
            switch(op){case OP_ADD:r->J[i]=x+y;break;case OP_SUB:r->J[i]=x-y;break; \
            case OP_MUL:r->J[i]=x*y;break;case OP_FLOORDIV:r->J[i]=i64_floordiv(x,y);break; \
            case OP_MOD:r->J[i]=i64_mod(x,y);break;default:break;}} return r; }
        V*r=v_fvec(n); double y=to_float(b);
        for(int64_t i=0;i<n;i++){double x=a->t==T_IVEC?(double)a->J[i]:a->F[i];
            switch(op){case OP_ADD:r->F[i]=x+y;break;case OP_SUB:r->F[i]=x-y;break;
            case OP_MUL:r->F[i]=x*y;break;case OP_DIV:r->F[i]=y!=0?x/y:0;break;
            case OP_FLOORDIV:r->F[i]=y!=0?floor(x/y):0;break;case OP_MOD:r->F[i]=y!=0?fmod(x,y):0;break;
            case OP_POW:r->F[i]=pow(x,y);break;default:break;}} return r;
    }
    if(a->t==T_LIST && b->t==T_LIST && op==OP_ADD) {
        int64_t sum = 0;
        if (__builtin_add_overflow(a->n, b->n, &sum) || sum < 0)
            return v_err("list concat too large");
        V *r = v_list(sum);
        for(int64_t i=0;i<a->n;i++) r->L[i] = v_ref(a->L[i]);
        for(int64_t i=0;i<b->n;i++) r->L[a->n+i] = v_ref(b->L[i]);
        return r;
    }
    return v_errf("unsupported operand types for %s: %s and %s",
                  op == OP_EQ ? "=" : op == OP_NE ? "!=" : op == OP_LT ? "<" :
                  op == OP_GT ? ">" : op == OP_LE ? "<=" : op == OP_GE ? ">=" :
                  op == OP_ADD ? "+" : op == OP_SUB ? "-" : op == OP_MUL ? "*" :
                  op == OP_DIV ? "/" : op == OP_FLOORDIV ? "//" : op == OP_MOD ? "%" :
                  op == OP_POW ? "**" : "that operator",
                  type_name(a->t), type_name(b->t));
    #undef VEC_BIN
}
/* Numbers compare by their exact values, as Python's do: an int (bool, char) with an int as
 * int64, an int with a float without rounding the int. Three-way: -1, 0, 1, or 2 when unordered. */
static int cmp_i64_f64(int64_t x, double y) {
    if (y != y) return 2;
    double d = (double)x;
    if (d != y) return d < y ? -1 : 1;
    if (y >= 9223372036854775808.0) return -1;   /* y is 2^63: x only rounded up to it */
    int64_t k = (int64_t)y;
    return x < k ? -1 : x > k;
}
/* item i of a number or numeric vector/matrix: 1 and *f for a float, 0 and *j otherwise */
static int num_at(V *v, int64_t i, int64_t *j, double *f) {
    switch(v->t) {
    case T_FLOAT: *f = v->f; return 1;
    case T_FVEC: case T_FMAT: *f = v->F[i]; return 1;
    case T_IVEC: case T_IMAT: *j = v->J[i]; return 0;
    case T_CVEC: case T_BVEC: case T_CMAT: case T_BMAT: *j = v->B[i]; return 0;
    default: *j = v->j; return 0;
    }
}
static int num_cmp(V *a, int64_t i, V *b, int64_t k, int op) {
    int64_t x = 0, y = 0; double xf = 0, yf = 0;
    int af = num_at(a, i, &x, &xf), bf = num_at(b, k, &y, &yf), c;
    if (af && bf) c = xf < yf ? -1 : xf > yf ? 1 : xf == yf ? 0 : 2;
    else if (af) c = -cmp_i64_f64(y, xf);
    else if (bf) c = cmp_i64_f64(x, yf);
    else c = x < y ? -1 : x > y;
    switch(op) {
    case OP_EQ: return c==0;  case OP_NE: return c!=0;
    case OP_LT: return c==-1; case OP_GT: return c==1;
    case OP_LE: return c==-1||c==0; case OP_GE: return c==0||c==1;
    default: return 0;
    }
}
static int cmp_mirror(int op) {
    return op==OP_LT?OP_GT:op==OP_GT?OP_LT:op==OP_LE?OP_GE:op==OP_GE?OP_LE:op;
}
/* r[i] = X op Y for i < n, X and Y written in terms of i_, with the switch outside the loop */
#define CMP_EACH(r, n, X, Y, op) do { int64_t i_; switch(op) { \
    case OP_EQ: for(i_=0;i_<(n);i_++) (r)[i_] = (X) == (Y); break; \
    case OP_NE: for(i_=0;i_<(n);i_++) (r)[i_] = (X) != (Y); break; \
    case OP_LT: for(i_=0;i_<(n);i_++) (r)[i_] = (X) < (Y); break; \
    case OP_GT: for(i_=0;i_<(n);i_++) (r)[i_] = (X) > (Y); break; \
    case OP_LE: for(i_=0;i_<(n);i_++) (r)[i_] = (X) <= (Y); break; \
    case OP_GE: for(i_=0;i_<(n);i_++) (r)[i_] = (X) >= (Y); break; \
    default: break; } } while(0)
/* A float y against every int x, as an int bound: x op y is x *op2 *k (returns -1), or is the same
 * for every x (returns 1 for true, 0 for false). */
static int int_bound(double y, int op, int *op2, int64_t *k) {
    if (y != y) return op == OP_NE;
    if (y >= 9223372036854775808.0) return op == OP_NE || op == OP_LT || op == OP_LE;
    if (y < -9223372036854775808.0) return op == OP_NE || op == OP_GT || op == OP_GE;
    int64_t t = (int64_t)y;                  /* y rounded toward 0 */
    *op2 = op;
    if ((double)t == y) { *k = t; return -1; }
    switch (op) {
    case OP_LT: case OP_LE: *op2 = OP_LE; *k = y < 0 ? t - 1 : t; return -1;   /* x <= floor(y) */
    case OP_GT: case OP_GE: *op2 = OP_GE; *k = y < 0 ? t : t + 1; return -1;   /* x >= ceil(y) */
    default: return op == OP_NE;
    }
}
/* A numeric vector or matrix a (ne items) against a number b, op as seen from a. The number is
 * converted once, exactly, and the plain float or int loop runs: an int (bool, char) within 2^53
 * becomes a double, a float an int bound. Only an int beyond 2^53 against floats goes item by item. */
static void nums_cmp_num(unsigned char *r, V *a, int64_t ne, V *b, int op) {
    if (a->t == T_FVEC || a->t == T_FMAT) {
        double y;
        if (b->t == T_FLOAT) y = b->f;
        else if (b->j >= -9007199254740992LL && b->j <= 9007199254740992LL) y = (double)b->j;
        else { for (int64_t i = 0; i < ne; i++) r[i] = num_cmp(a, i, b, 0, op); return; }
        if (a->t == T_FMAT) mat_fmat_cmp_bmat_scalar(r, a->F, y, ne, op);
        else { const double *x = a->F; CMP_EACH(r, ne, x[i_], y, op); }
        return;
    }
    int64_t k = b->j;
    if (b->t == T_FLOAT) {
        int all = int_bound(b->f, op, &op, &k);
        if (all >= 0) { memset(r, all, (size_t)ne); return; }
    }
    if (a->t == T_IMAT) mat_imat_cmp_bmat_scalar(r, a->J, k, ne, op);
    else if (a->t == T_IVEC) { const int64_t *x = a->J; CMP_EACH(r, ne, x[i_], k, op); }
    else { const unsigned char *x = a->B; CMP_EACH(r, ne, (int64_t)x[i_], k, op); }
}
/* the item kind a numeric vector or matrix keeps: J (int64), F (double) or B (bytes) */
static int num_kind(int t) {
    return t == T_IVEC || t == T_IMAT ? 'J' : t == T_FVEC || t == T_FMAT ? 'F' : 'B';
}
/* Two numeric vectors or matrices of ne items, item by item: same kinds in a plain loop. */
static void nums_cmp_nums(unsigned char *r, V *a, V *b, int64_t ne, int op) {
    int ka = num_kind(a->t);
    if (ka != num_kind(b->t)) { for (int64_t i = 0; i < ne; i++) r[i] = num_cmp(a, i, b, i, op); return; }
    if (ka == 'J') mat_imat_cmp_bmat_mm(r, a->J, b->J, ne, op);
    else if (ka == 'F') mat_fmat_cmp_bmat_mm(r, a->F, b->F, ne, op);
    else { const unsigned char *x = a->B, *y = b->B; CMP_EACH(r, ne, x[i_], y[i_], op); }
}
/* whether two numeric vectors or matrices of ne items are equal item by item */
static int nums_equal(V *a, V *b, int64_t ne) {
    int ka = num_kind(a->t);
    if (ka != num_kind(b->t)) {
        for (int64_t i = 0; i < ne; i++) P(!num_cmp(a, i, b, i, OP_EQ), 0)
        return 1;
    }
    if (ka == 'B') return memcmp(a->B, b->B, (size_t)ne) == 0;
    if (ka == 'J') { for (int64_t i = 0; i < ne; i++) P(a->J[i] != b->J[i], 0) return 1; }
    for (int64_t i = 0; i < ne; i++) P(a->F[i] != b->F[i], 0)
    return 1;
}
#undef CMP_EACH
/* whether the number b equals some item of the numeric vector a, by exact value as `=` compares
 * them: b is converted once, as for a comparison against every item */
int nums_have_num(V *a, V *b) {
    int64_t ne = a->n;
    if (a->t == T_FVEC) {
        if (b->t == T_FLOAT || (b->j >= -9007199254740992LL && b->j <= 9007199254740992LL)) {
            double y = b->t == T_FLOAT ? b->f : (double)b->j;
            for (int64_t i = 0; i < ne; i++) P(a->F[i] == y, 1)
            return 0;
        }
        for (int64_t i = 0; i < ne; i++) P(num_cmp(a, i, b, 0, OP_EQ), 1)
        return 0;
    }
    int64_t k = b->j; int op = OP_EQ;
    if (b->t == T_FLOAT && int_bound(b->f, OP_EQ, &op, &k) >= 0) return 0;   /* NaN, not whole, or no int */
    if (a->t == T_IVEC) { for (int64_t i = 0; i < ne; i++) P(a->J[i] == k, 1) return 0; }
    P(k < 0 || k > 255, 0)
    return ne > 0 && memchr(a->B, (int)k, (size_t)ne) != NULL;
}
/* whether some row of the numeric matrix m (m->n rows of a->n items) equals a, a numeric vector or
 * a list whose items are numbers, bools or chars, item by item by exact value as `=` compares them;
 * the kinds are tested once, so rows of the same kind compare in a plain loop */
int nums_have_row(V *m, V *a) {
    int64_t ne = a->n, rows = m->n;
    int km = num_kind(m->t);
    if (a->t == T_LIST || km != num_kind(a->t)) {
        for (int64_t r = 0; r < rows; r++) {
            int64_t base = r * ne, k = 0;
            if (a->t == T_LIST) while (k < ne && num_cmp(m, base + k, a->L[k], 0, OP_EQ)) k++;
            else while (k < ne && num_cmp(m, base + k, a, k, OP_EQ)) k++;
            P(k == ne, 1)
        }
        return 0;
    }
    if (km == 'B') {
        for (int64_t r = 0; r < rows; r++) P(ne == 0 || memcmp(m->B + r * ne, a->B, (size_t)ne) == 0, 1)
        return 0;
    }
    if (km == 'J') {
        const int64_t *x = a->J;
        for (int64_t r = 0; r < rows; r++) {
            const int64_t *y = m->J + r * ne; int64_t k = 0;
            while (k < ne && y[k] == x[k]) k++;
            P(k == ne, 1)
        }
        return 0;
    }
    const double *x = a->F;
    for (int64_t r = 0; r < rows; r++) {
        const double *y = m->F + r * ne; int64_t k = 0;
        while (k < ne && y[k] == x[k]) k++;
        P(k == ne, 1)
    }
    return 0;
}
static int cmp_depth;
static void cmp_depth_leave(int *p) { (void)p; if (cmp_depth > 0) cmp_depth--; }
V *vec_cmp(V *a, V *b, int op) {
    if (cmp_depth >= 2048) return v_err("comparison too deep");
    cmp_depth++;
    int cmp_guard __attribute__((cleanup(cmp_depth_leave))) = 1;
    (void)cmp_guard;
    if (a == b) {
        if (op == OP_EQ) return v_bool(1);
        if (op == OP_NE) return v_bool(0);
    }
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_BOOL||a->t==T_CHAR) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_BOOL||b->t==T_CHAR)) {
        return v_bool(num_cmp(a, 0, b, 0, op));
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
    if((a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_CHAR)) {
        V *r=v_bvec(a->n);
        nums_cmp_num(r->B, a, a->n, b, op);
        return r;
    }
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_CHAR) && (b->t==T_IVEC||b->t==T_FVEC||b->t==T_CVEC)) {
        V *r=v_bvec(b->n);
        nums_cmp_num(r->B, b, b->n, a, cmp_mirror(op));
        return r;
    }
    if((a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC) && (b->t==T_IVEC||b->t==T_FVEC||b->t==T_CVEC) && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        return v_bool(nums_equal(a, b, a->n) == (op==OP_EQ));
    }
    if(a->t==T_LIST && (b->t==T_LIST||b->t==T_IVEC||b->t==T_FVEC||b->t==T_CVEC) && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        for(int64_t i=0;i<a->n;i++) {
            V *belem;
            if(b->t==T_LIST) belem = b->L[i];
            else if(b->t==T_IVEC) belem = v_int(b->J[i]);
            else if(b->t==T_CVEC) belem = v_char(b->B[i]);
            else belem = v_float(b->F[i]);
            V *c = vec_cmp(a->L[i], belem, OP_EQ);
            if (c && c->t == T_ERR) { if (b->t != T_LIST) v_free(belem); return c; }
            int eq = c->t==T_BOOL && c->j;
            v_free(c); if(b->t != T_LIST) v_free(belem);
            P(!eq,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
    }
    if((a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC) && b->t==T_LIST && (op==OP_EQ||op==OP_NE)) {
        return vec_cmp(b, a, op);
    }
    if(a->t==T_LIST && b->t==T_STR) {
        int64_t n = a->n;
        V *r = v_bvec(n);
        for(int64_t i = 0; i < n; i++) {
            V *c = vec_cmp(a->L[i], b, op);
            r->B[i] = (c->t == T_BOOL && c->j) ? 1 : 0;
            v_free(c);
        }
        return r;
    }
    if(a->t==T_STR && b->t==T_LIST) {
        int64_t n = b->n;
        V *r = v_bvec(n);
        for(int64_t i = 0; i < n; i++) {
            V *c = vec_cmp(a, b->L[i], op);
            r->B[i] = (c->t == T_BOOL && c->j) ? 1 : 0;
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
                if (kc && kc->t == T_ERR) return kc;
                int keq = kc->t==T_BOOL && kc->j;
                v_free(kc);
                if(!keq) continue;
                V *vc = vec_cmp(av, b->vals->L[j], OP_EQ);
                if (vc && vc->t == T_ERR) return vc;
                int veq = vc->t==T_BOOL && vc->j;
                v_free(vc);
                P(!veq,v_bool(op==OP_NE))
                found = 1;
                break;
            }
            P(!found,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
    }
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT || b->t == T_BOOL || b->t == T_CHAR)) {
        int64_t ne = a->n * mat_cols(a);
        V *r = v_bmat(a->n, mat_cols(a));
        nums_cmp_num(r->B, a, ne, b, op);
        return r;
    }
    if ((a->t == T_INT || a->t == T_FLOAT || a->t == T_BOOL || a->t == T_CHAR) && is_mat_t(b->t))
        return vec_cmp(b, a, op == OP_LT ? OP_GT : op == OP_GT ? OP_LT :   /* x < m is m > x */
                             op == OP_LE ? OP_GE : op == OP_GE ? OP_LE : op);
    if (is_mat_t(a->t) && is_mat_t(b->t) && (op == OP_EQ || op == OP_NE)) {
        P(a->n != b->n || mat_cols(a) != mat_cols(b), v_bool(op == OP_NE))
        return v_bool(nums_equal(a, b, a->n * mat_cols(a)) == (op == OP_EQ));
    }
    if (is_mat_t(a->t) && is_mat_t(b->t) && a->n == b->n && mat_cols(a) == mat_cols(b)) {
        int64_t ne = a->n * mat_cols(a);
        V *r = v_bmat(a->n, mat_cols(a));
        nums_cmp_nums(r->B, a, b, ne, op);
        return r;
    }
    return v_errf("cannot compare types %s and %s", type_name(a->t), type_name(b->t));
}
/* Dict keys other than strings match by `=`, as vec_cmp decides it. A string or number key
   (int, float, bool or char) is found by v_dict_hashed_index, through the dict's hash table once
   it has more than 8 keys (it compares numbers as vec_cmp does); any other key is compared with
   each key by vec_cmp, here beside it, so that value.c doesn't depend on vec_ops.c. */
int64_t v_dict_index(V *d, V *k) {
    P(d->t != T_DICT,-1)
    int64_t h = v_dict_hashed_index(d, k);
    if (h != DICT_UNHASHED) return h;
    for (int64_t i = 0; i < d->n; i++) {
        V *c = vec_cmp(k, d->keys->L[i], OP_EQ);
        int eq = c->t == T_BOOL && c->j;
        v_free(c);
        if (eq) return i;
    }
    return -1;
}
V *v_dict_find(V *d, V *k) {
    int64_t i = v_dict_index(d, k);
    return i < 0 ? NULL : d->vals->L[i];
}
void v_dict_set_key(V *d, V *k, V *val) {
    Pv(d->t != T_DICT)
    if (k->t == T_STR) { v_dict_set(d, k->s, val); return; }
    int64_t i = v_dict_index(d, k);
    if (i >= 0) {
        v_list_unshare(&d->vals);
        v_free(d->vals->L[i]);
        d->vals->L[i] = v_ref(val);
        return;
    }
    v_dict_push(d, k, val);
}
V *table_filter(V *tbl, V *mask) {
    P(tbl->t != T_TABLE || mask->t != T_BVEC,v_err("bad filter"))
    P(mask->n != tbl->n,v_err("table mask length mismatch"))
    int64_t nr = tbl->n, count=0;
    for(int64_t i=0;i<nr;i++) if(mask->B[i]) count++;
    int nc = tbl->keys->n;
    V *new_data = v_list(nc);
    for(int c=0;c<nc;c++) {
        V *col = tbl->vals->L[c];
        if(col->t == T_IVEC) {
            V *nc2 = v_ivec(count); int64_t j=0;
            for(int64_t i=0;i<nr;i++) if(mask->B[i]) nc2->J[j++]=col->J[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_CVEC) {
            V *nc2 = v_cvec(count); int64_t j=0;
            for(int64_t i=0;i<nr;i++) if(mask->B[i]) nc2->B[j++]=col->B[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_FVEC) {
            V *nc2 = v_fvec(count); int64_t j=0;
            for(int64_t i=0;i<nr;i++) if(mask->B[i]) nc2->F[j++]=col->F[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_BVEC) {
            V *nc2 = v_bvec(count); int64_t j=0;
            for(int64_t i=0;i<nr;i++) if(mask->B[i]) nc2->B[j++]=col->B[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_LIST) {
            V *nc2 = v_list(count); int64_t j=0;
            for(int64_t i=0;i<nr;i++) if(mask->B[i]) nc2->L[j++]=v_ref(col->L[i]);
            new_data->L[c] = nc2;
        } else if(col->t == T_IMAT || col->t == T_FMAT || col->t == T_BMAT || col->t == T_CMAT) {
            int64_t cols = mat_cols(col);
            if(col->t == T_IMAT) {
                V *nc2 = v_imat(count, cols);
                mat_filter_imat_rows(nc2->J, col->J, mask->B, nr, cols);
                new_data->L[c] = nc2;
            } else if(col->t == T_FMAT) {
                V *nc2 = v_fmat(count, cols);
                mat_filter_fmat_rows(nc2->F, col->F, mask->B, nr, cols);
                new_data->L[c] = nc2;
            } else if(col->t == T_CMAT) {
                V *nc2 = v_cmat(count, cols);
                mat_filter_bmat_rows(nc2->B, col->B, mask->B, nr, cols);
                new_data->L[c] = nc2;
            } else {
                V *nc2 = v_bmat(count, cols);
                mat_filter_bmat_rows(nc2->B, col->B, mask->B, nr, cols);
                new_data->L[c] = nc2;
            }
        } else {
            v_free(new_data);
            return v_err("table_filter: unsupported column type");
        }
    }
    {
        V *r = v_table(tbl->keys, new_data);
        v_free(new_data);
        return r;
    }
}
