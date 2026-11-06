/*
 * parallel.c - multithreaded copy of one file using pthreads
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mycp.h"

/* Progress shared by all threads */
typedef struct {
    pthread_mutex_t lock;
    off_t           done;
    off_t           total;
    int             show_progress;
} Shared;

/* One thread copies bytes [start, end) of the file */
typedef struct {
    int     in_fd;
    int     out_fd;
    off_t   start;
    off_t   end;
    size_t  buf_size;
    int     failed;
    Shared *shared;
} Chunk;

/* pwrite() may write less than asked, so loop until everything is out. */
static int pwrite_all(int fd, const char *buf, size_t n, off_t pos)
{
    size_t off = 0;

    while (off < n) {
        ssize_t w = pwrite(fd, buf + off, n - off, pos + (off_t)off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* Thread body: pread/pwrite use explicit offsets, so threads never
 * disturb each other's file position. */
static void *copy_worker(void *arg)
{
    Chunk *c   = arg;
    char  *buf = malloc(c->buf_size);
    off_t  pos = c->start;

    if (!buf) {
        c->failed = 1;
        return NULL;
    }

    while (pos < c->end) {
        off_t   left = c->end - pos;
        size_t  want = (left < (off_t)c->buf_size) ? (size_t)left : c->buf_size;
        ssize_t n    = pread(c->in_fd, buf, want, pos);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            c->failed = 1;
            break;
        }
        if (n == 0) {               /* file shrank while copying */
            c->failed = 1;
            break;
        }
        if (pwrite_all(c->out_fd, buf, (size_t)n, pos) < 0) {
            c->failed = 1;
            break;
        }
        pos += n;

        pthread_mutex_lock(&c->shared->lock);
        c->shared->done += n;
        if (c->shared->show_progress)
            print_progress(c->shared->done, c->shared->total);
        pthread_mutex_unlock(&c->shared->lock);
    }

    free(buf);
    return NULL;
}

/* Copy one regular file using opt->threads threads. 0 = ok, -1 = failed. */
int parallel_copy_file(const char *src, const char *dst, const Options *opt)
{
    struct stat src_st, dst_st;
    pthread_t   tids[MAX_THREADS];
    Chunk       chunks[MAX_THREADS];
    Shared      shared;
    int         in_fd = -1, out_fd = -1, ret = -1;
    int         nthreads, started = 0, failed = 0, mutex_ready = 0, i;
    off_t       chunk_size;
    double      start, elapsed;

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        perror(src);
        log_msg("ERROR open %s: %s", src, strerror(errno));
        return -1;
    }
    if (fstat(in_fd, &src_st) < 0) {
        perror(src);
        goto cleanup;
    }
    if (!S_ISREG(src_st.st_mode)) {
        fprintf(stderr, "%s: not a regular file\n", src);
        goto cleanup;
    }
    if (stat(dst, &dst_st) == 0 && dst_st.st_dev == src_st.st_dev &&
        dst_st.st_ino == src_st.st_ino) {
        fprintf(stderr, "'%s' and '%s' are the same file\n", src, dst);
        goto cleanup;
    }

    out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode & 0777);
    if (out_fd < 0) {
        perror(dst);
        log_msg("ERROR open %s: %s", dst, strerror(errno));
        goto cleanup;
    }
    if (ftruncate(out_fd, src_st.st_size) < 0) {   /* reserve final size */
        perror(dst);
        goto cleanup;
    }

    /* Never use more threads than bytes; split into equal chunks */
    nthreads = opt->threads;
    if (src_st.st_size < nthreads)
        nthreads = (src_st.st_size > 0) ? (int)src_st.st_size : 1;
    chunk_size = (src_st.st_size + nthreads - 1) / nthreads;

    pthread_mutex_init(&shared.lock, NULL);
    mutex_ready        = 1;
    shared.done        = 0;
    shared.total       = src_st.st_size;
    shared.show_progress = opt->show_progress;

    start = now_seconds();
    for (i = 0; i < nthreads; i++) {
        chunks[i].in_fd    = in_fd;
        chunks[i].out_fd   = out_fd;
        chunks[i].start    = (off_t)i * chunk_size;
        chunks[i].end      = chunks[i].start + chunk_size;
        if (chunks[i].end > src_st.st_size)
            chunks[i].end = src_st.st_size;
        chunks[i].buf_size = opt->buf_size;
        chunks[i].failed   = 0;
        chunks[i].shared   = &shared;

        if (chunks[i].start >= chunks[i].end)
            break;                              /* nothing left to assign */
        if (pthread_create(&tids[i], NULL, copy_worker, &chunks[i]) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            failed = 1;
            break;
        }
        started++;
    }

    for (i = 0; i < started; i++) {
        pthread_join(tids[i], NULL);
        if (chunks[i].failed)
            failed = 1;
    }
    elapsed = now_seconds() - start;

    if (failed) {
        fprintf(stderr, "%s: parallel copy failed\n", src);
        log_msg("ERROR parallel copy %s -> %s", src, dst);
        goto cleanup;
    }

    if (opt->show_progress) {
        print_progress(src_st.st_size, src_st.st_size);
        putchar('\n');
    }

    if (fchmod(out_fd, src_st.st_mode & 07777) < 0)
        perror("fchmod");

    if (close(out_fd) < 0) {
        out_fd = -1;
        perror(dst);
        goto cleanup;
    }
    out_fd = -1;

    if (opt->benchmark) {
        double mb = (double)src_st.st_size / (1024.0 * 1024.0);
        printf("Copied %.2f MB in %.4f s (%.2f MB/s), %d threads, buffer %zu bytes\n",
               mb, elapsed, elapsed > 0 ? mb / elapsed : 0.0,
               started ? started : 1, opt->buf_size);
    }

    if (opt->verify) {
        if (verify_files(src, dst) == 0) {
            printf("Verify OK: %s\n", dst);
        } else {
            fprintf(stderr, "Verify FAILED: %s differs from %s\n", dst, src);
            log_msg("VERIFY FAILED %s -> %s", src, dst);
            goto cleanup;
        }
    }

    log_msg("OK (parallel, %d threads) %s -> %s (%lld bytes)",
            started ? started : 1, src, dst, (long long)src_st.st_size);
    ret = 0;

cleanup:
    if (mutex_ready)
        pthread_mutex_destroy(&shared.lock);
    if (in_fd >= 0)
        close(in_fd);
    if (out_fd >= 0)
        close(out_fd);
    return ret;
}
