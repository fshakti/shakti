#include "shakti.h"
#include "json_parse.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <dirent.h>
#include <unistd.h>
#define SHAKTI_HAVE_DIRENT 1
#if defined(__has_include)
#if __has_include(<regex.h>)
#include <regex.h>
#define SHAKTI_HAVE_POSIX_REGEX 1
#endif
#endif
#elif !defined(_WIN32)
#include <dirent.h>
#include <regex.h>
#include <unistd.h>
#define SHAKTI_HAVE_DIRENT 1
#define SHAKTI_HAVE_POSIX_REGEX 1
#else
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define SHAKTI_WIN32_NATIVE 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif
#if defined(__MINGW32__) || defined(__MINGW64__)
#define SHAKTI_HAVE_DIRENT 1
#if defined(__has_include)
#if __has_include(<regex.h>)
#define SHAKTI_HAVE_POSIX_REGEX 1
#endif
#endif
#elif !defined(_WIN32)
#define SHAKTI_HAVE_DIRENT 1
#define SHAKTI_HAVE_POSIX_REGEX 1
#else
#define mkdir(path, mode) _mkdir(path)
#define SHAKTI_WIN32_NATIVE 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#endif
static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}
static int cmp_f64(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
extern const char *type_name(int t);
extern Node *fn_ast[];
extern V *eval(Node *n, Env *e);
extern int g_returning;
extern V *g_retval;
static int v_truthy(V *v) {
    P(!v || v->t == T_NIL,0)
    P(v->t == T_BOOL,v->b)
    P(v->t == T_INT,v->j != 0)
    P(v->t == T_FLOAT,v->f != 0.0)
    P(v->t == T_STR,v->s[0] != 0)
    return 1;
}
V *bi_fread(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("read(path)"))
    FILE *f = fopen(a[0]->s, "rb");
    P(!f,v_errf("read: %s", strerror(errno)))
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (z < 0) {
        fclose(f);
        return v_err("read: size");
    }
    char *b = malloc((size_t)z + 1);
    if (!b) {
        fclose(f);
        return v_err("read: oom");
    }
    fread(b, 1, (size_t)z, f);
    b[z] = 0;
    fclose(f);
    return v_str(b);
}
V *bi_fwrite(V **a, in) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR,v_err("write(path, data)"))
    FILE *f = fopen(a[0]->s, "wb");
    P(!f,v_errf("write: %s", strerror(errno)))
    size_t w = fwrite(a[1]->s, 1, strlen(a[1]->s), f);
    fclose(f);
    return v_int((int64_t)w);
}
V *bi_readlines(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("readlines(path)"))
    FILE *f = fopen(a[0]->s, "r");
    P(!f,v_errf("readlines: %s", strerror(errno)))
    V *r = v_list(0);
    char buf[8192];
    while (fgets(buf, sizeof buf, f)) {
        size_t L = strlen(buf);
        W(L&&(buf[L-1]=='\n'||buf[L-1]=='\r'),buf[--L]=0)
        v_list_append(r, v_str(buf));
    }
    fclose(f);
    return r;
}
#if defined(SHAKTI_WIN32_NATIVE)
static int win_join_path(const char *base, const char *name, char *out, size_t cap) {
    size_t bl = strlen(base);
    int need_sep = bl > 0 && base[bl - 1] != '/' && base[bl - 1] != '\\';
    int n = snprintf(out, cap, need_sep ? "%s\\%s" : "%s%s", base, name);
    return n > 0 && (size_t)n < cap;
}
#endif
V *bi_listdir(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("listdir(path)"))
#if defined(SHAKTI_HAVE_DIRENT)
    DIR *d = opendir(a[0]->s);
    P(!d,v_errf("listdir: %s", strerror(errno)))
    int cap = 64, nent = 0;
    char **names = malloc((size_t)cap * sizeof(char*));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (nent >= cap) {
            cap *= 2;
            char **tmp = realloc(names, (size_t)cap * sizeof(char *));
            if (!tmp) {
                for (int j = 0; j < nent; j++) free(names[j]);
                free(names);
                closedir(d);
                return v_err("out of memory");
            }
            names = tmp;
        }
        names[nent++] = strdup(e->d_name);
    }
    closedir(d);
    V *r = v_list(nent);
    for (int i = 0; i < nent; i++) {
        r->L[i] = v_str_take(names[i]);
    }
    free(names);
    return r;
