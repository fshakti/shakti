// SPDX-License-Identifier: Apache-2.0
#include "shakti_internal.h"
#include "json_parse.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
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
#include <fcntl.h>
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
    int xn = x != x, yn = y != y;
    if (xn || yn) return xn - yn; /* NaN sorts after numbers */
    return (x > y) - (x < y);
}
extern const char *type_name(int t);
extern Node *fn_ast[];
extern V *eval(Node *n, Env *e);
extern int g_returning;
extern V *g_retval;
extern int is_truthy(V *v);
/* Safe mode: confine file paths to SHAKTI_FS_ROOT (no-op otherwise). */
#define FS_GUARD(who, path, w) do { \
    char fs_err_[512]; \
    if (shakti_fs_check((path), (w), fs_err_, sizeof fs_err_) != 0) \
        return v_errf("%s: %s", (who), fs_err_); \
} while (0)
#define FS_GUARD_BOOL(path) do { \
    if (shakti_fs_check((path), 0, NULL, 0) != 0) return v_bool(0); \
} while (0)
V *bi_fread(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("read(path)"))
    int as_bytes = n >= 2 && a[1] &&
        ((a[1]->t == T_BOOL && a[1]->j) || (a[1]->t == T_INT && a[1]->j));
    FS_GUARD("read", a[0]->s, 0);
    FILE *f = fopen(a[0]->s, "rb");
    P(!f,v_errf("read: %s", strerror(errno)))
    {
        char ferr[512];
        if (shakti_fs_check_fd(fileno(f), 0, ferr, sizeof ferr) != 0) {
            fclose(f);
            return v_errf("read: %s", ferr);
        }
    }
    fseek(f, 0, SEEK_END);
    long z = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (z < 0 || (unsigned long)z > (256u * 1024u * 1024u)) {
        fclose(f);
        return v_err("read: size");
    }
    char *b = malloc((size_t)z + 1);
    if (!b) {
        fclose(f);
        return v_err("read: oom");
    }
    size_t got = fread(b, 1, (size_t)z, f);
    b[got] = 0;
    fclose(f);
    if (as_bytes) {
        V *v = v_cvec((int64_t)got);
        if (got && v->B) memcpy(v->B, b, got);
        free(b);
        return v;
    }
    return v_str_take(b);
}
V *bi_fwrite(V **a, in) {
    P(n < 2 || a[0]->t != T_STR || a[1]->t != T_STR,v_err("write(path, data)"))
    FS_GUARD("write", a[0]->s, 1);
    /* L-a: in safe mode, open without O_TRUNC (and without following
     * symlinks), check the fd, and only then truncate — a symlink swapped in
     * after FS_GUARD cannot cause a file outside the root to be truncated.
     * Otherwise O_TRUNC on that O_NOFOLLOW open is the same file. */
    int safe_write = shakti_safe_mode();
    int wfd = open(a[0]->s, O_WRONLY | O_CREAT | O_NOFOLLOW | (safe_write ? 0 : O_TRUNC), 0666);
    P(wfd < 0, v_errf("write: %s", strerror(errno)))
    if (safe_write) {
        char ferr[512];
        if (shakti_fs_check_fd(wfd, 1, ferr, sizeof ferr) != 0) {
            close(wfd);
            return v_errf("write: %s", ferr);
        }
        if (ftruncate(wfd, 0) != 0) {
            int e = errno;
            close(wfd);
            return v_errf("write: %s", strerror(e));
        }
    }
    FILE *f = fdopen(wfd, "wb");
    if (!f) {
        close(wfd);
        return v_errf("write: %s", strerror(errno));
    }
    size_t w = fwrite(a[1]->s, 1, strlen(a[1]->s), f);
    fclose(f);
    return v_int((int64_t)w);
}
V *bi_readlines(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("readlines(path)"))
    FS_GUARD("readlines", a[0]->s, 0);
    FILE *f = fopen(a[0]->s, "r");
    P(!f,v_errf("readlines: %s", strerror(errno)))
    {
        char ferr[512];
        if (shakti_fs_check_fd(fileno(f), 0, ferr, sizeof ferr) != 0) {
            fclose(f);
            return v_errf("readlines: %s", ferr);
        }
    }
    V *r = v_list(0);
    char buf[8192];
    while (fgets(buf, sizeof buf, f)) {
        size_t L = strlen(buf);
        W(L&&(buf[L-1]=='\n'||buf[L-1]=='\r'),buf[--L]=0)
        v_list_append_own(r, v_str(buf));
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
    FS_GUARD("listdir", a[0]->s, 0);
#if defined(SHAKTI_HAVE_DIRENT)
    DIR *d = opendir(a[0]->s);
    P(!d,v_errf("listdir: %s", strerror(errno)))
    {
        char ferr[512];
        if (shakti_fs_check_fd(dirfd(d), 0, ferr, sizeof ferr) != 0) {
            closedir(d);
            return v_errf("listdir: %s", ferr);
        }
    }
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
        v_list_append_own(r, v_str(fd.cFileName));
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
        int cap = wp->cap ? wp->cap * 2 : 512;
        char **np = realloc(wp->paths, (size_t)cap * sizeof(char*));
        if (!np) {
            free(path);
            return;
        }
        wp->paths = np;
        wp->cap = cap;
    }
    wp->paths[wp->n++] = path;
}
#if defined(SHAKTI_HAVE_DIRENT)
static void walk_inner_paths_d(const char *base, WalkPaths *wp, int depth);
#endif
static void walk_inner_paths(const char *base, WalkPaths *wp) {
#if defined(SHAKTI_HAVE_DIRENT)
    walk_inner_paths_d(base, wp, 0);
#else
    (void)base;
    (void)wp;
#endif
}
#if defined(SHAKTI_HAVE_DIRENT)
/* Like DT_DIR: do not descend through symlinked directories. */
static int walk_is_real_dir(const char *path) {
    struct stat sb;
#if defined(__MINGW32__) || defined(__MINGW64__)
    return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
#else
    return lstat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
#endif
}
static void walk_inner_paths_d(const char *base, WalkPaths *wp, int depth) {
    if (depth >= 64) return;
    DIR *d = opendir(base);
    Pv(!d)
    {
        char ferr[64];
        if (shakti_fs_check_fd(dirfd(d), 0, ferr, sizeof ferr) != 0) {
            closedir(d);
            return;
        }
    }
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
        memcpy(path + bl + 1, e->d_name, nl + 1);
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_UNKNOWN)
        if (e->d_type == DT_DIR) {
            walk_inner_paths_d(path, wp, depth + 1);
        } else if (e->d_type == DT_UNKNOWN) {
            if (walk_is_real_dir(path)) walk_inner_paths_d(path, wp, depth + 1);
        }
