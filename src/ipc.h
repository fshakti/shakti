#ifndef SHAKTI_IPC_H
#define SHAKTI_IPC_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IPC_MAX_MSG (1024 * 1024)

typedef enum {
    IPC_TR_AUTO = 0,
    IPC_TR_TCP,
    IPC_TR_UDS,
    IPC_TR_RDMA,
} IpcTransport;

int ipc_rdma_available(void);

V *bi_ipc_listen(V **a, int n);
V *bi_ipc_accept(V **a, int n);
V *bi_ipc_connect(V **a, int n);
V *bi_ipc_send(V **a, int n);
V *bi_ipc_recv(V **a, int n);
V *bi_ipc_recv_nowait(V **a, int n);
V *bi_ipc_set_nonblock(V **a, int n);
V *bi_ipc_poll(V **a, int n);
V *bi_ipc_close(V **a, int n);
V *bi_ipc_shm_open(V **a, int n);
V *bi_ipc_shm_close(V **a, int n);
V *bi_ipc_rdma_available(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif
