#include "graph.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *s;
    char *p;
    char *o;
} Triple;

typedef struct IdxEntry {
    char *key;
    int64_t *idx;
    int64_t n;
    int64_t cap;
    struct IdxEntry *next;
} IdxEntry;

typedef struct {
    Triple *triples;
    int64_t count;
    int64_t cap;
    IdxEntry **subj_idx;
    IdxEntry **pred_idx;
    IdxEntry **obj_idx;
    int nbuckets;
} Graph;

#define GRAPH_IDX_BUCKETS 256
#define GRAPH_MAX 64

static Graph *g_graphs[GRAPH_MAX];
static int g_graph_count;

static unsigned graph_hash(const char *s) {
    unsigned h = 5381;
    if (!s) return 0;
    for (; *s; s++) h = ((h << 5) + h) + (unsigned char)*s;
    return h % GRAPH_IDX_BUCKETS;
}

static char *graph_dup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n + 1);
    return d;
}

static void idx_free_chain(IdxEntry *e) {
    while (e) {
        IdxEntry *n = e->next;
        free(e->key);
        free(e->idx);
        free(e);
        e = n;
    }
}

static void graph_free_idx(IdxEntry **buckets) {
    if (!buckets) return;
    for (int i = 0; i < GRAPH_IDX_BUCKETS; i++)
        idx_free_chain(buckets[i]);
    free(buckets);
}

static void graph_free(Graph *g) {
    if (!g) return;
    for (int64_t i = 0; i < g->count; i++) {
        free(g->triples[i].s);
        free(g->triples[i].p);
        free(g->triples[i].o);
    }
    free(g->triples);
    graph_free_idx(g->subj_idx);
    graph_free_idx(g->pred_idx);
    graph_free_idx(g->obj_idx);
    free(g);
}

static IdxEntry *idx_find(IdxEntry *head, const char *key) {
    for (IdxEntry *e = head; e; e = e->next)
        if (!strcmp(e->key, key)) return e;
    return NULL;
}

static int idx_append(IdxEntry **head, const char *key, int64_t triple_idx) {
    IdxEntry *e = idx_find(*head, key);
    if (!e) {
        e = calloc(1, sizeof(IdxEntry));
        if (!e) return -1;
        e->key = graph_dup(key);
        if (!e->key) { free(e); return -1; }
        e->next = *head;
        *head = e;
    }
    if (e->n >= e->cap) {
        int64_t cap = e->cap ? e->cap * 2 : 8;
        int64_t *nidx = realloc(e->idx, (size_t)cap * sizeof(int64_t));
        if (!nidx) return -1;
        e->idx = nidx;
        e->cap = cap;
    }
    e->idx[e->n++] = triple_idx;
    return 0;
}

static void graph_index_triple(Graph *g, int64_t idx) {
    Triple *t = &g->triples[idx];
    unsigned hs = graph_hash(t->s);
    unsigned hp = graph_hash(t->p);
    unsigned ho = graph_hash(t->o);
    idx_append(&g->subj_idx[hs], t->s, idx);
    idx_append(&g->pred_idx[hp], t->p, idx);
    idx_append(&g->obj_idx[ho], t->o, idx);
}

static Graph *graph_new(void) {
    Graph *g = calloc(1, sizeof(Graph));
    if (!g) return NULL;
    g->subj_idx = calloc(GRAPH_IDX_BUCKETS, sizeof(IdxEntry *));
    g->pred_idx = calloc(GRAPH_IDX_BUCKETS, sizeof(IdxEntry *));
    g->obj_idx = calloc(GRAPH_IDX_BUCKETS, sizeof(IdxEntry *));
    if (!g->subj_idx || !g->pred_idx || !g->obj_idx) {
        graph_free(g);
        return NULL;
    }
    g->nbuckets = GRAPH_IDX_BUCKETS;
    return g;
}

static Graph *graph_at(int64_t id) {
    P(id < 0 || id >= g_graph_count || !g_graphs[id], NULL)
    return g_graphs[id];
}

