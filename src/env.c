/* shakti/src/env.c — environments, fn_ast, interpreter globals */
#include "shakti_internal.h"

Node *fn_ast[MAX_FN];
int   fn_ast_n = 0;
static int fn_ast_refs[MAX_FN];
static int fn_ast_free_stack[MAX_FN];
static int fn_ast_free_n;
int   g_returning  = 0;
int   g_breaking   = 0;
int   g_continuing = 0;
int   g_error      = 0;
V    *g_retval     = NULL;
V    *g_error_val  = NULL;
char  g_lib_path[4096] = "";
char  g_script_dir[4096] = ".";
/* Collection (env_collect): a collection that walks W references waits at least
 * W/SHAKTI_COLLECT_WORK scopes before the next; a reference to a list, dict, table, function or
 * environment counts as SHAKTI_COLLECT_HOLDER (it costs a lookup); an import counts as
 * SHAKTI_COLLECT_IMPORT scopes. */
#define SHAKTI_COLLECT_WORK 64
#define SHAKTI_COLLECT_HOLDER 4
#define SHAKTI_COLLECT_IMPORT 64
static void env_collect(void);
int fn_ast_store(Node *n) {
    if (!n) return -1;
    if (n->fn_ast_i > -1) {
        if (n->fn_ast_i < fn_ast_n) fn_ast_refs[n->fn_ast_i]++;
        return n->fn_ast_i;
    }
    int idx;
    /* a full table may be full of functions that only hold each other: collect them first */
    if (fn_ast_free_n == 0 && fn_ast_n >= MAX_FN) env_collect();
    if (fn_ast_free_n > 0)
        idx = fn_ast_free_stack[--fn_ast_free_n];
    else {
        if (fn_ast_n >= MAX_FN) return -1;
        idx = fn_ast_n++;
    }
    fn_ast[idx] = n;
    fn_ast_refs[idx] = 1;
    n->fn_ast_i = idx;
    n->fn_detached = 0;
    return idx;
}
void fn_ast_release(int idx) {
    Node *n;
    if (idx < 0 || idx >= fn_ast_n) return;
    if (fn_ast_refs[idx] <= 0) return;
    if (--fn_ast_refs[idx] > 0) return;
    n = fn_ast[idx];
    fn_ast[idx] = NULL;
    if (fn_ast_free_n < MAX_FN)
        fn_ast_free_stack[fn_ast_free_n++] = idx;
    if (!n) return;
    if (n->fn_detached) {
        n->fn_ast_i = -1;
        node_free(n);
    } else {
        n->fn_ast_i = -2;
    }
}
Env *env_new(Env *parent) {
    Env *e = x_calloc(1, sizeof(Env), "env_new");
    e->rc = 1; e->cap = 16; e->len = 0;
    e->names  = x_calloc(16, sizeof(char*), "env_new");
    e->vals   = x_calloc(16, sizeof(V*), "env_new");
    e->hashes = x_calloc(16, sizeof(uint32_t), "env_new");
    e->kinds  = x_calloc(16, sizeof(uint8_t), "env_new");
    e->parent = parent;
    if(parent) parent->rc++;
    return e;
}
#define SHAKTI_ENV_POOL_MAX 64
static Env *shakti_env_pool[SHAKTI_ENV_POOL_MAX];
static int shakti_env_pool_n;
Env *env_acquire(Env *parent) {
    Env *e;
    if(shakti_env_pool_n > 0) {
        e = shakti_env_pool[--shakti_env_pool_n];
        for(int i = 0; i < e->len; i++) {
            free(e->names[i]);
            v_free(e->vals[i]);
        }
        e->len = 0;
        e->rc = 1;
        e->cand = 0;
        e->parent = parent;
        if(parent) parent->rc++;
        return e;
    }
    return env_new(parent);
}
void env_release(Env *e) {
    if(!e) return;
    if(e->rc != 1) {
        env_free(e);
        return;
    }
    Env *parent = e->parent;
    e->parent = NULL;
    if(parent) env_free(parent);
    for(int i = 0; i < e->len; i++) {
        free(e->names[i]);
        v_free(e->vals[i]);
        e->names[i] = NULL;
        e->vals[i] = NULL;
    }
    e->len = 0;
    e->rc = 1;
    /* the frees above can release other environments into the pool: look for room only now */
    if(shakti_env_pool_n >= SHAKTI_ENV_POOL_MAX) {
        free(e->names); free(e->vals); free(e->hashes); free(e->kinds);
        free(e);
        return;
    }
    shakti_env_pool[shakti_env_pool_n++] = e;
}
/* A function made in a scope holds the scope's environment (its closure) and the environment
 * holds the function, so reference counts never free either. env_leave ends a scope: an
 * environment nothing else holds is released; any other becomes a candidate, the list keeping the
 * scope's reference, and goes as soon as the list's is the only reference left. env_collect frees
 * the candidates that only candidates refer to. It counts, for the candidates and the lists,
 * dicts, tables and functions they reach, the references they hold to one another, and these
 * edges must be exactly the ones env_free and v_free let go of (an environment's values and
 * parent; a list's items; a dict's or table's keys and values; a function's parameters, defaults
 * and closure): counting one that is not there frees what is in use, counting too few only leaves
 * something behind. Values that hold nothing, and short lists of them, stay out of the graph.
 * Other environments (the root, scopes still running) are not walked, so a reference from one
 * counts as from outside. Anything with references left over is live, as is all it reaches; the
 * rest is unreachable, and emptying it frees it. Freeing lets go only of what the unreachable
 * part alone held: its references to anything that stays (a list in use, the environment of a
 * call in progress, the root) are kept, so every count stays what it would have been had the
 * cycle never been freed, and an environment so kept is never freed (it leaves the list for
 * good). A value C code borrows from an environment must stay held by that environment while it
 * is used, as without collection. */
