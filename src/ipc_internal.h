#ifndef SHAKTI_IPC_INTERNAL_H
#define SHAKTI_IPC_INTERNAL_H

#include "ipc.h"
#include "ipc_rdma.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/types.h>
#endif
#if defined(__APPLE__)
#include <sys/posix_shm.h>
#endif
#if defined(__linux__) && !defined(SHAKTI_WASM)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#define IPC_MAX_HANDLES 128
#define IPC_INBOX_MAX 64
#define IPC_REPLY_TO_MAX 32
#define IPC_ENV_MAGIC 0x49u
#define IPC_ENV_ASYNC 1u
#define IPC_ENV_REQ 2u
#define IPC_ENV_REP 3u
#define IPC_ENV_HDR 8u
#define IPC_REPLY_INFO 19u
#define IPC_REPLY_COOKIE 8u
#define IPC_SHM_MAGIC 0x31435053u
#define IPC_SHM_MAGIC_BCAST 0x434D5053u
#define IPC_SHM_VERSION 1u
#define IPC_SHM_HDR 256u
#define IPC_SHM_FLAG_BCAST 1u
#define IPC_BCAST_MAX_READERS 16

typedef enum {
    IPC_KIND_NONE = 0,
    IPC_KIND_SOCK_LISTEN,
    IPC_KIND_SOCK_CONN,
    IPC_KIND_MCAST,
    IPC_KIND_SHM_CHAN,
#ifdef SHAKTI_HAVE_RDMA
    IPC_KIND_RDMA_LISTEN,
    IPC_KIND_RDMA_CONN,
#endif
} IpcKind;

typedef struct {
    unsigned char *data;
    size_t cap;
    size_t len;
    int have_len;
    uint32_t msg_len;
} IpcRxBuf;

typedef struct {
    unsigned char *data;
    size_t len;
    uint8_t kind;
    uint32_t corr_id;
} IpcPendingMsg;

typedef struct {
    IpcPendingMsg msgs[IPC_INBOX_MAX];
    int n;
} IpcInbox;

typedef struct {
    int in_use;
    uint32_t corr;
    int af;
    uint16_t port;
    uint8_t addr[16];
    uint8_t cookie[8];
} IpcReplyTo;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t capacity;
    uint32_t total_size;
    atomic_uint head0;
    atomic_uint tail0;
    atomic_uint gen0;
    atomic_uint head1;
    atomic_uint tail1;
    atomic_uint gen1;
    atomic_uint side_b_claimed;
    uint8_t pad[IPC_SHM_HDR - 44];
} IpcShmHdr;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t capacity;
    uint32_t total_size;
    uint32_t max_readers;
    uint32_t _pad0;
    atomic_uint head;
    atomic_uint gen;
    uint8_t pad[IPC_SHM_HDR - 32];
} IpcShmBcastHdr;

typedef struct {
    int in_use;
    int closed;
    IpcKind kind;
    int fd;
    int nonblock;
    char uds_path[108];
    IpcRxBuf rx;
    IpcInbox inbox;
    uint32_t next_corr;
    int mcast_af;
    struct sockaddr_storage mcast_addr;
    socklen_t mcast_addrlen;
    int mcast_addr_ok;
    int reply_fd;
    uint16_t reply_port;
    IpcReplyTo reply_to[IPC_REPLY_TO_MAX];
    uint8_t sync_cookie[8];
    uint32_t sync_corr;
    int sync_cookie_set;
    char ssm_source[64];
    void *shm_ptr;
    size_t shm_size;
    char shm_name[256];
    int shm_owner;
    int shm_side;
    int shm_bcast;
#ifdef SHAKTI_HAVE_RDMA
    IpcRdmaConn *rdma;
#endif
} IpcHandle;

typedef struct {
    int in_use;
    void *ptr;
    size_t size;
    char name[256];
} IpcShmSlot;

int ipc_copy_cstr(char *dst, size_t cap, const char *src);
uint32_t ipc_rand_corr_seed(void);
int ipc_valid_shm_user_name(const char *s);
size_t ipc_posix_shm_name_max(void);
int ipc_format_posix_shm_name(char *dst, size_t cap, const char *prefix, const char *user);
IpcHandle *ipc_slot(int h);
IpcHandle *ipc_handle_at(int h);
int ipc_alloc(void);
void ipc_free_handle(int h);

void ipc_rx_free(IpcRxBuf *rx);
void ipc_inbox_free(IpcInbox *box);
int ipc_inbox_push(IpcInbox *box, uint8_t kind, uint32_t corr, const unsigned char *data, size_t len);
int ipc_inbox_take(IpcInbox *box, int want_corr, uint32_t corr,
                   uint8_t *kind, uint32_t *corr_out,
                   unsigned char **data, size_t *len);

