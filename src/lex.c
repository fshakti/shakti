/* shakti/src/lex.c — tokenizer */
#include "shakti_internal.h"

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
void lex_fail(Lexer *l, const char *message) {
    if (!l->failed) {
        l->failed = 1;
        snprintf(l->error, sizeof l->error, "%s", message);
    }
    l->pos = l->len;
    l->pending_dedents = 0;
    l->emit_newline = 0;
}
static int lex_append(Lexer *l, Token *t, int *len, char ch) {
    if ((size_t)*len >= sizeof t->sval - 1) {
        lex_fail(l, "token text exceeds 8191 bytes");
        return 0;
    }
    t->sval[(*len)++] = ch;
    return 1;
}

/* Update noun_pos after emitting a token (kparser-style left context). */
static void lex_note_token(Lexer *l, Token t) {
    switch (t.type) {
    case T_INT_: case T_FLOAT_: case T_STR_: case T_FSTR_: case T_DATETIME_:
    case T_TRUE_: case T_FALSE_: case T_NONE_: case T_NAME_: case T_CHARZ_:
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
    (void)lex_next(l);
    Token t = lex_peek(l);
    l->pos = save_pos;
    l->line = save_line;
    l->has_peek = save_peek;
    l->peek = save_tok;
    return t.type == T_INT_;
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
            p += 2;
        } else if(s[p]=='}' && p+1<l->len && s[p+1]=='}') {
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
    t.sval[qi] = 0;
    if(p < l->len) p++;
    l->pos = p;
    return t;
}
static Token lex_raw(Lexer *l) {
    const char *s = l->src;
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
            return lex_raw(l);
        }
        P(l->paren_depth > 0,lex_raw(l))
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
        P(l->paren_depth > 0,lex_raw(l))
        return make_tok(T_NEWLINE_);
    }
    if(c == '#') { skip_comment(l); return lex_raw(l); }
    if((c=='"'||c=='\'') && p+2<l->len && s[p+1]==c && s[p+2]==c) {
        Token t = {.type = T_STR_}; int qi = 0;
        char q = c; p += 3;
        while(p+2 < l->len && !(s[p]==q && s[p+1]==q && s[p+2]==q)) {
            if(s[p]=='\\' && p+1<l->len) {
                p++;
                char escaped;
                switch(s[p]) {
                case 'n': escaped='\n'; break;
                case 't': escaped='\t'; break;
                case '\\': escaped='\\'; break;
                default: escaped=s[p]; break;
                }
                if(!lex_append(l, &t, &qi, escaped)) return make_tok(T_EOF_);
            } else if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
            p++;
        }
        t.sval[qi] = 0;
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
                switch(s[p]) {
                case 'n': if(!lex_append(l,&t,&qi,'\n')) return make_tok(T_EOF_); break;
                case 't': if(!lex_append(l,&t,&qi,'\t')) return make_tok(T_EOF_); break;
                case '\\': if(!lex_append(l,&t,&qi,'\\')) return make_tok(T_EOF_); break;
                case '\'': if(!lex_append(l,&t,&qi,'\'')) return make_tok(T_EOF_); break;
                case '"': if(!lex_append(l,&t,&qi,'"')) return make_tok(T_EOF_); break;
                case 'r': if(!lex_append(l,&t,&qi,'\r')) return make_tok(T_EOF_); break;
                case '0': if(!lex_append(l,&t,&qi,'\0')) return make_tok(T_EOF_); break;
                default:
                    if(!lex_append(l,&t,&qi,'\\') || !lex_append(l,&t,&qi,s[p]))
                        return make_tok(T_EOF_);
                    break;
                }
            } else if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
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
            neg_lit = 1;
            p++;
            c = s[p];
        }
        if (isdigit((unsigned char)c) || (c == '.' && p + 1 < l->len && isdigit((unsigned char)s[p + 1]))) {
        Token t = {.type = T_INT_};
        size_t start = p;
        int is_float = 0;
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
            Token t = {.type = T_STR_}; int qi = 0;
            p++; char q = s[p]; p++;
            while(p < l->len && s[p] != q) {
                if(!lex_append(l, &t, &qi, s[p])) return make_tok(T_EOF_);
                p++;
            }
            t.sval[qi] = 0;
            if(p < l->len) p++;
            l->pos = p;
            return t;
        }
        if((c=='b'||c=='B') && p+1<l->len && (s[p+1]=='"'||s[p+1]=='\'')) {
            p++;
        }
        Token t = {.type = T_NAME_};
        size_t start = p;
        W(p<l->len && is_id_char(s[p]),p++)
        size_t n = p - start;
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
        else if(!strcmp(t.sval,"union"))    t.type=T_UNION_;
        else if(!strcmp(t.sval,"outer"))    t.type=T_OUTER_;
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
    case '(':
        if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
            lex_fail(l, "nesting too deep");
            return make_tok(T_EOF_);
        }
        l->paren_depth++;
        return make_tok(T_LPAREN_);
    case ')': l->paren_depth--; return make_tok(T_RPAREN_);
    case '[':
        if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
            lex_fail(l, "nesting too deep");
            return make_tok(T_EOF_);
        }
        l->paren_depth++;
        return make_tok(T_LBRACKET_);
    case ']': l->paren_depth--; return make_tok(T_RBRACKET_);
    case '{':
        if (l->paren_depth >= SHAKTI_PARSE_MAX_DEPTH) {
            lex_fail(l, "nesting too deep");
            return make_tok(T_EOF_);
        }
        l->paren_depth++;
        return make_tok(T_LBRACE_);
    case '}': l->paren_depth--; return make_tok(T_RBRACE_);
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
