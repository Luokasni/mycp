#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mycp.h" 

/* write() may write less than asked, so loop until everything is out. */
static int write_all(int fd, const char *buf, size_t n)
{
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* Copy one regular file. Returns 0 on success, -1 on failure. */
int copy_file(const char *src, const char *dst, const Options *opt)
{
    struct stat src_st, dst_st;
    int    in_fd = -1, out_fd = -1, ret = -1;
    int    dst_exists, flags;
    char  *buf = NULL;
    off_t  offset = 0, copied;
    double start, elapsed;

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

    dst_exists = (stat(dst, &dst_st) == 0);
    if (dst_exists && dst_st.st_dev == src_st.st_dev &&
        dst_st.st_ino == src_st.st_ino) {
        fprintf(stderr, "'%s' and '%s' are the same file\n", src, dst);
        goto cleanup;
    }

    /* Level 6 (resume): continue from the size of the partial destination */
    flags = O_WRONLY | O_CREAT;
    if (opt->resume && dst_exists && S_ISREG(dst_st.st_mode) &&
        dst_st.st_size <= src_st.st_size)
        offset = dst_st.st_size;
    else
        flags |= O_TRUNC;

    out_fd = open(dst, flags, src_st.st_mode & 0777);
    if (out_fd < 0) {
        perror(dst);
        log_msg("ERROR open %s: %s", dst, strerror(errno));
        goto cleanup;
    }

    if (offset > 0) {
        if (lseek(in_fd, offset, SEEK_SET) < 0 ||
            lseek(out_fd, offset, SEEK_SET) < 0) {
            perror("lseek");
            goto cleanup;
        }
        printf("Resuming %s from byte %lld\n", dst, (long long)offset);
    }

    buf = malloc(opt->buf_size);
    if (!buf) {
        perror("malloc");
        goto cleanup;
    }

    /* Level 1 + 2: read/write loop with configurable buffer */
    copied = offset;
    start  = now_seconds();
    for (;;) {
        ssize_t n = read(in_fd, buf, opt->buf_size);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror(src);
            log_msg("ERROR read %s: %s", src, strerror(errno));
            goto cleanup;
        }
        if (n == 0)
            break;
        if (write_all(out_fd, buf, (size_t)n) < 0) {
            perror(dst);
            log_msg("ERROR write %s: %s", dst, strerror(errno));
            goto cleanup;
        }
        copied += n;
        if (opt->show_progress)
            print_progress(copied, src_st.st_size);
    }
    if (opt->show_progress) {
        print_progress(copied, src_st.st_size);
        putchar('\n');
    }
    elapsed = now_seconds() - start;

    /* Level 5: preserve permissions */
    if (fchmod(out_fd, src_st.st_mode & 07777) < 0)
        perror("fchmod");

    if (close(out_fd) < 0) {
        out_fd = -1;
        perror(dst);
        goto cleanup;
    }
    out_fd = -1;

    if (opt->benchmark) {
        double mb = (double)(copied - offset) / (1024.0 * 1024.0);
        printf("Copied %.2f MB in %.4f s (%.2f MB/s), buffer %zu bytes\n",
               mb, elapsed, elapsed > 0 ? mb / elapsed : 0.0, opt->buf_size);
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

    log_msg("OK %s -> %s (%lld bytes)", src, dst, (long long)copied);
    ret = 0;

cleanup:
    free(buf);
    if (in_fd >= 0)
        close(in_fd);
    if (out_fd >= 0)
        close(out_fd);
    return ret;
}