enum {
    GC_VALUE,    /* a list, dict, table or function */
    GC_ENV,      /* a candidate environment */
    GC_OUTSIDE   /* while freeing: a value outside the graph the freed nodes hold (refs: how often) */
};
enum {
    GC_NOT_KEPT,
    GC_KEPT,     /* unreachable, but a cycle of values holds it: left alone */
    GC_PEELED    /* only inside gc_valuecycles */
};
/* adj: a node's edges to other nodes, kept while counting so marking needs no lookups; pin: a
 * candidate a freed node refers to, never freed */
typedef struct { void *p; int64_t refs; int adj, nadj, nleaf; unsigned char kind, live, kept, pin; } GcNode;
/* kept between collections (and let go of after a few much smaller ones) */
typedef struct { GcNode *n; int len, cap, *slot, nslot, *stack, top, small, *adj, nadjs, adjcap; int64_t walked; } GcGraph;
static GcGraph gc;
static Env **env_cands;
static int env_cands_n, env_cands_cap, env_cands_small, env_cands_limit = 256, env_cands_extra, env_collecting;
static uint32_t gc_hash(const void *p) {
    uint64_t u = (uint64_t)(uintptr_t)p * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(u >> 32);
}
/* the slot that holds p's node, or the empty slot where it would go, with room for one more node */
static uint32_t gc_slot(GcGraph *g, const void *p) {
    if (2 * (g->len + 1) > g->nslot) {
        g->nslot = g->nslot ? 2 * g->nslot : 64;
        free(g->slot);
        g->slot = x_calloc((size_t)g->nslot, sizeof(int), "env_collect");
        for (int i = 0; i < g->len; i++) {
            uint32_t h = gc_hash(g->n[i].p) & (uint32_t)(g->nslot - 1);
            while (g->slot[h]) h = (h + 1) & (uint32_t)(g->nslot - 1);
            g->slot[h] = i + 1;
        }
    }
    uint32_t h = gc_hash(p) & (uint32_t)(g->nslot - 1);
    while (g->slot[h] && g->n[g->slot[h] - 1].p != p) h = (h + 1) & (uint32_t)(g->nslot - 1);
    return h;
}
static int gc_add(GcGraph *g, uint32_t h, void *p, int kind, int64_t refs) {
    if (g->len == g->cap) {
        g->cap = g->cap ? 2 * g->cap : 64;
        g->n = x_realloc(g->n, (size_t)g->cap * sizeof(GcNode), "env_collect");
        g->stack = x_realloc(g->stack, (size_t)g->cap * sizeof(int), "env_collect");
    }
    g->slot[h] = g->len + 1;
    g->n[g->len] = (GcNode){ p, refs, 0, 0, 0, (unsigned char)kind, 0, GC_NOT_KEPT, 0 };
    return g->len++;
}
static int gc_holder(V *v) {
    return v->t == T_LIST || v->t == T_DICT || v->t == T_TABLE || (v->t == T_FN && v->n != -1);
}
/* what a choice made by reference count can look at: lists, vectors, matrices, dicts, tables */
static int gc_shareable(V *v) {
    return (v->t >= T_IVEC && v->t <= T_TABLE) || (v->t >= T_IMAT && v->t <= T_BMAT);
}
/* A short list of numbers, strings and the like can't lead back to an environment, and freeing
 * it lets go of nothing such a choice looks at: it stays out of the graph, and what holds it
 * frees it. */
