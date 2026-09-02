/* shakti/src/parse.c — Pratt parser + statements */
#include "shakti_internal.h"
#include <stdarg.h>

static int  g_parse_error_count = 0;
static int  g_parse_quiet = 0;
static int  g_parse_q_call_hint = 0;
static char g_parse_err_msg[256] = "";
static char g_parse_err_first[256] = "";

static int src_has_semi_in_parens(const char *s) {
    int d = 0;
    size_t i, n;
    if (!s) return 0;
    n = strlen(s);
    for (i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '#') {
            while (i < n && s[i] != '\n') i++;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            char q = ch;
            if (i + 2 < n && s[i + 1] == q && s[i + 2] == q) {
                i += 3;
                while (i + 2 < n && !(s[i] == q && s[i + 1] == q && s[i + 2] == q)) {
                    if (s[i] == '\\' && i + 1 < n) i += 2;
                    else i++;
                }
                if (i + 2 < n) i += 2;
                else i = n;
                continue;
            }
            i++;
            while (i < n && s[i] != q) {
                if (s[i] == '\\' && i + 1 < n) i += 2;
                else i++;
            }
            continue;
        }
        if (ch == '(') d++;
        else if (ch == ')') d--;
        else if (ch == ';' && d > 0) return 1;
    }
    return 0;
}
static int src_has_table_lbrack(const char *s) {
    return s && strstr(s, "table([") != NULL;
}

int shakti_parse_errors(void) { return g_parse_error_count; }
void parse_fail(const char *fmt, ...) {
    va_list ap;
    g_parse_error_count++;
    va_start(ap, fmt);
    vsnprintf(g_parse_err_msg, sizeof g_parse_err_msg, fmt, ap);
    va_end(ap);
    if (g_parse_error_count == 1)
        snprintf(g_parse_err_first, sizeof g_parse_err_first, "%s", g_parse_err_msg);
    if (!g_parse_quiet && !g_parse_q_call_hint) fprintf(stderr, "%s\n", g_parse_err_msg);
}

static Node *parse_power(Lexer *l);

static int is_each_callable_node(Node *n) {
    return n && (n->type == N_NAME || n->type == N_DOT || n->type == N_LAMBDA
                 || n->type == N_CALL);
}

static int probe_balanced(Lexer *p, int open_t, int close_t) {
    if (lex_next(p).type != open_t) return 0;
    int depth = 1;
    while (depth > 0) {
        Token t = lex_next(p);
        if (t.type == T_EOF_) return 0;
        if (t.type == open_t) depth++;
        else if (t.type == close_t) depth--;
    }
    return 1;
}

/* Token-only lookahead so ordinary expressions never emit speculative parse
 * diagnostics. Recognizes names/dotted paths, parenthesized callables, calls,
 * and indexing that are immediately followed by `@`. */
static int peek_each_verb_at(Lexer *l) {
    Lexer probe = *l;
    Token first = lex_peek(&probe);
    if (first.type == T_NAME_) {
        lex_next(&probe);
    } else if (first.type == T_LPAREN_) {
        if (!probe_balanced(&probe, T_LPAREN_, T_RPAREN_)) return 0;
    } else {
        return 0;
    }
    for (;;) {
        int t = lex_peek(&probe).type;
        if (t == T_DOT_) {
            lex_next(&probe);
            if (lex_next(&probe).type != T_NAME_) return 0;
        } else if (t == T_LPAREN_) {
            if (!probe_balanced(&probe, T_LPAREN_, T_RPAREN_)) return 0;
        } else if (t == T_LBRACKET_) {
            if (!probe_balanced(&probe, T_LBRACKET_, T_RBRACKET_)) return 0;
        } else {
            break;
        }
    }
    return lex_peek(&probe).type == T_AT_;
}

static Node *parse_each_verb(Lexer *l) {
    Node *n = parse_power(l);
    if (!is_each_callable_node(n)) {
        fprintf(stderr, "parse error: expected callable before '@' (use f@ xs or xs f@ ys)\n");
        node_free(n);
        return node_new(N_NONE);
    }
    return n;
}
static Node *parse_expr(Lexer *l);
static Node *parse_stmt(Lexer *l);
static Node *parse_block(Lexer *l);
static Node *parse_or(Lexer *l);
static Node *parse_ternary(Lexer *l);
static Node *parse_asof_comma(Lexer *l);
static Node *parse_query(Lexer *l);
static Node *parse_create_table(Lexer *l);
static Node *parse_insert(Lexer *l);
static Node *parse_join(Lexer *l, Node *left);
static Node *parse_union_outer(Lexer *l, Node *left, int ntype);
static Node *parse_atom(Lexer *l);

