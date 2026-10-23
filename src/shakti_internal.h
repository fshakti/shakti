/* shakti/src/shakti_internal.h — private cross-TU helpers (not public API) */
#ifndef SHAKTI_INTERNAL_H
#define SHAKTI_INTERNAL_H

#include "shakti.h"

#ifndef SHAKTI_PARSE_MAX_DEPTH
#define SHAKTI_PARSE_MAX_DEPTH 40
#endif
#ifndef SHAKTI_EXPR_MAX_DEPTH
#define SHAKTI_EXPR_MAX_DEPTH  4096
#endif
#ifndef SHAKTI_CALL_MAX_DEPTH
#define SHAKTI_CALL_MAX_DEPTH  3000
#endif
#ifndef SHAKTI_DESER_MAX_DEPTH
#define SHAKTI_DESER_MAX_DEPTH 512
#endif
#ifndef SHAKTI_DESER_MAX_LEN
#define SHAKTI_DESER_MAX_LEN   (64 * 1024 * 1024)
#endif
#ifndef SHAKTI_DESER_MAX_VARS
#define SHAKTI_DESER_MAX_VARS  65536
#endif
#ifndef SHAKTI_ENV_MAX_NAME
#define SHAKTI_ENV_MAX_NAME    4096
#endif
#ifndef SHAKTI_PRINT_MAX_DEPTH
#define SHAKTI_PRINT_MAX_DEPTH 512
#endif
/* AddressSanitizer's frames are several times larger, so the C stack reserves below are too. */
#if defined(__SANITIZE_ADDRESS__)
#define SHAKTI_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SHAKTI_ASAN 1
#endif
#endif
/* The parser gives "nesting too deep" when less than this much C stack is left below its frame
 * (brackets nest to SHAKTI_PARSE_MAX_DEPTH, but blocks to 255 levels, which the cap doesn't count).
 * One level and reporting a parse error there take under 8 KB (gcc -O2). */
#ifndef SHAKTI_PARSE_STACK_RESERVE
#ifdef SHAKTI_ASAN
#define SHAKTI_PARSE_STACK_RESERVE ((size_t)64 << 10)
#else
#define SHAKTI_PARSE_STACK_RESERVE ((size_t)16 << 10)
#endif
#endif
/* The lowest usable address of the C stack, for the parser's check; set by shakti_stack_start
 * (eval.c), 0 (no check) when the stack is not known. */
extern uintptr_t g_parse_stack_bottom;

#ifdef _WIN32
#define SHAKTI_TIMEGM(tm) _mkgmtime(tm)
#else
#define SHAKTI_TIMEGM(tm) timegm(tm)
#endif

extern char g_lib_path[4096];
extern char g_script_dir[4096];
void shakti_set_file_vars(Env *e, const char *path);
int shakti_safe_mode(void);
int shakti_exec_allowed(void);
int shakti_net_allowed(void);
int shakti_fs_check(const char *path, int write, char *err, size_t err_cap);

void shakti_oom(const char *where);
void *x_malloc(size_t sz, const char *where);
void *x_calloc(size_t nmemb, size_t sz, const char *where);
void *x_realloc(void *ptr, size_t sz, const char *where);
char *x_strdup(const char *s, const char *where);
size_t x_mul(size_t a, size_t b, const char *where);
uint32_t fnv1a(const char *s);
uint32_t hash_u64(uint64_t x);
/* Position of key k in dict d when k is a string or a number (int, float, bool, char), found
   through d's hash table once d has more than 8 keys, or -1 when d has no such key; DICT_UNHASHED
   for a key of any other kind, which v_dict_index finds by comparing it with each key by `=`. */
#define DICT_UNHASHED (-2)
int64_t v_dict_hashed_index(V *d, V *k);
/* Append the pair k: val to d, keeping its hash table in step; k is not in d already. Like
   v_dict_set, it first unshares d's keys and vals, so another dict sharing them is unchanged. */
void v_dict_push(V *d, V *k, V *val);

V *v_alloc(int t);
V *try_promote_matrix(V **elems, int nch);
void print_val(V *v, FILE *fp, int repr_mode);
void v_serialize(V *v, FILE *fp);
V *v_deserialize(FILE *fp);

int is_truthy(V *v);
double to_float(V *v);
V *vec_logic(V *a, V *b, int is_and);
V *vec_binop(V *a, V *b, int op);
V *mat_binop(V *a, V *b, int op);
V *table_filter(V *tbl, V *mask);

void lex_fail(Lexer *l, const char *message);
int lex_peek_is_signed_literal(Lexer *l);
int lex_peek_minus_is_subtraction(Lexer *l);
void parse_fail(const char *fmt, ...);

void node_sprint(Node *n, FILE *fp);

Env *env_acquire(Env *parent);
void env_release(Env *e);
void env_leave(Env *e);
void env_imported(void);
int env_set_int_inplace(Env *e, const char *name, int64_t j);
int env_get_int(Env *e, const char *name, int64_t *out);
int env_update(Env *e, const char *name, V *val);

FILE *fopen_regular(const char *path, char *err, size_t err_cap);
V *do_import(const char *name, Env *e);
V *require_sql(Env *e);

/* Call-site `*expr` parses as N_UNOP with this op. */
#define OP_SPLAT OP_MUL

void shakti_bind_params(Env *ce, V *fn, V **args, int nargs, V **kwn, V **kwv, int nkw);
V *shakti_call_fn(V *fn, V *self, V **args, int nargs, V **kwn, V **kwv, int nkw, Env *e);
int shakti_is_class(V *v);
int shakti_is_instance(V *v);
V *shakti_instance_str(V *v, Env *e);
int shakti_isinstance(V *obj, V *cls);

int shakti_prog_silent_last(Node *prog);
void run_repl(Env *e);

#endif /* SHAKTI_INTERNAL_H */
