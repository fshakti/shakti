/* shakti/src/shakti_internal.h — private cross-TU helpers (not public API) */
#ifndef SHAKTI_INTERNAL_H
#define SHAKTI_INTERNAL_H

#include "shakti.h"

#ifndef SHAKTI_PARSE_MAX_DEPTH
#define SHAKTI_PARSE_MAX_DEPTH 40
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

#ifdef _WIN32
#define SHAKTI_TIMEGM(tm) _mkgmtime(tm)
#else
#define SHAKTI_TIMEGM(tm) timegm(tm)
#endif

extern char g_lib_path[4096];
extern char g_script_dir[4096];

void shakti_oom(const char *where);
void *x_malloc(size_t sz, const char *where);
void *x_calloc(size_t nmemb, size_t sz, const char *where);
void *x_realloc(void *ptr, size_t sz, const char *where);
char *x_strdup(const char *s, const char *where);
size_t x_mul(size_t a, size_t b, const char *where);
uint32_t fnv1a(const char *s);

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

void node_sprint(Node *n, FILE *fp);

Env *env_acquire(Env *parent);
void env_release(Env *e);
int env_set_int_inplace(Env *e, const char *name, int64_t j);
int env_get_int(Env *e, const char *name, int64_t *out);
int env_update(Env *e, const char *name, V *val);

FILE *fopen_regular(const char *path, char *err, size_t err_cap);
V *do_import(const char *name, Env *e);
V *require_sql(Env *e);

int shakti_prog_silent_last(Node *prog);
void run_repl(Env *e);

#endif /* SHAKTI_INTERNAL_H */
