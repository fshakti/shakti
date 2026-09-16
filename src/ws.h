#ifndef SHAKTI_WS_H
#define SHAKTI_WS_H

#include "shakti.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

V *bi_ws_connect(V **a, int n);
V *bi_ws_accept(V **a, int n);
V *bi_ws_send(V **a, int n);
V *bi_ws_send_bin(V **a, int n);
V *bi_ws_recv(V **a, int n);
V *bi_ws_poll(V **a, int n);
V *bi_ws_close(V **a, int n);
V *bi_ws_set_nonblock(V **a, int n);

/* Low-level TCP client framing (ws://, no TLS). */
int ws_raw_connect(const char *ws_url, int *out_fd);
int ws_raw_send_text(int fd, const char *msg);
int ws_raw_recv_text(int fd, char *out, size_t out_cap);
void ws_raw_set_recv_timeout_ms(int fd, int ms);

#ifdef __cplusplus
}
#endif

#endif