static int gc_inert(V *v) {
    if (v->t != T_LIST || v->n > 16) return 0;
    for (int64_t k = 0; k < v->n; k++)
        if (v->L[k] && (gc_holder(v->L[k]) || gc_shareable(v->L[k]))) return 0;
    return 1;
}
/* An edge from node i: it comes off its target's count (a list, dict, table or function met for
 * the first time joins the graph) and is kept for marking. Values that hold nothing, and
 * environments that are not candidates, stay out. */
static void gc_edge(GcGraph *g, int i, void *p, int env) {
    V *v = p;
    int j = -1;
    if (!p) return;
    if (env || gc_holder(v)) {
        uint32_t h = gc_slot(g, p);
        if (g->slot[h]) j = g->slot[h] - 1;
        else if (!env && !gc_inert(v)) j = gc_add(g, h, p, GC_VALUE, v->rc);
    }
    if (j < 0) { g->n[i].nleaf++; return; }
    g->n[j].refs--;
    if (g->nadjs == g->adjcap) {
        g->adjcap = g->adjcap ? 2 * g->adjcap : 256;
        g->adj = x_realloc(g->adj, (size_t)g->adjcap * sizeof(int), "env_collect");
    }
    g->adj[g->nadjs++] = j;
}
static void gc_edges(GcGraph *g, int i) {
    g->n[i].adj = g->nadjs;
    if (g->n[i].kind == GC_ENV) {
        Env *e = g->n[i].p;
        for (int k = 0; k < e->len; k++) gc_edge(g, i, e->vals[k], 0);
        gc_edge(g, i, e->parent, 1);
    } else {
        V *v = g->n[i].p;
        switch (v->t) {
        case T_LIST: for (int64_t k = 0; k < v->n; k++) gc_edge(g, i, v->L[k], 0); break;
        case T_DICT: case T_TABLE: gc_edge(g, i, v->keys, 0); gc_edge(g, i, v->vals, 0); break;
        case T_FN:
            gc_edge(g, i, v->params, 0);
            gc_edge(g, i, v->defaults, 0);
            gc_edge(g, i, v->closure, 1);
            break;
        default: break;
        }
    }
    g->n[i].nadj = g->nadjs - g->n[i].adj;
}
static void gc_mark(GcGraph *g, int i) {
    if (g->n[i].live) return;
    g->n[i].live = 1;
    g->stack[g->top++] = i;
    while (g->top) {
        GcNode *x = &g->n[g->stack[--g->top]];
        g->walked += x->nleaf + (int64_t)SHAKTI_COLLECT_HOLDER * x->nadj;
        for (int k = x->adj; k < x->adj + x->nadj; k++) {
            int j = g->adj[k];
            if (!g->n[j].live) { g->n[j].live = 1; g->stack[g->top++] = j; }
        }
    }
}
/* Of the unreachable nodes, those a cycle of lists, dicts and tables (with no environment in it)
 * holds can't be freed: they, and all they reach, are left as they are. */