#else
        if (walk_is_real_dir(path)) walk_inner_paths_d(path, wp, depth + 1);
#endif
        walk_paths_add(wp, path);
    }
    closedir(d);
}
#endif
static void walk_inner(const char *base, V *out) {
#if defined(SHAKTI_HAVE_DIRENT)
    WalkPaths wp = {0};
    walk_inner_paths(base, &wp);
    if (out->_ht_cap < (uint32_t)wp.n) {
        uint32_t cap = wp.n > 0 ? (uint32_t)wp.n : 8;
        V **nl = realloc(out->L, (size_t)cap * sizeof(V*));
        if (!nl) {
            for (int i = 0; i < wp.n; i++) free(wp.paths[i]);
            free(wp.paths);
            return;
        }
        out->L = nl;
        out->_ht_cap = cap;
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
        v_list_append_own(out, v_str(path));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)base;
    (void)out;
#endif
}
V *bi_walk(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("walk(path)"))
    FS_GUARD("walk", a[0]->s, 0);
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
#if !defined(_WIN32)
    static int safe = -1;
    if (safe < 0) safe = shakti_safe_mode() ? 1 : 0;
    struct stat sb;
    if (!safe) {
        P(stat(a[0]->s, &sb) != 0, v_errf("stat: %s", strerror(errno)))
    } else {
        FS_GUARD("stat", a[0]->s, 0);
        int sfd = open(a[0]->s, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (sfd < 0) sfd = open(a[0]->s, O_RDONLY | O_CLOEXEC);
        P(sfd < 0, v_errf("stat: %s", strerror(errno)))
        {
            char ferr[512];
            if (shakti_fs_check_fd(sfd, 0, ferr, sizeof ferr) != 0) {
                close(sfd);
                return v_errf("stat: %s", ferr);
            }
        }
        if (fstat(sfd, &sb) != 0) {
            int e = errno;
            close(sfd);
            return v_errf("stat: %s", strerror(e));
        }
        close(sfd);
    }
#else
    struct stat sb;
    P(stat(a[0]->s, &sb) != 0,v_errf("stat: %s", strerror(errno)))
#endif
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
    size_t off = 0;
    for (int i = 0; i < n; i++) {
        if (i) o[off++] = '/';
        size_t li = strlen(a[i]->s);
        memcpy(o + off, a[i]->s, li);
        off += li;
    }
    o[off] = 0;
    V *r = v_str(o);
    free(o);
    return r;
}
/* In safe mode, open the path and check the descriptor. stat() after the name check can describe an object swapped in outside the root. */
static int path_probe(const char *path, struct stat *sb) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    return stat(path, sb);
#else
    int fd;
    char ferr[512];
    if (!shakti_safe_mode()) return stat(path, sb);
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP) return lstat(path, sb);
        return -1;
    }
    if (shakti_fs_check_fd(fd, 0, ferr, sizeof ferr) != 0) {
        close(fd);
        return -1;
    }
    if (fstat(fd, sb) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
#endif
}
V *bi_path_exists(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_exists"))
    FS_GUARD_BOOL(a[0]->s);
    struct stat sb;
    return v_bool(path_probe(a[0]->s, &sb) == 0);
}
V *bi_path_isdir(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_isdir"))
    FS_GUARD_BOOL(a[0]->s);
    struct stat sb;
    return v_bool(path_probe(a[0]->s, &sb) == 0 && S_ISDIR(sb.st_mode));
}
V *bi_path_isfile(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("path_isfile"))
    FS_GUARD_BOOL(a[0]->s);
    struct stat sb;
    return v_bool(path_probe(a[0]->s, &sb) == 0 && S_ISREG(sb.st_mode));
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
    else { b[0] = '.'; b[1] = 0; }
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
    FS_GUARD("mkdir", a[0]->s, 1);