#elif defined(SHAKTI_WIN32_NATIVE)
    char pattern[4096];
    snprintf(pattern, sizeof pattern, "%s\\*", a[0]->s);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    P(h == INVALID_HANDLE_VALUE,v_errf("listdir: %s", strerror(errno)))
    V *r = v_list(0);
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        v_list_append(r, v_str(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return r;
#else
    (void)a;
    return v_err("listdir: unsupported platform");
#endif
}
typedef struct { char **paths; int n, cap; } WalkPaths;
static void walk_paths_add(WalkPaths *wp, char *path) {
    if (wp->n >= wp->cap) {
        wp->cap = wp->cap ? wp->cap * 2 : 512;
        wp->paths = realloc(wp->paths, (size_t)wp->cap * sizeof(char*));
    }
    wp->paths[wp->n++] = path;
}
static void walk_inner_paths(const char *base, WalkPaths *wp) {
#if defined(SHAKTI_HAVE_DIRENT)
    DIR *d = opendir(base);
    Pv(!d)
    size_t bl = strlen(base);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t nl = strlen(e->d_name);
        if (bl + 1 + nl >= 4096) continue;
        size_t plen = bl + 1 + nl;
        char *path = malloc(plen + 1);
        memcpy(path, base, bl);
        path[bl] = '/';
        strcpy(path + bl + 1, e->d_name);
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_UNKNOWN)
        if (e->d_type == DT_DIR) {
            walk_inner_paths(path, wp);
        } else if (e->d_type == DT_UNKNOWN) {
            struct stat sb;
            if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) walk_inner_paths(path, wp);
        }
#else
        struct stat sb;
        if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) walk_inner_paths(path, wp);
#endif
        walk_paths_add(wp, path);
    }
    closedir(d);
#else
    (void)base;
    (void)wp;
#endif
}
static void walk_inner(const char *base, V *out) {
#if defined(SHAKTI_HAVE_DIRENT)
    WalkPaths wp = {0};
    walk_inner_paths(base, &wp);
    if (out->_ht_cap < wp.n) {
        out->_ht_cap = wp.n > 0 ? wp.n : 8;
        out->L = realloc(out->L, (size_t)out->_ht_cap * sizeof(V*));
    }
    for (int i = 0; i < wp.n; i++) {
        out->L[out->n++] = v_str_take(wp.paths[i]);
    }
    free(wp.paths);
#elif defined(SHAKTI_WIN32_NATIVE)
    char pattern[4096];
    snprintf(pattern, sizeof pattern, "%s\\*", base);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    Pv(h == INVALID_HANDLE_VALUE)
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char path[4096];
        if (!win_join_path(base, fd.cFileName, path, sizeof path)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk_inner(path, out);
        v_list_append(out, v_str(path));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)base;
    (void)out;
#endif
}
V *bi_walk(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("walk(path)"))
#if defined(SHAKTI_HAVE_DIRENT) || defined(SHAKTI_WIN32_NATIVE)
    V *r = v_list(0);
    walk_inner(a[0]->s, r);
    return r;
#else
    (void)a;
    return v_err("walk: unsupported platform");
