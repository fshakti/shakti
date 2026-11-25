// SPDX-License-Identifier: Apache-2.0
/* tls — OpenSSL transport for long-lived sockets (wss, rest.listen_tls). */
#include "tls.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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

void shakti_tls_init(void) {
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

/* OpenSSL writes to the socket with write(), and a write to a peer that has closed raises SIGPIPE,
 * which ends the process; SSL_write can't pass MSG_NOSIGNAL. Where the system has SO_NOSIGPIPE
 * (macOS), shakti_tls_connect and shakti_tls_accept_timeout set it on the socket, and such a write
 * fails with EPIPE. Where it doesn't (Linux), SIGPIPE is blocked in this thread around each OpenSSL
 * call that can write, and one raised meanwhile is taken off before it is unblocked, so the call
 * fails with EPIPE there too; Linux sends a write's SIGPIPE to the thread that wrote. (One another
 * program sends to the process while such a call itself waits for the peer, as ws.connect's
 * handshake does on its blocking socket, is taken off too. rest.read and rest.accept over TLS wait
 * in poll(), outside these calls, so one sent then still ends the process, as does one sent during
 * an SSL_write that succeeds.) A SIGPIPE already pending is left alone; one can be pending only if
 * the thread had SIGPIPE blocked already, as an unblocked one is taken at once. (On macOS the
 * signal goes to the process, so blocking it in one thread doesn't stop it when another thread
 * exists; SO_NOSIGPIPE does.) */
typedef struct {
#ifndef SO_NOSIGPIPE
    sigset_t old;
    int was_pending;
#else
    int unused;
#endif
} TlsSigpipe;

/* 0 if the socket can't take SO_NOSIGPIPE: macOS refuses it (EINVAL) once the socket can neither
 * send nor receive, as after the peer's reset. A handshake there can only fail, so it fails at
 * once, before OpenSSL writes to the socket and raises SIGPIPE. */
static int tls_set_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    return setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) == 0;
#else
    (void)fd;
    return 1;
#endif
}

static void tls_sigpipe_hold(TlsSigpipe *h) {
#ifndef SO_NOSIGPIPE
    sigset_t sp, pend;
    sigemptyset(&sp);
    sigaddset(&sp, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sp, &h->old);
    h->was_pending = sigismember(&h->old, SIGPIPE) && sigpending(&pend) == 0 &&
                     sigismember(&pend, SIGPIPE);
#else
    (void)h;
#endif
}

/* raised: 0 when the call can't have raised SIGPIPE, which spares a system call. After a write
 * fails with EPIPE every later write to the socket fails too, so an SSL_write that succeeded raised
 * none: its own record is the last thing it writes. Other calls can succeed after such a failure
 * (SSL_accept ignores a TLS 1.3 session ticket the client has closed before), so they pass 1. */
static void tls_sigpipe_release(TlsSigpipe *h, int raised) {
#ifndef SO_NOSIGPIPE
    int e = errno;
    if (raised && !h->was_pending) {
        /* takes a pending SIGPIPE off; with no time to wait, it never blocks, even if another
         * thread has taken the signal */
        sigset_t sp;
        struct timespec now = {0, 0};
        sigemptyset(&sp);
        sigaddset(&sp, SIGPIPE);
        sigtimedwait(&sp, NULL, &now);
    }
    pthread_sigmask(SIG_SETMASK, &h->old, NULL);
    errno = e;
#else
    (void)h;
    (void)raised;
#endif
}

/* OpenSSL's error queue is per thread and SSL_get_error reads it first, so it is cleared before
 * each read, write and shutdown, and before a handshake (before the accept loop, whose would-block
 * retries leave nothing there): an error another connection left there would otherwise turn this
 * one's would-block into a failure. */
static void tls_clear_err(void) {
    while (ERR_get_error()) {
    }
}

static long tls_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int tls_is_ip_literal(const char *s) {
    unsigned char buf[16];
    return inet_pton(AF_INET, s, buf) == 1 || inet_pton(AF_INET6, s, buf) == 1;
}

