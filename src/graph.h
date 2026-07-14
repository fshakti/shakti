#ifndef SHAKTI_GRAPH_H
#define SHAKTI_GRAPH_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

V *bi_graph_create(V **a, int n);
V *bi_graph_add(V **a, int n);
V *bi_graph_query(V **a, int n);
V *bi_graph_neighbors(V **a, int n);
V *bi_graph_path(V **a, int n);
V *bi_graph_from_table(V **a, int n);
V *bi_graph_to_table(V **a, int n);
V *bi_graph_count(V **a, int n);
V *bi_graph_clear(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif
