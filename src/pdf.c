/* pdf — from-scratch PDF 1.4 reader/writer (no third-party PDF libs)
 *
 *   import pdf
 *   w : pdf.create()
 *   pdf.add_page(w)
 *   pdf.text_at(w, 72, 720, "Hello Shakti", size:12)
 *   pdf.save(w, "out.pdf")
 */
#include "pdf.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PDF_MAX 64
#define PDF_LETTER_W 612.0
#define PDF_LETTER_H 792.0
#define PDF_MAX_PARSE_DEPTH 64
#define PDF_MAX_PAGE_DEPTH 256
#define PDF_MAX_FILE_BYTES (256u * 1024u * 1024u)

typedef struct {
    char *data;
    size_t len, cap;
} PdfBuf;

typedef struct {
    PdfBuf content;
    double width, height;
} PdfPage;

typedef enum { PDF_NONE = 0, PDF_WRITE = 1, PDF_READ = 2 } PdfMode;

typedef struct {
    PdfMode mode;
    PdfPage *pages;
    int npages, pages_cap;
    unsigned char *file;
    size_t file_len;
    long *xref; /* objnum -> file offset; 0 = free/missing */
    int xref_n;
    int root_obj;
    int info_obj;
    int encrypt;
} PdfDoc;

static PdfDoc *g_pdfs[PDF_MAX];

/* ---------- buffers ---------- */

static int buf_reserve(PdfBuf *b, size_t need) {
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p) return -1;
    b->data = p;
    b->cap = cap;
    return 0;
}

static int buf_append(PdfBuf *b, const void *p, size_t n) {
    if (buf_reserve(b, b->len + n + 1) < 0) return -1;
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}

static int buf_printf(PdfBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[4096];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n < sizeof tmp)
        return buf_append(b, tmp, (size_t)n);
    char *big = malloc((size_t)n + 1);
    if (!big) return -1;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int rc = buf_append(b, big, (size_t)n);
    free(big);
    return rc;
}

static void buf_free(PdfBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---------- doc table ---------- */

static void pdf_free_doc(PdfDoc *d) {
    if (!d) return;
    if (d->pages) {
        for (int i = 0; i < d->npages; i++)
            buf_free(&d->pages[i].content);
        free(d->pages);
    }
    free(d->file);
    free(d->xref);
    free(d);
}

static PdfDoc *pdf_at(int64_t id) {
    if (id < 0 || id >= PDF_MAX || !g_pdfs[id]) return NULL;
    return g_pdfs[id];
}

static int64_t pdf_alloc_slot(PdfDoc *d) {
    for (int i = 0; i < PDF_MAX; i++) {
        if (!g_pdfs[i]) {
            g_pdfs[i] = d;
            return i;
        }
    }
    return -1;
}

static void pdf_release_slot(int64_t id) {
    if (id < 0 || id >= PDF_MAX) return;
    pdf_free_doc(g_pdfs[id]);
    g_pdfs[id] = NULL;
}

/* ---------- write helpers ---------- */

static int pdf_escape_string(PdfBuf *out, const char *s) {
    if (buf_append(out, "(", 1) < 0) return -1;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '(' || c == ')') {
            char esc[3] = {'\\', (char)c, 0};
            if (buf_append(out, esc, 2) < 0) return -1;
        } else if (c < 32 || c > 126) {
            char oct[5];
            snprintf(oct, sizeof oct, "\\%03o", c);
            if (buf_append(out, oct, 4) < 0) return -1;
        } else {
            if (buf_append(out, s, 1) < 0) return -1;
        }
    }
    return buf_append(out, ")", 1);
}

static int pdf_emit_obj_header(PdfBuf *out, int obj, long *offsets) {
    offsets[obj] = (long)out->len;
    return buf_printf(out, "%d 0 obj\n", obj);
}

/* ---------- reader: lexer / objects ---------- */

typedef enum {
    PO_NULL = 0,
    PO_BOOL,
    PO_INT,
    PO_REAL,
    PO_NAME,
    PO_STRING,
    PO_ARRAY,
    PO_DICT,
    PO_REF,
    PO_STREAM
} PoType;

typedef struct PdfObj PdfObj;
struct PdfObj {
    PoType t;
    int64_t i;
    double f;
    char *s;
    size_t slen;
    PdfObj **items;
    int nitems;
    char **keys;
    PdfObj **vals;
    int nkeys;
    int ref_n, ref_g;
    PdfObj *stream_dict;
    size_t stream_off, stream_len;
};

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
    int depth;
    char err[128];
} PdfParse;

static void po_free(PdfObj *o);

static void po_free_arr(PdfObj **a, int n) {
    if (!a) return;
    for (int i = 0; i < n; i++) po_free(a[i]);
    free(a);
}

static void po_free(PdfObj *o) {
    if (!o) return;
    free(o->s);
    po_free_arr(o->items, o->nitems);
    if (o->keys) {
        for (int i = 0; i < o->nkeys; i++) free(o->keys[i]);
        free(o->keys);
    }
    po_free_arr(o->vals, o->nkeys);
    po_free(o->stream_dict);
    free(o);
}

static PdfObj *po_new(PoType t) {
    PdfObj *o = calloc(1, sizeof(PdfObj));
    if (o) o->t = t;
    return o;
}

static void pp_skip_ws(PdfParse *p) {
    while (p->pos < p->len) {
        unsigned char c = p->data[p->pos];
        if (c == '%') {
            while (p->pos < p->len && p->data[p->pos] != '\n' && p->data[p->pos] != '\r')
                p->pos++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0)
            p->pos++;
        else
            break;
    }
}

static int pp_startswith(PdfParse *p, const char *s) {
    size_t n = strlen(s);
    if (p->pos + n > p->len) return 0;
    return memcmp(p->data + p->pos, s, n) == 0;
}

static int pp_consume(PdfParse *p, const char *s) {
    pp_skip_ws(p);
    size_t n = strlen(s);
    if (p->pos + n > p->len || memcmp(p->data + p->pos, s, n) != 0) return 0;
    p->pos += n;
    return 1;
}

