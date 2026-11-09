/* shakti/src/parse.c — Pratt parser + statements */
#include "shakti_internal.h"
#include <stdarg.h>

static int  g_parse_error_count = 0;
static int  g_parse_quiet = 0;
static int  g_parse_q_call_hint = 0;
static char g_parse_err_msg[256] = "";
static char g_parse_err_first[256] = "";
/* The lexer parse() is reading, and whether the first parse error came after
 * it failed (a limit such as token length or nesting depth), so the error
 * reported is whichever came first. */
static const Lexer *g_parse_lexer = NULL;
static int  g_parse_first_after_lex = 0;
/* Lowest usable address of the C stack, set by shakti_stack_start; 0: no check. */
uintptr_t g_parse_stack_bottom;
/* Loops enclosing the statement being parsed, within the current def or
   class body: break and continue need one. */
static int  g_loop_depth = 0;

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
/* 1 if the statement consumed a newline, 2 if it consumed ';'. */
static int g_stmt_sep;
static void eat_stmt_end(Lexer *l) {
    int t = lex_peek(l).type;
    if(t == T_NEWLINE_) { lex_next(l); g_stmt_sep = 1; }
    else if(t == T_SEMI_) { lex_next(l); g_stmt_sep = 2; }
}
/* Two statements on one line need ';' or a newline (#177). */
static int stmt_boundary_ok(Lexer *l) {
    int t = lex_peek(l).type;
    if(t == T_NEWLINE_ || t == T_SEMI_ || t == T_DEDENT_ || t == T_EOF_ || t == T_INDENT_ || g_stmt_sep)
        return 1;
    parse_fail("parse error: expected newline");
    return 0;
}
void parse_fail(const char *fmt, ...) {
    va_list ap;
    /* Only the first error is reported: later ones mostly follow from it. */
    if (++g_parse_error_count > 1) return;
    /* After the lexer has failed, its error is the one reported. */
    g_parse_first_after_lex = g_parse_lexer && g_parse_lexer->failed;
    va_start(ap, fmt);
    vsnprintf(g_parse_err_first, sizeof g_parse_err_first, fmt, ap);
    va_end(ap);
    snprintf(g_parse_err_msg, sizeof g_parse_err_msg, "%s", g_parse_err_first);
}

static Node *parse_power(Lexer *l);
static Node *parse_factor(Lexer *l);

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
static int probe_each_verb_at(Lexer *probe) {
    Token first = lex_peek(probe);
    if (first.type == T_NAME_) {
        lex_next(probe);
    } else if (first.type == T_LPAREN_) {
        if (!probe_balanced(probe, T_LPAREN_, T_RPAREN_)) return 0;
    } else {
        return 0;
    }
    for (;;) {
        int t = lex_peek(probe).type;
        if (t == T_DOT_) {
            lex_next(probe);
            if (lex_next(probe).type != T_NAME_) return 0;
        } else if (t == T_LPAREN_) {
            if (!probe_balanced(probe, T_LPAREN_, T_RPAREN_)) return 0;
        } else if (t == T_LBRACKET_) {
            if (!probe_balanced(probe, T_LBRACKET_, T_RBRACKET_)) return 0;
        } else {
            break;
        }
    }
    return lex_peek(probe).type == T_AT_;
}
static int peek_each_verb_probe(Lexer *l) {
    int t = lex_peek(l).type;
    if (t != T_NAME_ && t != T_LPAREN_) return 0;
    LexMark mark = lex_mark(l);
    Lexer probe = *l;
    int r = probe_each_verb_at(&probe);
    lex_release(&probe, mark);
    return r;
}

/* The probe lexes tokens the parser lexes again, so an unexpected character
 * it meets is left for the parser to report, once and in order. */
static int peek_each_verb_at(Lexer *l) {
    int count = g_parse_error_count, quiet = g_parse_quiet;
    int after_lex = g_parse_first_after_lex;
    char msg[sizeof g_parse_err_msg], first[sizeof g_parse_err_first];
    memcpy(msg, g_parse_err_msg, sizeof msg);
    memcpy(first, g_parse_err_first, sizeof first);
    g_parse_quiet = 1;
    int r = peek_each_verb_probe(l);
    g_parse_quiet = quiet;
    g_parse_error_count = count;
    g_parse_first_after_lex = after_lex;
    memcpy(g_parse_err_msg, msg, sizeof msg);
    memcpy(g_parse_err_first, first, sizeof first);
    return r;
}

