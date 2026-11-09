/* shakti/src/lex.c — tokenizer */
#include "shakti_internal.h"
#include <errno.h>

static int is_id_start(char c) { return isalpha(c) || c=='_'; }
static int is_id_char(char c)  { return isalnum(c) || c=='_'; }
#define LEX_CHUNK_MIN 65536
struct LexChunk {
    LexChunk *next;
    size_t used, cap;
    char data[];
};
void lex_init(Lexer *l, const char *src) {
    memset(l, 0, sizeof(*l));
    l->src = src; l->len = strlen(src);
    l->indent_stack[0] = 0; l->indent_top = 0;
    l->at_line_start = 1;
}
void lex_free(Lexer *l) {
    while (l->chunk) {
        LexChunk *next = l->chunk->next;
        free(l->chunk);
        l->chunk = next;
    }
    free(l->text);
    l->text = NULL;
}
LexMark lex_mark(const Lexer *l) {
    LexMark m = {l->chunk, l->chunk ? l->chunk->used : 0};
    return m;
}
void lex_release(Lexer *l, LexMark m) {
    while (l->chunk && l->chunk != m.chunk) {
        LexChunk *next = l->chunk->next;
        free(l->chunk);
        l->chunk = next;
    }
    if (l->chunk) l->chunk->used = m.used;
}
static const char *lex_intern(Lexer *l, const char *s, size_t n) {
    LexChunk *c = l->chunk;
    if (!c || c->cap - c->used < n + 1) {
        size_t cap = n + 1 > LEX_CHUNK_MIN ? n + 1 : LEX_CHUNK_MIN;
        c = x_malloc(sizeof *c + cap, "lex_intern");
        c->next = l->chunk;
        c->used = 0;
        c->cap = cap;
        l->chunk = c;
    }
    char *d = c->data + c->used;
    memcpy(d, s, n);
    d[n] = 0;
    c->used += n + 1;
    return d;
}
static void skip_comment(Lexer *l) {
    W(l->pos < l->len && l->src[l->pos] != '\n',l->pos++)
}
static Token make_tok(int type) { Token t = {0}; t.type = type; t.sval = ""; return t; }
void lex_fail(Lexer *l, const char *message) {
    if (!l->failed) {
        l->failed = 1;
        snprintf(l->error, sizeof l->error, "%s", message);
    }
    l->pos = l->len;
    l->pending_dedents = 0;
    l->emit_newline = 0;
    l->has_peek = 0;
}
static int lex_append(Lexer *l, Token *t, int *len, char ch) {
    (void)t;
    if (*len >= SHAKTI_TOKEN_TEXT - 1) {
        lex_fail(l, "token text exceeds 8191 bytes");
        return 0;
    }
    if (!l->text) l->text = x_malloc(SHAKTI_TOKEN_TEXT, "lex_text");
    l->text[(*len)++] = ch;
    return 1;
}
static void lex_finish_text(Lexer *l, Token *t, int len) {
    t->sval = lex_intern(l, l->text ? l->text : "", (size_t)len);
}
/* The character after a backslash in a quoted string (single or triple quotes): \n \t \r \0 \\
   \' \" are decoded; any other keeps its backslash. */
static int lex_append_escape(Lexer *l, Token *t, int *len, char ch) {
    switch(ch) {
    case 'n': return lex_append(l, t, len, '\n');
    case 't': return lex_append(l, t, len, '\t');
    case 'r': return lex_append(l, t, len, '\r');
    case '0':
        lex_fail(l, "parse error: NUL in string");
        return 0;
    case '\\': case '\'': case '"': return lex_append(l, t, len, ch);
    default: return lex_append(l, t, len, '\\') && lex_append(l, t, len, ch);
    }
}

