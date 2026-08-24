/* shakti/src/ast.c — AST nodes */
#include "shakti_internal.h"

Node *node_new(int type) {
    Node *n = x_calloc(1, sizeof(Node), "node_new");
    n->type = type;
    n->fn_ast_i=-1;
    return n;
}
void node_add(Node *n, Node *child) {
    n->ch = x_realloc(n->ch, (size_t)(n->nch + 1) * sizeof(Node *), "node_add");
    n->ch[n->nch++] = child;
}
void node_free(Node *n) {
    Pv(!n||n->fn_ast_i>-1)
    free(n->sval);
    i(n->nch,node_free(n->ch[i]))
    free(n->ch);
    free(n);
}
static const char *node_op_name(int op) {
    switch (op) {
    case OP_ADD: return "+"; case OP_SUB: return "-"; case OP_MUL: return "*";
    case OP_DIV: return "/"; case OP_MOD: return "%"; case OP_POW: return "**";
    case OP_NEG: return "neg"; case OP_NOT: return "not";
    default: return "?";
    }
}

static void node_sprint_rec(Node *n, FILE *fp) {
    if (!n) { fputs("nil", fp); return; }
    switch (n->type) {
    case N_INT: fprintf(fp, "%lld", (long long)n->ival); return;
    case N_FLOAT: fprintf(fp, "%g", n->fval); return;
    case N_STR: fprintf(fp, "\"%s\"", n->sval ? n->sval : ""); return;
    case N_BOOL: fputs(n->ival ? "True" : "False", fp); return;
    case N_NONE: fputs("None", fp); return;
    case N_NAME: fprintf(fp, "`%s", n->sval ? n->sval : ""); return;
    case N_UNOP:
        fprintf(fp, "(%s ", node_op_name(n->op));
        node_sprint_rec(n->nch > 0 ? n->ch[0] : NULL, fp);
        fputc(')', fp);
        return;
    case N_BINOP:
        fprintf(fp, "(%s ", node_op_name(n->op));
        node_sprint_rec(n->nch > 0 ? n->ch[0] : NULL, fp);
        fputc(' ', fp);
        node_sprint_rec(n->nch > 1 ? n->ch[1] : NULL, fp);
        fputc(')', fp);
        return;
    case N_CALL:
        fputs("(call ", fp);
        for (int i = 0; i < n->nch; i++) {
            if (i) fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(')', fp);
        return;
    case N_EACH:
        fputs(n->nch >= 3 ? "(each2 " : "(each ", fp);
        for (int i = 0; i < n->nch; i++) {
            if (i) fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(')', fp);
        return;
    case N_LIST:
        fputs("[", fp);
        for (int i = 0; i < n->nch; i++) {
            if (i) fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(']', fp);
        return;
    default:
        fprintf(fp, "(n%d", n->type);
        for (int i = 0; i < n->nch; i++) {
            fputc(' ', fp);
            node_sprint_rec(n->ch[i], fp);
        }
        fputc(')', fp);
        return;
    }
}

void node_sprint(Node *n, FILE *fp) {
    node_sprint_rec(n, fp);
}
