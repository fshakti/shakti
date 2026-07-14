/* isolde_bridge.c — runtime dlopen of libisolde.so so import isolde uses native kernels. */
#include "shakti.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef V *(*isolde_call_fn)(const char *, V **, int);
typedef int (*isolde_check_fn)(const char *);

static isolde_call_fn  g_isolde_call  = NULL;
static isolde_check_fn g_is_isolde    = NULL;
static int             g_tried        = 0;

static void isolde_try_load(void) {
    if (g_tried) return;
    g_tried = 1;

    const char *so = getenv("ISOLDE_LIB");
    if (!so) {
        const char *env = getenv("SHAKTI_LIB");
        static char buf[4096];
        if (env) {
            snprintf(buf, sizeof buf, "%s/../../isolde/libisolde.so", env);
            so = buf;
        }
    }
    if (!so) return;

    void *h = dlopen(so, RTLD_LAZY | RTLD_GLOBAL);
    if (!h) return;

    g_isolde_call = (isolde_call_fn)dlsym(h, "isolde_builtin_call");
    g_is_isolde   = (isolde_check_fn)dlsym(h, "is_isolde_builtin");
    if (!g_isolde_call || !g_is_isolde) {
        g_isolde_call = NULL;
        g_is_isolde   = NULL;
    }
}

int is_isolde_builtin(const char *name) {
    isolde_try_load();
    return g_is_isolde ? g_is_isolde(name) : 0;
}

V *isolde_builtin_call(const char *name, V **args, int nargs) {
    isolde_try_load();
    return g_isolde_call ? g_isolde_call(name, args, nargs) : v_nil();
}

#else /* _WIN32 — stubs */

int is_isolde_builtin(const char *name) { (void)name; return 0; }
V  *isolde_builtin_call(const char *name, V **args, int nargs) {
    (void)name; (void)args; (void)nargs; return v_nil();
}
#endif
