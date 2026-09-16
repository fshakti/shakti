#ifndef SHAKTI_SERVER_H
#define SHAKTI_SERVER_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

void server_serve(int port, Env *global_env);
V *bi_server_serve(V **a, int n, Env *e);

#ifdef __cplusplus
}
#endif

#endif
