/* shakti/src/vec_ops.c — vector/matrix binops, compare, table filter */
#include "shakti_internal.h"
#include "mat_simd.h"

int is_truthy(V *v) {
    P(!v,0)
    switch(v->t) {
    case T_NIL:  return 0;
    case T_BOOL: return v->b;
    case T_INT:  return v->j != 0;
    case T_CHAR: return v->j != 0;
    case T_FLOAT:return v->f != 0.0;
    case T_STR:  return v->s[0] != 0;
    case T_IVEC: case T_FVEC: case T_BVEC: case T_CVEC: case T_LIST: return v->n > 0;
    case T_IMAT: case T_FMAT: case T_BMAT: case T_CMAT: return v->n > 0 && mat_cols(v) > 0;
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
    P(v->t==T_BOOL,(double)v->b)
    return 0;
}
V *mat_binop(V *a, V *b, int op) {
    if (!is_mat_t(a->t) && !is_mat_t(b->t)) return NULL;
    if (a->t == T_BMAT || b->t == T_BMAT)
        return v_err("arithmetic not supported on matrix[bool]");
    if (a->t == T_CMAT || b->t == T_CMAT)
        return v_err("arithmetic not supported on matrix[char]");
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT || b->t == T_CHAR)) {
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
    if ((a->t == T_INT || a->t == T_FLOAT || a->t == T_CHAR) && is_mat_t(b->t)) {
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
V *vec_binop(V *a, V *b, int op) {
    if (is_mat_t(a->t) || is_mat_t(b->t)) {
        V *r = mat_binop(a, b, op);
        if (r) return r;
    }
    if (a->t == T_IVEC && b->t == T_IVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n < b->n ? a->n : b->n;
        V *r = v_ivec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            mat_imat_binop_mm(r->J, a->J, b->J, n, op);
            return r;
        }
        const int64_t *aj = a->J, *bj = b->J;
        int64_t *rj = r->J;
        switch (op) {
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
    if (a->t == T_CVEC && b->t == T_CVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n < b->n ? a->n : b->n;
        V *r = v_cvec(n);
        const unsigned char *ac = a->B, *bc = b->B;
        unsigned char *rc = r->B;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] + bc[i]; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] - bc[i]; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] * bc[i]; break;
        case OP_FLOORDIV: for (int64_t i = 0; i < n; i++) rc[i] = bc[i] ? ac[i] / bc[i] : 0; break;
        case OP_MOD: for (int64_t i = 0; i < n; i++) rc[i] = bc[i] ? ac[i] % bc[i] : 0; break;
        default: break;
        }
        return r;
    }
    if (a->t == T_IVEC && b->t == T_INT && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n, y = b->j;
        V *r = v_ivec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            mat_imat_binop_scalar(r->J, a->J, y, n, op);
            return r;
        }
        const int64_t *aj = a->J;
        int64_t *rj = r->J;
        switch (op) {
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
    if (a->t == T_CVEC && (b->t == T_INT || b->t == T_CHAR) && op != OP_DIV && op != OP_POW) {
        int64_t n = a->n, y = b->j;
        V *r = v_cvec(n);
        const unsigned char *ac = a->B;
        unsigned char *rc = r->B;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] + y; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] - y; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = ac[i] * y; break;
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
            mat_imat_binop_scalar_rev(r->J, x, b->J, n, op);
            return r;
        }
        const int64_t *bj = b->J;
        int64_t *rj = r->J;
        switch (op) {
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
    if ((a->t == T_INT || a->t == T_CHAR) && b->t == T_CVEC && op != OP_DIV && op != OP_POW) {
        int64_t n = b->n, x = a->j;
        V *r = v_cvec(n);
        const unsigned char *bc = b->B;
        unsigned char *rc = r->B;
        switch (op) {
        case OP_ADD: for (int64_t i = 0; i < n; i++) rc[i] = x + bc[i]; break;
        case OP_SUB: for (int64_t i = 0; i < n; i++) rc[i] = x - bc[i]; break;
        case OP_MUL: for (int64_t i = 0; i < n; i++) rc[i] = x * bc[i]; break;
        case OP_FLOORDIV: for (int64_t i = 0; i < n; i++) rc[i] = bc[i] ? x / bc[i] : 0; break;
        case OP_MOD: for (int64_t i = 0; i < n; i++) rc[i] = bc[i] ? x % bc[i] : 0; break;
        default: break;
        }
        return r;
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
                /* Exact integer power via squaring; overflow falls back to
                 * float so we never emit a rounded integer (pow() on doubles
                 * loses precision above 2^53, e.g. 5**27). */
                if(y >= 0) {
                    int64_t base=x, exp=y, acc=1; int overflow=0;
                    while(exp > 0) {
                        if(exp & 1) {
                            if(__builtin_mul_overflow(acc, base, &acc)) { overflow=1; break; }
                        }
                        exp >>= 1;
                        if(exp && __builtin_mul_overflow(base, base, &base)) { overflow=1; break; }
                    }
                    if(!overflow) return v_int(acc);
                }
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
        if (n <= 0) return v_str("");
        size_t slen = strlen(a->s);
        if (slen > 0 && (uint64_t)n > (SIZE_MAX - 1) / slen)
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
        int64_t n = a->n < b->n ? a->n : b->n;
        V *r = v_fvec(n);
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV ||
            op == OP_FLOORDIV || op == OP_MOD || op == OP_POW) {
            mat_fmat_binop_mm(r->F, a->F, b->F, n, op);
            return r;
        }
    }
    if (a->t == T_FVEC && (b->t == T_INT || b->t == T_FLOAT)) {
        int64_t n = a->n;
        double y = to_float(b);
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
            mat_fmat_binop_scalar_rev(r->F, x, b->F, n, op);
            return r;
        }
    }
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
        int64_t sum = 0;
        if (__builtin_add_overflow(a->n, b->n, &sum) || sum < 0)
            return v_err("list concat too large");
        V *r = v_list(sum);
        for(int64_t i=0;i<a->n;i++) r->L[i] = v_ref(a->L[i]);
        for(int64_t i=0;i<b->n;i++) r->L[a->n+i] = v_ref(b->L[i]);
        return r;
    }
    return v_errf("unsupported operand types for op %d: %s and %s", op, type_name(a->t), type_name(b->t));
    #undef VEC_BIN
}
V *vec_cmp(V *a, V *b, int op) {
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_BOOL||a->t==T_CHAR) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_BOOL||b->t==T_CHAR)) {
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
    if (a->t == T_CVEC && (b->t == T_CHAR || b->t == T_INT)) {
        int64_t n = a->n, y = b->j;
        V *r = v_bvec(n);
        const unsigned char *ac = a->B;
        for (int64_t i = 0; i < n; i++) {
            int64_t x = ac[i];
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
    if ((a->t == T_INT || a->t == T_CHAR) && b->t == T_CVEC) {
        int64_t n = b->n, x = a->j;
        V *r = v_bvec(n);
        const unsigned char *bc = b->B;
        for (int64_t i = 0; i < n; i++) {
            int64_t y = bc[i];
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
    if((a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC) && (b->t==T_INT||b->t==T_FLOAT||b->t==T_CHAR)) {
        int64_t n=a->n; V *r=v_bvec(n); double y=to_float(b);
        for(int64_t i=0;i<n;i++) {
            double x = a->t==T_IVEC?(double)a->J[i]:a->t==T_CVEC?(double)a->B[i]:a->F[i];
            switch(op) {
            case OP_EQ: r->B[i]=(x==y); break; case OP_NE: r->B[i]=(x!=y); break;
            case OP_LT: r->B[i]=(x<y); break;  case OP_GT: r->B[i]=(x>y); break;
            case OP_LE: r->B[i]=(x<=y); break;  case OP_GE: r->B[i]=(x>=y); break;
            default: break;
            }
        }
        return r;
    }
    if((a->t==T_INT||a->t==T_FLOAT||a->t==T_CHAR) && (b->t==T_IVEC||b->t==T_FVEC||b->t==T_CVEC)) {
        int64_t n=b->n; V *r=v_bvec(n); double x=to_float(a);
        for(int64_t i=0;i<n;i++) {
            double y = b->t==T_IVEC?(double)b->J[i]:b->t==T_CVEC?(double)b->B[i]:b->F[i];
            switch(op) {
            case OP_EQ: r->B[i]=(x==y); break; case OP_NE: r->B[i]=(x!=y); break;
            case OP_LT: r->B[i]=(x<y); break;  case OP_GT: r->B[i]=(x>y); break;
            case OP_LE: r->B[i]=(x<=y); break;  case OP_GE: r->B[i]=(x>=y); break;
            default: break;
            }
        }
        return r;
    }
    if((a->t==T_IVEC||a->t==T_FVEC||a->t==T_CVEC) && (b->t==T_IVEC||b->t==T_FVEC||b->t==T_CVEC) && (op==OP_EQ||op==OP_NE)) {
        P(a->n != b->n,v_bool(op==OP_NE))
        for(int64_t i=0;i<a->n;i++) {
            double x = a->t==T_IVEC?(double)a->J[i]:a->t==T_CVEC?(double)a->B[i]:a->F[i];
            double y = b->t==T_IVEC?(double)b->J[i]:b->t==T_CVEC?(double)b->B[i]:b->F[i];
            P(x != y,v_bool(op==OP_NE))
        }
        return v_bool(op==OP_EQ);
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
            int eq = c->t==T_BOOL && c->b;
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
    if (is_mat_t(a->t) && (b->t == T_INT || b->t == T_FLOAT || b->t == T_BOOL || b->t == T_CHAR)) {
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
    if ((a->t == T_INT || a->t == T_FLOAT || a->t == T_BOOL || a->t == T_CHAR) && is_mat_t(b->t))
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
V *table_filter(V *tbl, V *mask) {
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
        } else if(col->t == T_CVEC) {
            V *nc2 = v_cvec(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->B[j++]=col->B[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_FVEC) {
            V *nc2 = v_fvec(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->F[j++]=col->F[i];
            new_data->L[c] = nc2;
        } else if(col->t == T_LIST) {
            V *nc2 = v_list(count); int64_t j=0;
            for(int64_t i=0;i<nr&&i<mask->n;i++) if(mask->B[i]) nc2->L[j++]=v_ref(col->L[i]);
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
            new_data->L[c] = v_ref(col);
        }
    }
    {
        V *r = v_table(tbl->keys, new_data);
        v_free(new_data);
        return r;
    }
}