static int graph_grow(Graph *g) {
    if (g->count < g->cap) return 0;
    int64_t cap = g->cap ? g->cap * 2 : 16;
    Triple *nt = realloc(g->triples, (size_t)cap * sizeof(Triple));
    P(!nt, -1)
    g->triples = nt;
    g->cap = cap;
    return 0;
}

static int graph_add_triple(Graph *g, const char *s, const char *p, const char *o) {
    P(graph_grow(g) < 0, -1)
    Triple *t = &g->triples[g->count];
    t->s = graph_dup(s);
    t->p = graph_dup(p);
    t->o = graph_dup(o);
    P(!t->s || !t->p || !t->o, -1)
    graph_index_triple(g, g->count);
    g->count++;
    return 0;
}

static int graph_wildcard(const char *pat) {
    return !pat || !pat[0] || !strcmp(pat, "*");
}

static int graph_match(const char *val, const char *pat) {
    return graph_wildcard(pat) || (val && pat && !strcmp(val, pat));
}

static void idx_collect(IdxEntry *head, const char *key, int64_t **out, int64_t *out_n, int64_t *out_cap) {
    IdxEntry *e = idx_find(head, key);
    if (!e) return;
    for (int64_t i = 0; i < e->n; i++) {
        if (*out_n >= *out_cap) {
            int64_t cap = *out_cap ? *out_cap * 2 : 16;
            int64_t *n = realloc(*out, (size_t)cap * sizeof(int64_t));
            Pv(!n)
            *out = n;
            *out_cap = cap;
        }
        (*out)[(*out_n)++] = e->idx[i];
    }
}

static int64_t *graph_candidate_indices(Graph *g, const char *s, const char *p, const char *o, int64_t *n_out) {
    int64_t *cand = NULL;
    int64_t cn = 0, cap = 0;

    if (!graph_wildcard(s)) {
        idx_collect(g->subj_idx[graph_hash(s)], s, &cand, &cn, &cap);
    } else if (!graph_wildcard(p)) {
        idx_collect(g->pred_idx[graph_hash(p)], p, &cand, &cn, &cap);
    } else if (!graph_wildcard(o)) {
        idx_collect(g->obj_idx[graph_hash(o)], o, &cand, &cn, &cap);
    } else {
        cand = malloc((size_t)g->count * sizeof(int64_t));
        P(!cand, NULL)
        for (int64_t i = 0; i < g->count; i++) cand[i] = i;
        cn = g->count;
    }
    *n_out = cn;
    return cand;
}

static V *graph_triples_table(Graph *g, int64_t *indices, int64_t n) {
    V *keys = v_list(3);
    keys->L[0] = v_str("subject");
    keys->L[1] = v_str("predicate");
    keys->L[2] = v_str("object");
    V *sc = v_list(n);
    V *pc = v_list(n);
    V *oc = v_list(n);
    for (int64_t i = 0; i < n; i++) {
        Triple *t = &g->triples[indices[i]];
        sc->L[i] = v_str(t->s);
        pc->L[i] = v_str(t->p);
        oc->L[i] = v_str(t->o);
    }
    V *cols = v_list(3);
    cols->L[0] = sc;
    cols->L[1] = pc;
    cols->L[2] = oc;
    V *tbl = v_table(keys, cols);
    v_free(keys);
    v_free(cols);
    return tbl;
}

static int tbl_col_idx(V *tbl, const char *name) {
    P(!tbl || tbl->t != T_TABLE || !name, -1)
    for (int64_t k = 0; k < tbl->keys->n; k++)
        if (!strcmp(tbl->keys->L[k]->s, name)) return (int)k;
    return -1;
}