void *shakti_tls_connect(int fd, const char *hostname, int insecure) {
    shakti_tls_init();
    if (fd < 0 || !g_client_ctx) return NULL;
    if (!tls_set_nosigpipe(fd)) return NULL;
    SSL *ssl = SSL_new(g_client_ctx);
    if (!ssl) return NULL;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    int ip = hostname && hostname[0] && tls_is_ip_literal(hostname);
    if (hostname && hostname[0] && !ip)
        SSL_set_tlsext_host_name(ssl, hostname);
    if (insecure) {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
        int ok;
        if (!hostname || !hostname[0])
            ok = 0;
        else if (ip)
            ok = X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), hostname) == 1;
        else
            /* SSL_set1_host is deprecated in OpenSSL 4; setting the expected
             * DNS name on the verify param directly is its equivalent and
             * is available in every supported OpenSSL/LibreSSL. */
            ok = X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), hostname, 0) == 1;
        if (!ok) {
            SSL_free(ssl);
            return NULL;
        }
    }
    tls_clear_err();
    TlsSigpipe hold;
    tls_sigpipe_hold(&hold);
    int ok = SSL_connect(ssl) == 1;
    tls_sigpipe_release(&hold, 1);
    if (!ok) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

void *shakti_tls_server_ctx(const char *cert_path, const char *key_path) {
    shakti_tls_init();
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

void shakti_tls_server_ctx_free(void *ctx) {
    if (ctx) SSL_CTX_free((SSL_CTX *)ctx);
}

void *shakti_tls_accept_timeout(int fd, void *server_ctx, int timeout_ms) {
    if (fd < 0 || !server_ctx || timeout_ms <= 0) return NULL;
    if (!tls_set_nosigpipe(fd)) return NULL;
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return NULL;
    SSL *ssl = SSL_new((SSL_CTX *)server_ctx);
    if (!ssl || SSL_set_fd(ssl, fd) != 1) {
        if (ssl) SSL_free(ssl);
        fcntl(fd, F_SETFL, fl);
        return NULL;
    }
    long deadline = tls_now_ms() + timeout_ms;
    int ok = 0;
    tls_clear_err();
    for (;;) {
        TlsSigpipe hold;
        tls_sigpipe_hold(&hold);
        int r = SSL_accept(ssl);
        tls_sigpipe_release(&hold, 1);
        if (r == 1) {
            ok = 1;
            break;
        }
        int e = SSL_get_error(ssl, r);
        short ev;
        if (e == SSL_ERROR_WANT_READ) ev = POLLIN;
        else if (e == SSL_ERROR_WANT_WRITE) ev = POLLOUT;
        else if (e == SSL_ERROR_SYSCALL && errno == EINTR) continue;
        else break;
        long left = deadline - tls_now_ms();
        if (left <= 0) break;
        struct pollfd pfd = {.fd = fd, .events = ev, .revents = 0};
        int pr = poll(&pfd, 1, (int)left);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) break;
    }
    fcntl(fd, F_SETFL, fl);
    if (!ok) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

void *shakti_tls_accept(int fd, void *server_ctx) {
    return shakti_tls_accept_timeout(fd, server_ctx, 5000);
}

ssize_t shakti_tls_read(void *ssl, void *buf, size_t n) {
    if (!ssl || !buf) return -1;
    for (;;) {
        /* Bytes already decrypted are copied out without touching the socket. */
        int r;
        tls_clear_err();
        if (SSL_pending((SSL *)ssl) > 0) {
            r = SSL_read((SSL *)ssl, buf, (int)n);
        } else {
            TlsSigpipe hold;
            tls_sigpipe_hold(&hold);
            r = SSL_read((SSL *)ssl, buf, (int)n);
            tls_sigpipe_release(&hold, 1);
        }
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

ssize_t shakti_tls_write(void *ssl, const void *buf, size_t n) {
    if (!ssl) return -1;
    if (!buf && n) return -1;
    size_t sent = 0;
    while (sent < n) {
        TlsSigpipe hold;
        tls_clear_err();
        tls_sigpipe_hold(&hold);
        int w = SSL_write((SSL *)ssl, (const char *)buf + sent, (int)(n - sent));
        tls_sigpipe_release(&hold, w <= 0);
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
        /* errno is the socket's error, or EIO for a TLS failure that left none (as on a connection
         * a write has already failed on, where OpenSSL refuses without writing). */
        if (e != SSL_ERROR_SYSCALL || errno == 0) errno = EIO;
        return -1;
    }
    return (ssize_t)sent;
}

int shakti_tls_pending(void *ssl) {
    if (!ssl) return 0;
    return SSL_pending((SSL *)ssl);
}

void shakti_tls_free(void *ssl) {
    if (!ssl) return;
    TlsSigpipe hold;
    tls_clear_err();
    tls_sigpipe_hold(&hold);
    SSL_shutdown((SSL *)ssl);
    tls_sigpipe_release(&hold, 1);
    SSL_free((SSL *)ssl);
}

void shakti_tls_close(void *ssl, int fd) {
    shakti_tls_free(ssl);
    if (fd >= 0) close(fd);
}