static int is_jux_arg_token(int tt) {
    return tt == T_NAME_ || tt == T_INT_ || tt == T_FLOAT_ || tt == T_DATETIME_
        || tt == T_CHARZ_ || tt == T_STR_ || tt == T_LPAREN_ || tt == T_LBRACKET_ || tt == T_MINUS_;
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
static int is_table_callee(Node *n) {
    return n && n->type == N_NAME && n->sval && !strcmp(n->sval, "table");
}
static int is_call_arg_sep(Lexer *l, int table_call) {
    int t = lex_peek(l).type;
    return t == T_COMMA_ || (table_call && t == T_SEMI_);
}
static Node *parse_call_arg(Lexer *l) {
    Node *arg = parse_expr(l);
    if(arg && arg->type == N_NAME && lex_peek(l).type == T_COLON_) {
        lex_next(l);
        Node *kw = node_new(N_KWARG);
        kw->sval = strdup(arg->sval);
        node_free(arg);
        node_add(kw, parse_expr(l));
        return kw;
    }
    return arg;
}

static Node *parse_jux_arg(Lexer *l) {
    return parse_atom(l);
}

static const char *expect_tok_name(int t) {
    switch (t) {
    case T_INDENT_: return "indent";
    case T_DEDENT_: return "dedent";
    case T_NEWLINE_: return "newline";
    case T_NAME_: return "name";
    case T_COLON_: return "':'";
    case T_EOF_: return "end of input";
    default: return NULL;
    }
}
static void expect(Lexer *l, int type) {
    if (l->failed) return;
    Token t = lex_next(l);
    if(t.type != type) {
        const char *en = expect_tok_name(type);
        const char *gn = expect_tok_name(t.type);
        if (en && gn) parse_fail("parse error: expected %s, got %s", en, gn);
        else parse_fail("parse error: expected token %d, got %d", type, t.type);
    }
}
static Node *parse_atom_body(Lexer *l) {
    Token t = lex_next(l);
    Node *n;
    switch(t.type) {
    case T_INT_:
        n = node_new(N_INT); n->ival = t.ival; return n;
    case T_CHARZ_:
        n = node_new(N_CHARS); n->ival = t.ival; return n;
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
        parse_fail("parse error: unexpected token %d ('%s')", t.type, t.sval);
        return node_new(N_NONE);
    }
}
static Node *parse_atom(Lexer *l) {
    if (l->failed) return NULL;
    if (l->parse_depth >= SHAKTI_PARSE_MAX_DEPTH) {
        lex_fail(l, "nesting too deep");
        return NULL;
    }
    l->parse_depth++;
    Node *n = parse_atom_body(l);
    l->parse_depth--;
    return n;
}
static Node *parse_postfix(Lexer *l) {
    Node *n = parse_atom(l);
    if (!n) return NULL;
    /* q/k-style numeric vectors: 1 2 3 or 1 -2 3 → N_LIST */
    if((n->type == N_INT || n->type == N_FLOAT || n->type == N_CHARS)) {
        Token pk0 = lex_peek(l);
        if(pk0.type == T_INT_ || pk0.type == T_FLOAT_ || pk0.type == T_CHARZ_
            || (pk0.type == T_MINUS_ && lex_peek_is_signed_literal(l))) {
            Node *vec = node_new(N_LIST);
            node_add(vec, n);
            while((pk0 = lex_peek(l)).type == T_INT_ || pk0.type == T_FLOAT_ || pk0.type == T_CHARZ_
                  || (pk0.type == T_MINUS_ && lex_peek_is_signed_literal(l)))
                node_add(vec, parse_jux_arg(l));
            n = vec;
        }
    }
    for(;;) {
        Token pk = lex_peek(l);
        if(pk.type == T_LPAREN_) {
            int table_call = is_table_callee(n);
            lex_next(l);
            Node *call = node_new(N_CALL);
            node_add(call, n);
            if(lex_peek(l).type != T_RPAREN_) {
                node_add(call, parse_call_arg(l));
                W(is_call_arg_sep(l, table_call),{
                    lex_next(l);
                    if(lex_peek(l).type == T_RPAREN_) break;
                    node_add(call, parse_call_arg(l));
                })
            }
            expect(l, T_RPAREN_);
            n = call;
        } else if(pk.type == T_LBRACKET_) {
            lex_next(l);
            if(n->type == N_NAME && n->sval && !strcmp(n->sval, "list")) {
                Token pk2 = lex_peek(l);
                int tag = 0, typed = 0;
                if(pk2.type == T_RBRACKET_) { typed = 1; tag = T_LIST; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "char")) { typed = 1; tag = T_CVEC; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "int")) { typed = 1; tag = T_IVEC; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "float")) { typed = 1; tag = T_FVEC; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "bool")) { typed = 1; tag = T_BVEC; }
                if(typed) {
                    if(pk2.type == T_NAME_) lex_next(l);
                    expect(l, T_RBRACKET_);
                    Node *tnode = node_new(N_LIST);
                    tnode->ival = tag;
                    node_free(n);
                    n = node_new(N_CALL);
                    node_add(n, tnode);
                    node_add(n, parse_expr(l));
                    continue;
                }
            } else if(n->type == N_NAME && n->sval && !strcmp(n->sval, "matrix")) {
                Token pk2 = lex_peek(l);
                int tag = 0, typed = 0;
                if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "char")) { typed = 1; tag = T_CMAT; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "int")) { typed = 1; tag = T_IMAT; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "float")) { typed = 1; tag = T_FMAT; }
                else if(pk2.type == T_NAME_ && !strcmp(pk2.sval, "bool")) { typed = 1; tag = T_BMAT; }
                if(typed) {
                    lex_next(l);
                    expect(l, T_RBRACKET_);
                    expect(l, T_LPAREN_);
                    Node *tnode = node_new(N_LIST);
                    tnode->ival = tag;
                    node_free(n);
                    n = node_new(N_CALL);
                    node_add(n, tnode);
                    node_add(n, parse_expr(l));
                    expect(l, T_COMMA_);
                    node_add(n, parse_expr(l));
                    expect(l, T_RPAREN_);
                    continue;
                }
            }
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
        } else if(is_jux_callee(n) && is_jux_arg_start(l) && !peek_each_verb_at(l)) {
            if (lex_peek_minus_is_subtraction(l))
                break;
            Node *call = node_new(N_CALL);
            node_add(call, n);
            /* Juxta assert takes a full expression so `assert a = b` is
             * assert(a = b), not (assert a) = b. Other callees stay atoms
             * so K-style `f x+y` remains (f x)+y. */
            if (n->type == N_NAME && n->sval && !strcmp(n->sval, "assert")) {
                node_add(call, parse_expr(l));
                if (lex_peek(l).type == T_COMMA_) {
                    lex_next(l);
                    node_add(call, parse_expr(l));
                }
            } else {
                while(is_jux_arg_start(l) && !peek_each_verb_at(l))
                    node_add(call, parse_jux_arg(l));
            }
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
static Node *parse_each(Lexer *l) {
    /* Each: f@ xs or xs f@ ys. */
    Node *n = parse_power(l);
    if (peek_each_verb_at(l)) {
        Node *fn = parse_each_verb(l);
        expect(l, T_AT_);
        Node *rhs = parse_each(l);
        Node *each = node_new(N_EACH);
        node_add(each, fn);
        node_add(each, n);
        node_add(each, rhs);
        return each;
    }
    if (lex_peek(l).type == T_AT_) {
        lex_next(l);
        if (!is_each_callable_node(n)) {
            fprintf(stderr,
                "parse error: expected callable before '@' (use f@ xs, xs f@ ys, or mmul)\n");
            node_free(n);
            n = node_new(N_NONE);
        }
        Node *rhs = parse_each(l);
        Node *each = node_new(N_EACH);
        node_add(each, n);
        node_add(each, rhs);
        return each;
    }
    return n;
}
static Node *parse_mul(Lexer *l) {
    Node *n = parse_each(l);
    W(lex_peek(l).type==T_STAR_ || lex_peek(l).type==T_SLASH_ ||
      lex_peek(l).type==T_PERCENT_ || lex_peek(l).type==T_DSLASH_,{
        Token op = lex_next(l);
        int o = op.type==T_STAR_?OP_MUL : op.type==T_SLASH_?OP_DIV : op.type==T_DSLASH_?OP_FLOORDIV : OP_MOD;
        Node *r = node_new(N_BINOP); r->op = o;
        node_add(r, n); node_add(r, parse_each(l));
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
            /* Trailing comma before from/where/newline is a syntax error. */
            if (lex_peek(l).type != T_NAME_) {
                fprintf(stderr, "parse error: trailing comma in by clause\n");
                node_free(n);
                return NULL;
            }
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
                if (kw.type != T_SELECT_) {
                    fprintf(stderr, "parse error: by is only supported on select\n");
                    node_free(n); node_free(cols); node_free(by); node_free(from); node_free(where);
                    return node_new(N_NONE);
                }
                lex_next(l);
                node_free(by);
                by = parse_by_cols(l);
                if (!by) {
                    node_free(n); node_free(cols); node_free(from); node_free(where);
                    return node_new(N_NONE);
                }
                continue;
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
static Node *parse_union_outer(Lexer *l, Node *left, int ntype) {
    lex_next(l);
    Node *right = parse_ternary(l);
    Node *n = node_new(ntype);
    node_add(n, left);
    node_add(n, right);
    W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    return n;
}
static Node *parse_expr(Lexer *l) {
    return parse_ternary(l);
}
static Node *parse_asof_comma(Lexer *l) {
    Node *n=parse_ternary(l);
    for(;;){
        if(lex_peek(l).type==T_COMMA_){
            lex_next(l);
            Node *r=node_new(N_BINOP);
            r->op=OP_ASOF_COMMA;
            node_add(r,n);
            node_add(r,parse_ternary(l));
            n=r;
        }else if(lex_peek(l).type==T_UNION_){
            lex_next(l);
            Node *r=node_new(N_UNION_JOIN);
            node_add(r,n);
            node_add(r,parse_ternary(l));
            n=r;
        }else if(lex_peek(l).type==T_OUTER_){
            lex_next(l);
            Node *r=node_new(N_OUTER_JOIN);
            node_add(r,n);
            node_add(r,parse_ternary(l));
            n=r;
        }else break;
    }
    return n;
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
/* Apply a decorator chain to a value expression.
   decos[0] is the first-written (outermost) decorator; the decorator
   nearest the definition is applied first:
       @a
       @b
       def f(...): ...     ->     f : a(b(f))
   Each application becomes an N_CALL whose callee is the decorator
   expression and whose single argument is the value being wrapped.     */
static Node *apply_decorators(Node **decos, int ndec, Node *inner) {
    Node *cur = inner;
    for(int k = ndec - 1; k >= 0; k--) {
        Node *call = node_new(N_CALL);
        node_add(call, decos[k]);   /* callee: name, mod.attr, or dec(args) */
        node_add(call, cur);        /* single positional argument          */
        cur = call;
    }
    return cur;
}

/* Parse one or more `@decorator` lines followed by the decorated target.
   Valid targets are a `def`, a `class`, or a single-name assignment
   (e.g. `sq : lambda x: x * x`). A decorator expression is parsed with
   parse_postfix, which accepts a bare name, a dotted path (`mod.dec`),
   or a decorator factory call (`dec(args)`) and nothing looser.          */
static Node *parse_decorated(Lexer *l) {
    Node *decos[64];
    int ndec = 0;
    while(lex_peek(l).type == T_AT_) {
        lex_next(l);                       /* consume '@' */
        Node *d = parse_postfix(l);
        if(ndec < 64) decos[ndec++] = d;
        else node_free(d);
        W(lex_peek(l).type == T_NEWLINE_ || lex_peek(l).type == T_SEMI_,lex_next(l))
    }
    Node *target = parse_stmt(l);
    if(!target) {
        fprintf(stderr, "parse error: decorator with no target definition\n");
        return node_new(N_NONE);
    }
    /* def / class: keep the binding, then rebind name = dec-chain(name). */
    if(target->type == N_DEF || target->type == N_CLASS) {
        Node *ref = node_new(N_NAME); ref->sval = strdup(target->sval);
        Node *val = apply_decorators(decos, ndec, ref);
        Node *tgt = node_new(N_NAME); tgt->sval = strdup(target->sval);
        Node *asn = node_new(N_ASSIGN);
        node_add(asn, tgt);
        node_add(asn, val);
        Node *blk = node_new(N_BLOCK);
        node_add(blk, target);
        node_add(blk, asn);
        return blk;
    }
    /* single-name assignment: wrap the right-hand side directly. */
    if(target->type == N_ASSIGN && target->nch >= 2 &&
       target->ch[0]->type == N_NAME) {
        target->ch[1] = apply_decorators(decos, ndec, target->ch[1]);
        return target;
    }
    fprintf(stderr,
        "parse error: decorators must precede a def, class, or name assignment\n");
    return target;
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
    P(pk.type == T_AT_,parse_decorated(l))
    P(pk.type == T_PASS_,(lex_next(l),({W(lex_peek(l).type==T_NEWLINE_,lex_next(l)); node_new(N_PASS);})))
    P(pk.type == T_CREATE_,parse_create_table(l))
    P(pk.type == T_INSERT_,parse_insert(l))
    if(pk.type == T_RETURN_) {
        lex_next(l);
        Node *n = node_new(N_RETURN);
        if(lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_ && lex_peek(l).type != T_DEDENT_)
            node_add(n, parse_asof_comma(l));
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
    if(lex_peek(l).type == T_UNION_) {
        return parse_union_outer(l, expr, N_UNION_JOIN);
    }
    if(lex_peek(l).type == T_OUTER_) {
        return parse_union_outer(l, expr, N_OUTER_JOIN);
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
            Node *val = parse_asof_comma(l);
            Node *n = node_new(N_ASSIGN);
            node_add(n, targets);
            node_add(n, val);
            W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
            return n;
        }
        Node *joined=targets->ch[0];
        for(int i=1;i<targets->nch;i++){
            Node *r=node_new(N_BINOP);r->op=OP_ASOF_COMMA;
            node_add(r,joined);node_add(r,targets->ch[i]);joined=r;
        }
        free(targets->ch);targets->ch=NULL;targets->nch=0;node_free(targets);
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return joined;
    }
    if(pk.type == T_COMMA_) {
        Node *joined=expr;
        while(lex_peek(l).type==T_COMMA_){
            lex_next(l);
            Node *r=node_new(N_BINOP);r->op=OP_ASOF_COMMA;
            node_add(r,joined);node_add(r,parse_ternary(l));joined=r;
        }
        W(lex_peek(l).type == T_NEWLINE_,lex_next(l))
        return joined;
    }
    if(pk.type == T_COLON_) {
        lex_next(l);
        Node *val = parse_asof_comma(l);
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
    g_parse_error_count = 0;
    g_parse_err_msg[0] = 0;
    g_parse_err_first[0] = 0;
    g_parse_q_call_hint = src_has_semi_in_parens(src) || src_has_table_lbrack(src);
    Node *prog = node_new(N_BLOCK);
    W(lex_peek(&l).type != T_EOF_ && !l.failed,{
        Token pk = lex_peek(&l);
        if(pk.type == T_INDENT_ || pk.type == T_DEDENT_ || pk.type == T_NEWLINE_) {
            lex_next(&l);
            continue;
        }
        Node *s = parse_stmt(&l);
        if(s) node_add(prog, s);
    })
    if (g_parse_error_count > 0 && g_parse_q_call_hint) {
        /* Keep unexpected-character (and similar) as the reported error.
         * Prefer table() kwargs hint when table([…] is present. */
        if (!strstr(g_parse_err_first, "unexpected character")) {
            if (src_has_table_lbrack(src))
                snprintf(g_parse_err_msg, sizeof g_parse_err_msg,
                         "parse error: table() takes name:col kwargs, not []. Try: table(a:1 2, b:3 4)");
            else if (src && strstr(src, "table(") && src_has_semi_in_parens(src))
                snprintf(g_parse_err_msg, sizeof g_parse_err_msg,
                         "parse error: table() takes name:col kwargs. Try: table(a:1 2, b:3 4)");
            else if (src_has_semi_in_parens(src))
                snprintf(g_parse_err_msg, sizeof g_parse_err_msg,
                         "parse error: ';' inside (...) is not valid. Use commas between arguments");
        }
        if (!g_parse_quiet) fprintf(stderr, "%s\n", g_parse_err_msg);
    }
    if(l.failed && !g_error) {
        g_error = 1;
        g_error_val = v_errf("parse: %s", l.error[0] ? l.error : "invalid input");
    }
    if(g_parse_error_count > 0 && !g_error) {
        g_error = 1;
        g_error_val = v_errf("parse: %s", g_parse_err_msg[0] ? g_parse_err_msg : "invalid input");
    }
    return prog;
}

int shakti_parse_check(const char *src, char *err, size_t errcap) {
    Node *prog;
    if (err && errcap) err[0] = 0;
    g_parse_error_count = 0;
    g_parse_err_msg[0] = 0;
    g_parse_quiet = 1;
    prog = parse(src ? src : "");
    g_parse_quiet = 0;
    int nerr = g_parse_error_count;
    if (g_error) {
        if (!nerr) nerr = 1;
        if (g_error_val && g_error_val->s && !g_parse_err_msg[0])
            snprintf(g_parse_err_msg, sizeof g_parse_err_msg, "%s", g_error_val->s);
        g_error = 0;
        if (g_error_val) { v_free(g_error_val); g_error_val = NULL; }
    }
    if (prog) node_free(prog);
    if (nerr == 0) return 1;
    if (err && errcap)
        snprintf(err, errcap, "%s", g_parse_err_msg[0] ? g_parse_err_msg : "parse error");
    return 0;
}
