#include "ipc_rdma.h"

#ifdef SHAKTI_HAVE_RDMA

#include "ipc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define IPC_RDMA_BUF (IPC_MAX_MSG + 4)

struct IpcRdmaConn {
    struct rdma_cm_id *id;
    struct ibv_mr *send_mr;
    struct ibv_mr *recv_mr;
    unsigned char *send_buf;
    unsigned char *recv_buf;
    int send_flags;
    int is_listen;
    int nonblock;
    int recv_ready;
    size_t recv_len;
    char *pending_msg;
    size_t pending_len;
};

static struct rdma_event_channel *g_ec;
static int g_ec_fd = -1;
static int g_rdma_devs;

static int ipc_rdma_ensure_ec(void) {
    if (g_ec) return 0;
    g_ec = rdma_create_event_channel();
    if (!g_ec) return -1;
    g_ec_fd = g_ec->fd;
    return 0;
}

int ipc_rdma_init(void) {
    struct ibv_device **list = ibv_get_device_list(&g_rdma_devs);
    if (list) ibv_free_device_list(list);
    return ipc_rdma_ensure_ec();
}

void ipc_rdma_shutdown(void) {
    if (g_ec) {
        rdma_destroy_event_channel(g_ec);
        g_ec = NULL;
        g_ec_fd = -1;
    }
}

int ipc_rdma_available(void) {
    struct ibv_device **list = ibv_get_device_list(NULL);
    if (!list) return 0;
    int ok = list[0] != NULL;
    ibv_free_device_list(list);
    return ok;
}

int ipc_rdma_poll_fd(void) {
    if (ipc_rdma_ensure_ec() != 0) return -1;
    return g_ec_fd;
}