static PdfObj *pp_parse_obj(PdfParse *p);

static PdfObj *pp_parse_literal_string(PdfParse *p) {
    if (p->pos >= p->len || p->data[p->pos] != '(') return NULL;
    p->pos++;
    PdfBuf b = {0};
    int depth = 1;
    while (p->pos < p->len && depth > 0) {
        unsigned char c = p->data[p->pos++];
        if (c == '\\' && p->pos < p->len) {
            unsigned char e = p->data[p->pos++];
            if (e >= '0' && e <= '7') {
                int v = e - '0';
                for (int k = 0; k < 2 && p->pos < p->len && p->data[p->pos] >= '0' && p->data[p->pos] <= '7'; k++)
                    v = (v << 3) + (p->data[p->pos++] - '0');
                char ch = (char)(v & 0xff);
                if (buf_append(&b, &ch, 1) < 0) { buf_free(&b); return NULL; }
            } else if (e == 'n') {
                if (buf_append(&b, "\n", 1) < 0) { buf_free(&b); return NULL; }
            } else if (e == 'r') {
                if (buf_append(&b, "\r", 1) < 0) { buf_free(&b); return NULL; }
            } else if (e == 't') {
                if (buf_append(&b, "\t", 1) < 0) { buf_free(&b); return NULL; }
            } else if (e == '\n' || e == '\r') {
                if (e == '\r' && p->pos < p->len && p->data[p->pos] == '\n') p->pos++;
            } else {
                if (buf_append(&b, (char *)&e, 1) < 0) { buf_free(&b); return NULL; }
            }
        } else if (c == '(') {
            depth++;
            if (buf_append(&b, "(", 1) < 0) { buf_free(&b); return NULL; }
        } else if (c == ')') {
            depth--;
            if (depth > 0 && buf_append(&b, ")", 1) < 0) { buf_free(&b); return NULL; }
        } else {
            if (buf_append(&b, (char *)&c, 1) < 0) { buf_free(&b); return NULL; }
        }
    }
    PdfObj *o = po_new(PO_STRING);
    if (!o) { buf_free(&b); return NULL; }
    o->s = b.data ? b.data : calloc(1, 1);
    o->slen = b.len;
    if (!o->s) { free(o); return NULL; }
    return o;
}

static PdfObj *pp_parse_hex_string(PdfParse *p) {
    if (p->pos >= p->len || p->data[p->pos] != '<') return NULL;
    p->pos++;
    PdfBuf b = {0};
    int hi = -1;
    while (p->pos < p->len) {
        unsigned char c = p->data[p->pos++];
        if (c == '>') break;
        if (isspace(c)) continue;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else { buf_free(&b); return NULL; }
        if (hi < 0) hi = v;
        else {
            char ch = (char)((hi << 4) | v);
            if (buf_append(&b, &ch, 1) < 0) { buf_free(&b); return NULL; }
            hi = -1;
        }
    }
    if (hi >= 0) {
        char ch = (char)(hi << 4);
        if (buf_append(&b, &ch, 1) < 0) { buf_free(&b); return NULL; }
    }
    PdfObj *o = po_new(PO_STRING);
    if (!o) { buf_free(&b); return NULL; }
    o->s = b.data ? b.data : calloc(1, 1);
    o->slen = b.len;
    return o;
}

static PdfObj *pp_parse_name(PdfParse *p) {
    if (p->pos >= p->len || p->data[p->pos] != '/') return NULL;
    p->pos++;
    PdfBuf b = {0};
    while (p->pos < p->len) {
        unsigned char c = p->data[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '/' || c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '{' || c == '}' || c == '%')
            break;
        if (c == '#') {
            if (p->pos + 2 >= p->len) break;
            char h1 = (char)p->data[p->pos + 1], h2 = (char)p->data[p->pos + 2];
            int v1 = isdigit((unsigned char)h1) ? h1 - '0' :
                     (h1 >= 'A' && h1 <= 'F') ? h1 - 'A' + 10 :
                     (h1 >= 'a' && h1 <= 'f') ? h1 - 'a' + 10 : -1;
            int v2 = isdigit((unsigned char)h2) ? h2 - '0' :
                     (h2 >= 'A' && h2 <= 'F') ? h2 - 'A' + 10 :
                     (h2 >= 'a' && h2 <= 'f') ? h2 - 'a' + 10 : -1;
            if (v1 < 0 || v2 < 0) break;
            char ch = (char)((v1 << 4) | v2);
            if (buf_append(&b, &ch, 1) < 0) { buf_free(&b); return NULL; }
            p->pos += 3;
        } else {
            if (buf_append(&b, (char *)&c, 1) < 0) { buf_free(&b); return NULL; }
            p->pos++;
        }
    }
    PdfObj *o = po_new(PO_NAME);
    if (!o) { buf_free(&b); return NULL; }
    o->s = b.data ? b.data : strdup("");
    o->slen = b.len;
    return o;
}

static PdfObj *pp_parse_number_or_ref(PdfParse *p) {
    pp_skip_ws(p);
    size_t start = p->pos;
    int neg = 0;
    if (p->pos < p->len && (p->data[p->pos] == '+' || p->data[p->pos] == '-')) {
        neg = p->data[p->pos] == '-';
        p->pos++;
    }
    int has_dig = 0, has_dot = 0;
    while (p->pos < p->len) {
        unsigned char c = p->data[p->pos];
        if (isdigit(c)) { has_dig = 1; p->pos++; }
        else if (c == '.' && !has_dot) { has_dot = 1; p->pos++; }
        else break;
    }
    if (!has_dig) { p->pos = start; return NULL; }

    char tmp[64];
    size_t n = p->pos - start;
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, p->data + start, n);
    tmp[n] = 0;

    /* Lookahead for " gen R" */
    size_t save = p->pos;
    pp_skip_ws(p);
    size_t gstart = p->pos;
    while (p->pos < p->len && isdigit(p->data[p->pos])) p->pos++;
    if (p->pos > gstart) {
        size_t after_gen = p->pos;
        pp_skip_ws(p);
        if (p->pos < p->len && p->data[p->pos] == 'R' &&
            (p->pos + 1 >= p->len || !isalnum(p->data[p->pos + 1]))) {
            char gtmp[32];
            size_t gn = after_gen - gstart;
            if (gn >= sizeof gtmp) gn = sizeof gtmp - 1;
            memcpy(gtmp, p->data + gstart, gn);
            gtmp[gn] = 0;
            PdfObj *o = po_new(PO_REF);
            if (!o) return NULL;
            o->ref_n = (int)atoi(tmp);
            o->ref_g = (int)atoi(gtmp);
            p->pos++; /* R */
            return o;
        }
    }
    p->pos = save;

    PdfObj *o = po_new(has_dot ? PO_REAL : PO_INT);
    if (!o) return NULL;
    if (has_dot) o->f = atof(tmp);
    else o->i = (int64_t)atoll(tmp);
    (void)neg;
    return o;
}