static void gc_valuecycles(GcGraph *g) {
    GcNode *n = g->n;
    for (int i = 0; i < g->len; i++)
        if (!n[i].live) n[i].refs = 0;
    for (int i = 0; i < g->len; i++)
        if (!n[i].live && n[i].kind == GC_VALUE)
            for (int k = n[i].adj; k < n[i].adj + n[i].nadj; k++)
                if (!n[g->adj[k]].live && n[g->adj[k]].kind == GC_VALUE) n[g->adj[k]].refs++;
    g->top = 0;
    for (int i = 0; i < g->len; i++)
        if (!n[i].live && n[i].kind == GC_VALUE && n[i].refs == 0) { n[i].kept = GC_PEELED; g->stack[g->top++] = i; }
    while (g->top) {
        int i = g->stack[--g->top];
        for (int k = n[i].adj; k < n[i].adj + n[i].nadj; k++) {
            GcNode *x = &n[g->adj[k]];
            if (!x->live && x->kind == GC_VALUE && --x->refs == 0) { x->kept = GC_PEELED; g->stack[g->top++] = g->adj[k]; }
        }
    }
    for (int i = 0; i < g->len; i++)
        if (!n[i].live && n[i].kind == GC_VALUE && n[i].kept == GC_NOT_KEPT) { n[i].kept = GC_KEPT; g->stack[g->top++] = i; }
    while (g->top) {
        int i = g->stack[--g->top];
        for (int k = n[i].adj; k < n[i].adj + n[i].nadj; k++) {
            GcNode *x = &n[g->adj[k]];
            if (!x->live && x->kept != GC_KEPT) { x->kept = GC_KEPT; g->stack[g->top++] = g->adj[k]; }
        }
    }
    for (int i = 0; i < g->len; i++)
        if (n[i].kept == GC_PEELED) n[i].kept = GC_NOT_KEPT;
}
#define GC_KEPT_MAX (1 << 30)  /* kept references stop adding to a count this large */
/* A reference a node about to be freed holds: keep it, unless its target is freed as well or is
 * a value no choice looks at the count of (a number, a string, an error, a builtin). A list,
 * vector or matrix outside the graph is counted here and decided once all are seen (gc_keep_out).
 * An environment keeps one such reference and is never freed. */
static void gc_keep(GcGraph *g, void *p, int env) {
    GcNode *x = NULL;
    V *v = p;
    if (!p || (!env && !gc_holder(v) && !gc_shareable(v))) return;
    uint32_t h = gc_slot(g, p);
    if (g->slot[h]) {
        x = &g->n[g->slot[h] - 1];
        if (x->kind == GC_OUTSIDE) { x->refs++; return; }
        if (!x->live && x->kept == GC_NOT_KEPT) return;
    }
    if (env) {
        Env *e = p;
        if (e->cand < 0 || (x && x->pin)) return;
        env_ref(e);
        if (x) x->pin = 1;
        else e->cand = -1;
        return;
    }
    if (!x) { gc_add(g, h, p, GC_OUTSIDE, 1); return; }
    if (v->rc < GC_KEPT_MAX) v_ref(v);
}
/* A list, vector or matrix outside the graph that the freed nodes hold r times: if something else
 * holds it too, keep those references; if not, the cascade frees it. */
