#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mycp.h"

#define BAR_WIDTH   30
#define VERIFY_SIZE (64 * 1024)

/* Read until n bytes are read or EOF. Returns bytes read, -1 on error. */
static ssize_t read_full(int fd, char *buf, size_t n)
{
    size_t total = 0;

    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;
        total += (size_t)r;
    }
    return (ssize_t)total;
}

/* Compare two files byte by byte. Returns 0 if identical, -1 otherwise. */
int verify_files(const char *path_a, const char *path_b)
{
    int   fa = open(path_a, O_RDONLY);
    int   fb = open(path_b, O_RDONLY);
    char *ba = malloc(VERIFY_SIZE);
    char *bb = malloc(VERIFY_SIZE);
    int   result = -1;

    if (fa < 0 || fb < 0 || !ba || !bb)
        goto done;

    for (;;) {
        ssize_t na = read_full(fa, ba, VERIFY_SIZE);
        ssize_t nb = read_full(fb, bb, VERIFY_SIZE);

        if (na < 0 || nb < 0 || na != nb || memcmp(ba, bb, (size_t)na) != 0)
            goto done;
        if (na == 0)
            break;
    }
    result = 0;

done:
    if (fa >= 0) close(fa);
    if (fb >= 0) close(fb);
    free(ba);
    free(bb);
    return result;
}

/* Level 3: show file metadata. */
void print_file_info(const char *path, const struct stat *st)
{
    printf("---- File info: %s ----\n", path);
    printf("Size        : %lld bytes\n", (long long)st->st_size);
    printf("Permissions : %04o\n", (unsigned)(st->st_mode & 07777));
    printf("Owner UID   : %u\n", (unsigned)st->st_uid);
    printf("Modified    : %s", ctime(&st->st_mtime));
    printf("----------------------------\n");
}

/* Level 4: [######----] 60% */
void print_progress(off_t done, off_t total)
{
    int pct = (total > 0) ? (int)((double)done * 100.0 / (double)total) : 100;
    int filled, i;

    if (pct > 100)
        pct = 100;
    filled = pct * BAR_WIDTH / 100;

    printf("\r[");
    for (i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : '-');
    printf("] %3d%%", pct);
    fflush(stdout);
}

double now_seconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