static PdfObj *pp_parse_array(PdfParse *p) {
    if (p->depth >= PDF_MAX_PARSE_DEPTH) return NULL;
    if (!pp_consume(p, "[")) return NULL;
    PdfObj *o = po_new(PO_ARRAY);
    if (!o) return NULL;
    p->depth++;
    for (;;) {
        pp_skip_ws(p);
        if (p->pos < p->len && p->data[p->pos] == ']') { p->pos++; break; }
        PdfObj *item = pp_parse_obj(p);
        if (!item) { po_free(o); p->depth--; return NULL; }
        PdfObj **ni = realloc(o->items, (size_t)(o->nitems + 1) * sizeof(PdfObj *));
        if (!ni) { po_free(item); po_free(o); p->depth--; return NULL; }
        o->items = ni;
        o->items[o->nitems++] = item;
    }
    p->depth--;
    return o;
}

static PdfObj *pp_parse_dict(PdfParse *p) {
    if (p->depth >= PDF_MAX_PARSE_DEPTH) return NULL;
    if (!pp_consume(p, "<<")) return NULL;
    PdfObj *o = po_new(PO_DICT);
    if (!o) return NULL;
    p->depth++;
    for (;;) {
        pp_skip_ws(p);
        if (pp_startswith(p, ">>")) { p->pos += 2; break; }
        PdfObj *key = pp_parse_name(p);
        if (!key) { po_free(o); p->depth--; return NULL; }
        PdfObj *val = pp_parse_obj(p);
        if (!val) { po_free(key); po_free(o); p->depth--; return NULL; }
        char **nk = realloc(o->keys, (size_t)(o->nkeys + 1) * sizeof(char *));
        if (!nk) {
            po_free(key); po_free(val); po_free(o); p->depth--;
            return NULL;
        }
        o->keys = nk;
        PdfObj **nv = realloc(o->vals, (size_t)(o->nkeys + 1) * sizeof(PdfObj *));
        if (!nv) {
            po_free(key); po_free(val); po_free(o); p->depth--;
            return NULL;
        }
        o->vals = nv;
        o->keys[o->nkeys] = key->s;
        key->s = NULL;
        po_free(key);
        o->vals[o->nkeys] = val;
        o->nkeys++;
    }
    p->depth--;
    return o;
}

static PdfObj *pp_parse_obj(PdfParse *p) {
    pp_skip_ws(p);
    if (p->pos >= p->len) return NULL;
    unsigned char c = p->data[p->pos];
    if (c == '[') return pp_parse_array(p);
    if (c == '(') return pp_parse_literal_string(p);
    if (c == '/') return pp_parse_name(p);
    if (c == '<' && p->pos + 1 < p->len && p->data[p->pos + 1] == '<') return pp_parse_dict(p);
    if (c == '<') return pp_parse_hex_string(p);
    if (pp_startswith(p, "null") && (p->pos + 4 >= p->len || !isalnum(p->data[p->pos + 4]))) {
        p->pos += 4;
        return po_new(PO_NULL);
    }
    if (pp_startswith(p, "true") && (p->pos + 4 >= p->len || !isalnum(p->data[p->pos + 4]))) {
        p->pos += 4;
        PdfObj *o = po_new(PO_BOOL);
        if (o) o->i = 1;
        return o;
    }
    if (pp_startswith(p, "false") && (p->pos + 5 >= p->len || !isalnum(p->data[p->pos + 5]))) {
        p->pos += 5;
        return po_new(PO_BOOL);
    }
    if (c == '+' || c == '-' || isdigit(c) || c == '.') return pp_parse_number_or_ref(p);
    snprintf(p->err, sizeof p->err, "unexpected byte 0x%02x at %zu", c, p->pos);
    return NULL;
}

static PdfObj *dict_get(PdfObj *d, const char *key) {
    if (!d || d->t != PO_DICT) return NULL;
    for (int i = 0; i < d->nkeys; i++)
        if (d->keys[i] && !strcmp(d->keys[i], key)) return d->vals[i];
    return NULL;
}

#define PDF_MAX_INDIRECT_DEPTH 32

static PdfObj *pdf_load_indirect(PdfDoc *d, int objn, char *err, size_t errcap, int depth);

static PdfObj *pdf_resolve(PdfDoc *d, PdfObj *o, char *err, size_t errcap) {
    int guard = 0;
    while (o && o->t == PO_REF && guard++ < PDF_MAX_INDIRECT_DEPTH) {
        PdfObj *n = pdf_load_indirect(d, o->ref_n, err, errcap, 0);
        po_free(o);
        o = n;
    }
    return o;
}

