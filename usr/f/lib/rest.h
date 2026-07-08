#ifndef SHAKTI_REST_H
#define SHAKTI_REST_H

#include "shakti.h"

V *bi_rest_request(V **a, int n);
V *bi_rest_get(V **a, int n);
V *bi_rest_post(V **a, int n);
V *bi_rest_put(V **a, int n);
V *bi_rest_delete(V **a, int n);
V *bi_rest_listen(V **a, int n);
V *bi_rest_accept(V **a, int n);
V *bi_rest_read(V **a, int n);
V *bi_rest_write(V **a, int n);
V *bi_rest_close(V **a, int n);

#endif