static void cell_to_str(V *col, int64_t row, char *buf, size_t cap) {
    buf[0] = 0;
    Pv(!col || !buf || !cap)
    if (col->t == T_STR) snprintf(buf, cap, "%s", col->s);
    else if (col->t == T_LIST && row < col->n && col->L[row]) {
        char *t = v_to_str(col->L[row]);
        snprintf(buf, cap, "%s", t ? t : "");
        free(t);
    } else if (col->t == T_INT) snprintf(buf, cap, "%lld", (long long)col->j);
    else if (col->t == T_IVEC && row < col->n) snprintf(buf, cap, "%lld", (long long)col->J[row]);
    else if (col->t == T_FVEC && row < col->n) snprintf(buf, cap, "%g", col->F[row]);
}

static int64_t graph_id_arg(V **a, int n, int *pos) {
    if (n > 0 && a[0]->t == T_INT) {
        *pos = 1;
        return a[0]->j;
    }
    *pos = 0;
    return 0;
}

V *bi_graph_create(V **a, int n) {
    (void)a;
    (void)n;
    P(g_graph_count >= GRAPH_MAX, v_err("graph_create: too many graphs"))
    Graph *g = graph_new();
    P(!g, v_err("graph_create: out of memory"))
    g_graphs[g_graph_count] = g;
    return v_int(g_graph_count++);
}

V *bi_graph_add(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_add: bad graph id"))
    P(n - pos < 3, v_err("graph_add(g, subject, predicate, object)"))
    P(a[pos]->t != T_STR || a[pos + 1]->t != T_STR || a[pos + 2]->t != T_STR,
      v_err("graph_add: subject, predicate, object must be strings"))
    P(graph_add_triple(g, a[pos]->s, a[pos + 1]->s, a[pos + 2]->s) < 0,
      v_err("graph_add: failed"))
    return v_int(g->count);
}

V *bi_graph_query(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_query: bad graph id"))
    const char *s = (n > pos && a[pos]->t == T_STR) ? a[pos]->s : "*";
    const char *p = (n > pos + 1 && a[pos + 1]->t == T_STR) ? a[pos + 1]->s : "*";
    const char *o = (n > pos + 2 && a[pos + 2]->t == T_STR) ? a[pos + 2]->s : "*";

    int64_t cn = 0;
    int64_t *cand = graph_candidate_indices(g, s, p, o, &cn);
    P(!cand && g->count > 0, v_err("graph_query: out of memory"))

    int64_t *hits = NULL;
    int64_t hn = 0, hcap = 0;
    for (int64_t i = 0; i < cn; i++) {
        Triple *t = &g->triples[cand[i]];
        if (!graph_match(t->s, s) || !graph_match(t->p, p) || !graph_match(t->o, o))
            continue;
        if (hn >= hcap) {
            int64_t cap = hcap ? hcap * 2 : 8;
            int64_t *nh = realloc(hits, (size_t)cap * sizeof(int64_t));
            P(!nh, (free(cand), v_err("graph_query: out of memory")))
            hits = nh;
            hcap = cap;
        }
        hits[hn++] = cand[i];
    }
    free(cand);
    V *r = graph_triples_table(g, hits, hn);
    free(hits);
    return r;
}

V *bi_graph_neighbors(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_neighbors: bad graph id"))
    P(n - pos < 2 || a[pos]->t != T_STR, v_err("graph_neighbors(g, node, direction)"))
    const char *node = a[pos]->s;
    const char *dir = (n > pos + 1 && a[pos + 1]->t == T_STR) ? a[pos + 1]->s : "out";
    int has_out = !strcmp(dir, "out") || !strcmp(dir, "both");
    int has_in = !strcmp(dir, "in") || !strcmp(dir, "both");
    P(!has_out && !has_in, v_err("graph_neighbors: direction must be out, in, or both"))

    int64_t *hits = NULL;
    int64_t hn = 0, hcap = 0;
    for (int64_t i = 0; i < g->count; i++) {
        Triple *t = &g->triples[i];
        int match = (has_out && !strcmp(t->s, node)) || (has_in && !strcmp(t->o, node));
        if (!match) continue;
        if (hn >= hcap) {
            int64_t cap = hcap ? hcap * 2 : 8;
            int64_t *nh = realloc(hits, (size_t)cap * sizeof(int64_t));
            P(!nh, v_err("graph_neighbors: out of memory"))
            hits = nh;
            hcap = cap;
        }
        hits[hn++] = i;
    }
    V *r = graph_triples_table(g, hits, hn);
    free(hits);
    return r;
}

