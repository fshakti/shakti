#ifndef SHAKTI_IPC_H
#define SHAKTI_IPC_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IPC_MAX_MSG (1024 * 1024)
#define IPC_MAX_SHM (256u * 1024u * 1024u)
#define IPC_UDP_MAX 65507

typedef enum {
    IPC_TR_AUTO = 0,
    IPC_TR_TCP,
    IPC_TR_UDS,
    IPC_TR_RDMA,
    IPC_TR_UDP,
    IPC_TR_MCAST,
} IpcTransport;

int ipc_rdma_available(void);

V *bi_ipc_listen(V **a, int n);
V *bi_ipc_accept(V **a, int n);
V *bi_ipc_connect(V **a, int n);
V *bi_ipc_send(V **a, int n);
V *bi_ipc_recv(V **a, int n);
V *bi_ipc_recv_nowait(V **a, int n);
V *bi_ipc_recv_bin(V **a, int n);
V *bi_ipc_recv_nowait_bin(V **a, int n);
V *bi_ipc_set_nonblock(V **a, int n);
V *bi_ipc_poll(V **a, int n);
V *bi_ipc_close(V **a, int n);
V *bi_ipc_shm_open(V **a, int n);
V *bi_ipc_shm_close(V **a, int n);
V *bi_ipc_rdma_available(V **a, int n);

V *bi_ipc_join(V **a, int n);
V *bi_ipc_publish(V **a, int n);
V *bi_ipc_shm_create(V **a, int n);
V *bi_ipc_shm_attach(V **a, int n);
V *bi_ipc_shm_broadcast_create(V **a, int n);
V *bi_ipc_shm_broadcast_attach(V **a, int n);
V *bi_ipc_shm_view(V **a, int n);
V *bi_ipc_send_async(V **a, int n);
V *bi_ipc_send_sync(V **a, int n);
V *bi_ipc_reply(V **a, int n);
V *bi_ipc_recv_msg(V **a, int n);
V *bi_ipc_recv_msg_nowait(V **a, int n);
V *bi_ipc_recv_req(V **a, int n);
V *bi_ipc_recv_req_nowait(V **a, int n);
V *bi_ipc_send_n(V **a, int n);

#ifdef __cplusplus
}
#endif

#endif
