/* tls — OpenSSL transport for long-lived sockets (wss, rest.listen_tls). */
#include "tls.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

static int g_tls_inited;
static SSL_CTX *g_client_ctx;

static void tls_harden_ctx(SSL_CTX *ctx) {
    if (!ctx) return;
#ifdef TLS1_2_VERSION
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#endif
}

void tls_init(void) {
    if (g_tls_inited) return;
    g_tls_inited = 1;
    OPENSSL_init_ssl(0, NULL);
    g_client_ctx = SSL_CTX_new(TLS_client_method());
    if (g_client_ctx) {
        tls_harden_ctx(g_client_ctx);
        SSL_CTX_set_default_verify_paths(g_client_ctx);
        SSL_CTX_set_verify(g_client_ctx, SSL_VERIFY_PEER, NULL);
    }
}

int tls_sha1(const void *data, size_t n, unsigned char out[20]) {
    if (!out) return -1;
    if (!SHA1((const unsigned char *)data, n, out)) return -1;
    return 0;
}

static void tls_clear_err(void) {
    while (ERR_get_error()) {
    }
}

void *tls_connect(int fd, const char *hostname, int insecure) {
    tls_init();
    if (fd < 0 || !g_client_ctx) return NULL;
    SSL *ssl = SSL_new(g_client_ctx);
    if (!ssl) return NULL;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    if (hostname && hostname[0])
        SSL_set_tlsext_host_name(ssl, hostname);
    if (insecure) {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
        if (hostname && hostname[0])
            SSL_set1_host(ssl, hostname);
    }
    tls_clear_err();
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

void *tls_server_ctx(const char *cert_path, const char *key_path) {
    tls_init();
    if (!cert_path || !key_path || !cert_path[0] || !key_path[0]) return NULL;
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return NULL;
    tls_harden_ctx(ctx);
    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

void tls_server_ctx_free(void *ctx) {
    if (ctx) SSL_CTX_free((SSL_CTX *)ctx);
}

void *tls_accept(int fd, void *server_ctx) {
    if (fd < 0 || !server_ctx) return NULL;
    SSL *ssl = SSL_new((SSL_CTX *)server_ctx);
    if (!ssl) return NULL;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    tls_clear_err();
    if (SSL_accept(ssl) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

ssize_t tls_read(void *ssl, void *buf, size_t n) {
    if (!ssl || !buf) return -1;
    for (;;) {
        int r = SSL_read((SSL *)ssl, buf, (int)n);
        if (r > 0) return r;
        int e = SSL_get_error((SSL *)ssl, r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_SYSCALL && errno == EINTR) continue;
        return -1;
    }
}

ssize_t tls_write(void *ssl, const void *buf, size_t n) {
    if (!ssl) return -1;
    if (!buf && n) return -1;
    size_t sent = 0;
    while (sent < n) {
        int w = SSL_write((SSL *)ssl, (const char *)buf + sent, (int)(n - sent));
        if (w > 0) {
            sent += (size_t)w;
            continue;
        }
        int e = SSL_get_error((SSL *)ssl, w);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return sent > 0 ? (ssize_t)sent : -1;
        }
        if (e == SSL_ERROR_SYSCALL && errno == EINTR) continue;
        return -1;
    }
    return (ssize_t)sent;
}

int tls_pending(void *ssl) {
    if (!ssl) return 0;
    return SSL_pending((SSL *)ssl);
}

void tls_free(void *ssl) {
    if (!ssl) return;
    SSL_shutdown((SSL *)ssl);
    SSL_free((SSL *)ssl);
}

void tls_close(void *ssl, int fd) {
    tls_free(ssl);
    if (fd >= 0) close(fd);
}
