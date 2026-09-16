#ifndef SHAKTI_REST_H
#define SHAKTI_REST_H

#include "shakti.h"
#include <stddef.h>

V *bi_rest_request(V **a, int n);
V *bi_rest_get(V **a, int n);
V *bi_rest_post(V **a, int n);
V *bi_rest_put(V **a, int n);
V *bi_rest_delete(V **a, int n);
V *bi_rest_listen(V **a, int n);
V *bi_rest_listen_tls(V **a, int n);
V *bi_rest_accept(V **a, int n);
V *bi_rest_read(V **a, int n);
V *bi_rest_write(V **a, int n);
V *bi_rest_close(V **a, int n);
V *bi_rest_set_nonblock(V **a, int n);

/* Transfer conn fd/SSL ownership to ws.accept. Returns 0 on success. */
int rest_take_conn(int h, int *out_fd, void **out_ssl);
int rest_peek_ws_key(int h, char *out, size_t out_cap);
int rest_peek_ws_protocol(int h, char *out, size_t out_cap);

#endif