static Node *parse_each_verb(Lexer *l) {
    Node *n = parse_power(l);
    if (!is_each_callable_node(n)) {
        parse_fail("parse error: expected callable before '@' (use f@ xs or xs f@ ys)");
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
static Node *parse_query_kw(Lexer *l, Token kw);
static Node *parse_create_table(Lexer *l);
static Node *parse_insert(Lexer *l);
static Node *parse_join(Lexer *l, Node *left);
static Node *parse_union_outer(Lexer *l, Node *left, int ntype);
static Node *parse_atom(Lexer *l);

static int is_jux_arg_token(int tt) {
    return tt == T_NAME_ || tt == T_INT_ || tt == T_FLOAT_ || tt == T_DATETIME_
        || tt == T_CHARZ_ || tt == T_CVECZ_ || tt == T_STR_ || tt == T_LPAREN_ || tt == T_LBRACKET_ || tt == T_MINUS_
        || tt == T_SELECT_;
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
    if(lex_peek(l).type == T_STAR_) {
        lex_next(l);
        Node *splat = node_new(N_UNOP); splat->op = OP_SPLAT;
        node_add(splat, parse_expr(l));
        return splat;
    }
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
    case T_INT_: return "integer";
    case T_FLOAT_: return "float";
    case T_STR_: return "string";
    case T_COLON_: return "':'";
    case T_COMMA_: return "','";
    case T_DOT_: return "'.'";
    case T_EQ_: return "'='";
    case T_NE_: return "'!='";
    case T_LT_: return "'<'";
    case T_GT_: return "'>'";
    case T_LE_: return "'<='";
    case T_GE_: return "'>='";
    case T_PLUS_: return "'+'";
    case T_MINUS_: return "'-'";
    case T_STAR_: return "'*'";
    case T_SLASH_: return "'/'";
    case T_DSLASH_: return "'//'";
    case T_PERCENT_: return "'%'";
    case T_LPAREN_: return "'('";
    case T_RPAREN_: return "')'";
    case T_LBRACKET_: return "'['";
    case T_RBRACKET_: return "']'";
    case T_LBRACE_: return "'{'";
    case T_RBRACE_: return "'}'";
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
    case T_CVECZ_:
        n = node_new(N_CVECS); n->sval = strdup(t.sval); n->ival = t.ival; return n;
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
                if(lex_peek(l).type == T_RPAREN_ || l->failed) break;
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
            while(lex_peek(l).type != T_RBRACKET_ && lex_peek(l).type != T_EOF_ && !l->failed) {
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
                lex_next(l); if(lex_peek(l).type==T_RBRACE_ || l->failed) break;
                k = parse_expr(l); expect(l, T_COLON_); v = parse_expr(l);
                node_add(n, k); node_add(n, v);
            })
        }
        expect(l, T_RBRACE_);
        return n;
    }
    case T_MINUS_: {
        /* Signed literal inside juxtaposition (`1 -2 3`, `f x -1`). A leading
         * minus in an expression is parse_factor, so `-abs(3)` and `-x ** 2`
         * include the call and the power (#94, #98). */
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
            for(;;) {
                Token p = lex_next(l);
                Node *pn = node_new(N_NAME); pn->sval = strdup(p.sval);
                node_add(params, pn);
                node_add(defaults, node_new(N_PASS));
                if(lex_peek(l).type != T_COMMA_) break;
                lex_next(l);
            }
        }
        expect(l, T_COLON_);
        node_add(body, parse_ternary(l));
        n = node_new(N_LAMBDA);
        node_add(n, params);
        node_add(n, body);
        node_add(n, defaults);
        return n;
    }
    case T_PASS_:
        return node_new(N_PASS);
    case T_SELECT_: case T_UPDATE_: case T_DELETE_: {
        int nt = lex_peek(l).type;
        int query = 0;
        if(t.type == T_DELETE_) query = nt == T_FROM_ || nt == T_WHERE_;
        else if(t.type == T_SELECT_)
            query = nt == T_NAME_ || nt == T_STAR_ || nt == T_FROM_ || nt == T_WHERE_;
        else query = nt == T_NAME_ || nt == T_FROM_ || nt == T_BY_;
        if(query) return parse_query_kw(l, t);
        n = node_new(N_NAME); n->sval = strdup(t.sval ? t.sval : ""); return n;
    }
    case T_BY_: case T_FROM_: case T_WHERE_: case T_CREATE_:
    case T_INSERT_: case T_INTO_: case T_JOIN_: case T_ON_:
    case T_UNION_: case T_OUTER_: case T_AS_: {
        /* SQL words are keywords in a statement and names elsewhere (#152, #184). */
        const char *spell = NULL;
        switch(t.type) {
        case T_SELECT_: spell = "select"; break; case T_UPDATE_: spell = "update"; break;
        case T_DELETE_: spell = "delete"; break; case T_BY_: spell = "by"; break;
        case T_FROM_: spell = "from"; break; case T_WHERE_: spell = "where"; break;
        case T_CREATE_: spell = "create"; break; case T_INSERT_: spell = "insert"; break;
        case T_INTO_: spell = "into"; break; case T_JOIN_: spell = "join"; break;
        case T_ON_: spell = "on"; break; case T_UNION_: spell = "union"; break;
        case T_OUTER_: spell = "outer"; break; case T_AS_: spell = "as"; break;
        }
        n = node_new(N_NAME); n->sval = strdup(spell); return n;
    }
    case T_VALUES_: {
        Node *nm = node_new(N_NAME);
        nm->sval = strdup("values");
        return nm;
    }
    default:
        {
            const char *nm = expect_tok_name(t.type);
            if (nm) parse_fail("parse error: unexpected %s", nm);
            else parse_fail("parse error: unexpected token %d", t.type);
        }
        return node_new(N_NONE);
    }
}
/* Nonzero when less than SHAKTI_PARSE_STACK_RESERVE of the C stack is left: nesting the cap allows
 * (blocks, or brackets at the bottom of a deep recursion) would otherwise run the stack out. A
 * frame on another stack is far from the bottom, and the unsigned difference is large. */