#if defined(SHAKTI_WIN32_NATIVE)
    int rc = _mkdir(a[0]->s);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    int rc = mkdir(a[0]->s);
#else
    int rc = mkdir(a[0]->s, 0755);
#endif
    if (rc != 0) return v_errf("mkdir: %s", strerror(errno));
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
    if (shakti_safe_mode()) {
        int mfd = open(a[0]->s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        char ferr[512];
        if (mfd < 0 || shakti_fs_check_fd(mfd, 1, ferr, sizeof ferr) != 0) {
            if (mfd >= 0) close(mfd);
            rmdir(a[0]->s);
            return v_err(mfd < 0 ? "mkdir: cannot open created directory" : ferr);
        }
        close(mfd);
    }
#endif
    return v_nil();
}
static int env_name_secret(const char *s) {
    return strstr(s, "TOKEN") || strstr(s, "SECRET") || strstr(s, "PASSWORD") ||
           strstr(s, "PASSWD") || strstr(s, "_KEY");
}
V *bi_getenv(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("getenv"))
    /* Safe mode: the host environment carries secrets (tokens, keys).
     * Shakti's own configuration stays readable, except names that hold a secret. */
    if (shakti_safe_mode() &&
        (strncmp(a[0]->s, "SHAKTI_", 7) != 0 || env_name_secret(a[0]->s)))
        return v_err("getenv: blocked in safe mode (only SHAKTI_* variables)");
    const char *v = getenv(a[0]->s);
    return v ? v_str(v) : v_nil();
}
#if !defined(SHAKTI_WIN32_NATIVE) && !defined(__EMSCRIPTEN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
/* Shell via posix_spawn of /bin/sh -c (waitpid status). */
static int sh_env_keep(const char *kv) {
    static const char *const pfx[] = {
        "PATH=", "LANG=", "LC_ALL=", "LC_CTYPE=", "TZ=", "HOME=", "SHAKTI_",
        "XDG_RUNTIME_DIR="
    };
    size_t i;
    for (i = 0; i < sizeof pfx / sizeof pfx[0]; i++)
        if (!strncmp(kv, pfx[i], strlen(pfx[i]))) return 1;
    return 0;
}
static int shakti_run_shell(const char *cmd) {
    if (!cmd) return -1;
    pid_t pid = 0;
    char *argv[] = {"/bin/sh", "-c", (char *)cmd, NULL};
    /* Outside safe mode the child inherits the parent environment, so git, make
     * and cloud CLIs still see USER, TERM and SSH_AUTH_SOCK. Safe mode keeps
     * the allowlist below. */
    char **envp = environ;
    char **owned = NULL;
    if (shakti_safe_mode()) {
        int envn = 0, ek = 0;
        for (char **e = environ; e && *e; e++) envn++;
        owned = malloc((size_t)(envn + 1) * sizeof(char *));
        if (!owned) return -1;
        for (char **e = environ; e && *e; e++)
            if (sh_env_keep(*e)) owned[ek++] = *e;
        owned[ek] = NULL;
        envp = owned;
    }
    int rc = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, envp);
    free(owned);
    if (rc != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return status;
}
#endif
V *bi_sh(V **a, in) {
    P(n < 1 || a[0]->t != T_STR,v_err("sh(cmd)"))
    if (!shakti_exec_allowed())
        return v_err("sh: disabled (SHAKTI_SAFE or SHAKTI_ALLOW_EXEC=0)");
#if defined(__EMSCRIPTEN__)
    return v_err("sh: not available in WASM builds");
#elif defined(SHAKTI_WIN32_NATIVE) || defined(__MINGW32__) || defined(__MINGW64__)
    (void)a;
    return v_err("sh: not available on this Windows build");
#else
    int rc = shakti_run_shell(a[0]->s);
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
        v_list_append_own(out, v_str(chunk));
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
    size_t cap = strlen(s) + 1;
    if (cap < 64) cap = 64;
    char *buf = malloc(cap);
    if (!buf) { regfree(&rx); return v_err("re_sub: oom"); }
    size_t o = 0, off = 0;
    while (regexec(&rx, s + off, 1, m, 0) == 0) {
        size_t prefix = (size_t)m[0].rm_so;
        size_t rep_len = strlen(rep);
        if (prefix > SIZE_MAX - rep_len || o > SIZE_MAX - prefix - rep_len - 1) {
            free(buf);
            regfree(&rx);
            return v_err("re_sub: result too large");
        }
        size_t need = o + prefix + rep_len + 1;
        if (need > cap) {
            size_t next = cap;
            while (next < need) {
                if (next > SIZE_MAX / 2) { next = need; break; }
                next *= 2;
            }
            char *grown = realloc(buf, next);
            if (!grown) { free(buf); regfree(&rx); return v_err("re_sub: oom"); }
            buf = grown;
            cap = next;
        }
        memcpy(buf + o, s + off, prefix);
        o += prefix;
        memcpy(buf + o, rep, rep_len);
        o += rep_len;
        off += (size_t)m[0].rm_eo;
        if (m[0].rm_eo == 0) break;
    }
    size_t suffix = strlen(s + off);
    if (o > SIZE_MAX - suffix - 1) {
        free(buf);
        regfree(&rx);
        return v_err("re_sub: result too large");
    }
    size_t need = o + suffix + 1;
    if (need > cap) {
        char *grown = realloc(buf, need);
        if (!grown) { free(buf); regfree(&rx); return v_err("re_sub: oom"); }
        buf = grown;
    }
    memcpy(buf + o, s + off, suffix + 1);
    regfree(&rx);
    return v_str_take(buf);
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
            v_list_append_own(out, v_str(chunk));
            free(chunk);
        }
        off += (int)m[0].rm_eo;
        if (m[0].rm_eo == 0) break;
    }
    v_list_append_own(out, v_str(s + off));
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
static int json_putc(char **b, size_t *n, size_t *cap, char c) {
    if (*n + 2 >= *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        char *p = realloc(*b, nc);
        if (!p) return -1;
        *b = p;
        *cap = nc;
    }
    (*b)[(*n)++] = c;
    (*b)[*n] = 0;
    return 0;
}
static int json_puts(char **b, size_t *n, size_t *cap, const char *s) {
    while (*s) if (json_putc(b, n, cap, *s++) < 0) return -1;
    return 0;
}
static int json_emit(V *v, char **b, size_t *n, size_t *cap, int depth) {
    char tmp[64];
    if (!v || depth > 64) return json_puts(b, n, cap, "null");
    switch (v->t) {
    case T_NIL: return json_puts(b, n, cap, "null");
    case T_BOOL: return json_puts(b, n, cap, v->j ? "true" : "false");
    case T_INT: case T_CHAR:
        snprintf(tmp, sizeof tmp, "%lld", (long long)v->j);
        return json_puts(b, n, cap, tmp);
    case T_FLOAT:
        if (!isfinite(v->f)) return json_puts(b, n, cap, "null");
        snprintf(tmp, sizeof tmp, "%.17g", v->f);
        return json_puts(b, n, cap, tmp);
    case T_STR: {
        const unsigned char *p;
        if (json_putc(b, n, cap, '"') < 0) return -1;
        for (p = (const unsigned char *)(v->s ? v->s : ""); *p; p++) {
            if (*p == '"' || *p == '\\') {
                if (json_putc(b, n, cap, '\\') < 0 || json_putc(b, n, cap, (char)*p) < 0) return -1;
            } else if (*p == '\n') {
                if (json_puts(b, n, cap, "\\n") < 0) return -1;
            } else if (*p == '\r') {
                if (json_puts(b, n, cap, "\\r") < 0) return -1;
            } else if (*p == '\t') {
                if (json_puts(b, n, cap, "\\t") < 0) return -1;
            } else if (json_putc(b, n, cap, (char)*p) < 0) return -1;
        }
        return json_putc(b, n, cap, '"');
    }
    case T_LIST: case T_IVEC: case T_FVEC: case T_BVEC: case T_CVEC: {
        int64_t i;
        if (json_putc(b, n, cap, '[') < 0) return -1;
        for (i = 0; i < v->n; i++) {
            V *el = v->t == T_LIST ? v->L[i] : v->t == T_IVEC ? v_int(v->J[i]) :
                    v->t == T_FVEC ? v_float(v->F[i]) : v->t == T_BVEC ? v_bool(v->B[i] != 0) : v_char(v->B[i]);
            int own = v->t != T_LIST;
            if (i && json_putc(b, n, cap, ',') < 0) { if (own) v_free(el); return -1; }
            if (json_emit(el, b, n, cap, depth + 1) < 0) { if (own) v_free(el); return -1; }
            if (own) v_free(el);
        }
        return json_putc(b, n, cap, ']');
    }
    case T_DICT: {
        int64_t i, first = 1;
        if (json_putc(b, n, cap, '{') < 0) return -1;
        for (i = 0; i < v->n; i++) {
            V *k = v->keys ? v->keys->L[i] : NULL;
            V *val = v->vals ? v->vals->L[i] : NULL;
            if (!k || k->t != T_STR) return -2;
            if (!first && json_putc(b, n, cap, ',') < 0) return -1;
            first = 0;
            if (json_emit(k, b, n, cap, depth + 1) < 0) return -1;
            if (json_putc(b, n, cap, ':') < 0) return -1;
            if (json_emit(val, b, n, cap, depth + 1) < 0) return -1;
        }
        return json_putc(b, n, cap, '}');
    }
    case T_TABLE: {
        int64_t r, c;
        if (!v->keys || !v->vals) return json_puts(b, n, cap, "[]");
        if (json_putc(b, n, cap, '[') < 0) return -1;
        for (r = 0; r < v->n; r++) {
            int first = 1;
            if (r && json_putc(b, n, cap, ',') < 0) return -1;
            if (json_putc(b, n, cap, '{') < 0) return -1;
            for (c = 0; c < v->keys->n; c++) {
                V *name = v->keys->L[c];
                V *cell;
                if (!name || name->t != T_STR) continue;
                if (!first && json_putc(b, n, cap, ',') < 0) return -1;
                first = 0;
                if (json_emit(name, b, n, cap, depth + 1) < 0) return -1;
                if (json_putc(b, n, cap, ':') < 0) return -1;
                cell = col_get(v->vals->L[c], r);
                if (json_emit(cell, b, n, cap, depth + 1) < 0) { v_free(cell); return -1; }
                v_free(cell);
            }
            if (json_putc(b, n, cap, '}') < 0) return -1;
        }
        return json_putc(b, n, cap, ']');
    }
    default:
        return json_puts(b, n, cap, "null");
    }
}
V *bi_json_dumps(V **a, in) {
    char *buf = NULL;
    size_t nch = 0, cap = 0;
    P(n < 1,v_str(""))
    {
        int rc = json_emit(a[0], &buf, &nch, &cap, 0);
        if (rc < 0) {
            free(buf);
            return v_err(rc == -2 ? "json_dumps: key must be str" : "json_dumps: out of memory");
        }
    }
    if (!buf) return v_str("");
    return v_str_take(buf);
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
    int c = strcmp(ra ? ra : "", rb ? rb : "");
    free(ra);
    free(rb);
    return c;
}
static int cmp_vptr(const void *a, const void *b) {
    V *x = *(V *const *)a, *y = *(V *const *)b;
    if (!x || !y) return (x != NULL) - (y != NULL);
    if ((x->t == T_INT || x->t == T_FLOAT) && (y->t == T_INT || y->t == T_FLOAT)) {
        double dx = x->t == T_INT ? (double)x->j : x->f;
        double dy = y->t == T_INT ? (double)y->j : y->f;
        int xn = dx != dx, yn = dy != dy;
        if (xn || yn) return xn - yn;
        return (dx > dy) - (dx < dy);
    }
    if (x->t == T_STR && y->t == T_STR) return strcmp(x->s ? x->s : "", y->s ? y->s : "");
    if (x->t != y->t) return (x->t > y->t) - (x->t < y->t);
    return v_cmp_repr(x, y);
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
    qsort(r->L, (size_t)r->n, sizeof(V *), cmp_vptr);
    return r;
}
/* any(xs) and all(xs) of one list or vector test its items, as Python's do; with several
   arguments, or one that isn't a list or vector, they test the arguments. */