static void gc_keep_out(GcGraph *g, int i) {
    V *v = g->n[i].p;
    if (v->rc <= g->n[i].refs) return;
    for (int64_t k = 0; k < g->n[i].refs && v->rc < GC_KEPT_MAX; k++) v_ref(v);
}
static void gc_keep_edges(GcGraph *g, int i) {
    if (g->n[i].kind == GC_ENV) {
        Env *e = g->n[i].p;
        for (int k = 0; k < e->len; k++) gc_keep(g, e->vals[k], 0);
        gc_keep(g, e->parent, 1);
        return;
    }
    V *v = g->n[i].p;
    switch (v->t) {
    case T_LIST: for (int64_t k = 0; k < v->n; k++) gc_keep(g, v->L[k], 0); break;
    case T_DICT: case T_TABLE: gc_keep(g, v->keys, 0); gc_keep(g, v->vals, 0); break;
    case T_FN:
        gc_keep(g, v->params, 0);
        gc_keep(g, v->defaults, 0);
        gc_keep(g, v->closure, 1);
        break;
    default: break;
    }
}
static void env_collect(void) {
    GcGraph *g = &gc;
    int n = env_cands_n, kept = 0;
    if (env_collecting || n == 0) return;
    env_collecting = 1;
    g->len = g->top = g->nadjs = 0;
    g->walked = 0;
    if (g->nslot) memset(g->slot, 0, (size_t)g->nslot * sizeof(int));
    for (int i = 0; i < n; i++) gc_add(g, gc_slot(g, env_cands[i]), env_cands[i], GC_ENV, env_cands[i]->rc - 1);
    for (int i = 0; i < g->len; i++) gc_edges(g, i);
    for (int i = 0; i < g->len; i++)
        if (g->n[i].refs > 0) gc_mark(g, i);
    gc_valuecycles(g);
    for (int i = 0; i < g->len; i++)
        if (!g->n[i].live && !g->n[i].kept && g->n[i].kind != GC_OUTSIDE) gc_keep_edges(g, i);
    for (int i = 0; i < g->len; i++)
        if (g->n[i].kind == GC_OUTSIDE) gc_keep_out(g, i);
    /* hold each candidate to free, empty it (letting go of its functions, which let go of it and
     * of their slots), then let go of it and of the list's reference */
    for (int i = 0; i < n; i++)
        if (!g->n[i].live && !g->n[i].kept) env_ref(env_cands[i]);
    for (int i = 0; i < n; i++) {
        Env *e = env_cands[i];
        if (g->n[i].live || g->n[i].kept) continue;
        for (int k = 0; k < e->len; k++) { free(e->names[k]); v_free(e->vals[k]); }
        e->len = 0;
    }
    for (int i = 0; i < n; i++) {
        Env *e = env_cands[i];
        if (g->n[i].live && !g->n[i].pin) { env_cands[kept++] = e; e->cand = kept; continue; }
        if (g->n[i].live || g->n[i].kept) {
            /* never freed now (a kept reference, or a value cycle holds it): off the list for good */
            e->cand = -1;
            env_free(e);
            continue;
        }
        e->cand = 0;
        env_free(e);
        if (e->rc == 1) env_release(e);
        else env_free(e);
    }
    for (int i = kept; i < n; i++) env_cands[i] = NULL;
    env_cands_n = kept;
    env_cands_extra = 0;
    /* collect again after twice the survivors, and after enough scopes to pay for walking them */
    env_cands_limit = 2 * kept > 256 ? 2 * kept : 256;
    if (g->walked / SHAKTI_COLLECT_WORK > env_cands_limit)
        env_cands_limit = g->walked / SHAKTI_COLLECT_WORK > INT_MAX / 2 ? INT_MAX / 2 : (int)(g->walked / SHAKTI_COLLECT_WORK);
    if (g->cap > 4096 && g->len * 8 < g->cap) {
        if (++g->small >= 4) {
            free(g->n); free(g->slot); free(g->stack); free(g->adj);
            *g = (GcGraph){0};
        }
    } else g->small = 0;
    if (env_cands_cap > 4096 && kept * 8 < env_cands_cap) {
        if (++env_cands_small >= 4) {
            env_cands_cap = 2 * kept > 256 ? 2 * kept : 256;
            env_cands = x_realloc(env_cands, (size_t)env_cands_cap * sizeof(Env *), "env_collect");
            env_cands_small = 0;
        }
    } else env_cands_small = 0;
    env_collecting = 0;
}
void env_leave(Env *e) {
    if (!e) return;
    if (e->cand < 0) env_free(e); /* a collection kept a reference to it: it is never freed */
    else if (e->rc == 1) env_release(e);
    else {
        if (env_cands_n == env_cands_cap) {
            env_cands_cap = env_cands_cap ? 2 * env_cands_cap : 256;
            env_cands = x_realloc(env_cands, (size_t)env_cands_cap * sizeof(Env *), "env_leave");
        }
        env_cands[env_cands_n++] = e;
        e->cand = env_cands_n;
#ifndef SHAKTI_COLLECT_EVERY
        if (env_cands_n + env_cands_extra >= env_cands_limit) env_collect();
#endif
    }
#ifdef SHAKTI_COLLECT_EVERY
    /* test builds only: collect at the end of every scope, to check the walk under a sanitizer */
    env_collect();
#endif
}
/* An import counts for SHAKTI_COLLECT_IMPORT scopes: running a module is much more work than a
 * call, and each import leaves the last run of the module for the collector. */
