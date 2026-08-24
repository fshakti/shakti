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
static const char *SHAKTI_SQL_FLAG = "__shakti_sql__";
static int is_sql_import(const char *name) {
    P(!name || !name[0],0)
    if (!strcmp(name, "sql")) return 1;
    const char *dot = strrchr(name, '.');
    return dot && !strcmp(dot + 1, "sql");
}
static int shakti_sql_enabled(Env *e) {
    V *v = env_get(e, SHAKTI_SQL_FLAG);
    return v && v->t == T_BOOL && v->b;
}
V *require_sql(Env *e) {
    P(!shakti_sql_enabled(e),v_err("SQL requires: import sql"))
    return NULL;
}
V *do_import(const char *name, Env *e) {
    P(!name || !name[0],v_err("import requires a module name"))
    char path[8192];
    char open_err[256];
    FILE *f = NULL;
    open_err[0] = 0;
    f = fopen_regular(name, open_err, sizeof open_err);
    if(!f) { snprintf(path,sizeof(path),"%s.ie",name); f=fopen_regular(path, open_err, sizeof open_err); }
    if(!f) { snprintf(path,sizeof(path),"%s/%s",g_script_dir,name); f=fopen_regular(path, open_err, sizeof open_err); }
    if(!f) { snprintf(path,sizeof(path),"%s/%s.ie",g_script_dir,name); f=fopen_regular(path, open_err, sizeof open_err); }
    if(!f && g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s",g_lib_path,name); f=fopen_regular(path, open_err, sizeof open_err); }
    if(!f && g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s.ie",g_lib_path,name); f=fopen_regular(path, open_err, sizeof open_err); }
    if(!f) {
        const char *env = getenv("SHAKTI_LIB");
        if(env) {
            snprintf(path,sizeof(path),"%s/%s",env,name); f=fopen_regular(path, open_err, sizeof open_err);
            if(!f) { snprintf(path,sizeof(path),"%s/%s.ie",env,name); f=fopen_regular(path, open_err, sizeof open_err); }
        }
    }
    if(!f) {
        char dotpath[8192];
        const char *envlib;
        strncpy(dotpath, name, sizeof(dotpath)-1);
        dotpath[sizeof(dotpath)-1] = 0;
        for(char *p=dotpath; *p; p++) if(*p=='.') *p='/';
        if(g_lib_path[0]) { snprintf(path,sizeof(path),"%s/%s.ie",g_lib_path,dotpath); f=fopen_regular(path, open_err, sizeof open_err); }
        if(!f) {
            envlib = getenv("SHAKTI_LIB");
            if(envlib && envlib[0]) {
                snprintf(path,sizeof(path),"%s/%s.ie",envlib,dotpath); f=fopen_regular(path, open_err, sizeof open_err);
            }
        }
        if(!f) { snprintf(path,sizeof(path),"%s.ie",dotpath); f=fopen_regular(path, open_err, sizeof open_err); }
        if(!f && g_script_dir[0]) {
            snprintf(path,sizeof(path),"%s/%s.ie",g_script_dir,dotpath); f=fopen_regular(path, open_err, sizeof open_err);
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
    Node *prog = parse(buf);
    V *r = eval(prog, mod_env);
    v_free(r);
    node_free(prog);
    free(buf);
    V *mk = v_list(mod_env->len), *mv = v_list(mod_env->len);
    i(mod_env->len,{
        mk->L[i] = v_str(mod_env->names[i]);
        mv->L[i] = v_ref(mod_env->vals[i]);
    })
    V *mod_dict = v_dict(mk, mv);
    v_free(mk); v_free(mv);
    char *dot = strchr(name, '.');
    if(dot) {
        char parent[256];
        int plen = dot - name;
        memcpy(parent, name, plen); parent[plen] = 0;
        const char *child = dot + 1;
        V *existing = env_get(e, parent);
        if(existing && existing->t == T_DICT) {
            v_dict_set(existing, child, mod_dict);
        } else {
            V *nk = v_list(1), *nv = v_list(1);
            nk->L[0] = v_str(child);
            nv->L[0] = v_ref(mod_dict);
            V *ns = v_dict(nk, nv);
            env_set(e, parent, ns);
            v_free(nk); v_free(nv); v_free(ns);
        }
    } else {
        env_set(e, name, mod_dict);
    }
    if (is_sql_import(name))
        env_set(e, SHAKTI_SQL_FLAG, v_bool(1));
    v_free(mod_dict);
    env_free(mod_env);
    return v_nil();
}