typedef struct {
    char **names;
    int64_t n;
    int64_t cap;
} NodePool;

static int pool_find(NodePool *pool, const char *name) {
    for (int64_t i = 0; i < pool->n; i++)
        if (!strcmp(pool->names[i], name)) return (int)i;
    return -1;
}

static int pool_add(NodePool *pool, const char *name) {
    int id = pool_find(pool, name);
    if (id >= 0) return id;
    if (pool->n >= pool->cap) {
        int64_t cap = pool->cap ? pool->cap * 2 : 8;
        char **nn = realloc(pool->names, (size_t)cap * sizeof(char *));
        P(!nn, -1)
        pool->names = nn;
        pool->cap = cap;
    }
    pool->names[pool->n] = graph_dup(name);
    P(!pool->names[pool->n], -1)
    return (int)pool->n++;
}

static void pool_free(NodePool *pool) {
    for (int64_t i = 0; i < pool->n; i++) free(pool->names[i]);
    free(pool->names);
    memset(pool, 0, sizeof *pool);
}

static int pool_ensure_aux(NodePool *pool, int **parent, int **depth, unsigned char **visited, int **q, int64_t *aux_cap) {
    if (pool->cap <= *aux_cap) return 0;
    int *np = realloc(*parent, (size_t)pool->cap * sizeof(int));
    int *nd = realloc(*depth, (size_t)pool->cap * sizeof(int));
    unsigned char *nv = realloc(*visited, (size_t)pool->cap);
    int *nq = realloc(*q, (size_t)pool->cap * sizeof(int));
    P(!np || !nd || !nv || !nq, -1)
    *parent = np;
    *depth = nd;
    *visited = nv;
    *q = nq;
    *aux_cap = pool->cap;
    return 0;
}