static int parse_stack_short(void) {
    return g_parse_stack_bottom
        && (uintptr_t)__builtin_frame_address(0) - g_parse_stack_bottom < SHAKTI_PARSE_STACK_RESERVE;
}
static int parse_enter(Lexer *l) {
    if (l->failed) return 0;
    if (l->parse_depth >= SHAKTI_PARSE_MAX_DEPTH || parse_stack_short()) {
        lex_fail(l, "nesting too deep");
        return 0;
    }
    l->parse_depth++;
    return 1;
}
/* A juxtaposed assert's argument (and its message) has no bracket of its own to count, so it
 * counts one level here: a chain of them stops at the cap instead of running the C stack out. */
static Node *parse_nested_expr(Lexer *l) {
    if (!parse_enter(l)) return node_new(N_NONE);
    Node *n = parse_expr(l);
    l->parse_depth--;
    return n;
}
static Node *parse_atom(Lexer *l) {
    if (!parse_enter(l)) return NULL;
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
                    if(lex_peek(l).type == T_RPAREN_ || l->failed) break;
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
                    /* only the parenthesised argument: (), (x) or (x, y) */
                    if(lex_peek(l).type != T_LPAREN_) {
                        /* stop: the argument's end can't be told from what follows */
                        const char *msg = "parse error: list[T] takes its argument in parentheses: list[int](n)";
                        parse_fail("%s", msg);
                        lex_fail(l, msg);
                        return n;
                    }
                    node_add(n, parse_atom(l));
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
            } else if(lex_peek(l).type == T_RBRACKET_) {
                /* f[] applies f to []. An empty list is a value. */
                lex_next(l);
                Node *idx = node_new(N_INDEX);
                node_add(idx, n);
                node_add(idx, node_new(N_LIST));
                n = idx;
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
                    W(lex_peek(l).type == T_COMMA_ && !l->failed,{
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
             * so K-style `f x+y` remains (f x)+y. At the start of a
             * statement, `assert e` is the statement form (parse_stmt). */
            if (n->type == N_NAME && n->sval && !strcmp(n->sval, "assert")) {
                node_add(call, parse_nested_expr(l));
                if (lex_peek(l).type == T_COMMA_) {
                    lex_next(l);
                    node_add(call, parse_nested_expr(l));
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
    /* A folded negative literal (`-2`) is still -(2 ** …), as `-x ** 2` is (#98). */
    if(lex_peek(l).type == T_DSTAR_ && n &&
       ((n->type == N_INT && n->ival < 0) || (n->type == N_FLOAT && n->fval < 0))) {
        lex_next(l);
        if(n->type == N_INT) n->ival = -n->ival;
        else n->fval = -n->fval;
        if (!parse_enter(l)) return n;
        Node *pow = node_new(N_BINOP); pow->op = OP_POW;
        node_add(pow, n); node_add(pow, parse_factor(l));
        l->parse_depth--;
        Node *neg = node_new(N_UNOP); neg->op = OP_NEG;
        node_add(neg, pow);
        return neg;
    }
    if(lex_peek(l).type == T_DSTAR_) {
        lex_next(l);
        if (!parse_enter(l)) return n;
        Node *r = node_new(N_BINOP); r->op = OP_POW;
        node_add(r, n); node_add(r, parse_factor(l));
        l->parse_depth--;
        return r;
    }
    return n;
}
static Node *parse_each(Lexer *l) {
    /* Each: f@ xs or xs f@ ys. */
    Node *n = parse_power(l);
    if (!n || l->failed) return n;
    if (peek_each_verb_at(l)) {
        Node *fn = parse_each_verb(l);
        expect(l, T_AT_);
        if (!parse_enter(l)) { node_free(fn); return n; }
        Node *rhs = parse_each(l);
        l->parse_depth--;
        Node *each = node_new(N_EACH);
        node_add(each, fn);
        node_add(each, n);
        node_add(each, rhs);
        return each;
    }
    if (lex_peek(l).type == T_AT_) {
        lex_next(l);
        if (!is_each_callable_node(n)) {
            parse_fail("parse error: expected callable before '@' (use f@ xs, xs f@ ys, or mmul)");
            node_free(n);
            n = node_new(N_NONE);
        }
        if (!parse_enter(l)) return n;
        Node *rhs = parse_each(l);
        l->parse_depth--;
        Node *each = node_new(N_EACH);
        node_add(each, n);
        node_add(each, rhs);
        return each;
    }
    return n;
}
/* Unary minus binds looser than a call, an index, an attribute and `**` (#94, #98). */
static Node *parse_factor(Lexer *l) {
    if(lex_peek(l).type == T_MINUS_) {
        lex_next(l);
        if (!parse_enter(l)) return node_new(N_NONE);
        Node *n = node_new(N_UNOP); n->op = OP_NEG;
        node_add(n, parse_factor(l));
        l->parse_depth--;
        return n;
    }
    return parse_each(l);
}
static Node *parse_mul(Lexer *l) {
    Node *n = parse_factor(l);
    W(lex_peek(l).type==T_STAR_ || lex_peek(l).type==T_SLASH_ ||
      lex_peek(l).type==T_PERCENT_ || lex_peek(l).type==T_DSLASH_,{
        Token op = lex_next(l);
        int o = op.type==T_STAR_?OP_MUL : op.type==T_SLASH_?OP_DIV : op.type==T_DSLASH_?OP_FLOORDIV : OP_MOD;
        Node *r = node_new(N_BINOP); r->op = o;
        node_add(r, n); node_add(r, parse_factor(l));
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
        parse_fail("parse error: expected 'in' after 'not'");
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
        if (!parse_enter(l)) return NULL;
        Node *n = node_new(N_UNOP); n->op = OP_NOT;
        node_add(n, parse_not(l));
        l->parse_depth--;
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
        if (!parse_enter(l)) { node_free(cond); return n; }
        Node *alt = parse_ternary(l);
        l->parse_depth--;
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
                parse_fail("parse error: trailing comma in by clause");
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
static Node *parse_query_kw(Lexer *l, Token kw) {
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
                    parse_fail("parse error: by is only supported on select");
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
                Lexer saved = *l;
                LexMark mark = lex_mark(l);
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
                int next = lex_peek(l).type;
                lex_release(l, mark);
                *l = saved;
                if (next == T_BY_)
                    parse_fail("parse error: by is only supported on select");
                else
                    parse_fail("parse error: expected col: expr");
                node_free(n); node_free(cols); node_free(by); node_free(from); node_free(where);
                return node_new(N_NONE);
            }
            if (kw.type == T_SELECT_) {
                parse_select_col(l, cols);
            } else {
                node_add(cols, parse_expr(l));
            }
            if (lex_peek(l).type == T_COMMA_) lex_next(l);
        }
    }
    /* delete takes no column list: delete from t where expr. A refused delete still has the
     * rest of its statement parsed below, so parsing resumes after it, and is then dropped. */
    int delete_cols = kw.type == T_DELETE_ && cols->nch > 0 && lex_peek(l).type == T_FROM_;
    if (delete_cols) parse_fail("parse error: delete takes no columns (delete from t where expr)");
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
    if (delete_cols) { node_free(n); return node_new(N_NONE); }
    return n;
}
static Node *parse_create_table(Lexer *l) {
    lex_next(l);
    Token kwt = lex_next(l);
    if(kwt.type != T_NAME_ || strcmp(kwt.sval, "table")) {
        parse_fail("parse error: expected 'table' after create");
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
    eat_stmt_end(l);
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
    eat_stmt_end(l);
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
    eat_stmt_end(l);
    return n;
}
static Node *parse_union_outer(Lexer *l, Node *left, int ntype) {
    lex_next(l);
    Node *right = parse_ternary(l);
    Node *n = node_new(ntype);
    node_add(n, left);
    node_add(n, right);
    eat_stmt_end(l);
    return n;
}
static Node *parse_expr(Lexer *l) {
    Node *n = parse_ternary(l);
    for(;;) {
        int t = lex_peek(l).type;
        if(t == T_JOIN_) n = parse_join(l, n);
        else if(t == T_UNION_) n = parse_union_outer(l, n, N_UNION_JOIN);
        else if(t == T_OUTER_) n = parse_union_outer(l, n, N_OUTER_JOIN);
        else break;
        if(!n || l->failed) break;
    }
    return n;
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
    /* blocks nest without parse_enter (the lexer caps them at 255 levels), so check the stack here */
    if (!l->failed && parse_stack_short()) lex_fail(l, "nesting too deep");
    expect(l, T_INDENT_);
    Node *block = node_new(N_BLOCK);
    while(lex_peek(l).type != T_DEDENT_ && lex_peek(l).type != T_EOF_ && !l->failed) {
        if(lex_peek(l).type == T_INDENT_) {
            parse_fail("parse error: unexpected indent");
            break;
        }
        g_stmt_sep = 0;
        Node *s = parse_stmt(l);
        if(s) node_add(block, s);
        if(!stmt_boundary_ok(l)) break;
    }
    if(lex_peek(l).type == T_DEDENT_) lex_next(l);
    return block;
}
/* A suite is an indented block, or simple statements on the colon's line (#163). */
static int suite_is_compound(int t) {
    return t==T_IF_||t==T_WHILE_||t==T_FOR_||t==T_DEF_||t==T_TRY_||t==T_CLASS_||t==T_WITH_||t==T_AT_;
}
static Node *parse_suite(Lexer *l) {
    expect(l, T_COLON_);
    int t = lex_peek(l).type;
    if(t == T_NEWLINE_ || t == T_EOF_ || t == T_DEDENT_) {
        if(t == T_NEWLINE_) lex_next(l);
        return parse_block(l);
    }
    if(suite_is_compound(t)) {
        parse_fail("parse error: expected newline");
        return node_new(N_BLOCK);
    }
    Node *block = node_new(N_BLOCK);
    while(!l->failed) {
        g_stmt_sep = 0;
        Node *s = parse_stmt(l);
        if(s) node_add(block, s);
        if(g_stmt_sep == 2) continue;
        break;
    }
    return block;
}
static Node *parse_if(Lexer *l) {
    Node *n = node_new(N_IF);
    Node *cond = parse_expr(l);
    Node *body = parse_suite(l);
    node_add(n, cond);
    node_add(n, body);
    while(lex_peek(l).type == T_ELIF_) {
        lex_next(l);
        Node *econd = parse_expr(l);
        Node *ebody = parse_suite(l);
        node_add(n, econd);
        node_add(n, ebody);
    }
    if(lex_peek(l).type == T_ELSE_) {
        lex_next(l);
        Node *ebody = parse_suite(l);
        node_add(n, node_new(N_BOOL));
        n->ch[n->nch-1]->ival = 1;
        node_add(n, ebody);
    }
    return n;
}
static Node *parse_while(Lexer *l) {
    Node *n = node_new(N_WHILE);
    Node *cond = parse_expr(l);
    g_loop_depth++;
    Node *body = parse_suite(l);
    g_loop_depth--;
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
    g_loop_depth++;
    Node *body = parse_suite(l);
    g_loop_depth--;
    node_add(n, vars);
    node_add(n, iter);
    node_add(n, body);
    return n;
}
/* `*a` / `**k` parameters keep their stars in the name so binding can find them. */
static char *param_name(const char *star, const char *name) {
    size_t ls = strlen(star), ln = strlen(name);
    char *s = x_malloc(ls + ln + 1, "param_name");
    memcpy(s, star, ls);
    memcpy(s + ls, name, ln + 1);
    return s;
}
static Node *parse_def(Lexer *l) {
    Token name = lex_next(l);
    expect(l, T_LPAREN_);
    Node *params = node_new(N_LIST);
    Node *defaults = node_new(N_LIST);
    if(lex_peek(l).type != T_RPAREN_) {
        int saw_star = 0, saw_dstar = 0;
        while(!l->failed && lex_peek(l).type != T_RPAREN_) {
            const char *star = "";
            if(lex_peek(l).type == T_STAR_) { lex_next(l); star = "*"; }
            else if(lex_peek(l).type == T_DSTAR_) { lex_next(l); star = "**"; }
            if(star[0] == '*' && star[1] == 0) {
                if(saw_star || saw_dstar) parse_fail("parse error: * parameter may appear once");
                saw_star = 1;
            } else if(star[0] == '*') {
                if(saw_dstar) parse_fail("parse error: * parameter may appear once");
                saw_dstar = 1;
            }
            Token p = lex_next(l);
            Node *pn = node_new(N_NAME); pn->sval = param_name(star, p.sval);
            node_add(params, pn);
            if(lex_peek(l).type == T_COLON_) {
                if(star[0]) parse_fail("parse error: * parameter cannot have a default");
                lex_next(l); node_add(defaults, parse_expr(l));
            } else {
                node_add(defaults, node_new(N_PASS));
            }
            if(lex_peek(l).type == T_COMMA_) lex_next(l);
            else break;
        }
    }
    expect(l, T_RPAREN_);
    if(lex_peek(l).type == T_MINUS_) {
        lex_next(l);
        if(lex_peek(l).type == T_GT_) { lex_next(l); parse_expr(l);  }
    }
    int loops = g_loop_depth;
    g_loop_depth = 0;
    Node *body = parse_suite(l);
    g_loop_depth = loops;
    Node *n = node_new(N_DEF);
    n->sval = strdup(name.sval);
    node_add(n, params);
    node_add(n, body);
    node_add(n, defaults);
    return n;
}
static Node *parse_try(Lexer *l) {
    Node *try_body = parse_suite(l);
    Node *n = node_new(N_TRY);
    node_add(n, try_body);
    if(lex_peek(l).type == T_EXCEPT_) {
        lex_next(l);
        if(lex_peek(l).type == T_NAME_) {
            Token ename = lex_next(l);
            n->sval = strdup(ename.sval);
        }
        if(lex_peek(l).type == T_AS_) {
            lex_next(l);
            Token ename = lex_next(l);
            free(n->sval);
            n->sval = strdup(ename.sval);
        }
        Node *except_body = parse_suite(l);
        node_add(n, except_body);
    } else {
        node_add(n, node_new(N_PASS));
    }
    if(lex_peek(l).type == T_ELSE_) {
        lex_next(l);
        Node *else_body = parse_suite(l);
        node_add(n, else_body);
    } else {
        node_add(n, node_new(N_PASS));
    }
    if(lex_peek(l).type == T_FINALLY_) {
        lex_next(l);
        Node *finally_body = parse_suite(l);
        node_add(n, finally_body);
    } else {
        node_add(n, node_new(N_PASS));
    }
    return n;
}
static Node *parse_class(Lexer *l) {
    Token name = lex_next(l);
    Node *bases = node_new(N_LIST);
    if(lex_peek(l).type == T_LPAREN_) {
        lex_next(l);
        W(lex_peek(l).type != T_RPAREN_ && lex_peek(l).type != T_EOF_ && !l->failed,{
            node_add(bases, parse_expr(l));
            if(lex_peek(l).type != T_COMMA_) break;
            lex_next(l);
        })
        expect(l, T_RPAREN_);
    }
    int loops = g_loop_depth;
    g_loop_depth = 0;
    Node *body = parse_suite(l);
    g_loop_depth = loops;
    Node *n = node_new(N_CLASS);
    n->sval = strdup(name.sval);
    node_add(n, body);
    node_add(n, bases);
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
        parse_fail("parse error: decorator with no target definition");
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
    parse_fail("parse error: decorators must precede a def, class, or name assignment");
    return target;
}

/* At the start of a statement, `assert` followed by an expression is the
 * statement form. Not when it is a call form, `assert(...)` or `assert[...]`
 * closing its statement (each takes a message as a second argument), nor
 * `assert@ xs`, `assert.f`, nor when `assert` stands alone or is bound
 * (`assert : v`, `assert += v`). */
static int probe_assert_stmt(Lexer *probe) {
    lex_next(probe);
    int t = lex_peek(probe).type;
    if (t == T_NEWLINE_ || t == T_SEMI_ || t == T_EOF_ || t == T_DEDENT_ ||
        t == T_COLON_ || t == T_COMMA_ || t == T_PLUSEQ_ || t == T_MINUSEQ_ ||
        t == T_STAREQ_ || t == T_SLASHEQ_ || t == T_AT_ || t == T_DOT_) return 0;
    if (t != T_LPAREN_ && t != T_LBRACKET_) return 1;
    if (!probe_balanced(probe, t, t == T_LPAREN_ ? T_RPAREN_ : T_RBRACKET_))
        return 1;
    t = lex_peek(probe).type;
    return !(t == T_NEWLINE_ || t == T_SEMI_ || t == T_EOF_ || t == T_DEDENT_);
}
static int peek_assert_probe(Lexer *l) {
    LexMark mark = lex_mark(l);
    Lexer probe = *l;
    int r = probe_assert_stmt(&probe);
    lex_release(&probe, mark);
    return r;
}

/* The probe lexes tokens the parser lexes again, so an unexpected character
 * it meets is left for the parser to report, once. */
static int peek_assert_stmt(Lexer *l) {
    int count = g_parse_error_count, quiet = g_parse_quiet;
    char msg[sizeof g_parse_err_msg], first[sizeof g_parse_err_first];
    memcpy(msg, g_parse_err_msg, sizeof msg);
    memcpy(first, g_parse_err_first, sizeof first);
    g_parse_quiet = 1;
    int r = peek_assert_probe(l);
    g_parse_quiet = quiet;
    g_parse_error_count = count;
    memcpy(g_parse_err_msg, msg, sizeof msg);
    memcpy(g_parse_err_first, first, sizeof first);
    return r;
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
    P(pk.type == T_PASS_,(lex_next(l),({eat_stmt_end(l); node_new(N_PASS);})))
    if(pk.type == T_CREATE_) {
        Lexer saved = *l; LexMark mark = lex_mark(l);
        lex_next(l);
        Token nx = lex_peek(l);
        int table = nx.type == T_NAME_ && nx.sval && !strcmp(nx.sval, "table");
        lex_release(l, mark); *l = saved;
        if(table) return parse_create_table(l);
    }
    if(pk.type == T_INSERT_) {
        Lexer saved = *l; LexMark mark = lex_mark(l);
        lex_next(l);
        int into = lex_peek(l).type == T_INTO_;
        lex_release(l, mark); *l = saved;
        if(into) return parse_insert(l);
    }
    if(pk.type == T_RETURN_) {
        lex_next(l);
        Node *n = node_new(N_RETURN);
        if(lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_ && lex_peek(l).type != T_DEDENT_)
            node_add(n, parse_asof_comma(l));
        eat_stmt_end(l);
        return n;
    }
    if(pk.type == T_BREAK_) {
        lex_next(l);
        if(!g_loop_depth) parse_fail("parse error: 'break' outside a loop");
        eat_stmt_end(l);
        return node_new(N_BREAK);
    }
    if(pk.type == T_CONTINUE_) {
        lex_next(l);
        if(!g_loop_depth) parse_fail("parse error: 'continue' outside a loop");
        eat_stmt_end(l);
        return node_new(N_CONTINUE);
    }
    if(pk.type == T_RAISE_) {
        lex_next(l);
        Node *n = node_new(N_RAISE);
        if(lex_peek(l).type != T_NEWLINE_ && lex_peek(l).type != T_EOF_) {
            node_add(n, parse_expr(l));
        }
        eat_stmt_end(l);
        return n;
    }
    if(pk.type == T_DEL_) {
        lex_next(l);
        Node *n = node_new(N_DEL);
        node_add(n, parse_expr(l));
        eat_stmt_end(l);
        return n;
    }
    if(pk.type == T_GLOBAL_) {
        lex_next(l);
        Node *n = node_new(N_GLOBAL);
        Token nm = lex_next(l);
        n->sval = strdup(nm.sval);
        W(lex_peek(l).type == T_COMMA_,{lex_next(l); lex_next(l);})
        eat_stmt_end(l);
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
        Node *body = parse_suite(l);
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
            size_t a = strlen(n->sval), b = strlen(sub.sval);
            char *joined = x_malloc(a + b + 2, "parse_import");
            memcpy(joined, n->sval, a);
            joined[a] = '.';
            memcpy(joined + a + 1, sub.sval, b + 1);
            free(n->sval);
            n->sval = joined;
        })
        eat_stmt_end(l);
        return n;
    }
    if(pk.type == T_NAME_ && !strcmp(pk.sval, "assert") && peek_assert_stmt(l)) {
        /* assert e[, msg] takes whole expressions, so `assert [1] = [2]`,
         * `assert (a) = b` and `assert -x < 0` assert what they say. */
        lex_next(l);
        Node *n = node_new(N_CALL);
        Node *fn = node_new(N_NAME); fn->sval = strdup("assert");
        node_add(n, fn);
        node_add(n, parse_expr(l));
        if(lex_peek(l).type == T_COMMA_) {
            lex_next(l);
            node_add(n, parse_expr(l));
        }
        eat_stmt_end(l);
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
            eat_stmt_end(l);
            return n;
        }
        Node *joined=targets->ch[0];
        for(int i=1;i<targets->nch;i++){
            Node *r=node_new(N_BINOP);r->op=OP_ASOF_COMMA;
            node_add(r,joined);node_add(r,targets->ch[i]);joined=r;
        }
        free(targets->ch);targets->ch=NULL;targets->nch=0;node_free(targets);
        eat_stmt_end(l);
        return joined;
    }
    if(pk.type == T_COMMA_) {
        Node *joined=expr;
        while(lex_peek(l).type==T_COMMA_){
            lex_next(l);
            Node *r=node_new(N_BINOP);r->op=OP_ASOF_COMMA;
            node_add(r,joined);node_add(r,parse_ternary(l));joined=r;
        }
        eat_stmt_end(l);
        return joined;
    }
    if(pk.type == T_COLON_) {
        lex_next(l);
        Node *val = parse_asof_comma(l);
        Node *n = node_new(N_ASSIGN);
        node_add(n, expr);
        node_add(n, val);
        eat_stmt_end(l);
        return n;
    }
    if(pk.type == T_PLUSEQ_ || pk.type == T_MINUSEQ_ || pk.type == T_STAREQ_ || pk.type == T_SLASHEQ_
       || pk.type == T_DSLASHEQ_ || pk.type == T_PERCENTEQ_ || pk.type == T_DSTAREQ_) {
        Token op = lex_next(l);
        int o = op.type==T_PLUSEQ_?OP_ADD : op.type==T_MINUSEQ_?OP_SUB : op.type==T_STAREQ_?OP_MUL
              : op.type==T_DSLASHEQ_?OP_FLOORDIV : op.type==T_PERCENTEQ_?OP_MOD
              : op.type==T_DSTAREQ_?OP_POW : OP_DIV;
        Node *val = parse_expr(l);
        Node *n = node_new(N_AUGASSIGN); n->op = o;
        node_add(n, expr);
        node_add(n, val);
        eat_stmt_end(l);
        return n;
    }
    eat_stmt_end(l);
    return expr;
}
/* Eval recurses once per tree level, so cap depth here instead of checking on every eval. */
typedef struct { Node *n; int d; } DepthItem;
static int tree_too_deep(Node *root, int limit) {
    DepthItem local[256];
    DepthItem *stk = local;
    size_t sn = 0, cap = 256;
    int deep = 0;
    if (!root) return 0;
    stk[sn++] = (DepthItem){root, 1};
    while (sn && !deep) {
        DepthItem cur = stk[--sn];
        if (cur.d > limit) { deep = 1; break; }
        for (int k = 0; k < cur.n->nch; k++) {
            if (!cur.n->ch[k]) continue;
            if (sn == cap) {
                DepthItem *grown = stk == local ? malloc(cap * 2 * sizeof *stk) : realloc(stk, cap * 2 * sizeof *stk);
                if (!grown) { sn = 0; break; }
                if (stk == local) memcpy(grown, local, sizeof local);
                stk = grown;
                cap *= 2;
            }
            stk[sn++] = (DepthItem){cur.n->ch[k], cur.d + 1};
        }
    }
    if (stk != local) free(stk);
    return deep;
}
Node *parse(const char *src) {
    Lexer l;
    lex_init(&l, src);
    g_parse_error_count = 0;
    g_parse_err_msg[0] = 0;
    g_parse_err_first[0] = 0;
    g_parse_lexer = &l;
    g_parse_first_after_lex = 0;
    g_loop_depth = 0;
    g_stmt_sep = 0;
    g_parse_q_call_hint = src_has_semi_in_parens(src) || src_has_table_lbrack(src);
    Node *prog = node_new(N_BLOCK);
    W(lex_peek(&l).type != T_EOF_ && !l.failed,{
        Token pk = lex_peek(&l);
        if(pk.type == T_INDENT_) {
            parse_fail("parse error: unexpected indent");
            break;
        }
        if(pk.type == T_DEDENT_ || pk.type == T_NEWLINE_ || pk.type == T_SEMI_) {
            lex_next(&l);
            continue;
        }
        g_stmt_sep = 0;
        Node *s = parse_stmt(&l);
        if(s) node_add(prog, s);
        if(!stmt_boundary_ok(&l)) break;
    })
    if (g_parse_error_count > 0 && g_parse_q_call_hint && !g_parse_first_after_lex) {
        /* Only a generic first error (an unexpected or mismatched token)
         * is replaced by the hint; unexpected-character and the specific
         * messages stay. Prefer table() kwargs hint when table([…] is
         * present. */
        if (strstr(g_parse_err_first, "unexpected") ||
            strstr(g_parse_err_first, ", got ")) {
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
    }
    if(!l.failed && g_parse_error_count == 0 && tree_too_deep(prog, SHAKTI_EXPR_MAX_DEPTH))
        lex_fail(&l, "expression nesting too deep");
    g_parse_lexer = NULL;
    if(l.failed && !g_error && (!g_parse_error_count || g_parse_first_after_lex)) {
        g_error = 1;
        g_error_val = v_errf("parse: %s", l.error[0] ? l.error : "invalid input");
        /* parse_check() reports what the error value holds */
        snprintf(g_parse_err_msg, sizeof g_parse_err_msg, "%s", g_error_val->s);
    }
    lex_free(&l);
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