static PdfObj *pdf_load_indirect(PdfDoc *d, int objn, char *err, size_t errcap, int depth) {
    if (depth >= PDF_MAX_INDIRECT_DEPTH) {
        snprintf(err, errcap, "pdf: indirect load nesting too deep");
        return NULL;
    }
    if (objn <= 0 || objn >= d->xref_n || d->xref[objn] <= 0) {
        snprintf(err, errcap, "pdf: missing object %d", objn);
        return NULL;
    }
    PdfParse p = {.data = d->file, .len = d->file_len, .pos = (size_t)d->xref[objn]};
    pp_skip_ws(&p);
    /* objnum gen obj */
    PdfObj *num = pp_parse_number_or_ref(&p);
    if (!num || num->t != PO_INT) {
        po_free(num);
        snprintf(err, errcap, "pdf: bad object header %d", objn);
        return NULL;
    }
    po_free(num);
    pp_skip_ws(&p);
    PdfObj *gen = pp_parse_number_or_ref(&p);
    if (!gen || gen->t != PO_INT) {
        po_free(gen);
        snprintf(err, errcap, "pdf: bad generation for object %d", objn);
        return NULL;
    }
    po_free(gen);
    if (!pp_consume(&p, "obj")) {
        snprintf(err, errcap, "pdf: expected 'obj' for %d", objn);
        return NULL;
    }
    PdfObj *body = pp_parse_obj(&p);
    if (!body) {
        snprintf(err, errcap, "pdf: cannot parse object %d%s%s", objn,
                 p.err[0] ? ": " : "", p.err);
        return NULL;
    }
    pp_skip_ws(&p);
    if (pp_startswith(&p, "stream")) {
        if (body->t != PO_DICT) {
            po_free(body);
            snprintf(err, errcap, "pdf: stream without dict (obj %d)", objn);
            return NULL;
        }
        PdfObj *filter = dict_get(body, "Filter");
        if (filter) {
            po_free(body);
            snprintf(err, errcap, "pdf: compressed streams not supported (obj %d)", objn);
            return NULL;
        }
        p.pos += 6;
        if (p.pos < p.len && p.data[p.pos] == '\r') p.pos++;
        if (p.pos < p.len && p.data[p.pos] == '\n') p.pos++;
        else if (p.pos < p.len && p.data[p.pos] == '\r') p.pos++;

        PdfObj *len_o = dict_get(body, "Length");
        int64_t slen = -1;
        if (len_o && len_o->t == PO_INT) slen = len_o->i;
        else if (len_o && len_o->t == PO_REF) {
            PdfObj *lr = pdf_load_indirect(d, len_o->ref_n, err, errcap, depth + 1);
            if (lr && lr->t == PO_INT) slen = lr->i;
            po_free(lr);
        }
        if (slen < 0) {
            po_free(body);
            snprintf(err, errcap, "pdf: stream Length missing (obj %d)", objn);
            return NULL;
        }
        if ((uint64_t)slen > (uint64_t)(d->file_len - p.pos)) {
            po_free(body);
            snprintf(err, errcap, "pdf: stream Length past EOF (obj %d)", objn);
            return NULL;
        }
        PdfObj *stream_obj = po_new(PO_STREAM);
        if (!stream_obj) { po_free(body); return NULL; }
        stream_obj->stream_dict = body;
        stream_obj->stream_off = p.pos;
        stream_obj->stream_len = (size_t)slen;
        return stream_obj;
    }
    return body;
}

static int pdf_parse_xref(PdfDoc *d, char *err, size_t errcap) {
    if (d->file_len < 16) {
        snprintf(err, errcap, "pdf: file too small");
        return -1;
    }
    /* find startxref from end */
    size_t scan = d->file_len > 2048 ? d->file_len - 2048 : 0;
    const char *base = (const char *)d->file;
    const char *found = NULL;
    for (size_t i = d->file_len; i > scan + 9; ) {
        i--;
        if (i + 9 <= d->file_len && !memcmp(base + i, "startxref", 9)) {
            found = base + i;
            break;
        }
    }
    if (!found) {
        snprintf(err, errcap, "pdf: startxref not found");
        return -1;
    }
    const char *p = found + 9;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
    long xref_off = atol(p);
    if (xref_off < 0 || (size_t)xref_off >= d->file_len) {
        snprintf(err, errcap, "pdf: bad startxref offset");
        return -1;
    }

    PdfParse xp = {.data = d->file, .len = d->file_len, .pos = (size_t)xref_off};
    pp_skip_ws(&xp);
    if (!pp_consume(&xp, "xref")) {
        snprintf(err, errcap, "pdf: xref streams not supported");
        return -1;
    }

    int max_obj = 0;
    long *tmp_xref = NULL;
    int tmp_n = 0;

    for (;;) {
        pp_skip_ws(&xp);
        if (pp_startswith(&xp, "trailer")) break;
        /* subsection: start count */
        PdfObj *start_o = pp_parse_number_or_ref(&xp);
        if (!start_o || start_o->t != PO_INT) {
            po_free(start_o);
            snprintf(err, errcap, "pdf: bad xref subsection");
            free(tmp_xref);
            return -1;
        }
        int start = (int)start_o->i;
        po_free(start_o);
        pp_skip_ws(&xp);
        PdfObj *count_o = pp_parse_number_or_ref(&xp);
        if (!count_o || count_o->t != PO_INT) {
            po_free(count_o);
            snprintf(err, errcap, "pdf: bad xref count");
            free(tmp_xref);
            return -1;
        }
        int count = (int)count_o->i;
        po_free(count_o);
        int64_t end = (int64_t)start + (int64_t)count;
        if (start < 0 || count < 0 || end > 1000000) {
            snprintf(err, errcap, "pdf: xref subsection out of range");
            free(tmp_xref);
            return -1;
        }
        if (end > tmp_n) {
            long *nx = realloc(tmp_xref, (size_t)end * sizeof(long));
            if (!nx) { free(tmp_xref); return -1; }
            for (int64_t i = tmp_n; i < end; i++) nx[i] = 0;
            tmp_xref = nx;
            tmp_n = (int)end;
        }
        if (end > max_obj) max_obj = (int)end;

        for (int i = 0; i < count; i++) {
            pp_skip_ws(&xp);
            if (xp.pos + 20 > xp.len) {
                snprintf(err, errcap, "pdf: truncated xref");
                free(tmp_xref);
                return -1;
            }
            char line[22];
            memcpy(line, xp.data + xp.pos, 20);
            line[20] = 0;
            long off = 0;
            int gen = 0;
            char use = 'n';
            if (sscanf(line, "%ld %d %c", &off, &gen, &use) < 3) {
                /* try looser: read 10-digit offset */
                off = atol(line);
                use = line[17];
            }
            xp.pos += 20;
            if (xp.pos < xp.len && (xp.data[xp.pos] == '\r' || xp.data[xp.pos] == '\n')) {
                if (xp.data[xp.pos] == '\r') xp.pos++;
                if (xp.pos < xp.len && xp.data[xp.pos] == '\n') xp.pos++;
            }
            if (use == 'n') tmp_xref[(int64_t)start + i] = off;
            else tmp_xref[(int64_t)start + i] = 0;
            (void)gen;
        }
    }

    if (!pp_consume(&xp, "trailer")) {
        snprintf(err, errcap, "pdf: trailer not found");
        free(tmp_xref);
        return -1;
    }
    PdfObj *trailer = pp_parse_dict(&xp);
    if (!trailer) {
        snprintf(err, errcap, "pdf: cannot parse trailer");
        free(tmp_xref);
        return -1;
    }
    if (dict_get(trailer, "Encrypt")) {
        po_free(trailer);
        free(tmp_xref);
        snprintf(err, errcap, "pdf: encrypted documents not supported");
        return -1;
    }
    PdfObj *root = dict_get(trailer, "Root");
    if (!root || root->t != PO_REF) {
        po_free(trailer);
        free(tmp_xref);
        snprintf(err, errcap, "pdf: trailer missing /Root");
        return -1;
    }
    d->root_obj = root->ref_n;
    PdfObj *info = dict_get(trailer, "Info");
    d->info_obj = (info && info->t == PO_REF) ? info->ref_n : 0;
    po_free(trailer);

    d->xref = tmp_xref;
    d->xref_n = tmp_n;
    return 0;
}