V *bi_graph_path(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_path: bad graph id"))
    P(n - pos < 2, v_err("graph_path(g, from, to[, max_depth])"))
    P(a[pos]->t != T_STR || a[pos + 1]->t != T_STR, v_err("graph_path: from and to must be strings"))
    const char *from = a[pos]->s;
    const char *to = a[pos + 1]->s;
    int max_depth = (n > pos + 2 && a[pos + 2]->t == T_INT) ? (int)a[pos + 2]->j : 8;
    if (max_depth < 1) max_depth = 1;
    if (max_depth > 64) max_depth = 64;

    if (!strcmp(from, to)) {
        V *r = v_list(1);
        r->L[0] = v_str(from);
        return r;
    }

    NodePool pool = {0};
    int from_id = pool_add(&pool, from);
    P(from_id < 0, v_err("graph_path: out of memory"))

    int *parent = calloc((size_t)pool.cap, sizeof(int));
    int *depth = calloc((size_t)pool.cap, sizeof(int));
    unsigned char *visited = calloc((size_t)pool.cap, 1);
    int *q = malloc((size_t)pool.cap * sizeof(int));
    int64_t aux_cap = pool.cap;
    P(!parent || !depth || !visited || !q,
      (free(parent), free(depth), free(visited), free(q), pool_free(&pool), v_err("graph_path: out of memory")))

    parent[from_id] = -1;
    depth[from_id] = 0;
    visited[from_id] = 1;
    int qh = 0, qt = 0;
    q[qt++] = from_id;
    int found = -1;

    while (qh < qt) {
        int cur = q[qh++];
        if (depth[cur] >= max_depth) continue;
        const char *cur_name = pool.names[cur];
        for (int64_t i = 0; i < g->count; i++) {
            Triple *t = &g->triples[i];
            const char *next = NULL;
            if (!strcmp(t->s, cur_name)) next = t->o;
            else if (!strcmp(t->o, cur_name)) next = t->s;
            else continue;
            int nid = pool_find(&pool, next);
            if (nid < 0) {
                nid = pool_add(&pool, next);
                if (nid < 0) continue;
                if (pool_ensure_aux(&pool, &parent, &depth, &visited, &q, &aux_cap) < 0) break;
                parent[nid] = cur;
                depth[nid] = depth[cur] + 1;
                visited[nid] = 1;
                if (qt >= (int)aux_cap && pool_ensure_aux(&pool, &parent, &depth, &visited, &q, &aux_cap) < 0) break;
                q[qt++] = nid;
                if (!strcmp(next, to)) {
                    found = nid;
                    goto done;
                }
            } else if (!visited[nid]) {
                visited[nid] = 1;
                parent[nid] = cur;
                depth[nid] = depth[cur] + 1;
                if (qt >= (int)aux_cap && pool_ensure_aux(&pool, &parent, &depth, &visited, &q, &aux_cap) < 0) break;
                q[qt++] = nid;
                if (!strcmp(next, to)) {
                    found = nid;
                    goto done;
                }
            }
        }
    }
done:
    V *r = v_list(0);
    if (found >= 0) {
        int path_len = depth[found] + 1;
        int *chain = malloc((size_t)path_len * sizeof(int));
        if (chain) {
            int at = found;
            for (int i = path_len - 1; i >= 0; i--) {
                chain[i] = at;
                at = parent[at];
            }
            r = v_list(path_len);
            for (int i = 0; i < path_len; i++)
                r->L[i] = v_str(pool.names[chain[i]]);
            free(chain);
        }
    }
    free(q);
    free(parent);
    free(depth);
    free(visited);
    pool_free(&pool);
    return r;
}

V *bi_graph_from_table(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_from_table: bad graph id"))
    P(n - pos < 4 || a[pos]->t != T_TABLE, v_err("graph_from_table(g, table, subj_col, pred, obj_col)"))
    P(a[pos + 2]->t != T_STR, v_err("graph_from_table: pred must be a string"))
    V *tbl = a[pos];
    const char *subj_name = (a[pos + 1]->t == T_STR) ? a[pos + 1]->s : NULL;
    const char *pred = a[pos + 2]->s;
    const char *obj_name = (a[pos + 3]->t == T_STR) ? a[pos + 3]->s : NULL;
    P(!subj_name || !obj_name, v_err("graph_from_table: column names must be strings"))
    int si = tbl_col_idx(tbl, subj_name);
    int oi = tbl_col_idx(tbl, obj_name);
    P(si < 0 || oi < 0, v_err("graph_from_table: column not found"))
    V *sc = tbl->vals->L[si];
    V *oc = tbl->vals->L[oi];
    char buf_s[512], buf_o[512];
    for (int64_t row = 0; row < tbl->n; row++) {
        cell_to_str(sc, row, buf_s, sizeof buf_s);
        cell_to_str(oc, row, buf_o, sizeof buf_o);
        P(graph_add_triple(g, buf_s, pred, buf_o) < 0, v_err("graph_from_table: failed"))
    }
    return v_int(g->count);
}

V *bi_graph_to_table(V **a, int n) {
    return bi_graph_query(a, n);
}

V *bi_graph_count(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    Graph *g = graph_at(gid);
    P(!g, v_err("graph_count: bad graph id"))
    return v_int(g->count);
}

V *bi_graph_clear(V **a, int n) {
    int pos = 0;
    int64_t gid = graph_id_arg(a, n, &pos);
    P(gid < 0 || gid >= g_graph_count, v_err("graph_clear: bad graph id"))
    graph_free(g_graphs[gid]);
    g_graphs[gid] = graph_new();
    P(!g_graphs[gid], v_err("graph_clear: out of memory"))
    return v_int(0);
}
