/* shakti/src/import.c — .ie module loader */
#include "shakti_internal.h"
#include <sys/stat.h>

/* fopen() may succeed on directories (Linux); reject non-regular files. */
FILE *fopen_regular(const char *path, char *err, size_t err_cap) {
    struct stat sb;
    FILE *f;
    if (!path || !path[0]) return NULL;
    if (stat(path, &sb) != 0) return NULL;
    if (!S_ISREG(sb.st_mode)) {
        if (err && err_cap)
            snprintf(err, err_cap, "not a regular file: %s", path);
        return NULL;
    }
    f = fopen(path, "r");
    return f;
}
void shakti_set_file_vars(Env *e, const char *path) {
    char full[8192];
    char *sep;
    V *v;
    if (!e || !path || !path[0]) return;
#ifdef _WIN32
    if (!_fullpath(full, path, sizeof full)) snprintf(full, sizeof full, "%s", path);
#else
    char *rp = realpath(path, NULL);
    snprintf(full, sizeof full, "%s", rp ? rp : path);
    free(rp);
#endif
    v = v_str(full);
    env_set_kind(e, "__file__", v, ENV_BIND_INTERNAL);
    v_free(v);
    sep = strrchr(full, '/');
#ifdef _WIN32
    {
        char *bs = strrchr(full, '\\');
        if (bs && (!sep || bs > sep)) sep = bs;
    }
#endif
    if (!sep) snprintf(full, sizeof full, ".");
    else if (sep == full) full[1] = 0;
    else *sep = 0;
    v = v_str(full);
    env_set_kind(e, "__dir__", v, ENV_BIND_INTERNAL);
    v_free(v);
}
static const char *SHAKTI_SQL_FLAG = "__shakti_sql__";
static int is_sql_import(const char *name) {
    P(!name || !name[0],0)
    if (!strcmp(name, "sql")) return 1;
    const char *dot = strrchr(name, '.');
    return dot && !strcmp(dot + 1, "sql");
}
static int shakti_sql_enabled(Env *e) {
    V *v = env_get(e, SHAKTI_SQL_FLAG);
    return v && v->t == T_BOOL && v->j;
}
V *require_sql(Env *e) {
    P(!shakti_sql_enabled(e),v_err("SQL requires: import sql"))
    return NULL;
}
static int import_name_safe(const char *name) {
    const char *p;
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (strstr(name, "..")) return 0;
    for (p = name; *p; p++)
        if (*p == '/' || *p == '\\') return 0;
    return 1;
}
static V *import_module(const char *name, Env *e) {
    P(!name || !name[0],v_err("import requires a module name"))
    if (!import_name_safe(name))
        return v_err("import: module name must be a relative identifier");
    {
        const char *dot = strchr(name, '.');
        if (dot && (dot - name) >= 256)
            return v_err("import: parent name too long");
    }
    char path[8192];
    char dotpath[8192];
    FILE *f = NULL;
    snprintf(dotpath, sizeof dotpath, "%s", name);
    for (char *p = dotpath; *p; p++) if (*p == '.') *p = '/';
    /* Search order: the running script's directory, then the stdlib dirs
     * (exe-relative lib, $SHAKTI_LIB). The working directory is searched only
     * when there is no script (REPL / -c), and then after the stdlib, so an
     * untrusted cwd cannot shadow modules. Only NAME.ie files are imported. */
    {
        const char *envlib = getenv("SHAKTI_LIB");
        const char *dirs[3];
        int nd = 0;
        if (g_script_dir[0]) dirs[nd++] = g_script_dir;
        if (g_lib_path[0]) dirs[nd++] = g_lib_path;
        if (envlib && envlib[0]) dirs[nd++] = envlib;
        if (!g_script_dir[0]) dirs[nd++] = ".";
        for (int pass = 0; pass < 2 && !f; pass++) {
            const char *rel = pass == 0 ? name : dotpath;
            if (pass == 1 && !strcmp(dotpath, name)) break;
            for (int k = 0; k < nd && !f; k++) {
                if (snprintf(path, sizeof path, "%s/%s.ie", dirs[k], rel) >= (int)sizeof path)
                    continue;
                f = fopen_regular(path, NULL, 0);
            }
        }
    }
    P(!f,v_errf("cannot import '%s'", name))
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if(sz < 0 || sz > 64 * 1024 * 1024) { fclose(f); return v_errf("cannot import '%s': invalid file size", name); }
    char *buf = malloc((size_t)sz+2);
    if(!buf) { fclose(f); return v_err("import: oom"); }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got]='\n'; buf[got+1]=0;
    fclose(f);
    Env *mod_env = env_new(e);
    shakti_set_file_vars(mod_env, path);
    Node *prog = parse(buf);
    V *r = eval(prog, mod_env);
    {
        int stray = g_returning;
        shakti_clear_flow();
        v_free(r);
        if (stray) {
            node_free(prog);
            free(buf);
            env_leave(mod_env);
            return v_err("import: return at module top level");
        }
    }
    node_free(prog);
    free(buf);
    V *mk = v_list(mod_env->len), *mv = v_list(mod_env->len);
    i(mod_env->len,{
        mk->L[i] = v_str(mod_env->names[i]);
        mv->L[i] = v_ref(mod_env->vals[i]);
    })
    V *mod_dict = v_dict(mk, mv);
    v_free(mk); v_free(mv);
    const char *dot = strchr(name, '.');
    if(dot) {
        char parent[256];
        int plen = dot - name;
        if (plen < 0 || (size_t)plen >= sizeof parent) {
            v_free(mod_dict);
            env_leave(mod_env);
            return v_err("import: parent name too long");
        }
        memcpy(parent, name, (size_t)plen); parent[plen] = 0;
        const char *child = dot + 1;
        V *existing = env_get(e, parent);
        if(existing && existing->t == T_DICT) {
            v_dict_set(existing, child, mod_dict);
        } else {
            V *nk = v_list(1), *nv = v_list(1);
            nk->L[0] = v_str(child);
            nv->L[0] = v_ref(mod_dict);
            V *ns = v_dict(nk, nv);
            env_set_kind(e, parent, ns, ENV_BIND_IMPORT);
            v_free(nk); v_free(nv); v_free(ns);
        }
    } else {
        env_set_kind(e, name, mod_dict, ENV_BIND_IMPORT);
    }
    if (is_sql_import(name)) {
        V *flag = v_bool(1);
        env_set_kind(e, SHAKTI_SQL_FLAG, flag, ENV_BIND_INTERNAL);
        v_free(flag);
    }
    v_free(mod_dict);
    env_leave(mod_env);
    return v_nil();
}
/* The modules being imported, innermost first. A module that imports itself, directly or through
 * others, would otherwise run again and again until the C stack is gone: that import raises
 * instead, so the error reaches the import that began the cycle (as a module's parse error does). */
typedef struct ImportFrame { const char *name; struct ImportFrame *up; } ImportFrame;
static ImportFrame *import_stack;
V *do_import(const char *name, Env *e) {
    for (ImportFrame *p = import_stack; p && name; p = p->up)
        if (!strcmp(p->name, name)) {
            V *err = v_errf("import: circular import of '%s'", name);
            if (g_error_val) v_free(g_error_val);
            g_error = 1;
            g_error_val = v_ref(err);
            return err;
        }
    ImportFrame self = { name, import_stack };
    import_stack = &self;
    V *r = import_module(name, e);
    import_stack = self.up;
    env_imported();
    return r;
}