/* Collect page object numbers from Kids refs */
static int pdf_collect_page_nums_depth(PdfDoc *d, int objn, int **out, int *nout, int *cap,
                                       char *err, size_t errcap, int depth, unsigned char *seen);

static int pdf_collect_page_nums(PdfDoc *d, int objn, int **out, int *nout, int *cap,
                                 char *err, size_t errcap) {
    int n = d->xref_n > 0 ? d->xref_n : 1;
    unsigned char *seen = calloc((size_t)n, 1);
    if (!seen) {
        snprintf(err, errcap, "pdf: out of memory");
        return -1;
    }
    int rc = pdf_collect_page_nums_depth(d, objn, out, nout, cap, err, errcap, 0, seen);
    free(seen);
    return rc;
}

static int pdf_collect_page_nums_depth(PdfDoc *d, int objn, int **out, int *nout, int *cap,
                                       char *err, size_t errcap, int depth, unsigned char *seen) {
    if (depth >= PDF_MAX_PAGE_DEPTH) {
        snprintf(err, errcap, "pdf: page tree too deep");
        return -1;
    }
    if (objn > 0 && objn < d->xref_n) {
        if (seen[objn]) {
            snprintf(err, errcap, "pdf: page tree cycle");
            return -1;
        }
        seen[objn] = 1;
    }
    PdfObj *node = pdf_load_indirect(d, objn, err, errcap, 0);
    if (!node) return -1;
    if (node->t == PO_STREAM) {
        /* unwrap unlikely */
        po_free(node);
        snprintf(err, errcap, "pdf: page tree is a stream");
        return -1;
    }
    if (node->t != PO_DICT) {
        po_free(node);
        snprintf(err, errcap, "pdf: page tree not a dict");
        return -1;
    }
    PdfObj *type = dict_get(node, "Type");
    const char *tn = (type && type->t == PO_NAME) ? type->s : "";
    if (!strcmp(tn, "Pages") || dict_get(node, "Kids")) {
        PdfObj *kids = dict_get(node, "Kids");
        if (!kids || kids->t != PO_ARRAY) {
            po_free(node);
            snprintf(err, errcap, "pdf: Pages missing Kids");
            return -1;
        }
        for (int i = 0; i < kids->nitems; i++) {
            PdfObj *kid = kids->items[i];
            if (kid->t != PO_REF) {
                po_free(node);
                snprintf(err, errcap, "pdf: Kids entry not a ref");
                return -1;
            }
            if (pdf_collect_page_nums_depth(d, kid->ref_n, out, nout, cap, err, errcap, depth + 1, seen) < 0) {
                po_free(node);
                return -1;
            }
        }
        po_free(node);
        return 0;
    }
    /* leaf page */
    if (*nout >= *cap) {
        int ncap = *cap ? *cap * 2 : 8;
        int *n = realloc(*out, (size_t)ncap * sizeof(int));
        if (!n) { po_free(node); return -1; }
        *out = n;
        *cap = ncap;
    }
    (*out)[(*nout)++] = objn;
    po_free(node);
    return 0;
}

static int pdf_get_page_nums(PdfDoc *d, int **out, int *nout, char *err, size_t errcap) {
    PdfObj *catalog = pdf_load_indirect(d, d->root_obj, err, errcap, 0);
    if (!catalog) return -1;
    PdfObj *pages = dict_get(catalog, "Pages");
    if (!pages || pages->t != PO_REF) {
        po_free(catalog);
        snprintf(err, errcap, "pdf: Catalog missing /Pages");
        return -1;
    }
    int pages_obj = pages->ref_n;
    po_free(catalog);
    *out = NULL;
    *nout = 0;
    int cap = 0;
    return pdf_collect_page_nums(d, pages_obj, out, nout, &cap, err, errcap);
}