void env_imported(void) {
    env_cands_extra += SHAKTI_COLLECT_IMPORT - 1;
    if (env_cands_n + env_cands_extra >= env_cands_limit) env_collect();
}
/* A candidate the list alone holds: take it off the list and release it, as a scope's end would. */
static void env_uncand(Env *e) {
    Env *last = env_cands[--env_cands_n];
    env_cands[e->cand - 1] = last;
    last->cand = e->cand;
    env_cands[env_cands_n] = NULL;
    e->cand = 0;
    env_release(e);
}
void env_set_kind(Env *e, const char *name, V *val, unsigned kind) {
    uint32_t h = fnv1a(name);
    for(int i=0; i<e->len; i++) {
        if(e->hashes[i] == h && strcmp(e->names[i], name)==0) {
            v_free(e->vals[i]);
            e->vals[i] = v_ref(val);
            e->kinds[i] = (uint8_t)kind;
            return;
        }
    }
    if(e->len >= e->cap) {
        e->cap *= 2;
        e->names  = x_realloc(e->names,  e->cap * sizeof(char*), "env_set");
        e->vals   = x_realloc(e->vals,   e->cap * sizeof(V*), "env_set");
        e->hashes = x_realloc(e->hashes, e->cap * sizeof(uint32_t), "env_set");
        e->kinds  = x_realloc(e->kinds,  e->cap * sizeof(uint8_t), "env_set");
    }
    e->names[e->len]  = x_strdup(name, "env_set");
    e->vals[e->len]   = v_ref(val);
    e->hashes[e->len] = h;
    e->kinds[e->len]  = (uint8_t)kind;
    e->len++;
}
void env_set(Env *e, const char *name, V *val) {
    env_set_kind(e, name, val, ENV_BIND_USER);
}
int env_slot_listed(const Env *e, int i) {
    const char *n;
    if(!e || i < 0 || i >= e->len) return 0;
    if(e->kinds && e->kinds[i] != ENV_BIND_USER) return 0;
    n = e->names[i];
    if(n && n[0] == '_' && n[1] == '_') return 0;
    return 1;
}
/* Mutate an existing INT binding in place when uniquely owned (rc==1). */
int env_set_int_inplace(Env *e, const char *name, int64_t j) {
    uint32_t h = fnv1a(name);
    for(; e; e=e->parent)
        for(int i=0; i<e->len; i++)
            if(e->hashes[i] == h && strcmp(e->names[i], name)==0) {
                V *cur = e->vals[i];
                if(cur && cur->t == T_INT && shakti_count(cur) == 1) {
                    cur->j = j;
                    return 1;
                }
                V *nv = v_int(j);
                v_free(cur);
                e->vals[i] = nv;
                return 1;
            }
    return 0;
}
int env_get_int(Env *e, const char *name, int64_t *out) {
    V *v = env_get(e, name);
    if(!v || v->t != T_INT) return 0;
    *out = v->j;
    return 1;
}
void env_set_local(Env *e, const char *name, V *val) {
    env_set(e, name, val);
}
int env_update(Env *e, const char *name, V *val) {
    uint32_t h = fnv1a(name);
    for(; e; e=e->parent)
        for(int i=0; i<e->len; i++)
            if(e->hashes[i] == h && strcmp(e->names[i], name)==0) {
                if (e->vals[i] != val) {
                    v_free(e->vals[i]);
                    e->vals[i] = v_ref(val);
                }
                return 1;
            }
    return 0;
}
int env_index_here(Env *e, const char *name) {
    uint32_t h;
    int i;
    if (!e || !name) return -1;
    h = fnv1a(name);
    for (i = 0; i < e->len; i++)
        if (e->hashes[i] == h && strcmp(e->names[i], name) == 0)
            return i;
    return -1;
}
V *env_get_here(Env *e, const char *name) {
    int i = env_index_here(e, name);
    return i < 0 ? NULL : e->vals[i];
}
V *env_get(Env *e, const char *name) {
    uint32_t h = fnv1a(name);
    for(; e; e=e->parent)
        for(int i=0; i<e->len; i++)
            if(e->hashes[i] == h && strcmp(e->names[i], name)==0)
                return e->vals[i];
    return NULL;
}
void env_ref(Env *e) { if(e) e->rc++; }
void env_free(Env *e) {
    Pv(!e)
    if (--e->rc > 0) {
        if (e->rc == 1 && e->cand > 0 && !env_collecting) env_uncand(e);
        return;
    }
    i(e->len,{free(e->names[i]); v_free(e->vals[i]);})
    free(e->names); free(e->vals); free(e->hashes); free(e->kinds);
    if(e->parent) env_free(e->parent);
    free(e);
}
int env_save(Env *e, const char *path) {
    FILE *fp = fopen(path, "wb");
    P(!fp,0)
    fwrite("KAIO_MCP", 1, 8, fp);
    fwrite(&e->len, 4, 1, fp);
    i(e->len,{
        int nlen = strlen(e->names[i]);
        fwrite(&nlen, 4, 1, fp);
        fwrite(e->names[i], 1, nlen, fp);
        v_serialize(e->vals[i], fp);
    })
    fclose(fp);
    return 1;
}
int env_load(Env *e, const char *path) {
    FILE *fp = fopen(path, "rb");
    P(!fp,0)
    char hdr[8];
    if (fread(hdr, 1, 8, fp) != 8 || memcmp(hdr, "KAIO_MCP", 8)) { fclose(fp); return 0; }
    int count;
    if (fread(&count, 4, 1, fp) != 1 || count < 0 || count > SHAKTI_DESER_MAX_VARS) { fclose(fp); return 0; }
    for (int i = 0; i < count; i++) {
        int nlen;
        if (fread(&nlen, 4, 1, fp) != 1 || nlen < 0 || nlen > SHAKTI_ENV_MAX_NAME) { fclose(fp); return 0; }
        char *name = malloc((size_t)nlen + 1);
        if (!name) { fclose(fp); return 0; }
        if (nlen > 0 && fread(name, 1, (size_t)nlen, fp) != (size_t)nlen) { free(name); fclose(fp); return 0; }
        name[nlen] = 0;
        V *val = v_deserialize(fp);
        if (val->t == T_ERR) { free(name); v_free(val); fclose(fp); return 0; }
        env_set(e, name, val);
        v_free(val);
        free(name);
    }
    fclose(fp);
    return 1;
}