static int ipc_rdma_setup_conn(IpcRdmaConn *c, char *err, size_t err_cap) {
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.cap.max_send_wr = 4;
    attr.cap.max_recv_wr = 4;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = 64;
    attr.sq_sig_all = 1;
    if (c->id->qp) {
        struct ibv_qp_init_attr init_attr;
        if (ibv_query_qp(c->id->qp, NULL, IBV_QP_CAP, &init_attr) == 0 &&
            init_attr.cap.max_inline_data >= 64)
            c->send_flags = IBV_SEND_INLINE;
    }
    c->send_buf = calloc(1, IPC_RDMA_BUF);
    c->recv_buf = calloc(1, IPC_RDMA_BUF);
    if (!c->send_buf || !c->recv_buf) {
        snprintf(err, err_cap, "ipc rdma: oom");
        return -1;
    }
    c->recv_mr = rdma_reg_msgs(c->id, c->recv_buf, IPC_RDMA_BUF);
    if (!c->recv_mr) {
        snprintf(err, err_cap, "ipc rdma: reg recv: %s", strerror(errno));
        return -1;
    }
    if (c->send_flags & IBV_SEND_INLINE)
        c->send_mr = c->recv_mr;
    else {
        c->send_mr = rdma_reg_msgs(c->id, c->send_buf, IPC_RDMA_BUF);
        if (!c->send_mr) {
            snprintf(err, err_cap, "ipc rdma: reg send: %s", strerror(errno));
            return -1;
        }
    }
    if (rdma_post_recv(c->id, NULL, c->recv_buf, IPC_RDMA_BUF, c->recv_mr) != 0) {
        snprintf(err, err_cap, "ipc rdma: post_recv: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int ipc_rdma_wait_send(IpcRdmaConn *c, int block, char *err, size_t err_cap) {
    struct ibv_wc wc;
    for (;;) {
        int rc = rdma_get_send_comp(c->id, &wc);
        if (rc == 0) return 0;
        if (rc < 0) {
            snprintf(err, err_cap, "ipc rdma: send comp: %s", strerror(errno));
            return -1;
        }
        if (!block) {
            snprintf(err, err_cap, "ipc rdma: send pending");
            return -2;
        }
    }
}

static int ipc_rdma_drain_recv(IpcRdmaConn *c, int block, char *err, size_t err_cap) {
    if (c->recv_ready) return 0;
    struct ibv_wc wc;
    for (;;) {
        int rc = rdma_get_recv_comp(c->id, &wc);
        if (rc == 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                snprintf(err, err_cap, "ipc rdma: recv wc status %d", wc.status);
                return -1;
            }
            c->recv_len = wc.byte_len;
            c->recv_ready = 1;
            return 0;
        }
        if (rc < 0) {
            snprintf(err, err_cap, "ipc rdma: recv comp: %s", strerror(errno));
            return -1;
        }
        if (!block) return -2;
    }
}

static int ipc_rdma_port_str(int port, char *out, size_t cap) {
    return snprintf(out, cap, "%d", port) > 0 ? 0 : -1;
}

int ipc_rdma_listen(const char *host, int port, IpcRdmaConn **out, char *err, size_t err_cap) {
    if (ipc_rdma_ensure_ec() != 0) {
        snprintf(err, err_cap, "ipc rdma: event channel");
        return -1;
    }
    char port_s[16];
    if (ipc_rdma_port_str(port, port_s, sizeof port_s) != 0) {
        snprintf(err, err_cap, "ipc rdma: bad port");
        return -1;
    }
    struct rdma_addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = RAI_PASSIVE;
    hints.ai_port_space = RDMA_PS_TCP;
    hints.ai_family = AF_INET;
    int ret = rdma_getaddrinfo(host && host[0] ? host : NULL, port_s, &hints, &res);
    if (ret) {
        snprintf(err, err_cap, "ipc rdma: getaddrinfo: %s", gai_strerror(ret));
        return -1;
    }
    IpcRdmaConn *c = calloc(1, sizeof *c);
    if (!c) {
        rdma_freeaddrinfo(res);
        snprintf(err, err_cap, "ipc rdma: oom");
        return -1;
    }
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.cap.max_send_wr = 4;
    attr.cap.max_recv_wr = 4;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = 64;
    attr.sq_sig_all = 1;
    if (rdma_create_ep(&c->id, res, NULL, &attr) != 0) {
        snprintf(err, err_cap, "ipc rdma: create_ep: %s", strerror(errno));
        free(c);
        rdma_freeaddrinfo(res);
        return -1;
    }
    rdma_freeaddrinfo(res);
    if (rdma_listen(c->id, 4) != 0) {
        snprintf(err, err_cap, "ipc rdma: listen: %s", strerror(errno));
        rdma_destroy_ep(c->id);
        free(c);
        return -1;
    }
    c->is_listen = 1;
    *out = c;
    return 0;
}

int ipc_rdma_accept(IpcRdmaConn *listen, IpcRdmaConn **out, char *err, size_t err_cap) {
    if (!listen || !listen->is_listen || !listen->id) {
        snprintf(err, err_cap, "ipc rdma: bad listen");
        return -1;
    }
    struct rdma_cm_id *child = NULL;
    if (rdma_get_request(listen->id, &child) != 0) {
        snprintf(err, err_cap, "ipc rdma: get_request: %s", strerror(errno));
        return -1;
    }
    IpcRdmaConn *c = calloc(1, sizeof *c);
    if (!c) {
        rdma_reject(child, NULL, 0);
        rdma_destroy_ep(child);
        snprintf(err, err_cap, "ipc rdma: oom");
        return -1;
    }
    c->id = child;
    if (ipc_rdma_setup_conn(c, err, err_cap) != 0) {
        ipc_rdma_close(c);
        return -1;
    }
    if (rdma_accept(c->id, NULL) != 0) {
        snprintf(err, err_cap, "ipc rdma: accept: %s", strerror(errno));
        ipc_rdma_close(c);
        return -1;
    }
    *out = c;
    return 0;
}

int ipc_rdma_connect(const char *host, int port, IpcRdmaConn **out, char *err, size_t err_cap) {
    char port_s[16];
    if (ipc_rdma_port_str(port, port_s, sizeof port_s) != 0) {
        snprintf(err, err_cap, "ipc rdma: bad port");
        return -1;
    }
    struct rdma_addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_port_space = RDMA_PS_TCP;
    hints.ai_family = AF_INET;
    int ret = rdma_getaddrinfo(host, port_s, &hints, &res);
    if (ret) {
        snprintf(err, err_cap, "ipc rdma: getaddrinfo: %s", gai_strerror(ret));
        return -1;
    }
    IpcRdmaConn *c = calloc(1, sizeof *c);
    if (!c) {
        rdma_freeaddrinfo(res);
        snprintf(err, err_cap, "ipc rdma: oom");
        return -1;
    }
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.cap.max_send_wr = 4;
    attr.cap.max_recv_wr = 4;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = 64;
    attr.sq_sig_all = 1;
    if (rdma_create_ep(&c->id, res, NULL, &attr) != 0) {
        snprintf(err, err_cap, "ipc rdma: create_ep: %s", strerror(errno));
        free(c);
        rdma_freeaddrinfo(res);
        return -1;
    }
    rdma_freeaddrinfo(res);
    if (ipc_rdma_setup_conn(c, err, err_cap) != 0) {
        ipc_rdma_close(c);
        return -1;
    }
    if (rdma_connect(c->id, NULL) != 0) {
        snprintf(err, err_cap, "ipc rdma: connect: %s", strerror(errno));
        ipc_rdma_close(c);
        return -1;
    }
    *out = c;
    return 0;
}

int ipc_rdma_send(IpcRdmaConn *c, const void *data, size_t len, char *err, size_t err_cap) {
    if (!c || !c->id) {
        snprintf(err, err_cap, "ipc rdma: bad conn");
        return -1;
    }
    if (len + 4 > IPC_RDMA_BUF || len > IPC_MAX_MSG) {
        snprintf(err, err_cap, "ipc rdma: message too large");
        return -1;
    }
    uint32_t be = htonl((uint32_t)len);
    memcpy(c->send_buf, &be, 4);
    memcpy(c->send_buf + 4, data, len);
    if (rdma_post_send(c->id, NULL, c->send_buf, len + 4, c->send_mr, c->send_flags) != 0) {
        snprintf(err, err_cap, "ipc rdma: post_send: %s", strerror(errno));
        return -1;
    }
    return ipc_rdma_wait_send(c, 1, err, err_cap);
}

int ipc_rdma_recv(IpcRdmaConn *c, int block, char **out, size_t *out_len, char *err, size_t err_cap) {
    if (!c || !c->id) {
        snprintf(err, err_cap, "ipc rdma: bad conn");
        return -1;
    }
    if (c->pending_msg) {
        *out = c->pending_msg;
        *out_len = c->pending_len;
        c->pending_msg = NULL;
        c->pending_len = 0;
        return 0;
    }
    int dr = ipc_rdma_drain_recv(c, block, err, err_cap);
    if (dr == -2) return -2;
    if (dr != 0) return -1;
    if (c->recv_len < 4) {
        snprintf(err, err_cap, "ipc rdma: short message");
        c->recv_ready = 0;
        rdma_post_recv(c->id, NULL, c->recv_buf, IPC_RDMA_BUF, c->recv_mr);
        return -1;
    }
    uint32_t be;
    memcpy(&be, c->recv_buf, 4);
    uint32_t mlen = ntohl(be);
    if (mlen > IPC_MAX_MSG || c->recv_len < 4 + mlen) {
        snprintf(err, err_cap, "ipc rdma: bad frame");
        c->recv_ready = 0;
        rdma_post_recv(c->id, NULL, c->recv_buf, IPC_RDMA_BUF, c->recv_mr);
        return -1;
    }
    char *msg = malloc(mlen + 1);
    if (!msg) {
        snprintf(err, err_cap, "ipc rdma: oom");
        return -1;
    }
    memcpy(msg, c->recv_buf + 4, mlen);
    msg[mlen] = 0;
    *out = msg;
    *out_len = mlen;
    c->recv_ready = 0;
    if (rdma_post_recv(c->id, NULL, c->recv_buf, IPC_RDMA_BUF, c->recv_mr) != 0) {
        c->pending_msg = msg;
        c->pending_len = mlen;
        snprintf(err, err_cap, "ipc rdma: repost recv failed");
        return -1;
    }
    return 0;
}

int ipc_rdma_has_message(IpcRdmaConn *c) {
    if (!c) return 0;
    if (c->pending_msg || c->recv_ready) return 1;
    char err[64];
    return ipc_rdma_drain_recv(c, 0, err, sizeof err) == 0;
}

void ipc_rdma_set_nonblock(IpcRdmaConn *c, int on) {
    if (c) c->nonblock = on;
}

void ipc_rdma_close(IpcRdmaConn *c) {
    if (!c) return;
    if (c->id) {
        if (!c->is_listen) rdma_disconnect(c->id);
        rdma_destroy_ep(c->id);
    }
    if (c->send_mr && c->send_mr != c->recv_mr) rdma_dereg_mr(c->send_mr);
    if (c->recv_mr) rdma_dereg_mr(c->recv_mr);
    free(c->send_buf);
    free(c->recv_buf);
    free(c->pending_msg);
    free(c);
}

#endif /* SHAKTI_HAVE_RDMA */