#endif
}
V *bi_stat(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("stat(path)"))
    struct stat sb;
    P(stat(a[0]->s, &sb) != 0,v_errf("stat: %s", strerror(errno)))
    V *k = v_list(3);
    k->L[0] = v_str("size");
    k->L[1] = v_str("mtime");
    k->L[2] = v_str("isdir");
    V *v = v_list(3);
    v->L[0] = v_int((int64_t)sb.st_size);
    v->L[1] = v_int((int64_t)sb.st_mtime);
    v->L[2] = v_bool(S_ISDIR(sb.st_mode));
    V *d = v_dict(k, v);
    v_free(k);
    v_free(v);
    return d;
}
V *bi_path_join(V **a, in) {
    P(n < 2,v_err("path_join(a,b,...)"))
    size_t tot = 0;
    for (int i = 0; i < n; i++) {
        P(a[i]->t != T_STR,v_err("path_join: str"))
        tot += strlen(a[i]->s) + 1;
    }
    char *o = malloc(tot + 1);
    P(!o,v_err("path_join: oom"))
    o[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) strcat(o, "/");
        strcat(o, a[i]->s);
    }
    V *r = v_str(o);
    free(o);
    return r;
}
V *bi_path_exists(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_exists"))
    struct stat sb;
    return v_bool(stat(a[0]->s, &sb) == 0);
}
V *bi_path_isdir(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_isdir"))
    struct stat sb;
    return v_bool(stat(a[0]->s, &sb) == 0 && S_ISDIR(sb.st_mode));
}
V *bi_path_isfile(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_isfile"))
    struct stat sb;
    return v_bool(stat(a[0]->s, &sb) == 0 && S_ISREG(sb.st_mode));
}
V *bi_path_basename(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_basename"))
    const char *s = a[0]->s;
    const char *slash = strrchr(s, '/');
#ifdef _WIN32
    const char *bs = strrchr(s, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
#endif
    return v_str(slash ? slash + 1 : s);
}
V *bi_path_dirname(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_dirname"))
    const char *s = a[0]->s;
    char b[4096];
    snprintf(b, sizeof b, "%s", s);
    char *slash = strrchr(b, '/');
#ifdef _WIN32
    char *bs = strrchr(b, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
#endif
    if (slash) *slash = 0;
    else strcpy(b, ".");
    return v_str(b);
}
V *bi_path_splitext(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_splitext"))
    const char *s = a[0]->s;
    const char *dot = strrchr(s, '.');
    const char *slash = strrchr(s, '/');
    if (!dot || (slash && dot < slash)) {
        V *r = v_list(2);
        r->L[0] = v_str(s);
        r->L[1] = v_str("");
        return r;
    }
    size_t base_len = (size_t)(dot - s);
    char *base = malloc(base_len + 1);
    memcpy(base, s, base_len);
    base[base_len] = 0;
    V *r = v_list(2);
    r->L[0] = v_str(base);
    r->L[1] = v_str(dot);
    free(base);
    return r;
}
V *bi_getcwd(V **a, in) {
    (void)a;
    (void)n;
    char b[4096];
#if defined(SHAKTI_WIN32_NATIVE)
    P(!_getcwd(b, sizeof b),v_err("getcwd"))
#else
    P(!getcwd(b, sizeof b),v_err("getcwd"))
#endif
    return v_str(b);
}
V *bi_mkdir(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("mkdir"))
#if defined(SHAKTI_WIN32_NATIVE)
    int rc = _mkdir(a[0]->s);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    int rc = mkdir(a[0]->s);
#else
    int rc = mkdir(a[0]->s, 0755);
#endif
    return rc == 0 ? v_nil() : v_errf("mkdir: %s", strerror(errno));
}
V *bi_getenv(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("getenv"))
    const char *v = getenv(a[0]->s);
    return v ? v_str(v) : v_nil();
}
V *bi_sh(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("sh(cmd)"))
#if defined(__EMSCRIPTEN__)
    return v_err("sh: not available in WASM builds");
#else
    int rc = system(a[0]->s);
    return v_int(rc);
