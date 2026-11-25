// SPDX-License-Identifier: Apache-2.0
#ifndef SHAKTI_TLS_H
#define SHAKTI_TLS_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thin OpenSSL transport for long-lived sockets (WebSocket wss://, rest.listen_tls).
 * Opaque SSL* / SSL_CTX* are void* so callers need not include openssl headers.
 * A peer that has closed makes a call fail (EPIPE, ECONNRESET) instead of raising SIGPIPE: on
 * macOS shakti_tls_connect and shakti_tls_accept set SO_NOSIGPIPE on the socket; elsewhere (Linux)
 * SIGPIPE is blocked in the calling thread around each call that can write. */

void shakti_tls_init(void);

/* SHA-1 digest (20 bytes). Returns 0 on success. */
int tls_sha1(const void *data, size_t n, unsigned char out[20]);

/* Client: wrap connected TCP fd. hostname used for SNI + verify (may be NULL if insecure).
 * insecure=1 skips certificate verification. Returns SSL* or NULL. */
void *shakti_tls_connect(int fd, const char *hostname, int insecure);

/* Server context from PEM cert + key paths. Returns SSL_CTX* or NULL. */
void *shakti_tls_server_ctx(const char *cert_path, const char *key_path);
void shakti_tls_server_ctx_free(void *ctx);

/* Server accept: TLS handshake on connected fd. Returns SSL* or NULL. */
void *shakti_tls_accept(int fd, void *server_ctx);
/* Like shakti_tls_accept, but the whole handshake must finish within timeout_ms. */
void *shakti_tls_accept_timeout(int fd, void *server_ctx, int timeout_ms);

ssize_t shakti_tls_read(void *ssl, void *buf, size_t n);
/* -1 on failure, with errno EAGAIN (would block), the socket's error, or EIO (a TLS failure that
 * left no socket error). */
ssize_t shakti_tls_write(void *ssl, const void *buf, size_t n);

/* Bytes buffered in SSL that can be read without blocking the fd. */
int shakti_tls_pending(void *ssl);

/* Shutdown + free SSL; does not close fd. */
void shakti_tls_free(void *ssl);

/* Shutdown + free SSL + close fd. */
void shakti_tls_close(void *ssl, int fd);

#ifdef __cplusplus
}
#endif

#endif
