#ifndef SHAKTI_TLS_H
#define SHAKTI_TLS_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thin OpenSSL transport for long-lived sockets (WebSocket wss://, rest.listen_tls).
 * Opaque SSL* / SSL_CTX* are void* so callers need not include openssl headers. */

void tls_init(void);

/* SHA-1 digest (20 bytes). Returns 0 on success. */
int tls_sha1(const void *data, size_t n, unsigned char out[20]);

/* Client: wrap connected TCP fd. hostname used for SNI + verify (may be NULL if insecure).
 * insecure=1 skips certificate verification. Returns SSL* or NULL. */
void *tls_connect(int fd, const char *hostname, int insecure);

/* Server context from PEM cert + key paths. Returns SSL_CTX* or NULL. */
void *tls_server_ctx(const char *cert_path, const char *key_path);
void tls_server_ctx_free(void *ctx);

/* Server accept: TLS handshake on connected fd. Returns SSL* or NULL. */
void *tls_accept(int fd, void *server_ctx);

ssize_t tls_read(void *ssl, void *buf, size_t n);
ssize_t tls_write(void *ssl, const void *buf, size_t n);

/* Bytes buffered in SSL that can be read without blocking the fd. */
int tls_pending(void *ssl);

/* Shutdown + free SSL; does not close fd. */
void tls_free(void *ssl);

/* Shutdown + free SSL + close fd. */
void tls_close(void *ssl, int fd);

#ifdef __cplusplus
}
#endif

#endif