#endif
}
#if defined(SHAKTI_HAVE_POSIX_REGEX)
V *bi_re_findall(V **a, in) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR,v_err("re_findall(pat, s)"))
    regex_t rx;
    P(regcomp(&rx, a[0]->s, REG_EXTENDED) != 0,v_err("re_findall: bad pattern"))
    const char *s = a[1]->s;
    V *out = v_list(0);
    regmatch_t m[1];
    int off = 0;
    while (regexec(&rx, s + off, 1, m, 0) == 0) {
        long len = m[0].rm_eo - m[0].rm_so;
        char *chunk = malloc((size_t)len + 1);
        memcpy(chunk, s + off + m[0].rm_so, (size_t)len);
        chunk[len] = 0;
        v_list_append(out, v_str(chunk));
        free(chunk);
        off += (int)m[0].rm_eo;
        if (m[0].rm_eo == 0) break;
    }
    regfree(&rx);
    return out;
}
V *bi_re_sub(V **a, in) {
    P(n < 3 || a[0]->t != T_STR || a[1]->t != T_STR || a[2]->t != T_STR,v_err("re_sub(pat, rep, s)"))
    regex_t rx;
    P(regcomp(&rx, a[0]->s, REG_EXTENDED) != 0,v_err("re_sub: bad pattern"))
    const char *s = a[2]->s;
    const char *rep = a[1]->s;
    regmatch_t m[1];
    char buf[16384];
    size_t o = 0;
    buf[0] = 0;
    int off = 0;
    while (o < sizeof(buf) - 1 && regexec(&rx, s + off, 1, m, 0) == 0) {
        size_t prefix = (size_t)m[0].rm_so;
        strncat(buf, s + off, prefix);
        o = strlen(buf);
        strncat(buf, rep, sizeof(buf) - o - 1);
        o = strlen(buf);
        off += (int)m[0].rm_eo;
        if (m[0].rm_eo == 0) break;
    }
    strncat(buf, s + off, sizeof(buf) - strlen(buf) - 1);
    regfree(&rx);
    return v_str(buf);
}
V *bi_re_match(V **a, in) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR,v_err("re_match"))
    regex_t rx;
    P(regcomp(&rx, a[0]->s, REG_EXTENDED | REG_NOSUB) != 0,v_err("re_match: bad pattern"))
    int ok = regexec(&rx, a[1]->s, 0, NULL, 0) == 0;
    regfree(&rx);
    return v_bool(ok);
}
V *bi_re_split(V **a, in) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR,v_err("re_split"))
    regex_t rx;
    P(regcomp(&rx, a[0]->s, REG_EXTENDED) != 0,v_err("re_split: bad pattern"))
    const char *s = a[1]->s;
    V *out = v_list(0);
    regmatch_t m[1];
    int off = 0;
    while (regexec(&rx, s + off, 1, m, 0) == 0) {
        long pre = m[0].rm_so;
        if (pre > 0) {
            char *chunk = malloc((size_t)pre + 1);
            memcpy(chunk, s + off, (size_t)pre);
            chunk[pre] = 0;
            v_list_append(out, v_str(chunk));
            free(chunk);
        }
        off += (int)m[0].rm_eo;
        if (m[0].rm_eo == 0) break;
    }
    v_list_append(out, v_str(s + off));
    regfree(&rx);
    return out;
}
#else
V *bi_re_findall(V **a, in) {
    (void)a;
    (void)n;
    return v_err("re_findall: POSIX regex (use MinGW UCRT64 or Unix)");
}
V *bi_re_sub(V **a, in) {
    (void)a;
    (void)n;
    return v_err("re_sub: POSIX regex (use MinGW UCRT64 or Unix)");
}
V *bi_re_match(V **a, in) {
    (void)a;
    (void)n;
    return v_err("re_match: POSIX regex (use MinGW UCRT64 or Unix)");
}
V *bi_re_split(V **a, in) {
    (void)a;
    (void)n;
    return v_err("re_split: POSIX regex (use MinGW UCRT64 or Unix)");
}
#endif
V *bi_json_loads(V **a, in) {
    (void)n;
    P(n < 1 || a[0]->t != T_STR,v_err("json_loads(s)"))
    return shakti_json_parse(a[0]->s, NULL);
}
V *bi_json_dumps(V **a, in) {
    P(n < 1,v_str(""))
    char *r = v_repr(a[0]);
    V *o = v_str(r);
    free(r);
    return o;
}
V *bi_json_load(V **a, in) {
    V*x=bi_fread(a, n);
    P(!x||x->t==T_ERR,x)
    V*argv[1]={x};
    V *o = bi_json_loads(argv,1);
    v_free(x);
    return o;
}
V *bi_json_dump(V **a, in) {
    P(n < 2 || a[1]->t != T_STR,v_err("json_dump(obj, path)"))
    V *s = bi_json_dumps(a, 1);
    V *p[2] = {a[1], s};
    V *r = bi_fwrite(p, 2);
    v_free(s);
    return r;
}
static int v_cmp_repr(V *a, V *b) {
    char *ra = v_repr(a), *rb = v_repr(b);
    int c = strcmp(ra, rb);
    free(ra);
    free(rb);
    return c;
}
V *bi_sorted(V **a, in, V **kwn, V **kwv, int nkw, Env *e) {
    (void)kwn;
    (void)kwv;
    (void)nkw;
    (void)e;
    P(n < 1,v_list(0))
    if (a[0]->t == T_IVEC || a[0]->t == T_FVEC) {
        V*x=v_copy(a[0]);
        if(x->t==T_IVEC)
            qsort(x->J, (size_t)x->n, sizeof(int64_t), cmp_i64);
        else
            qsort(x->F, (size_t)x->n, sizeof(double), cmp_f64);
        return x;
    }
    P(a[0]->t != T_LIST,v_err("sorted(list)"))
    V *r = v_copy(a[0]);
    for (int64_t i = 0; i + 1 < r->n; i++)
        for (int64_t j = 0; j + 1 < r->n; j++)
            if (v_cmp_repr(r->L[j], r->L[j + 1]) > 0) {
                V *tmp = r->L[j];
                r->L[j] = r->L[j + 1];
                r->L[j + 1] = tmp;
            }
    return r;
}
V *bi_any(V **a, in) {
    i(n,{P(v_truthy(a[i]),v_bool(1))})
    return v_bool(0);
}
V *bi_all(V **a, in) {
    i(n,{P(!v_truthy(a[i]),v_bool(0))})
    return v_bool(1);
}
V *bi_isinstance(V **a, in) {
    P(n < 2,v_bool(0))
    const char *want = a[1]->t == T_STR ? a[1]->s : "";
    const char *got = type_name(a[0]->t);
    return v_bool(!strcmp(want, got));
}
V *bi_hasattr(V **a, in) {
    P(n < 2 || (a[0]->t != T_DICT && a[0]->t != T_TABLE),v_bool(0))
    P(a[1]->t != T_STR,v_bool(0))
    for (int64_t i = 0; i < a[0]->keys->n; i++) {
        V *k = a[0]->keys->L[i];
        P(k->t == T_STR && !strcmp(k->s, a[1]->s),v_bool(1))
    }
    return v_bool(0);
}
V *bi_getattr(V **a, in) {
    P(n < 2 || (a[0]->t != T_DICT && a[0]->t != T_TABLE) || a[1]->t != T_STR,v_nil())
    for (int64_t i = 0; i < a[0]->keys->n; i++) {
        V *k = a[0]->keys->L[i];
        P(k->t == T_STR && !strcmp(k->s, a[1]->s),v_ref(a[0]->vals->L[i]))
    }
    return n > 2 ? v_ref(a[2]) : v_nil();
}
V *bi_chr(V **a, in) {
    P(n < 1 || a[0]->t != T_INT,v_err("chr"))
    char b[8];
    b[0] = (char)(a[0]->j & 255);
    b[1] = 0;
    return v_str(b);
}
V *bi_ord(V **a, in) {
    P(n < 1 || a[0]->t != T_STR || !a[0]->s[0],v_err("ord"))
    return v_int((unsigned char)a[0]->s[0]);
}
V *bi_hex(V **a, in) {
    P(n < 1 || a[0]->t != T_INT,v_err("hex"))
    char b[32];
    snprintf(b, sizeof b, "%llx", (unsigned long long)a[0]->j);
    return v_str(b);
}
f(bi_is_listlike,x==T_LIST||x==T_IVEC||x==T_FVEC||x==T_BVEC)
static V *bi_list_from_column(V *col) {
    if (col->t == T_LIST)
        return v_copy(col);
    if (col->t == T_IVEC) {
        V *r = v_list(col->n);
        for (int64_t i = 0; i < col->n; i++)
            r->L[i] = v_int(col->J[i]);
        return r;
    }
    if (col->t == T_FVEC) {
        V *r = v_list(col->n);
        for (int64_t i = 0; i < col->n; i++)
            r->L[i] = v_float(col->F[i]);
        return r;
    }
    if (col->t == T_BVEC) {
        V *r = v_list(col->n);
        for (int64_t i = 0; i < col->n; i++)
            r->L[i] = v_bool(col->B[i]);
        return r;
    }
    return NULL;
}
static V *bi_kv_from_kwargs(V **kwn, V **kwv, int nkw, V *(*build)(V *, V *)) {
    V *k = v_list(nkw);
    V *vl = v_list(nkw);
    for (int i = 0; i < nkw; i++) {
        k->L[i] = v_ref(kwn[i]);
        vl->L[i] = v_ref(kwv[i]);
    }
    V *r = build(k, vl);
    v_free(k);
    v_free(vl);
    return r;
}
V *bi_dict(V **a, int nargs, V **kwn, V **kwv, int nkw) {
    if (nargs == 1 && a[0]->t == T_DICT)
        return v_copy(a[0]);
    if (nargs == 2 && bi_is_listlike(a[0]->t) && bi_is_listlike(a[1]->t)) {
        if (a[0]->n != a[1]->n)
            return v_err("dict(keys, values): length mismatch");
        V *kl = bi_list_from_column(a[0]);
        V *vl = bi_list_from_column(a[1]);
        if (!kl || !vl) {
            if (kl) v_free(kl);
            if (vl) v_free(vl);
            return v_err("dict(keys, values): keys and values must be lists or vectors");
        }
        V *r = v_dict(kl, vl);
        v_free(kl);
        v_free(vl);
        return r;
    }
    if (nkw > 0)
        return bi_kv_from_kwargs(kwn, kwv, nkw, v_dict);
    return v_err("dict()");
}
V *bi_ktable(V **a, int nargs, V **kwn, V **kwv, int nkw) {
    if (nargs == 1 && a[0]->t == T_DICT) {
        V *cols = v_list(2);
        cols->L[0] = v_str("key");
        cols->L[1] = v_str("value");
        V *data = v_list(2);
        data->L[0] = v_copy(a[0]->keys);
        data->L[1] = v_copy(a[0]->vals);
        V *r = v_table(cols, data);
        v_free(cols);
        v_free(data);
        return r;
    }
    if (nargs == 2 && bi_is_listlike(a[0]->t) && bi_is_listlike(a[1]->t)) {
        if (a[0]->n != a[1]->n)
            return v_err("ktable(keys, values): length mismatch");
        V *keycol = bi_list_from_column(a[0]);
        V *valcol = bi_list_from_column(a[1]);
        if (!keycol || !valcol) {
            if (keycol) v_free(keycol);
            if (valcol) v_free(valcol);
            return v_err("ktable(keys, values): keys and values must be lists or vectors");
        }
        V *cols = v_list(2);
        cols->L[0] = v_str("key");
        cols->L[1] = v_str("value");
        V *data = v_list(2);
        data->L[0] = keycol;
        data->L[1] = valcol;
        V *r = v_table(cols, data);
        v_free(cols);
        v_free(data);
        return r;
    }
    if (nkw > 0) {
        V *keycol = v_list(nkw);
        V *valcol = v_list(nkw);
        for (int i = 0; i < nkw; i++) {
            keycol->L[i] = v_ref(kwn[i]);
            valcol->L[i] = v_ref(kwv[i]);
        }
        V *cols = v_list(2);
        cols->L[0] = v_str("key");
        cols->L[1] = v_str("value");
        V *data = v_list(2);
        data->L[0] = keycol;
        data->L[1] = valcol;
        V *r = v_table(cols, data);
        v_free(cols);
        v_free(data);
        return r;
    }
    return v_err("ktable()");
}
V *bi_set(V **a, in) {
    V *r = v_list(0);
    for (int i = 0; i < n; i++) {
        int dup = 0;
        for (int64_t j = 0; j < r->n; j++) {
            char *x = v_repr(r->L[j]), *y = v_repr(a[i]);
            dup = !strcmp(x, y);
            free(x);
            free(y);
            if (dup) break;
        }
        if (!dup) v_list_append(r, v_ref(a[i]));
    }
    return r;
}