int sock_set_block(int fd, int block);
int ipc_is_localhost(const char *host);
IpcTransport ipc_parse_transport(const char *s);
int ipc_sock_send(IpcHandle *s, const char *data, size_t len, char *err, size_t err_cap);
int ipc_sock_send_parts(IpcHandle *s,
                        const void *p0, size_t n0,
                        const void *p1, size_t n1,
                        const void *p2, size_t n2,
                        char *err, size_t err_cap);
int ipc_sock_recv_msg(IpcHandle *s, int block, char **out, size_t *out_len, char *err, size_t err_cap);

int ipc_mcast_make_reply_sock(int af, uint16_t *port_out, char *err, size_t err_cap);
int ipc_mcast_open(const char *group, int port, const char *iface, const char *source,
                   int *af_out, struct sockaddr_storage *grp_out, socklen_t *grp_len_out,
                   char *err, size_t err_cap);
int ipc_mcast_publish_on(IpcHandle *s, int fd, const unsigned char *data, size_t len,
                         char *err, size_t err_cap);
int ipc_mcast_publish(IpcHandle *s, const unsigned char *data, size_t len, char *err, size_t err_cap);
int ipc_mcast_recv(IpcHandle *s, int block, char **out, size_t *out_len,
                   struct sockaddr_storage *peer, socklen_t *peer_len,
                   char *err, size_t err_cap);
int ipc_reply_to_store(IpcHandle *s, uint32_t corr, int af, uint16_t port,
                       const uint8_t addr[16], const uint8_t cookie[8]);
int ipc_reply_to_store_peer(IpcHandle *s, uint32_t corr, const struct sockaddr_storage *peer,
                            const uint8_t cookie[8]);
int ipc_reply_to_take(IpcHandle *s, uint32_t corr, int *af, uint16_t *port, uint8_t addr[16],
                      uint8_t cookie[8]);
void ipc_pack_reply_info(unsigned char *dst, int af, uint16_t port, const void *addr);
int ipc_parse_reply_info(const unsigned char *src, int *af, uint16_t *port, uint8_t addr[16]);

int ipc_shm_push(IpcShmHdr *hdr, int which, const void *data, uint32_t len, char *err, size_t err_cap);
int ipc_shm_pop(IpcShmHdr *hdr, int which, int block, int timeout_ms,
                char **out, size_t *out_len, char *err, size_t err_cap);
int ipc_shm_send_side(IpcHandle *s, const void *data, size_t len, char *err, size_t err_cap);
int ipc_shm_recv_side(IpcHandle *s, int block, int timeout_ms,
                      char **out, size_t *out_len, char *err, size_t err_cap);
int ipc_shm_chan_open(const char *user_name, size_t size, int create,
                      void **ptr_out, size_t *size_out, char *posix_out, size_t posix_cap,
                      int *owner, char *err, size_t err_cap);
int ipc_shm_readable(IpcHandle *s);
size_t ipc_bcast_total_size(size_t ring_cap, uint32_t max_readers);

void ipc_env_pack(unsigned char hdr[IPC_ENV_HDR], uint8_t kind, uint32_t corr);
int ipc_env_parse(const unsigned char *buf, size_t len, uint8_t *kind, uint32_t *corr,
                  const unsigned char **payload, size_t *payload_len);
V *ipc_msg_dict(uint8_t kind, uint32_t corr, const unsigned char *data, size_t len, int as_bin);
int ipc_payload_bytes(V *v, const unsigned char **out, size_t *out_len);
V *ipc_u8vec_from_raw(const unsigned char *p, size_t n);
V *ipc_u8vec_take(unsigned char *p, size_t n);
int ipc_raw_send(IpcHandle *s, const unsigned char *data, size_t len, char *err, size_t err_cap);
int ipc_raw_recv(IpcHandle *s, int block, int timeout_ms,
                 char **out, size_t *out_len, char *err, size_t err_cap);
int ipc_env_send_ex(IpcHandle *s, uint8_t kind, uint32_t corr,
                    const unsigned char *extra, size_t elen,
                    const unsigned char *payload, size_t plen, char *err, size_t err_cap);
int ipc_env_send(IpcHandle *s, uint8_t kind, uint32_t corr,
                 const unsigned char *payload, size_t plen, char *err, size_t err_cap);
int ipc_env_recv(IpcHandle *s, int block, int timeout_ms, int want_corr, uint32_t corr,
                 uint8_t *kind, uint32_t *corr_out,
                 unsigned char **data, size_t *len, char *err, size_t err_cap);
int ipc_env_recv_ex(IpcHandle *s, int block, int timeout_ms, int want_corr, uint32_t corr,
                    int use_inbox,
                    uint8_t *kind, uint32_t *corr_out,
                    unsigned char **data, size_t *len, char *err, size_t err_cap);

#endif
