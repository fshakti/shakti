/* shakti/src/shakti.h — language/runtime public header (generated) */
#ifndef SHAKTI_H
#define SHAKTI_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#if defined(_WIN32) && !defined(_USE_MATH_DEFINES)
#define _USE_MATH_DEFINES 1
#endif
#include <math.h>
#include <ctype.h>
#include "a.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_FN
#define MAX_FN 4096
#endif
#define SHAKTI_INDENT_STACK 256

typedef struct Node Node;
typedef struct Env Env;
typedef struct V V;

typedef struct {
    int type;
    int line;
    int64_t ival;
    double fval;
    char sval[8192];
} Token;

typedef struct Lexer {
    const char *src;
    size_t len, pos;
    int line;
    int at_line_start;
    int indent_stack[SHAKTI_INDENT_STACK];
    int indent_top;
    int paren_depth;
    int emit_newline;
    int pending_dedents;
    int has_peek;
    Token peek;
    /* 1 after a noun/literal/closer: next '-' before a digit is subtract, not sign. */
    int noun_pos;
} Lexer;

struct Node {
    int type;
    char *sval;
    int64_t ival;
    int64_t j;
    double fval;
    int op;
    Node **ch;
    int nch;
};

struct Env {
    int rc;
    int cap, len;
    char **names;
    V **vals;
    uint32_t *hashes;
    Env *parent;
};

struct V {
    int t;
    int rc;
    int64_t n;
    int b;
    int64_t j;
    double f;
    char *s;
    int64_t *J;
    double *F;
    unsigned char *B;
    V **L;
    V *keys, *vals;
    uint32_t *_ht;
    uint32_t _ht_cap;
    V *params, *defaults;
    Env *closure;
};

enum {
    T_FSTR_ = 0,
    T_DEDENT_ = 1,
    T_NEWLINE_ = 2,
    T_INDENT_ = 3,
    T_EOF_ = 4,
    T_STR_ = 5,
    T_DATETIME_ = 6,
    T_INT_ = 7,
    T_FLOAT_ = 8,
    T_NAME_ = 9,
    T_DEF_ = 10,
    T_RETURN_ = 11,
    T_IF_ = 12,
    T_ELIF_ = 13,
    T_ELSE_ = 14,
    T_WHILE_ = 15,
    T_FOR_ = 16,
    T_IN_ = 17,
    T_BREAK_ = 18,
    T_CONTINUE_ = 19,
    T_AND_ = 20,
    T_OR_ = 21,
    T_NOT_ = 22,
    T_TRUE_ = 23,
    T_FALSE_ = 24,
    T_NONE_ = 25,
    T_IMPORT_ = 26,
    T_TRY_ = 27,
    T_EXCEPT_ = 28,
    T_FINALLY_ = 29,
    T_AS_ = 30,
    T_LAMBDA_ = 31,
    T_PASS_ = 32,
    T_CLASS_ = 33,
    T_GLOBAL_ = 34,
    T_DEL_ = 35,
    T_RAISE_ = 36,
    T_WITH_ = 37,
    T_YIELD_ = 38,
    T_SELECT_ = 39,
    T_UPDATE_ = 40,
    T_DELETE_ = 41,
    T_BY_ = 42,
    T_FROM_ = 43,
    T_WHERE_ = 44,
    T_CREATE_ = 45,
    T_INSERT_ = 46,
    T_INTO_ = 47,
    T_VALUES_ = 48,
    T_JOIN_ = 49,
    T_ON_ = 50,
    T_PLUSEQ_ = 51,
    T_PLUS_ = 52,
    T_MINUSEQ_ = 53,
    T_MINUS_ = 54,
    T_DSTAR_ = 55,
    T_STAREQ_ = 56,
    T_STAR_ = 57,
    T_DSLASH_ = 58,
    T_SLASHEQ_ = 59,
    T_SLASH_ = 60,
    T_PERCENT_ = 61,
    T_EQ_ = 62,
    T_NE_ = 63,
    T_LE_ = 64,
    T_LT_ = 65,
    T_GE_ = 66,
    T_GT_ = 67,
    T_LPAREN_ = 68,
    T_RPAREN_ = 69,
    T_LBRACKET_ = 70,
    T_RBRACKET_ = 71,
    T_LBRACE_ = 72,
    T_RBRACE_ = 73,
    T_COMMA_ = 74,
    T_COLON_ = 75,
    T_DOT_ = 76,
    T_SEMI_ = 77,
    T_AT_ = 78,
};

enum {
    T_NIL = 0,
    T_BOOL = 1,
    T_INT = 2,
    T_FLOAT = 3,
    T_STR = 4,
    T_DATE = 5,
    T_ERR = 6,
    T_IVEC = 7,
    T_FVEC = 8,
    T_BVEC = 9,
    T_LIST = 10,
    T_DICT = 11,
    T_TABLE = 12,
    T_FN = 13,
    T_DATETIME = 14,
    T_TIME = 15,
    T_INPUT = 16,
    T_IMAT = 17,
    T_FMAT = 18,
    T_BMAT = 19,
};

