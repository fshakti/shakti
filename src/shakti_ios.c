#include "shakti_ios.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Host entry for the iOS Simulator app. Not linked into .build/shakti.
 * Mirrors src/shakti_jni.c, and also returns the printed text. */

extern int shakti_lang_main(int argc, char **argv);

typedef struct {
    int fd;
    char *buf;
    int cap;
} ShaktiIosDrain;

static void shakti_ios_clear(char *buf, int cap) {
    if (buf && cap > 0) buf[0] = 0;
}

static void *shakti_ios_drain(void *arg) {
    ShaktiIosDrain *job = arg;
    int n = 0;
    char junk[1024];
    for (;;) {
        char *dst;
        size_t room;
        ssize_t got;
        if (job->buf && job->cap > 1 && n < job->cap - 1) {
            dst = job->buf + n;
            room = (size_t)(job->cap - 1 - n);
        } else {
            dst = junk;
            room = sizeof junk;
        }
        got = read(job->fd, dst, room);
        if (got <= 0) break;
        if (dst != junk) n += (int)got;
    }
    if (job->buf && job->cap > 0) {
        if (n >= job->cap) n = job->cap - 1;
        job->buf[n] = 0;
    }
    return NULL;
}

int shakti_ios_run_file(const char *path, char *out, int out_cap, char *err, int err_cap) {
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int saved_out = -1;
    int saved_err = -1;
    pthread_t out_th, err_th;
    int out_th_ok = 0, err_th_ok = 0;
    ShaktiIosDrain out_job, err_job;
    char *owned = NULL;
    char *argv0 = "shakti";
    char *argv[3];
    int rc = 2;

    shakti_ios_clear(out, out_cap);
    shakti_ios_clear(err, err_cap);
    if (!path || !path[0]) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "shakti_ios: empty path");
        return 2;
    }
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "shakti_ios: pipe: %s", strerror(errno));
        goto done;
    }
    saved_out = dup(STDOUT_FILENO);
    saved_err = dup(STDERR_FILENO);
    if (saved_out < 0 || saved_err < 0 ||
        dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
        dup2(err_pipe[1], STDERR_FILENO) < 0) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "shakti_ios: dup: %s", strerror(errno));
        goto restore;
    }
    close(out_pipe[1]);
    out_pipe[1] = -1;
    close(err_pipe[1]);
    err_pipe[1] = -1;

    out_job.fd = out_pipe[0];
    out_job.buf = out;
    out_job.cap = out_cap;
    err_job.fd = err_pipe[0];
    err_job.buf = err;
    err_job.cap = err_cap;
    if (pthread_create(&out_th, NULL, shakti_ios_drain, &out_job) == 0)
        out_th_ok = 1;
    if (pthread_create(&err_th, NULL, shakti_ios_drain, &err_job) == 0)
        err_th_ok = 1;
    if (!out_th_ok || !err_th_ok) {
        if (err && err_cap > 0 && !err_th_ok)
            snprintf(err, (size_t)err_cap, "shakti_ios: capture thread failed");
        goto restore;
    }

    owned = strdup(path);
    if (!owned) goto restore;
    argv[0] = argv0;
    argv[1] = owned;
    argv[2] = NULL;
    rc = shakti_lang_main(2, argv);

restore:
    fflush(stdout);
    fflush(stderr);
    if (saved_out >= 0) {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    if (saved_err >= 0) {
        dup2(saved_err, STDERR_FILENO);
        close(saved_err);
    }
    if (out_pipe[1] >= 0) close(out_pipe[1]);
    if (err_pipe[1] >= 0) close(err_pipe[1]);
    if (out_th_ok) pthread_join(out_th, NULL);
    if (err_th_ok) pthread_join(err_th, NULL);

done:
    if (out_pipe[0] >= 0) close(out_pipe[0]);
    if (err_pipe[0] >= 0) close(err_pipe[0]);
    free(owned);
    return rc;
}
