/*
 * Shakti partitioned HDB over IEFS v3 (no SQLite / kdb).
 * Layout: <root>/meta.iefs + <root>/<table>/<part>/data.iefs
 */
#ifndef SHAKTI_HDB_H
#define SHAKTI_HDB_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

V *bi_hdb_open(V **a, int n);
V *bi_hdb_close(V **a, int n);
V *bi_hdb_create(V **a, int n);
V *bi_hdb_write(V **a, int n);
V *bi_hdb_parts(V **a, int n);
V *bi_hdb_map(V **a, int n);
V *bi_hdb_load_part(V **a, int n);
V *bi_hdb_load(V **a, int n);
V *bi_hdb_scan(V **a, int n);
V *bi_hdb_next(V **a, int n);
V *bi_hdb_current(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif /* SHAKTI_HDB_H */