enum {
    N_ASSIGN = 0,
    N_AUGASSIGN = 1,
    N_BINOP = 2,
    N_BLOCK = 3,
    N_BOOL = 4,
    N_BREAK = 5,
    N_CALL = 6,
    N_CLASS = 7,
    N_CMP = 8,
    N_CONTINUE = 9,
    N_CREATE_TABLE = 10,
    N_DATETIME = 11,
    N_DEF = 12,
    N_DEL = 13,
    N_DELETE = 14,
    N_DICT = 15,
    N_DOT = 16,
    N_FLOAT = 17,
    N_FOR = 18,
    N_FSTRING = 19,
    N_GLOBAL = 20,
    N_IF = 21,
    N_IMPORT = 22,
    N_INDEX = 23,
    N_INSERT = 24,
    N_INT = 25,
    N_JOIN = 26,
    N_KWARG = 27,
    N_LAMBDA = 28,
    N_LIST = 29,
    N_NAME = 30,
    N_NONE = 31,
    N_PASS = 32,
    N_RAISE = 33,
    N_RETURN = 34,
    N_SELECT = 35,
    N_SLICE = 36,
    N_STR = 37,
    N_TRY = 38,
    N_UNOP = 39,
    N_UPDATE = 40,
    N_WHILE = 41,
    N_WITH = 42,
};

enum {
    OP_ADD = 0,
    OP_AND = 1,
    OP_DIV = 2,
    OP_EQ = 3,
    OP_FLOORDIV = 4,
    OP_GE = 5,
    OP_GT = 6,
    OP_IN = 7,
    OP_LE = 8,
    OP_LT = 9,
    OP_MOD = 10,
    OP_MUL = 11,
    OP_NE = 12,
    OP_NEG = 13,
    OP_NOT = 14,
    OP_NOT_IN = 15,
    OP_OR = 16,
    OP_POW = 17,
    OP_SUB = 18,
    OP_MATMUL = 19,
};

void lex_init(Lexer *l, const char *src);
Token lex_next(Lexer *l);
Token lex_peek(Lexer *l);
Node *node_new(int type);
void node_add(Node *n, Node *child);
void node_free(Node *n);
V *v_nil(void);
V *v_bool(int b);
V *v_int(int64_t j);
V *v_float(double f);
V *v_str(const char *s);
V *v_str_take(char *s);
V *v_date(int64_t utc_midnight_ms);
V *v_time(int64_t ms_since_midnight);
V *v_err(const char *s);
V *v_errf(const char *fmt, ...);
V *v_ivec(int64_t n);
V *v_fvec(int64_t n);
V *v_bvec(int64_t n);
V *v_imat(int64_t rows, int64_t cols);
V *v_fmat(int64_t rows, int64_t cols);
V *v_bmat(int64_t rows, int64_t cols);
static inline int64_t mat_cols(V *m) { return (int64_t)m->_ht_cap; }
static inline int64_t mat_idx(V *m, int64_t r, int64_t c) { return r * mat_cols(m) + c; }
V *v_mat_row(V *m, int64_t row);
V *v_list(int64_t n);
void v_list_append(V *v, V *item);
V *v_dict(V *keys, V *vals);
V *v_table(V *cols, V *data);
V *v_fn(V *params, V *defaults, Node *body_ast, Env *closure);
V *v_datetime(int64_t ms_utc);
V *v_ref(V *v);
void v_free(V *v);
V *v_copy(V *v);
void v_print(V *v, int nl);
char *v_repr(V *v);
char *v_to_str(V *v);
Env *env_new(Env *parent);
void env_set(Env *e, const char *name, V *val);
void env_set_local(Env *e, const char *name, V *val);
V *env_get(Env *e, const char *name);
void env_ref(Env *e);
void env_free(Env *e);
extern Node *fn_ast[MAX_FN];
extern int fn_ast_n;
extern int g_returning;
extern int g_breaking;
extern int g_continuing;
extern int g_error;
extern V *g_retval;
extern V *g_error_val;
int fn_ast_store(Node *n);
void v_dict_set(V *d, const char *key, V *val);
V *v_dict_get(V *d, const char *key);
int env_save(Env *e, const char *path);
int env_load(Env *e, const char *path);
int shakti_parse_datetime_ms(const char *s, int64_t *out_ms);
void shakti_format_datetime_ms(int64_t ms, char *buf, size_t cap);
int shakti_parse_date_ymd(const char *s, int64_t *out_ms);
void shakti_format_date_ms(int64_t utc_midnight_ms, char *buf, size_t cap);
void shakti_format_time_ms(int64_t ms_in_day, char *buf, size_t cap);
const char *type_name(int t);
V *vec_cmp(V *a, V *b, int op);
Node *parse(const char *src);
void node_sprint(Node *n, FILE *fp);
V *eval(Node *n, Env *e);
int shakti_lang_main(int argc, char **argv);
V *table_load(const char *path, V *columns_opt);
int table_save(V *table, const char *path);
V *method_call(V *obj, const char *method, V **args, int nargs, Env *e);
V *builtin_call(const char *name, V **args, int nargs, V **kwn, V **kwv, int nkw, Env *e);
V *table_sql_select(V *from, V *cols, V *by, V *where);
V *table_sql_update(V *from, V *cols, V *where);
V *table_sql_delete(V *from, V *cols, V *where);
V *table_sql_create_table(V *name, V *cols);
V *table_sql_insert(V *table, V *cols, V *vals);
V *table_sql_join(V *left, V *right, V *on_col);
int is_builtin(const char *name);
void builtin_register(Env *e);

#ifdef _WIN32
FILE *win_open_memstream(char **ptr, size_t *sizeloc);
void win_close_memstream(FILE *fp, char **ptr, size_t *sizeloc);
#define OPEN_MEMSTREAM(ptr,sz) win_open_memstream((ptr),(sz))
#define CLOSE_MEMSTREAM(fp,ptr,sz) win_close_memstream((fp),(ptr),(sz))
#else
#define OPEN_MEMSTREAM(ptr,sz) open_memstream((ptr),(sz))
#define CLOSE_MEMSTREAM(fp,ptr,sz) do { fclose(fp); } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_H */
