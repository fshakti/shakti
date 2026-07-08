#ifndef SHAKTI_LISSEN_H
#define SHAKTI_LISSEN_H

#include "shakti.h"

V *bi_lissen_trpc_query(V **a, int n);
V *bi_lissen_trpc_mutation(V **a, int n);
V *bi_lissen_set_token(V **a, int n);
V *bi_lissen_get_token(V **a, int n);
V *bi_lissen_set_api_base(V **a, int n);
V *bi_lissen_get_api_base(V **a, int n);
V *bi_lissen_app_url(V **a, int n);
V *bi_lissen_web_url(V **a, int n);
V *bi_lissen_open(V **a, int n);

#endif