static int pdf_append_content_bytes(PdfDoc *d, PdfObj *contents, PdfBuf *out,
                                    char *err, size_t errcap, int depth) {
    if (depth >= PDF_MAX_PARSE_DEPTH) {
        po_free(contents);
        snprintf(err, errcap, "pdf: Contents nesting too deep");
        return -1;
    }
    contents = pdf_resolve(d, contents, err, errcap);
    if (!contents) return -1;
    if (contents->t == PO_ARRAY) {
        for (int i = 0; i < contents->nitems; i++) {
            PdfObj *item = contents->items[i];
            PdfObj *copy = NULL;
            if (item->t == PO_REF) {
                copy = po_new(PO_REF);
                if (!copy) { po_free(contents); return -1; }
                copy->ref_n = item->ref_n;
                copy->ref_g = item->ref_g;
            } else {
                po_free(contents);
                snprintf(err, errcap, "pdf: Contents array entry not a ref");
                return -1;
            }
            if (pdf_append_content_bytes(d, copy, out, err, errcap, depth + 1) < 0) {
                po_free(contents);
                return -1;
            }
        }
        po_free(contents);
        return 0;
    }
    if (contents->t != PO_STREAM) {
        po_free(contents);
        snprintf(err, errcap, "pdf: Contents not a stream");
        return -1;
    }
    if (contents->stream_len > d->file_len ||
        contents->stream_off > d->file_len - contents->stream_len) {
        po_free(contents);
        snprintf(err, errcap, "pdf: stream past EOF");
        return -1;
    }
    if (buf_append(out, d->file + contents->stream_off, contents->stream_len) < 0) {
        po_free(contents);
        return -1;
    }
    if (out->len && out->data[out->len - 1] != '\n' && buf_append(out, "\n", 1) < 0) {
        po_free(contents);
        return -1;
    }
    po_free(contents);
    return 0;
}

static int pdf_page_content(PdfDoc *d, int page_obj, PdfBuf *out, char *err, size_t errcap) {
    PdfObj *page = pdf_load_indirect(d, page_obj, err, errcap, 0);
    if (!page) return -1;
    PdfObj *contents = dict_get(page, "Contents");
    if (!contents) {
        po_free(page);
        return 0; /* empty page */
    }
    PdfObj *copy;
    if (contents->t == PO_REF) {
        copy = po_new(PO_REF);
        if (!copy) { po_free(page); return -1; }
        copy->ref_n = contents->ref_n;
        copy->ref_g = contents->ref_g;
    } else if (contents->t == PO_ARRAY) {
        /* steal shallow: re-parse by cloning refs */
        copy = po_new(PO_ARRAY);
        if (!copy) { po_free(page); return -1; }
        for (int i = 0; i < contents->nitems; i++) {
            PdfObj *item = contents->items[i];
            PdfObj *c = po_new(item->t);
            if (!c) { po_free(copy); po_free(page); return -1; }
            if (item->t == PO_REF) { c->ref_n = item->ref_n; c->ref_g = item->ref_g; }
            PdfObj **ni = realloc(copy->items, (size_t)(copy->nitems + 1) * sizeof(PdfObj *));
            if (!ni) { po_free(c); po_free(copy); po_free(page); return -1; }
            copy->items = ni;
            copy->items[copy->nitems++] = c;
        }
    } else {
        po_free(page);
        snprintf(err, errcap, "pdf: bad Contents type");
        return -1;
    }
    po_free(page);
    return pdf_append_content_bytes(d, copy, out, err, errcap, 0);
}

static int extract_append_str(PdfBuf *out, const char *s, size_t n) {
    if (n == 0) return 0;
    if (out->len && buf_append(out, " ", 1) < 0) return -1;
    return buf_append(out, s, n);
}

static int pdf_extract_text_from_content(const unsigned char *data, size_t len, PdfBuf *out) {
    PdfParse p = {.data = data, .len = len, .pos = 0};
    for (;;) {
        pp_skip_ws(&p);
        if (p.pos >= p.len) break;
        unsigned char c = p.data[p.pos];
        if (c == '(') {
            PdfObj *str = pp_parse_literal_string(&p);
            if (!str) return -1;
            pp_skip_ws(&p);
            if (pp_startswith(&p, "Tj") || pp_startswith(&p, "'") ||
                (p.pos < p.len && p.data[p.pos] == '"' )) {
                if (extract_append_str(out, str->s, str->slen) < 0) { po_free(str); return -1; }
                if (pp_startswith(&p, "Tj")) p.pos += 2;
                else if (pp_startswith(&p, "'")) p.pos += 1;
                else p.pos += 1; /* " */
            }
            po_free(str);
            continue;
        }
        if (c == '<' && !(p.pos + 1 < p.len && p.data[p.pos + 1] == '<')) {
            PdfObj *str = pp_parse_hex_string(&p);
            if (!str) return -1;
            pp_skip_ws(&p);
            if (pp_startswith(&p, "Tj") || pp_startswith(&p, "'")) {
                if (extract_append_str(out, str->s, str->slen) < 0) { po_free(str); return -1; }
                if (pp_startswith(&p, "Tj")) p.pos += 2;
                else p.pos += 1;
            }
            po_free(str);
            continue;
        }
        if (c == '[') {
            PdfObj *arr = pp_parse_array(&p);
            if (!arr) return -1;
            pp_skip_ws(&p);
            if (pp_startswith(&p, "TJ")) {
                for (int i = 0; i < arr->nitems; i++) {
                    PdfObj *item = arr->items[i];
                    if (item->t == PO_STRING) {
                        if (extract_append_str(out, item->s, item->slen) < 0) {
                            po_free(arr);
                            return -1;
                        }
                    }
                }
                p.pos += 2;
            }
            po_free(arr);
            continue;
        }
        /* skip operator / number / name / dict */
        if (c == '/' ) {
            PdfObj *nm = pp_parse_name(&p);
            po_free(nm);
            continue;
        }
        if (c == '<' && p.pos + 1 < p.len && p.data[p.pos + 1] == '<') {
            PdfObj *d = pp_parse_dict(&p);
            po_free(d);
            continue;
        }
        if (c == '+' || c == '-' || isdigit(c) || c == '.') {
            PdfObj *n = pp_parse_number_or_ref(&p);
            po_free(n);
            continue;
        }
        /* operator token */
        while (p.pos < p.len) {
            unsigned char x = p.data[p.pos];
            if (x == ' ' || x == '\t' || x == '\r' || x == '\n' || x == '\f' ||
                x == '/' || x == '[' || x == ']' || x == '(' || x == ')' ||
                x == '<' || x == '>' || x == '%')
                break;
            p.pos++;
        }
    }
    return 0;
}

