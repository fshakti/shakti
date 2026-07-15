#!/usr/bin/env python3
"""Generate src/shakti.h from src/shakti_lang.c (token/node order)."""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
LANG = ROOT / "src" / "shakti_lang.c"
OUT = ROOT / "src" / "shakti.h"


def main() -> None:
    import sys

    check_only = "--check" in sys.argv
    if not LANG.is_file():
        print("error: missing", LANG, file=sys.stderr)
        raise SystemExit(1)
    t = LANG.read_text(encoding="utf-8", errors="replace")
    seen: list[str] = []
    for m in re.finditer(r"(?<![A-Z0-9_])T_[A-Z][A-Z0-9_]*_(?![A-Z0-9_])", t):
        s = m.group(0)
        if s == "T_HT_":
            continue
        if s not in seen:
            seen.append(s)

    vals = [
        "T_NIL",
        "T_BOOL",
        "T_INT",
        "T_FLOAT",
        "T_STR",
        "T_DATE",
        "T_ERR",
        "T_IVEC",
        "T_FVEC",
        "T_BVEC",
        "T_LIST",
        "T_DICT",
        "T_TABLE",
        "T_FN",
        "T_DATETIME",
        "T_TIME",
        "T_INPUT",
        "T_IMAT",
        "T_FMAT",
        "T_BMAT",
    ]
    nodes = sorted(set(re.findall(r"\bN_[A-Z][A-Z0-9_]*\b", t)))
    nodes = [n for n in nodes if len(n) > 2 and not n.endswith("__")]
    ops = sorted(set(re.findall(r"OP_[A-Z_]+", t)))

    lines: list[str] = []
    lines.append("/* shakti/src/shakti.h — language/runtime public header (generated) */")
    lines.append("#ifndef SHAKTI_H")
    lines.append("#define SHAKTI_H")
    lines.append("#include <stdio.h>")
    lines.append("#include <stdlib.h>")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("#include <string.h>")
    lines.append("#include <stdarg.h>")
    lines.append("#include <time.h>")
    lines.append("#if defined(_WIN32) && !defined(_USE_MATH_DEFINES)")
    lines.append("#define _USE_MATH_DEFINES 1")
    lines.append("#endif")
    lines.append("#include <math.h>")
    lines.append("#include <ctype.h>")
    lines.append('#include "a.h"')
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append('extern "C" {')
    lines.append("#endif")
    lines.append("")
    lines.append("#ifndef MAX_FN")
    lines.append("#define MAX_FN 4096")
    lines.append("#endif")
    lines.append("#define SHAKTI_INDENT_STACK 256")
    lines.append("#define SHAKTI_TOKEN_TEXT 8192")
    lines.append("")
    lines.append("typedef struct Node Node;")
    lines.append("typedef struct Env Env;")
    lines.append("typedef struct V V;")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    int type;")
    lines.append("    int line;")
    lines.append("    int64_t ival;")
    lines.append("    double fval;")
    lines.append("    char sval[SHAKTI_TOKEN_TEXT];")
    lines.append("} Token;")
    lines.append("")
    lines.append("typedef struct Lexer {")
    lines.append("    const char *src;")
    lines.append("    size_t len, pos;")
    lines.append("    int line;")
    lines.append("    int at_line_start;")
    lines.append("    int indent_stack[SHAKTI_INDENT_STACK];")
    lines.append("    int indent_top;")
    lines.append("    int paren_depth;")
    lines.append("    int emit_newline;")
    lines.append("    int pending_dedents;")
    lines.append("    int has_peek;")
    lines.append("    int failed;")
    lines.append("    char error[128];")
    lines.append("    Token peek;")
    lines.append("    /* 1 after a noun/literal/closer: next '-' before a digit is subtract, not sign. */")
    lines.append("    int noun_pos;")
    lines.append("} Lexer;")
    lines.append("")
    lines.append("struct Node {")
    lines.append("    int type;")
    lines.append("    char *sval;")
    lines.append("    int64_t ival;")
    lines.append("    int64_t j;")
    lines.append("    double fval;")
    lines.append("    int op;")
    lines.append("    Node **ch;")
    lines.append("    int nch;")
    lines.append("};")
    lines.append("")
    lines.append("struct Env {")
    lines.append("    int rc;")
    lines.append("    int cap, len;")
    lines.append("    char **names;")
    lines.append("    V **vals;")
    lines.append("    uint32_t *hashes;")
    lines.append("    Env *parent;")
    lines.append("};")
    lines.append("")
    lines.append("struct V {")
    lines.append("    int t;")
    lines.append("    int rc;")
    lines.append("    int64_t n;")
    lines.append("    int b;")
    lines.append("    int64_t j;")
    lines.append("    double f;")
    lines.append("    char *s;")
    lines.append("    int64_t *J;")
    lines.append("    double *F;")
    lines.append("    unsigned char *B;")
    lines.append("    V **L;")
    lines.append("    V *keys, *vals;")
    lines.append("    uint32_t *_ht;")
    lines.append("    uint32_t _ht_cap;")
    lines.append("    V *params, *defaults;")
    lines.append("    Env *closure;")
    lines.append("};")
    lines.append("")
    lines.append("enum {")
    for i, name in enumerate(seen):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("enum {")
    for i, name in enumerate(vals):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("enum {")
    for i, name in enumerate(nodes):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("enum {")
    for i, name in enumerate(ops):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("")
    lines.append("void lex_init(Lexer *l, const char *src);")
    lines.append("Token lex_next(Lexer *l);")
    lines.append("Token lex_peek(Lexer *l);")
    lines.append("Node *node_new(int type);")
    lines.append("void node_add(Node *n, Node *child);")
    lines.append("void node_free(Node *n);")
    lines.append("V *v_nil(void);")
    lines.append("V *v_bool(int b);")
    lines.append("V *v_int(int64_t j);")
    lines.append("V *v_float(double f);")
    lines.append("V *v_str(const char *s);")
    lines.append("V *v_str_take(char *s);")
    lines.append("V *v_date(int64_t utc_midnight_ms);")
    lines.append("V *v_time(int64_t ms_since_midnight);")
    lines.append("V *v_err(const char *s);")
    lines.append("V *v_errf(const char *fmt, ...);")
    lines.append("V *v_ivec(int64_t n);")
    lines.append("V *v_fvec(int64_t n);")
    lines.append("V *v_bvec(int64_t n);")
    lines.append("V *v_imat(int64_t rows, int64_t cols);")
    lines.append("V *v_fmat(int64_t rows, int64_t cols);")
    lines.append("V *v_bmat(int64_t rows, int64_t cols);")
    lines.append("static inline int64_t mat_cols(V *m) { return (int64_t)m->_ht_cap; }")
    lines.append("static inline int64_t mat_idx(V *m, int64_t r, int64_t c) { return r * mat_cols(m) + c; }")
    lines.append("V *mat_matmul(V *a, V *b);")
    lines.append("V *v_mat_row(V *m, int64_t row);")
    lines.append("V *v_list(int64_t n);")
    lines.append("void v_list_append(V *v, V *item);")
    lines.append("V *v_dict(V *keys, V *vals);")
    lines.append("V *v_table(V *cols, V *data);")
    lines.append("V *v_fn(V *params, V *defaults, Node *body_ast, Env *closure);")
    lines.append("V *v_datetime(int64_t ms_utc);")
    lines.append("V *v_ref(V *v);")
    lines.append("void v_free(V *v);")
    lines.append("V *v_copy(V *v);")
    lines.append("void v_print(V *v, int nl);")
    lines.append("char *v_repr(V *v);")
    lines.append("char *v_to_str(V *v);")
    lines.append("Env *env_new(Env *parent);")
    lines.append("void env_set(Env *e, const char *name, V *val);")
    lines.append("void env_set_local(Env *e, const char *name, V *val);")
    lines.append("V *env_get(Env *e, const char *name);")
    lines.append("void env_ref(Env *e);")
    lines.append("void env_free(Env *e);")
    lines.append("extern Node *fn_ast[MAX_FN];")
    lines.append("extern int fn_ast_n;")
    lines.append("extern int g_returning;")
    lines.append("extern int g_breaking;")
    lines.append("extern int g_continuing;")
    lines.append("extern int g_error;")
    lines.append("extern V *g_retval;")
    lines.append("extern V *g_error_val;")
    lines.append("int fn_ast_store(Node *n);")
    lines.append("void v_dict_set(V *d, const char *key, V *val);")
    lines.append("V *v_dict_get(V *d, const char *key);")
    lines.append("int env_save(Env *e, const char *path);")
    lines.append("int env_load(Env *e, const char *path);")
    lines.append("int shakti_parse_datetime_ms(const char *s, int64_t *out_ms);")
    lines.append("void shakti_format_datetime_ms(int64_t ms, char *buf, size_t cap);")
    lines.append("int shakti_parse_date_ymd(const char *s, int64_t *out_ms);")
    lines.append("void shakti_format_date_ms(int64_t utc_midnight_ms, char *buf, size_t cap);")
    lines.append("void shakti_format_time_ms(int64_t ms_in_day, char *buf, size_t cap);")
    lines.append("const char *type_name(int t);")
    lines.append("V *vec_cmp(V *a, V *b, int op);")
    lines.append("Node *parse(const char *src);")
    lines.append("V *eval(Node *n, Env *e);")
    lines.append("int shakti_lang_main(int argc, char **argv);")
    lines.append("V *table_load(const char *path, V *columns_opt);")
    lines.append("int table_save(V *table, const char *path);")
    lines.append("V *method_call(V *obj, const char *method, V **args, int nargs, Env *e);")
    lines.append(
        "V *builtin_call(const char *name, V **args, int nargs, V **kwn, V **kwv, int nkw, Env *e);"
    )
    lines.append("V *table_sql_select(V *from, V *cols, V *by, V *where);")
    lines.append("V *table_sql_update(V *from, V *cols, V *where);")
    lines.append("V *table_sql_delete(V *from, V *cols, V *where);")
    lines.append("V *table_sql_create_table(V *name, V *cols);")
    lines.append("V *table_sql_insert(V *table, V *cols, V *vals);")
    lines.append("V *table_sql_join(V *left, V *right, V *on_col);")
    lines.append("int is_builtin(const char *name);")
    lines.append("void builtin_register(Env *e);")
    lines.append("")
    lines.append("#ifdef _WIN32")
    lines.append("FILE *win_open_memstream(char **ptr, size_t *sizeloc);")
    lines.append("void win_close_memstream(FILE *fp, char **ptr, size_t *sizeloc);")
    lines.append("#define OPEN_MEMSTREAM(ptr,sz) win_open_memstream((ptr),(sz))")
    lines.append("#define CLOSE_MEMSTREAM(fp,ptr,sz) win_close_memstream((fp),(ptr),(sz))")
    lines.append("#else")
    lines.append("#define OPEN_MEMSTREAM(ptr,sz) open_memstream((ptr),(sz))")
    lines.append("#define CLOSE_MEMSTREAM(fp,ptr,sz) do { fclose(fp); } while (0)")
    lines.append("#endif")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")
    lines.append("")
    lines.append("#endif /* SHAKTI_H */")

    new_text = "\n".join(lines) + "\n"
    if check_only:
        if not OUT.is_file():
            print("error: missing", OUT, "(run without --check to generate)", file=sys.stderr)
            raise SystemExit(1)
        old = OUT.read_text(encoding="utf-8")
        if old != new_text:
            print("error: shakti.h is out of sync with", LANG, file=sys.stderr)
            print("run: python3 scripts/gen_shakti_h.py", file=sys.stderr)
            raise SystemExit(1)
        print("shakti.h OK")
        return
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(new_text, encoding="utf-8")
    print("Wrote", OUT, "lines", len(lines))


if __name__ == "__main__":
    main()
