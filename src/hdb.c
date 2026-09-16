/*
 * Shakti HDB: partitioned IEFS v3 tables with prune/scan.
 */
#include "hdb.h"
#include "iefs_format.h"
#include "iefs_map.h"
#include "pack.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef T_UINT
enum {
    T_UINT = 1000, T_UVEC = 1001, T_UMAT = 1002, T_DEC = 1003, T_DVEC = 1004, T_DMAT = 1005,
    T_GUID = 1006, T_GVEC = 1007
};
#endif
static V *v_uvec(int64_t n, int bits) { (void)bits; return v_cvec(n); }
static V *v_ivec_bits(int64_t n, int bits) { (void)bits; return v_ivec(n); }
static V *v_fvec_bits(int64_t n, int bits) { (void)bits; return v_fvec(n); }
static V *v_imat_bits(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_fmat_bits(int64_t r, int64_t c, int bits) { (void)bits; return v_fmat(r, c); }
static V *v_umat(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_dvec(int64_t n, int bits) { (void)bits; return v_ivec(n); }
static V *v_dmat(int64_t r, int64_t c, int bits) { (void)bits; return v_imat(r, c); }
static V *v_gvec(int64_t n) { return v_cvec(n); }

#define HDB_PATH_MAX 4096
#define HDB_META_VERSION 1

static int hdb_mkdir_p(const char *path) {
    char tmp[HDB_PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int hdb_safe_name(const char *s) {
    if (!s || !*s) return 0;
    if (s[0] == '.') return 0;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\') return 0;
    }
    return 1;
}

static int hdb_join3(char *out, size_t out_n, const char *a, const char *b, const char *c) {
    int n = snprintf(out, out_n, "%s/%s/%s", a, b, c);
    return (n > 0 && (size_t)n < out_n) ? 0 : -1;
}

static int hdb_join4(char *out, size_t out_n, const char *a, const char *b, const char *c,
                     const char *d) {
    int n = snprintf(out, out_n, "%s/%s/%s/%s", a, b, c, d);
    return (n > 0 && (size_t)n < out_n) ? 0 : -1;
}

static V *hdb_empty_meta(void) {
    V *meta = v_dict_empty();
    v_dict_put(meta, "version", v_int(HDB_META_VERSION));
    v_dict_put(meta, "tables", v_dict_empty());
    return meta;
}

static const char *hdb_handle_root(V *h) {
    if (!h || h->t != T_DICT) return NULL;
    V *kind = v_dict_get(h, "kind");
    if (!kind || kind->t != T_STR || strcmp(kind->s, "hdb") != 0) return NULL;
    V *root = v_dict_get(h, "root");
    if (!root || root->t != T_STR) return NULL;
    return root->s;
}

static V *hdb_handle_meta(V *h) {
    if (!h || h->t != T_DICT) return NULL;
    V *meta = v_dict_get(h, "meta");
    if (!meta || meta->t != T_DICT) return NULL;
    return meta;
}

static V *hdb_make_handle(const char *root, V *meta) {
    V *h = v_dict_empty();
    v_dict_put(h, "kind", v_str("hdb"));
    v_dict_put(h, "root", v_str(root));
    v_dict_put(h, "meta", meta);
    return h;
}

static int hdb_save_meta(V *h) {
    const char *root = hdb_handle_root(h);
    V *meta = hdb_handle_meta(h);
    if (!root || !meta) return -1;
    char path[HDB_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/meta.iefs", root) >= (int)sizeof path) return -1;
    char err[256];
    if (iefs_store_write_ex(meta, path, 0, 0, 3, 2, err, sizeof err) != 0)
        return -1;
    return 0;
}

static int hdb_part_path(char *out, size_t out_n, const char *root, const char *table,
                         const char *part) {
    return hdb_join4(out, out_n, root, table, part, "data.iefs");
}

static int hdb_name_cmp(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static int hdb_part_match(const char *name, const char *from, const char *to, const char *eq) {
    if (eq && eq[0]) return strcmp(name, eq) == 0;
    if (from && from[0] && strcmp(name, from) < 0) return 0;
    if (to && to[0] && strcmp(name, to) > 0) return 0;
    return 1;
}

/* List partition directory names under root/table that contain data.iefs. */
static V *hdb_list_parts(const char *root, const char *table, const char *from, const char *to,
                         const char *eq) {
    char tdir[HDB_PATH_MAX];
    if (snprintf(tdir, sizeof tdir, "%s/%s", root, table) >= (int)sizeof tdir)
        return v_err("hdb: path too long");

    DIR *d = opendir(tdir);
    if (!d) return v_list(0);

    int cap = 64, nent = 0;
    char **names = malloc((size_t)cap * sizeof(char *));
    if (!names) {
        closedir(d);
        return v_err("hdb: out of memory");
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!hdb_safe_name(e->d_name)) continue;
        if (!hdb_part_match(e->d_name, from, to, eq)) continue;

        char pfile[HDB_PATH_MAX];
        if (hdb_part_path(pfile, sizeof pfile, root, table, e->d_name) != 0) continue;
        struct stat sb;
        if (stat(pfile, &sb) != 0 || !S_ISREG(sb.st_mode)) continue;

        if (nent >= cap) {
            cap *= 2;
            char **tmp = realloc(names, (size_t)cap * sizeof(char *));
            if (!tmp) {
                for (int i = 0; i < nent; i++) free(names[i]);
                free(names);
                closedir(d);
                return v_err("hdb: out of memory");
            }
            names = tmp;
        }
        names[nent] = strdup(e->d_name);
        if (!names[nent]) {
            for (int i = 0; i < nent; i++) free(names[i]);
            free(names);
            closedir(d);
            return v_err("hdb: out of memory");
        }
        nent++;
    }
    closedir(d);

    if (nent > 1) qsort(names, (size_t)nent, sizeof(char *), hdb_name_cmp);

    V *r = v_list(nent);
    for (int i = 0; i < nent; i++) {
        r->L[i] = v_str(names[i]);
        free(names[i]);
    }
    free(names);
    return r;
}

static void hdb_pack_concat(unsigned char *dst, const unsigned char *a, int64_t na,
                            const unsigned char *b, int64_t nb, int bits) {
    size_t nbytes_a = pack_nbytes(na, bits);
    if (na > 0 && a)
        memcpy(dst, a, nbytes_a);
    int64_t bit0 = na * (int64_t)bits;
    if ((bit0 & 7) == 0) {
        size_t nbytes_b = pack_nbytes(nb, bits);
        if (nb > 0 && b)
            memcpy(dst + (size_t)(bit0 >> 3), b, nbytes_b);
        return;
    }
    for (int64_t i = 0; i < nb; i++)
        pack_set_u(dst, na + i, bits, pack_get_u(b, i, bits));
}

static V *hdb_mat_concat(V *a, V *b) {
    int64_t cols = mat_cols(a);
    if (cols != mat_cols(b))
        return v_err("hdb.load: matrix width mismatch");
    int bits = 64 > 0 ? 64 : 64;
    int bits_b = 64 > 0 ? 64 : 64;
    if (bits != bits_b)
        return v_err("hdb.load: matrix bits mismatch");
    if (a->n == 0)
        return v_copy(b);
    if (b->n == 0)
        return v_copy(a);
    int64_t n = a->n + b->n;
    if (n < a->n)
        return v_err("hdb.load: overflow");
    int64_t ca = a->n * cols;
    int64_t cb = b->n * cols;
    V *o = NULL;
    if (a->t == T_FMAT)
        o = bits >= 64 ? v_fmat(n, cols) : v_fmat_bits(n, cols, bits);
    else if (a->t == T_IMAT)
        o = bits >= 64 ? v_imat(n, cols) : v_imat_bits(n, cols, bits);
    else if (a->t == T_UMAT)
        o = v_umat(n, cols, bits);
    else if (a->t == T_CMAT)
        o = v_cmat(n, cols);
    else if (a->t == T_BMAT)
        o = v_bmat(n, cols);
    else
        o = v_dmat(n, cols, bits);

    if (a->t == T_CMAT) {
        if (ca && a->B)
            memcpy(o->B, a->B, (size_t)ca);
        if (cb && b->B)
            memcpy(o->B + ca, b->B, (size_t)cb);
        return o;
    }
    if (a->t == T_BMAT) {
        if (ca && a->B) memcpy(o->B, a->B, (size_t)ca);
        if (cb && b->B) memcpy(o->B + ca, b->B, (size_t)cb);
        return o;
    }
    if (a->t == T_DMAT) {
        int nbytes = bits / 8;
        if (nbytes < 4)
            nbytes = 4;
        if (ca && a->B)
            memcpy(o->B, a->B, (size_t)ca * (size_t)nbytes);
        if (cb && b->B)
            memcpy(o->B + ca * (int64_t)nbytes, b->B, (size_t)cb * (size_t)nbytes);
        return o;
    }
    if (bits >= 64) {
        if (a->t == T_FMAT) {
            if (ca && a->F)
                memcpy(o->F, a->F, (size_t)ca * sizeof(double));
            if (cb && b->F)
                memcpy(o->F + ca, b->F, (size_t)cb * sizeof(double));
        } else {
            if (ca && a->J)
                memcpy(o->J, a->J, (size_t)ca * sizeof(int64_t));
            if (cb && b->J)
                memcpy(o->J + ca, b->J, (size_t)cb * sizeof(int64_t));
        }
        return o;
    }
    hdb_pack_concat(o->B, a->B, ca, b->B, cb, bits);
    return o;
}

static int hdb_lane_bits(const V *v) { return v && 64 > 0 ? 64 : 64; }

static V *hdb_col_concat(V *a, V *b) {
    if (!a || !b) return v_err("hdb.load: bad column");
    if (a->t != b->t) return v_err("hdb.load: column type mismatch");
    int64_t n = a->n + b->n;
    if (n < a->n) return v_err("hdb.load: overflow");

    if (a->t == T_IVEC || a->t == T_UVEC || a->t == T_FVEC) {
        int bits = hdb_lane_bits(a);
        V *o;
        if (bits != hdb_lane_bits(b))
            return v_err("hdb.load: column bits mismatch");
        if (a->n == 0)
            return v_copy(b);
        if (b->n == 0)
            return v_copy(a);
        if (a->t == T_UVEC)
            o = v_uvec(n, bits);
        else if (a->t == T_FVEC)
            o = v_fvec_bits(n, bits);
        else
            o = v_ivec_bits(n, bits);
        if (bits < 64) {
            hdb_pack_concat(o->B, a->B, a->n, b->B, b->n, bits);
            return o;
        }
        if (a->t == T_FVEC) {
            if (a->F)
                memcpy(o->F, a->F, (size_t)a->n * sizeof(double));
            if (b->F)
                memcpy(o->F + a->n, b->F, (size_t)b->n * sizeof(double));
        } else {
            if (a->J)
                memcpy(o->J, a->J, (size_t)a->n * sizeof(int64_t));
            if (b->J)
                memcpy(o->J + a->n, b->J, (size_t)b->n * sizeof(int64_t));
        }
        return o;
    }
    if (a->t == T_BVEC) {
        V *o = v_bvec(n);
        if (a->n && a->B) memcpy(o->B, a->B, (size_t)a->n);
        if (b->n && b->B) memcpy(o->B + a->n, b->B, (size_t)b->n);
        return o;
    }
    if (a->t == T_CVEC) {
        V *o = v_cvec(n);
        if (a->n && a->B) memcpy(o->B, a->B, (size_t)a->n);
        if (b->n && b->B) memcpy(o->B + a->n, b->B, (size_t)b->n);
        return o;
    }
    if (a->t == T_GVEC) {
        V *o = v_gvec(n);
        if (a->n && a->B) memcpy(o->B, a->B, (size_t)a->n * 16);
        if (b->n && b->B) memcpy(o->B + a->n * 16, b->B, (size_t)b->n * 16);
        return o;
    }
    if (a->t == T_LIST) {
        V *o = v_list(n);
        for (int64_t i = 0; i < a->n; i++) o->L[i] = v_ref(a->L[i]);
        for (int64_t i = 0; i < b->n; i++) o->L[a->n + i] = v_ref(b->L[i]);
        return o;
    }
    if (a->t == T_DVEC) {
        V *o = v_dvec(n, 64 > 0 ? 64 : 64);
        size_t esz = (64 == 32) ? 4 : (64 == 128) ? 16 : 8;
        if (a->n && a->B) memcpy(o->B, a->B, (size_t)a->n * esz);
        if (b->n && b->B) memcpy(o->B + a->n * (int64_t)esz, b->B, (size_t)b->n * esz);
        return o;
    }
    if (a->t == T_IMAT || a->t == T_FMAT || a->t == T_BMAT || a->t == T_CMAT || a->t == T_UMAT || a->t == T_DMAT)
        return hdb_mat_concat(a, b);
    return v_err("hdb.load: unsupported column type");
}

static V *hdb_table_concat(V *left, V *right) {
    if (!left || left->t != T_TABLE) return v_err("hdb.load: expected table");
    if (!right || right->t != T_TABLE) return v_err("hdb.load: expected table");
    if (left->keys->n != right->keys->n) return v_err("hdb.load: schema mismatch");
    for (int64_t i = 0; i < left->keys->n; i++) {
        if (left->keys->L[i]->t != T_STR || right->keys->L[i]->t != T_STR)
            return v_err("hdb.load: bad column name");
        if (strcmp(left->keys->L[i]->s, right->keys->L[i]->s) != 0)
            return v_err("hdb.load: column order/name mismatch");
    }
    V *keys = v_list(left->keys->n);
    V *data = v_list(left->keys->n);
    for (int64_t i = 0; i < left->keys->n; i++) {
        keys->L[i] = v_ref(left->keys->L[i]);
        V *c = hdb_col_concat(left->vals->L[i], right->vals->L[i]);
        if (c->t == T_ERR) {
            v_free(keys);
            v_free(data);
            return c;
        }
        data->L[i] = c;
    }
    return v_table_own(keys, data);
}

static int hdb_schema_ok(V *schema, V *table) {
    if (!schema || schema->t != T_DICT) return 0;
    if (!table || table->t != T_TABLE) return 0;
    if (schema->n != table->keys->n) return 0;
    for (int64_t i = 0; i < table->keys->n; i++) {
        if (table->keys->L[i]->t != T_STR) return 0;
        if (!v_dict_get(schema, table->keys->L[i]->s)) return 0;
    }
    return 1;
}

V *bi_hdb_open(V **a, int n) {
    if (n < 1 || !a[0] || a[0]->t != T_STR)
        return v_err("hdb_open(root)");
    const char *root = a[0]->s;
    if (hdb_mkdir_p(root) != 0)
        return v_errf("hdb.open: mkdir %s: %s", root, strerror(errno));

    char mpath[HDB_PATH_MAX];
    if (snprintf(mpath, sizeof mpath, "%s/meta.iefs", root) >= (int)sizeof mpath)
        return v_err("hdb.open: path too long");

    V *meta;
    struct stat sb;
    if (stat(mpath, &sb) == 0 && S_ISREG(sb.st_mode)) {
        meta = iefs_store_read(mpath);
        if (!meta || meta->t == T_ERR) return meta ? meta : v_err("hdb.open: meta load failed");
        if (meta->t != T_DICT) {
            v_free(meta);
            return v_err("hdb.open: meta.iefs must be a dict");
        }
        V *tables = v_dict_get(meta, "tables");
        if (!tables || tables->t != T_DICT) {
            v_free(meta);
            return v_err("hdb.open: meta missing tables dict");
        }
    } else {
        meta = hdb_empty_meta();
        char err[256];
        if (iefs_store_write_ex(meta, mpath, 0, 0, 3, 2, err, sizeof err) != 0) {
            v_free(meta);
            return v_err(err[0] ? err : "hdb.open: write meta failed");
        }
    }
    return hdb_make_handle(root, meta);
}

V *bi_hdb_close(V **a, int n) {
    (void)a;
    (void)n;
    return v_nil();
}

V *bi_hdb_create(V **a, int n) {
    /* hdb_create(h, name, schema[, part_key]) */
    if (n < 3 || !a[1] || a[1]->t != T_STR || !a[2] || a[2]->t != T_DICT)
        return v_err("hdb_create(h, name, schema[, part])");
    if (!hdb_handle_root(a[0])) return v_err("hdb_create: bad handle");
    if (!hdb_safe_name(a[1]->s)) return v_err("hdb_create: bad table name");

    const char *part_key = "date";
    if (n > 3 && a[3] && a[3]->t == T_STR && a[3]->s[0]) part_key = a[3]->s;

    V *meta = hdb_handle_meta(a[0]);
    V *tables = v_dict_get(meta, "tables");
    if (!tables || tables->t != T_DICT) return v_err("hdb_create: bad meta");

    V *entry = v_dict_empty();
    v_dict_put(entry, "part_key", v_str(part_key));
    v_dict_put(entry, "schema", v_ref(a[2]));
    v_dict_put(tables, a[1]->s, entry);

    char tdir[HDB_PATH_MAX];
    if (snprintf(tdir, sizeof tdir, "%s/%s", hdb_handle_root(a[0]), a[1]->s) >= (int)sizeof tdir)
        return v_err("hdb_create: path too long");
    if (hdb_mkdir_p(tdir) != 0)
        return v_errf("hdb_create: mkdir: %s", strerror(errno));

    if (hdb_save_meta(a[0]) != 0) return v_err("hdb_create: save meta failed");
    return v_nil();
}

V *bi_hdb_write(V **a, int n) {
    /* hdb_write(h, name, part, table[, codecs]) */
    if (n < 4 || !a[1] || a[1]->t != T_STR || !a[2] || a[2]->t != T_STR || !a[3] ||
        a[3]->t != T_TABLE)
        return v_err("hdb_write(h, name, part, table[, codecs])");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_write: bad handle");
    if (!hdb_safe_name(a[1]->s) || !hdb_safe_name(a[2]->s))
        return v_err("hdb_write: bad table/part name");

    V *meta = hdb_handle_meta(a[0]);
    V *tables = v_dict_get(meta, "tables");
    if (!tables || tables->t != T_DICT) return v_err("hdb_write: bad meta");
    V *entry = v_dict_get(tables, a[1]->s);
    if (!entry || entry->t != T_DICT) return v_err("hdb_write: table not created");
    V *schema = v_dict_get(entry, "schema");
    if (!hdb_schema_ok(schema, a[3])) return v_err("hdb_write: table schema mismatch");

    V *codecs = NULL;
    if (n > 4 && a[4] && a[4]->t == T_DICT) codecs = a[4];

    char pdir[HDB_PATH_MAX], pfile[HDB_PATH_MAX];
    if (hdb_join3(pdir, sizeof pdir, root, a[1]->s, a[2]->s) != 0)
        return v_err("hdb_write: path too long");
    if (hdb_mkdir_p(pdir) != 0) return v_errf("hdb_write: mkdir: %s", strerror(errno));
    if (hdb_part_path(pfile, sizeof pfile, root, a[1]->s, a[2]->s) != 0)
        return v_err("hdb_write: path too long");

    char err[256];
    if (iefs_store_write_full(a[3], pfile, 0, 0, 0, 3, 3, codecs, err, sizeof err) != 0) {
        const char *e = iefs_last_error();
        return v_err(e && e[0] ? e : (err[0] ? err : "hdb_write: iefs save failed"));
    }
    return v_nil();
}

V *bi_hdb_parts(V **a, int n) {
    /* hdb_parts(h, name[, from[, to[, eq]]]) */
    if (n < 2 || !a[1] || a[1]->t != T_STR) return v_err("hdb_parts(h, name[, from[, to[, eq]]])");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_parts: bad handle");
    if (!hdb_safe_name(a[1]->s)) return v_err("hdb_parts: bad table name");

    const char *from = "", *to = "", *eq = "";
    if (n > 2 && a[2] && a[2]->t == T_STR) from = a[2]->s;
    if (n > 3 && a[3] && a[3]->t == T_STR) to = a[3]->s;
    if (n > 4 && a[4] && a[4]->t == T_STR) eq = a[4]->s;
    return hdb_list_parts(root, a[1]->s, from, to, eq);
}

V *bi_hdb_map(V **a, int n) {
    /* hdb_map(h, name, part[, pages[, device]]) */
    if (n < 3 || !a[1] || a[1]->t != T_STR || !a[2] || a[2]->t != T_STR)
        return v_err("hdb_map(h, name, part[, pages[, device]])");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_map: bad handle");
    if (!hdb_safe_name(a[1]->s) || !hdb_safe_name(a[2]->s))
        return v_err("hdb_map: bad table/part name");

    char pfile[HDB_PATH_MAX];
    if (hdb_part_path(pfile, sizeof pfile, root, a[1]->s, a[2]->s) != 0)
        return v_err("hdb_map: path too long");

    int pages = IEFS_MAP_PAGES_THP;
    int gpu_warm = 0;
    if (n > 3 && a[3] && a[3]->t == T_STR) {
        if (!strcmp(a[3]->s, "1g") || !strcmp(a[3]->s, "1G"))
            pages = IEFS_MAP_PAGES_1G;
        else if (!strcmp(a[3]->s, "2m") || !strcmp(a[3]->s, "2M"))
            pages = IEFS_MAP_PAGES_2M;
        else if (strcmp(a[3]->s, "thp") != 0 && a[3]->s[0])
            return v_err("hdb_map: pages must be \"thp\", \"2m\", or \"1g\"");
    }
    if (n > 4 && a[4] && a[4]->t == T_STR && a[4]->s[0]) {
        if (!strcmp(a[4]->s, "gpu"))
            return v_err("hdb_map: device gpu not supported");
        else
            return v_err("hdb_map: device must be \"\"");
    }
    (void)gpu_warm;
    return iefs_store_map(pfile, pages);
}

V *bi_hdb_load_part(V **a, int n) {
    if (n < 3 || !a[1] || a[1]->t != T_STR || !a[2] || a[2]->t != T_STR)
        return v_err("hdb_load_part(h, name, part)");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_load_part: bad handle");
    if (!hdb_safe_name(a[1]->s) || !hdb_safe_name(a[2]->s))
        return v_err("hdb_load_part: bad table/part name");

    char pfile[HDB_PATH_MAX];
    if (hdb_part_path(pfile, sizeof pfile, root, a[1]->s, a[2]->s) != 0)
        return v_err("hdb_load_part: path too long");
    return iefs_store_read(pfile);
}

V *bi_hdb_load(V **a, int n) {
    /* hdb_load(h, name[, from[, to[, eq]]]) */
    if (n < 2 || !a[1] || a[1]->t != T_STR) return v_err("hdb_load(h, name[, from[, to[, eq]]])");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_load: bad handle");
    if (!hdb_safe_name(a[1]->s)) return v_err("hdb_load: bad table name");

    const char *from = "", *to = "", *eq = "";
    if (n > 2 && a[2] && a[2]->t == T_STR) from = a[2]->s;
    if (n > 3 && a[3] && a[3]->t == T_STR) to = a[3]->s;
    if (n > 4 && a[4] && a[4]->t == T_STR) eq = a[4]->s;

    V *parts = hdb_list_parts(root, a[1]->s, from, to, eq);
    if (parts->t == T_ERR) return parts;
    if (parts->n == 0) {
        v_free(parts);
        return v_err("hdb_load: no matching partitions");
    }

    V *acc = NULL;
    for (int64_t i = 0; i < parts->n; i++) {
        char pfile[HDB_PATH_MAX];
        if (hdb_part_path(pfile, sizeof pfile, root, a[1]->s, parts->L[i]->s) != 0) {
            v_free(parts);
            if (acc) v_free(acc);
            return v_err("hdb_load: path too long");
        }
        V *part = iefs_store_read(pfile);
        if (!part || part->t == T_ERR) {
            v_free(parts);
            if (acc) v_free(acc);
            return part ? part : v_err("hdb_load: read failed");
        }
        if (part->t != T_TABLE) {
            v_free(part);
            v_free(parts);
            if (acc) v_free(acc);
            return v_err("hdb_load: partition is not a table");
        }
        if (!acc) {
            acc = part;
        } else {
            V *merged = hdb_table_concat(acc, part);
            v_free(acc);
            v_free(part);
            if (merged->t == T_ERR) {
                v_free(parts);
                return merged;
            }
            acc = merged;
        }
    }
    v_free(parts);
    return acc;
}

V *bi_hdb_scan(V **a, int n) {
    /* hdb_scan(h, name[, from[, to[, eq]]]) */
    if (n < 2 || !a[1] || a[1]->t != T_STR) return v_err("hdb_scan(h, name[, from[, to[, eq]]])");
    const char *root = hdb_handle_root(a[0]);
    if (!root) return v_err("hdb_scan: bad handle");
    if (!hdb_safe_name(a[1]->s)) return v_err("hdb_scan: bad table name");

    const char *from = "", *to = "", *eq = "";
    if (n > 2 && a[2] && a[2]->t == T_STR) from = a[2]->s;
    if (n > 3 && a[3] && a[3]->t == T_STR) to = a[3]->s;
    if (n > 4 && a[4] && a[4]->t == T_STR) eq = a[4]->s;

    V *parts = hdb_list_parts(root, a[1]->s, from, to, eq);
    if (parts->t == T_ERR) return parts;

    V *c = v_dict_empty();
    v_dict_put(c, "kind", v_str("hdb_scan"));
    v_dict_put(c, "root", v_str(root));
    v_dict_put(c, "table", v_str(a[1]->s));
    v_dict_put(c, "parts", parts);
    v_dict_put(c, "idx", v_int(-1));
    v_dict_put(c, "current", v_nil());
    return c;
}

static int hdb_is_scan(V *c) {
    if (!c || c->t != T_DICT) return 0;
    V *kind = v_dict_get(c, "kind");
    return kind && kind->t == T_STR && strcmp(kind->s, "hdb_scan") == 0;
}

V *bi_hdb_next(V **a, int n) {
    if (n < 1 || !hdb_is_scan(a[0])) return v_err("hdb_next(cursor)");
    V *c = a[0];
    V *parts = v_dict_get(c, "parts");
    V *idx_v = v_dict_get(c, "idx");
    V *root_v = v_dict_get(c, "root");
    V *table_v = v_dict_get(c, "table");
    if (!parts || parts->t != T_LIST || !idx_v || idx_v->t != T_INT || !root_v ||
        root_v->t != T_STR || !table_v || table_v->t != T_STR)
        return v_err("hdb_next: bad cursor");

    int64_t idx = idx_v->j + 1;
    if (idx < 0 || idx >= parts->n) {
        v_dict_put(c, "idx", v_int(idx));
        v_dict_put(c, "current", v_nil());
        return v_bool(0);
    }

    char pfile[HDB_PATH_MAX];
    if (hdb_part_path(pfile, sizeof pfile, root_v->s, table_v->s, parts->L[idx]->s) != 0)
        return v_err("hdb_next: path too long");

    V *part = iefs_store_map(pfile, IEFS_MAP_PAGES_THP);
    if (!part || part->t == T_ERR) return part ? part : v_err("hdb_next: map failed");
    if (part->t != T_TABLE) {
        v_free(part);
        return v_err("hdb_next: partition is not a table");
    }

    v_dict_put(c, "idx", v_int(idx));
    v_dict_put(c, "current", part);
    return v_bool(1);
}

V *bi_hdb_current(V **a, int n) {
    if (n < 1 || !hdb_is_scan(a[0])) return v_err("hdb_current(cursor)");
    V *cur = v_dict_get(a[0], "current");
    if (!cur || cur->t == T_NIL) return v_err("hdb_current: no current partition");
    return v_ref(cur);
}