/* ---------- builtins ---------- */

V *bi_pdf_create(V **a, int n) {
    (void)a;
    (void)n;
    PdfDoc *d = calloc(1, sizeof(PdfDoc));
    P(!d, v_err("pdf_create: out of memory"))
    d->mode = PDF_WRITE;
    int64_t id = pdf_alloc_slot(d);
    if (id < 0) {
        pdf_free_doc(d);
        return v_err("pdf_create: too many open documents");
    }
    return v_int(id);
}

V *bi_pdf_add_page(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("pdf_add_page(handle)"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d || d->mode != PDF_WRITE, v_err("pdf_add_page: bad write handle"))
    if (d->npages >= d->pages_cap) {
        int cap = d->pages_cap ? d->pages_cap * 2 : 4;
        PdfPage *np = realloc(d->pages, (size_t)cap * sizeof(PdfPage));
        P(!np, v_err("pdf_add_page: out of memory"))
        d->pages = np;
        d->pages_cap = cap;
    }
    PdfPage *pg = &d->pages[d->npages++];
    memset(pg, 0, sizeof(*pg));
    pg->width = PDF_LETTER_W;
    pg->height = PDF_LETTER_H;
    return v_int(d->npages);
}

V *bi_pdf_text_at(V **a, int n) {
    P(n < 4, v_err("pdf_text_at(handle, x, y, text, [size])"))
    P(a[0]->t != T_INT, v_err("pdf_text_at: bad handle"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d || d->mode != PDF_WRITE, v_err("pdf_text_at: bad write handle"))
    P(d->npages < 1, v_err("pdf_text_at: add_page first"))
    double x = a[1]->t == T_FLOAT ? a[1]->f : (a[1]->t == T_INT ? (double)a[1]->j : 0);
    double y = a[2]->t == T_FLOAT ? a[2]->f : (a[2]->t == T_INT ? (double)a[2]->j : 0);
    P(a[3]->t != T_STR, v_err("pdf_text_at: text must be string"))
    double size = 12.0;
    if (n >= 5) {
        if (a[4]->t == T_FLOAT) size = a[4]->f;
        else if (a[4]->t == T_INT) size = (double)a[4]->j;
    }
    PdfPage *pg = &d->pages[d->npages - 1];
    if (buf_printf(&pg->content, "BT\n/F1 %.2f Tf\n%.2f %.2f Td\n", size, x, y) < 0)
        return v_err("pdf_text_at: out of memory");
    if (pdf_escape_string(&pg->content, a[3]->s) < 0)
        return v_err("pdf_text_at: out of memory");
    if (buf_append(&pg->content, " Tj\nET\n", 7) < 0)
        return v_err("pdf_text_at: out of memory");
    return v_nil();
}

V *bi_pdf_save(V **a, int n) {
    P(n < 2 || a[0]->t != T_INT || a[1]->t != T_STR, v_err("pdf_save(handle, path)"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d || d->mode != PDF_WRITE, v_err("pdf_save: bad write handle"))
    P(d->npages < 1, v_err("pdf_save: no pages"))

    /* inline write with correct length tracking */
    int n_pages = d->npages;
    int font_obj = 3 + n_pages * 2;
    int n_obj = font_obj;
    PdfBuf out = {0};
    long *offsets = calloc((size_t)n_obj + 1, sizeof(long));
    P(!offsets, v_err("pdf_save: out of memory"))

    if (buf_append(&out, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n", 15) < 0) goto oom;
    if (pdf_emit_obj_header(&out, 1, offsets) < 0) goto oom;
    if (buf_printf(&out, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n") < 0) goto oom;
    if (pdf_emit_obj_header(&out, 2, offsets) < 0) goto oom;
    if (buf_printf(&out, "<< /Type /Pages /Count %d /Kids [", n_pages) < 0) goto oom;
    for (int i = 0; i < n_pages; i++) {
        if (buf_printf(&out, "%d 0 R ", 3 + i * 2) < 0) goto oom;
    }
    if (buf_printf(&out, "] >>\nendobj\n") < 0) goto oom;
    for (int i = 0; i < n_pages; i++) {
        int page_obj = 3 + i * 2;
        int content_obj = page_obj + 1;
        PdfPage *pg = &d->pages[i];
        if (pdf_emit_obj_header(&out, page_obj, offsets) < 0) goto oom;
        if (buf_printf(&out,
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] "
                "/Contents %d 0 R /Resources << /Font << /F1 %d 0 R >> >> >>\nendobj\n",
                pg->width, pg->height, content_obj, font_obj) < 0)
            goto oom;
        if (pdf_emit_obj_header(&out, content_obj, offsets) < 0) goto oom;
        if (buf_printf(&out, "<< /Length %zu >>\nstream\n", pg->content.len) < 0) goto oom;
        if (pg->content.len && buf_append(&out, pg->content.data, pg->content.len) < 0) goto oom;
        if (buf_append(&out, "\nendstream\nendobj\n", 18) < 0) goto oom;
    }
    if (pdf_emit_obj_header(&out, font_obj, offsets) < 0) goto oom;
    if (buf_printf(&out,
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n") < 0)
        goto oom;
    long xref_pos = (long)out.len;
    if (buf_printf(&out, "xref\n0 %d\n", n_obj + 1) < 0) goto oom;
    if (buf_printf(&out, "0000000000 65535 f \n") < 0) goto oom;
    for (int i = 1; i <= n_obj; i++) {
        if (buf_printf(&out, "%010ld 00000 n \n", offsets[i]) < 0) goto oom;
    }
    if (buf_printf(&out, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
                   n_obj + 1, xref_pos) < 0)
        goto oom;

    size_t total = out.len;
    FILE *fp = fopen(a[1]->s, "wb");
    if (!fp) {
        free(offsets);
        buf_free(&out);
        return v_errf("pdf_save: cannot write '%s'", a[1]->s);
    }
    size_t wr = fwrite(out.data, 1, total, fp);
    fclose(fp);
    free(offsets);
    buf_free(&out);
    P(wr != total, v_err("pdf_save: short write"))
    return v_nil();
oom:
    free(offsets);
    buf_free(&out);
    return v_err("pdf_save: out of memory");
}

V *bi_pdf_open(V **a, int n) {
    P(n < 1 || a[0]->t != T_STR, v_err("pdf_open(path)"))
    FILE *fp = fopen(a[0]->s, "rb");
    P(!fp, v_errf("pdf_open: cannot open '%s'", a[0]->s))
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return v_err("pdf_open: seek failed"); }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return v_err("pdf_open: tell failed"); }
    if ((unsigned long)sz > PDF_MAX_FILE_BYTES) {
        fclose(fp);
        return v_err("pdf_open: file too large");
    }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return v_err("pdf_open: seek failed"); }
    PdfDoc *d = calloc(1, sizeof(PdfDoc));
    if (!d) { fclose(fp); return v_err("pdf_open: out of memory"); }
    d->mode = PDF_READ;
    d->file_len = (size_t)sz;
    d->file = malloc(d->file_len + 1);
    if (!d->file) { fclose(fp); pdf_free_doc(d); return v_err("pdf_open: out of memory"); }
    if (fread(d->file, 1, d->file_len, fp) != d->file_len) {
        fclose(fp);
        pdf_free_doc(d);
        return v_err("pdf_open: short read");
    }
    fclose(fp);
    d->file[d->file_len] = 0;
    if (d->file_len < 5 || memcmp(d->file, "%PDF-", 5) != 0) {
        pdf_free_doc(d);
        return v_err("pdf_open: not a PDF file");
    }
    char err[160];
    err[0] = 0;
    if (pdf_parse_xref(d, err, sizeof err) < 0) {
        pdf_free_doc(d);
        return v_err(err[0] ? err : "pdf_open: bad xref");
    }
    int64_t id = pdf_alloc_slot(d);
    if (id < 0) {
        pdf_free_doc(d);
        return v_err("pdf_open: too many open documents");
    }
    return v_int(id);
}

V *bi_pdf_page_count(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("pdf_page_count(handle)"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d, v_err("pdf_page_count: bad handle"))
    if (d->mode == PDF_WRITE) return v_int(d->npages);
    P(d->mode != PDF_READ, v_err("pdf_page_count: bad handle"))
    int *pages = NULL, np = 0;
    char err[160];
    err[0] = 0;
    if (pdf_get_page_nums(d, &pages, &np, err, sizeof err) < 0) {
        free(pages);
        return v_err(err[0] ? err : "pdf_page_count: failed");
    }
    free(pages);
    return v_int(np);
}

V *bi_pdf_info(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("pdf_info(handle)"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d || d->mode != PDF_READ, v_err("pdf_info: bad read handle"))
    V *keys = v_list(0), *vals = v_list(0);
    V *res = v_dict(keys, vals);
    v_free(keys);
    v_free(vals);
    if (!d->info_obj) return res;
    char err[160];
    err[0] = 0;
    PdfObj *info = pdf_load_indirect(d, d->info_obj, err, sizeof err, 0);
    if (!info || info->t != PO_DICT) {
        po_free(info);
        return res;
    }
    static const char *fields[] = {"Title", "Author", "Creator", "CreationDate", "Producer", "Subject", NULL};
    for (int i = 0; fields[i]; i++) {
        PdfObj *v = dict_get(info, fields[i]);
        if (v && v->t == PO_STRING && v->s)
            v_dict_put(res, fields[i], v_str(v->s));
    }
    po_free(info);
    return res;
}

V *bi_pdf_text(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("pdf_text(handle, [page])"))
    PdfDoc *d = pdf_at(a[0]->j);
    P(!d || d->mode != PDF_READ, v_err("pdf_text: bad read handle"))
    int page_sel = 0; /* 0 = all, else 1-based */
    if (n >= 2) {
        if (a[1]->t == T_INT) page_sel = (int)a[1]->j;
        else if (a[1]->t == T_FLOAT) page_sel = (int)a[1]->f;
    }
    int *pages = NULL, np = 0;
    char err[160];
    err[0] = 0;
    if (pdf_get_page_nums(d, &pages, &np, err, sizeof err) < 0) {
        free(pages);
        return v_err(err[0] ? err : "pdf_text: page tree failed");
    }
    if (page_sel < 0 || page_sel > np) {
        free(pages);
        return v_err("pdf_text: page out of range");
    }
    PdfBuf text = {0};
    int from = page_sel ? page_sel - 1 : 0;
    int to = page_sel ? page_sel : np;
    for (int i = from; i < to; i++) {
        PdfBuf content = {0};
        if (pdf_page_content(d, pages[i], &content, err, sizeof err) < 0) {
            buf_free(&content);
            buf_free(&text);
            free(pages);
            return v_err(err[0] ? err : "pdf_text: content failed");
        }
        if (content.len && pdf_extract_text_from_content(
                (const unsigned char *)content.data, content.len, &text) < 0) {
            buf_free(&content);
            buf_free(&text);
            free(pages);
            return v_err("pdf_text: extract failed");
        }
        buf_free(&content);
        if (i + 1 < to && text.len && buf_append(&text, "\n", 1) < 0) {
            buf_free(&text);
            free(pages);
            return v_err("pdf_text: out of memory");
        }
    }
    free(pages);
    char *s = text.data ? text.data : strdup("");
    if (!s) return v_err("pdf_text: out of memory");
    /* v_str_take owns the buffer */
    return v_str_take(s);
}

V *bi_pdf_close(V **a, int n) {
    P(n < 1 || a[0]->t != T_INT, v_err("pdf_close(handle)"))
    int64_t id = a[0]->j;
    P(id < 0 || id >= PDF_MAX || !g_pdfs[id], v_err("pdf_close: bad handle"))
    pdf_release_slot(id);
    return v_nil();
}
