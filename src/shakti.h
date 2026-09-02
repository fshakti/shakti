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
#define SHAKTI_TOKEN_TEXT 8192

typedef struct Node Node;
typedef struct Env Env;
typedef struct V V;

typedef struct {
    int type;
    int line;
    int64_t ival;
    double fval;
    char sval[SHAKTI_TOKEN_TEXT];
} Token;

typedef struct Lexer {
    const char *src;
    size_t len, pos;
    int line;
    int at_line_start;
    int indent_stack[SHAKTI_INDENT_STACK];
    int indent_top;
    int paren_depth;
    int parse_depth;
    int emit_newline;
    int pending_dedents;
    int has_peek;
    int failed;
    char error[128];
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
    int fn_ast_i;
};

struct Env {
    int rc;
    int cap, len;
    char **names;
    V **vals;
    uint32_t *hashes;
    Env *parent;
};

/* Buffer ownership for vector/matrix payloads (J/F/B). */
enum {
    V_OWNER_MALLOC = 0,   /* free() buffers in v_free (default) */
    V_OWNER_MAP_ALIAS     /* buffers alias an IefsMapRegion; munmap via map_reg */
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
    int owner_kind;   /* V_OWNER_* */
    void *map_reg;    /* IefsMapRegion* when V_OWNER_MAP_ALIAS */
};

