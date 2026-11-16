// SPDX-License-Identifier: Apache-2.0
#ifndef SHAKTI_SERVER_H
#define SHAKTI_SERVER_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *server_serve(int port, Env *global_env);
V *bi_server_serve(V **a, int n, Env *e);

#ifdef __cplusplus
}
#endif

#endif