static int env_flag_on(const char *name) {
    const char *v = getenv(name);
    return v && v[0] && strcmp(v, "0") != 0;
}
int shakti_safe_mode(void) {
    return env_flag_on("SHAKTI_SAFE");
}
int shakti_exec_allowed(void) {
    const char *allow = getenv("SHAKTI_ALLOW_EXEC");
    if (shakti_safe_mode()) return 0;
    return !(allow && allow[0] == '0' && allow[1] == '\0');
}
int shakti_net_allowed(void) {
    return !shakti_safe_mode() || env_flag_on("SHAKTI_ALLOW_NET");
}
/* Safe mode confines file access to SHAKTI_FS_ROOT (default: the working
 * directory at first use). Paths are resolved with realpath, so symlinks that
 * point outside the root are rejected. */
int shakti_fs_check(const char *path, int write, char *err, size_t err_cap) {
#ifdef _WIN32
    (void)path; (void)write; (void)err; (void)err_cap;
    return 0;
#else
    static char root[4096];
    char full[8192];
    if (!shakti_safe_mode()) return 0;
    if (!path || !path[0]) {
        if (err && err_cap) snprintf(err, err_cap, "safe mode: empty path");
        return -1;
    }
    if (!root[0]) {
        const char *r = getenv("SHAKTI_FS_ROOT");
        char *rp = realpath(r && r[0] ? r : ".", NULL);
        if (!rp) {
            if (err && err_cap) snprintf(err, err_cap, "safe mode: bad SHAKTI_FS_ROOT");
            return -1;
        }
        snprintf(root, sizeof root, "%s", rp);
        free(rp);
    }
    char *rp = realpath(path, NULL);
    if (rp) {
        snprintf(full, sizeof full, "%s", rp);
        free(rp);
    } else {
        char dir[8192];
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        if (slash == path) snprintf(dir, sizeof dir, "/");
        else if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
        else snprintf(dir, sizeof dir, ".");
        if (!base[0] || !strcmp(base, ".") || !strcmp(base, "..")) {
            if (err && err_cap) snprintf(err, err_cap, "safe mode: bad path '%s'", path);
            return -1;
        }
        char *dp = realpath(dir, NULL);
        if (!dp) {
            if (err && err_cap) snprintf(err, err_cap, "safe mode: cannot resolve '%s'", path);
            return -1;
        }
        snprintf(full, sizeof full, "%s/%s", dp, base);
        free(dp);
    }
    size_t rl = strlen(root);
    int inside = !strcmp(root, "/") ||
                 (!strncmp(full, root, rl) && (full[rl] == 0 || full[rl] == '/'));
    if (!inside) {
        if (err && err_cap)
            snprintf(err, err_cap, "safe mode: %s outside SHAKTI_FS_ROOT: %s", write ? "write" : "read", path);
        return -1;
    }
    return 0;
#endif
}