static int any_all_items(V **a, in) { return n == 1 && (a[0]->t == T_LIST || is_vec_t(a[0]->t)); }
static int item_truthy(V *v, int64_t i) {
    switch (v->t) {
    case T_IVEC: return v->J[i] != 0;
    case T_FVEC: return v->F[i] != 0.0;
    case T_BVEC: case T_CVEC: return v->B[i] != 0;
    default: return is_truthy(v->L[i]);
    }
}
static V *any_all_err(V *v) {
    if (v && v->t == T_LIST) {
        for (int64_t j = 0; j < v->n; j++)
            if (v->L[j] && v->L[j]->t == T_ERR) return v_ref(v->L[j]);
    }
    return NULL;
}
V *bi_any(V **a, in) {
    if (any_all_items(a, n)) {
        V *err = any_all_err(a[0]);
        if (err) return err;
        for (int64_t j = 0; j < a[0]->n; j++) P(item_truthy(a[0], j),v_bool(1))
        return v_bool(0);
    }
    i(n,{
        if (a[i] && a[i]->t == T_ERR) return v_ref(a[i]);
        P(is_truthy(a[i]),v_bool(1))
    })
    return v_bool(0);
}
V *bi_all(V **a, in) {
    if (any_all_items(a, n)) {
        V *err = any_all_err(a[0]);
        if (err) return err;
        for (int64_t j = 0; j < a[0]->n; j++) P(!item_truthy(a[0], j),v_bool(0))
        return v_bool(1);
    }
    i(n,{
        if (a[i] && a[i]->t == T_ERR) return v_ref(a[i]);
        P(!is_truthy(a[i]),v_bool(0))
    })
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
    int64_t j;
    char b[8];
    P(n < 1, v_err("chr"))
    if (a[0]->t != T_INT && a[0]->t != T_CHAR) return v_err("chr");
    j = a[0]->j;
    if ((j & 255) == 0) return v_err("chr");
    b[0] = (char)(j & 255);
    b[1] = 0;
    return v_str(b);
}
V *bi_char(V **a, in) {
    P(n < 1, v_err("char()"))
    if (a[0]->t == T_CHAR) return v_char((unsigned char)a[0]->j);
    if (a[0]->t == T_INT) return v_char((unsigned char)a[0]->j);
    if (a[0]->t == T_IVEC) {
        V *r = v_cvec(a[0]->n);
        for (int64_t i = 0; i < a[0]->n; i++)
            r->B[i] = (unsigned char)a[0]->J[i];
        return r;
    }
    return v_err("char: need int, char, or list[int]");
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
f(bi_is_listlike,x==T_LIST||x==T_IVEC||x==T_FVEC||x==T_BVEC||x==T_CVEC)
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
    if (col->t == T_CVEC) {
        V *r = v_list(col->n);
        for (int64_t i = 0; i < col->n; i++)
            r->L[i] = v_char(col->B[i]);
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
        if (!dup) v_list_append_own(r, v_ref(a[i]));
    }
    return r;
}