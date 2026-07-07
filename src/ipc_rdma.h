#ifndef SHAKTI_IPC_RDMA_H
#define SHAKTI_IPC_RDMA_H

#include <stddef.h>

#ifdef SHAKTI_HAVE_RDMA

typedef struct IpcRdmaConn IpcRdmaConn;

int ipc_rdma_init(void);
void ipc_rdma_shutdown(void);
int ipc_rdma_available(void);
int ipc_rdma_poll_fd(void);

int ipc_rdma_listen(const char *host, int port, IpcRdmaConn **out, char *err, size_t err_cap);
int ipc_rdma_accept(IpcRdmaConn *listen, IpcRdmaConn **out, char *err, size_t err_cap);
int ipc_rdma_connect(const char *host, int port, IpcRdmaConn **out, char *err, size_t err_cap);

int ipc_rdma_send(IpcRdmaConn *c, const void *data, size_t len, char *err, size_t err_cap);
int ipc_rdma_recv(IpcRdmaConn *c, int block, char **out, size_t *out_len, char *err, size_t err_cap);
int ipc_rdma_has_message(IpcRdmaConn *c);

void ipc_rdma_set_nonblock(IpcRdmaConn *c, int on);
void ipc_rdma_close(IpcRdmaConn *c);

#endif /* SHAKTI_HAVE_RDMA */

#endif