/* Update noun_pos after emitting a token (kparser-style left context). */
static void lex_note_token(Lexer *l, Token t) {
    switch (t.type) {
    case T_INT_: case T_FLOAT_: case T_STR_: case T_FSTR_: case T_DATETIME_:
    case T_TRUE_: case T_FALSE_: case T_NONE_: case T_NAME_: case T_CHARZ_:
    case T_CVECZ_:
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

/* Decimal digits from *pp on, with `_` as a separator (Python): each `_` sits between two digits. */
static int lex_digits(Lexer *l, size_t *pp, int *under) {
    const char *s = l->src;
    size_t p = *pp;
    W(p<l->len && (isdigit((unsigned char)s[p]) || s[p]=='_'), {
        if (s[p] == '_') {
            if (p == 0 || !isdigit((unsigned char)s[p - 1]) || p + 1 >= l->len
                || !isdigit((unsigned char)s[p + 1])) {
                lex_fail(l, "invalid number: '_' must sit between two digits");
                return 0;
            }
            *under = 1;
        }
        p++;
    })
    *pp = p;
    return 1;
}

int lex_peek_is_signed_literal(Lexer *l) {
    if (!l->has_peek)
        lex_peek(l);
    if (l->peek.type != T_MINUS_)
        return 0;
    /* k/q signed-literal convention: whitespace before the '-', none after,
     * digit immediately following. That keeps `abs -1.2` and `1 -2 3` as
     * jux/vector forms while letting both `x - 1` and `x-1` be subtraction. */
    if (l->pos >= 2) {
        char prev = l->src[l->pos - 2];
        if (prev != ' ' && prev != '\t') return 0;
    }
    return lex_src_is_digit_start(l, l->pos);
}
int lex_peek_minus_is_subtraction(Lexer *l) {
    if (!l->has_peek)
        lex_peek(l);
    if (l->peek.type != T_MINUS_ || !lex_peek_is_signed_literal(l))
        return 0;
    size_t save_pos = l->pos;
    int save_line = l->line;
    int save_peek = l->has_peek;
    Token save_tok = l->peek;
    LexMark mark = lex_mark(l);
    (void)lex_next(l);
    Token t = lex_peek(l);
    lex_release(l, mark);
    l->pos = save_pos;
    l->line = save_line;
    l->has_peek = save_peek;
    l->peek = save_tok;
    return t.type == T_INT_ || t.type == T_FLOAT_;
}
static Token lex_fstring(Lexer *l) {
    const char *s = l->src;
    size_t p = l->pos;
    char q = s[p]; p++;
    Token t = {.type = T_FSTR_};
    int qi = 0;
    while(p < l->len && s[p] != q) {
        if(s[p]=='{' && p+1<l->len && s[p+1]=='{') {
            if(!lex_append(l, &t, &qi, '{')) return make_tok(T_EOF_);
            if(!lex_append(l, &t, &qi, '{')) return make_tok(T_EOF_);
            p += 2;
        } else if(s[p]=='}' && p+1<l->len && s[p+1]=='}') {
            if(!lex_append(l, &t, &qi, '}')) return make_tok(T_EOF_);
            if(!lex_append(l, &t, &qi, '}')) return make_tok(T_EOF_);
            p += 2;
        } else if(s[p]=='{') {
            if(!lex_append(l, &t, &qi, '{')) return make_tok(T_EOF_);
            p++;
            int depth = 1;
            while(p < l->len && depth > 0) {
                if(s[p]=='{') depth++;
                else if(s[p]=='}') { depth--; if(depth==0) break; }
                if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                p++;
            }
            if(p < l->len) {
                if(!lex_append(l, &t, &qi, '}')) return make_tok(T_EOF_);
                p++;
            }
        } else if(s[p]=='\\' && p+1<l->len) {
            p++;
            if (s[p] == '{' || s[p] == '}') {
                if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                p++;
                continue;
            }
            char escaped;
            switch(s[p]) {
            case 'n': escaped='\n'; break;
            case 't': escaped='\t'; break;
            case '\\': escaped='\\'; break;
            case '\'': escaped='\''; break;
            case '"': escaped='"'; break;
            default: escaped=s[p]; break;
            }
            if(!lex_append(l, &t, &qi, escaped)) return make_tok(T_EOF_);
            p++;
        } else {
            if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
            p++;
        }
    }
    lex_finish_text(l, &t, qi);
    if(p < l->len) p++;
    l->pos = p;
    return t;
}
static const struct { const char *s; int type; } lex_keywords[] = {
    {"def", T_DEF_},
    {"return", T_RETURN_},
    {"if", T_IF_},
    {"elif", T_ELIF_},
    {"else", T_ELSE_},
    {"while", T_WHILE_},
    {"for", T_FOR_},
    {"in", T_IN_},
    {"break", T_BREAK_},
    {"continue", T_CONTINUE_},
    {"and", T_AND_},
    {"or", T_OR_},
    {"not", T_NOT_},
    {"True", T_TRUE_},
    {"False", T_FALSE_},
    {"None", T_NONE_},
    {"import", T_IMPORT_},
    {"try", T_TRY_},
    {"except", T_EXCEPT_},
    {"finally", T_FINALLY_},
    {"as", T_AS_},
    {"lambda", T_LAMBDA_},
    {"pass", T_PASS_},
    {"class", T_CLASS_},
    {"global", T_GLOBAL_},
    {"del", T_DEL_},
    {"raise", T_RAISE_},
    {"with", T_WITH_},
    {"yield", T_YIELD_},
    {"select", T_SELECT_},
    {"update", T_UPDATE_},
    {"delete", T_DELETE_},
    {"by", T_BY_},
    {"from", T_FROM_},
    {"where", T_WHERE_},
    {"create", T_CREATE_},
    {"insert", T_INSERT_},
    {"into", T_INTO_},
    {"values", T_VALUES_},
    {"join", T_JOIN_},
    {"on", T_ON_},
    {"union", T_UNION_},
    {"outer", T_OUTER_},
};
static Token lex_raw(Lexer *l) {
    const char *s = l->src;
    for(;;) {
        size_t p = l->pos;
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
                continue;
            }
            if(l->paren_depth > 0) continue;
            int cur = l->indent_stack[l->indent_top];
            if(indent > cur) {
                if(l->indent_top + 1 >= SHAKTI_INDENT_STACK) {
                    lex_fail(l, "indentation nesting exceeds 255 levels");
                    return make_tok(T_EOF_);
                }
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
            if(l->indent_top > 0) {
                l->pending_dedents = l->indent_top - 1;
                l->indent_top = 0;
                return make_tok(T_DEDENT_);
            }
            return make_tok(T_EOF_);
        }
        char c = s[p];
        if(c == '\n') {
            l->pos = p+1;
            l->at_line_start = 1;
            if(l->paren_depth > 0) continue;
            return make_tok(T_NEWLINE_);
        }
        if(c == '#') { skip_comment(l); continue; }
        if((c=='"'||c=='\'') && p+2<l->len && s[p+1]==c && s[p+2]==c) {
            Token t = {.type = T_STR_}; int qi = 0;
            char q = c; p += 3;
            while(p+2 < l->len && !(s[p]==q && s[p+1]==q && s[p+2]==q)) {
                if(s[p]=='\\' && p+1<l->len) {
                    p++;
                    if(!lex_append_escape(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                } else if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                p++;
            }
            lex_finish_text(l, &t, qi);
            if(p+2 < l->len) p += 3;
            l->pos = p;
            return t;
        }
        if(c == '"' || c == '\'') {
            Token t = {.type = T_STR_}; int qi = 0;
            char q = c; p++;
            while(p < l->len && s[p] != q) {
                if(s[p]=='\\' && p+1<l->len) {
                    p++;
                    if(!lex_append_escape(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                } else if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                p++;
            }
            lex_finish_text(l, &t, qi);
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
                Token t = {.type = T_DATETIME_};
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
                /* -0c… is unary minus of a blob, not a signed integer. */
                int blob0c = p + 3 < l->len && s[p + 1] == '0' && (s[p + 2] == 'c' || s[p + 2] == 'C');
                if (!blob0c) {
                    neg_lit = 1;
                    p++;
                    c = s[p];
                }
            }
            if (isdigit((unsigned char)c) || (c == '.' && p + 1 < l->len && isdigit((unsigned char)s[p + 1]))) {
            Token t = {.type = T_INT_};
            size_t start = p;
            int is_float = 0;
            if (!neg_lit && c == '0' && p + 2 < l->len && (s[p + 1] == 'c' || s[p + 1] == 'C')
                && isxdigit((unsigned char)s[p + 2])) {
                p += 2;
                size_t hex0 = p;
                W(p < l->len && isxdigit((unsigned char)s[p]), p++)
                size_t ndig = p - hex0;
                if (ndig >= 2 && (ndig % 2) == 0 && ndig < SHAKTI_TOKEN_TEXT) {
                    Token ct = {.type = T_CVECZ_};
                    ct.sval = lex_intern(l, s + hex0, ndig);
                    ct.ival = (int64_t)(ndig / 2);
                    ct.line = l->line;
                    l->pos = p;
                    return ct;
                }
                p = start;
            }
            if(c=='0' && p+1<l->len && (s[p+1]=='x'||s[p+1]=='X')) {
                p += 2;
                size_t hex0 = p;
                W(p<l->len && isxdigit((unsigned char)s[p]),p++)
                size_t ndig = p - hex0;
                if (!neg_lit && ndig == 2) {
                    Token ct = {.type = T_CHARZ_};
                    ct.ival = strtoll(s + start, NULL, 16);
                    ct.line = l->line;
                    l->pos = p;
                    return ct;
                }
                if (!neg_lit && ndig >= 4 && (ndig % 2) == 0 && ndig != 6 && ndig != 8
                    && ndig < SHAKTI_TOKEN_TEXT) {
                    Token ct = {.type = T_CVECZ_};
                    ct.sval = lex_intern(l, s + hex0, ndig);
                    ct.ival = (int64_t)(ndig / 2);
                    ct.line = l->line;
                    l->pos = p;
                    return ct;
                }
                t.ival = strtoll(s+start, NULL, 16);
                if (neg_lit) t.ival = -t.ival;
            } else {
                int under = 0;
                if(!lex_digits(l, &p, &under)) return make_tok(T_EOF_);
                if(p<l->len && s[p]=='.') { is_float=1; p++; if(!lex_digits(l, &p, &under)) return make_tok(T_EOF_); }
                if(p<l->len && (s[p]=='e'||s[p]=='E')) {
                    is_float=1; p++;
                    if(p<l->len && (s[p]=='+'||s[p]=='-')) p++;
                    if(!lex_digits(l, &p, &under)) return make_tok(T_EOF_);
                }
                if (is_float && p < l->len && s[p] == '.' && p + 1 < l->len && isdigit((unsigned char)s[p + 1])) {
                    lex_fail(l, "invalid number");
                    return make_tok(T_EOF_);
                }
                /* With separators, convert the digits alone, gathered in the lexer's text scratch. */
                const char *lit = s + start;
                if(under) {
                    int n = 0;
                    for(size_t i = start; i < p; i++)
                        if(s[i] != '_' && !lex_append(l, &t, &n, s[i])) return make_tok(T_EOF_);
                    if(!lex_append(l, &t, &n, 0)) return make_tok(T_EOF_);
                    lit = l->text;
                }
                if(is_float) {
                    t.type=T_FLOAT_;
                    t.fval=strtod(lit,NULL);
                    if (neg_lit) t.fval = -t.fval;
                } else {
                    errno = 0;
                    t.ival = strtoll(lit, NULL, 10);
                    if (errno == ERANGE) {
                        t.type = T_FLOAT_;
                        t.fval = strtod(lit, NULL);
                        if (neg_lit) t.fval = -t.fval;
                    } else if (neg_lit) {
                        t.ival = -t.ival;
                    }
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
                Token t = {.type = T_STR_}; int qi = 0;
                p++; char q = s[p]; p++;
                while(p < l->len && s[p] != q) {
                    if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                    p++;
                }
                lex_finish_text(l, &t, qi);
                if(p < l->len) p++;
                l->pos = p;
                return t;
            }
            if((c=='b'||c=='B') && p+1<l->len && (s[p+1]=='"'||s[p+1]=='\'')) {
                /* There is no bytes type: b"abc" is the str "abc" (#121). */
                p++;
                char q = s[p];
                p++;
                Token bt = {.type = T_STR_};
                int qi = 0;
                while (p < l->len && s[p] != q) {
                    if (s[p] == '\\' && p + 1 < l->len) {
                        p++;
                        if (!lex_append_escape(l, &bt, &qi, s[p])) return make_tok(T_EOF_);
                    } else if (!lex_append(l, &bt, &qi, s[p])) return make_tok(T_EOF_);
                    p++;
                }
                lex_finish_text(l, &bt, qi);
                if (p < l->len) p++;
                l->pos = p;
                return bt;
            }
            Token t = {.type = T_NAME_};
            size_t start = p;
            W(p<l->len && is_id_char(s[p]),p++)
            size_t n = p - start;
            if(n >= SHAKTI_TOKEN_TEXT) n = SHAKTI_TOKEN_TEXT-1;
            l->pos = p;
            for(size_t k = 0; k < sizeof lex_keywords / sizeof lex_keywords[0]; k++) {
                const char *kw = lex_keywords[k].s;
                if(!strncmp(kw, s+start, n) && kw[n] == 0) {
                    t.type = lex_keywords[k].type;
                    t.sval = kw;
                    return t;
                }
            }
            t.sval = lex_intern(l, s+start, n);
            return t;
        }
        l->pos = p+1;
        switch(c) {
        case '+': if(p+1<l->len && (s[p+1]=='=' || s[p+1]==':')) { l->pos=p+2; return make_tok(T_PLUSEQ_); } return make_tok(T_PLUS_);
        case '-': if(p+1<l->len && (s[p+1]=='=' || s[p+1]==':')) { l->pos=p+2; return make_tok(T_MINUSEQ_); } return make_tok(T_MINUS_);
        case '*':
            if(p+1<l->len && s[p+1]=='*') {
                if(p+2<l->len && (s[p+2]=='=' || s[p+2]==':')) { l->pos=p+3; return make_tok(T_DSTAREQ_); }
                l->pos=p+2; return make_tok(T_DSTAR_);
            }
            if(p+1<l->len && (s[p+1]=='=' || s[p+1]==':')) { l->pos=p+2; return make_tok(T_STAREQ_); }
            return make_tok(T_STAR_);
        case '/':
            if(p+1<l->len && s[p+1]=='/') {
                if(p+2<l->len && (s[p+2]=='=' || s[p+2]==':')) { l->pos=p+3; return make_tok(T_DSLASHEQ_); }
                l->pos=p+2; return make_tok(T_DSLASH_);
            }
            if(p+1<l->len && (s[p+1]=='=' || s[p+1]==':')) { l->pos=p+2; return make_tok(T_SLASHEQ_); }
            return make_tok(T_SLASH_);
        case '%':
            if(p+1<l->len && (s[p+1]=='=' || s[p+1]==':')) { l->pos=p+2; return make_tok(T_PERCENTEQ_); }
            return make_tok(T_PERCENT_);
        case '=': return make_tok(T_EQ_);
        case '!': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_NE_); } break;
        case '<': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_LE_); } return make_tok(T_LT_);
        case '>': if(p+1<l->len && s[p+1]=='=') { l->pos=p+2; return make_tok(T_GE_); } return make_tok(T_GT_);
        case '(':
            if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
                lex_fail(l, "nesting too deep");
                return make_tok(T_EOF_);
            }
            l->paren_depth++;
            return make_tok(T_LPAREN_);
        case ')': if (l->paren_depth > 0) l->paren_depth--; return make_tok(T_RPAREN_);
        case '[':
            if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
                lex_fail(l, "nesting too deep");
                return make_tok(T_EOF_);
            }
            l->paren_depth++;
            return make_tok(T_LBRACKET_);
        case ']': if (l->paren_depth > 0) l->paren_depth--; return make_tok(T_RBRACKET_);
        case '{':
            if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
                lex_fail(l, "nesting too deep");
                return make_tok(T_EOF_);
            }
            l->paren_depth++;
            return make_tok(T_LBRACE_);
        case '}': if (l->paren_depth > 0) l->paren_depth--; return make_tok(T_RBRACE_);
        case ',': return make_tok(T_COMMA_);
        case ':': return make_tok(T_COLON_);
        case '.': return make_tok(T_DOT_);
        case ';': return make_tok(T_SEMI_);
        case '@': return make_tok(T_AT_);
        }
        {
            char shown = (c >= 32 && c < 127) ? (char)c : '?';
            parse_fail("parse error: unexpected character '%c' (0x%02x)", shown, (unsigned char)c);
            return make_tok(T_NEWLINE_);
        }
    }
}
static Token lex_tok(Lexer *l) {
    Token t = lex_raw(l);
    if(!t.sval) t.sval = "";
    return t;
}
Token lex_next(Lexer *l) {
    Token t;
    if(l->has_peek) { l->has_peek=0; t = l->peek; }
    else t = lex_tok(l);
    lex_note_token(l, t);
    return t;
}
Token lex_peek(Lexer *l) {
    if(!l->has_peek) { l->peek = lex_tok(l); l->has_peek=1; }
    return l->peek;
}