enum {
    T_INT_ = 0,
    T_FLOAT_ = 1,
    T_STR_ = 2,
    T_FSTR_ = 3,
    T_DATETIME_ = 4,
    T_TRUE_ = 5,
    T_FALSE_ = 6,
    T_NONE_ = 7,
    T_NAME_ = 8,
    T_RPAREN_ = 9,
    T_RBRACKET_ = 10,
    T_RBRACE_ = 11,
    T_MINUS_ = 12,
    T_EOF_ = 13,
    T_LPAREN_ = 14,
    T_DOT_ = 15,
    T_LBRACKET_ = 16,
    T_AT_ = 17,
    T_DEDENT_ = 18,
    T_NEWLINE_ = 19,
    T_INDENT_ = 20,
    T_DEF_ = 21,
    T_RETURN_ = 22,
    T_IF_ = 23,
    T_ELIF_ = 24,
    T_ELSE_ = 25,
    T_WHILE_ = 26,
    T_FOR_ = 27,
    T_IN_ = 28,
    T_BREAK_ = 29,
    T_CONTINUE_ = 30,
    T_AND_ = 31,
    T_OR_ = 32,
    T_NOT_ = 33,
    T_IMPORT_ = 34,
    T_TRY_ = 35,
    T_EXCEPT_ = 36,
    T_FINALLY_ = 37,
    T_AS_ = 38,
    T_LAMBDA_ = 39,
    T_PASS_ = 40,
    T_CLASS_ = 41,
    T_GLOBAL_ = 42,
    T_DEL_ = 43,
    T_RAISE_ = 44,
    T_WITH_ = 45,
    T_YIELD_ = 46,
    T_SELECT_ = 47,
    T_UPDATE_ = 48,
    T_DELETE_ = 49,
    T_BY_ = 50,
    T_FROM_ = 51,
    T_WHERE_ = 52,
    T_CREATE_ = 53,
    T_INSERT_ = 54,
    T_INTO_ = 55,
    T_VALUES_ = 56,
    T_JOIN_ = 57,
    T_ON_ = 58,
    T_PLUSEQ_ = 59,
    T_PLUS_ = 60,
    T_MINUSEQ_ = 61,
    T_DSTAR_ = 62,
    T_STAREQ_ = 63,
    T_STAR_ = 64,
    T_DSLASH_ = 65,
    T_SLASHEQ_ = 66,
    T_SLASH_ = 67,
    T_PERCENT_ = 68,
    T_EQ_ = 69,
    T_NE_ = 70,
    T_LE_ = 71,
    T_LT_ = 72,
    T_GE_ = 73,
    T_GT_ = 74,
    T_LBRACE_ = 75,
    T_COMMA_ = 76,
    T_COLON_ = 77,
    T_SEMI_ = 78,
    T_UNION_ = 79,
    T_OUTER_ = 80,
    T_CHARZ_ = 81,
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
    T_SUBPROCESS = 20,
    T_CHAR = 21,
    T_CVEC = 22,
    T_CMAT = 23,
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
    N_EACH = 17,
    N_FLOAT = 18,
    N_FOR = 19,
    N_FSTRING = 20,
    N_GLOBAL = 21,
    N_IF = 22,
    N_IMPORT = 23,
    N_INDEX = 24,
    N_INSERT = 25,
    N_INT = 26,
    N_JOIN = 27,
    N_KWARG = 28,
    N_LAMBDA = 29,
    N_LIST = 30,
    N_NAME = 31,
    N_NONE = 32,
    N_PASS = 33,
    N_RAISE = 34,
    N_RETURN = 35,
    N_SELECT = 36,
    N_SLICE = 37,
    N_STR = 38,
    N_TRY = 39,
    N_UNOP = 40,
    N_UPDATE = 41,
    N_WHILE = 42,
    N_WITH = 43,
    N_UNION_JOIN = 44,
    N_OUTER_JOIN = 45,
    N_CHARS = 46,
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
    OP_ASOF_COMMA = 19,
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
V *v_cvec(int64_t n);
V *v_char(unsigned char b);
V *v_subprocess(int fd, int64_t pid);
V *v_imat(int64_t rows, int64_t cols);
V *v_fmat(int64_t rows, int64_t cols);
V *v_bmat(int64_t rows, int64_t cols);
V *v_cmat(int64_t rows, int64_t cols);
static inline int64_t mat_cols(V *m) { return (int64_t)m->_ht_cap; }
static inline int64_t mat_idx(V *m, int64_t r, int64_t c) { return r * mat_cols(m) + c; }
static inline int is_vec_t(int t) {
    return t == T_IVEC || t == T_FVEC || t == T_BVEC || t == T_CVEC;
}
static inline int is_mat_t(int t) {
    return t == T_IMAT || t == T_FMAT || t == T_BMAT || t == T_CMAT;
}
V *mat_matmul(V *a, V *b);
V *v_mat_row(V *m, int64_t row);
V *v_list(int64_t n);
void v_list_append(V *v, V *item);
void v_list_append_own(V *v, V *item);
V *v_dict(V *keys, V *vals);
V *v_dict_empty(void);
V *v_dict_own(V *keys, V *vals);
V *v_table(V *cols, V *data);
V *v_table_own(V *cols, V *data);
void v_dict_put(V *d, const char *key, V *val);
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
int shakti_parse_check(const char *src, char *err, size_t errcap);
int shakti_parse_errors(void);
V *eval(Node *n, Env *e);
/* Evaluate a user-function body with interpreter call-depth accounting. */
V *eval_fn(Node *body, Env *e);
int shakti_lang_main(int argc, char **argv);
V *table_load(const char *path, V *columns_opt);
int table_save(V *table, const char *path);
/* Materialize MAP_ALIAS payloads before in-place mutation. 0 ok, -1 OOM. */
int v_ensure_writable(V *v);
V *method_call(V *obj, const char *method, V **args, int nargs, Env *e);
V *builtin_call(const char *name, V **args, int nargs, V **kwn, V **kwv, int nkw, Env *e);
V *table_sql_select(V *from, V *cols, V *by, V *where);
V *table_sql_update(V *from, V *cols, V *where);
V *table_sql_delete(V *from, V *cols, V *where);
V *table_sql_create_table(V *name, V *cols);
V *table_sql_insert(V *table, V *cols, V *vals);
V *table_sql_join(V *left, V *right, V *on_col);
V *table_comma_join(V *left, V *right);
V *table_outer_join(V *left, V *right);
V *table_union_join(V *left, V *right);
V *list_union(V *left, V *right);
V *table_asof_comma_join(V *left, V *right);
int is_builtin(const char *name);
void builtin_register(Env *e);
V *subprocess(V **a, int n);
int64_t subprocess_send(V *p, V **args, int n);
V *subprocess_status(V *p);
V *subprocess_next(V *p, double timeout);

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
