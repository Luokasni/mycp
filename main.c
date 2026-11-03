#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mycp.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <source> <destination>\n"
        "  -b <bytes>  buffer size (default %d)\n"
        "  -i          show source file info\n"
        "  -p          show progress bar\n"
        "  -r          recursive directory copy\n"
        "  -c          verify copy after finishing\n"
        "  -R          resume an interrupted copy\n"
        "  -t          show time and speed (benchmark)\n"
        "  -l <file>   write a log file\n"
        "  -h          this help\n",
        prog, DEFAULT_BUF_SIZE);
}

/* Build "<dir>/<basename of src>" into out. */
static int build_target(const char *dir, const char *src, char *out, size_t n)
{
    char        tmp[PATH_MAX_LEN];
    const char *base;
    size_t      len;

    snprintf(tmp, sizeof tmp, "%s", src);
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    base = strrchr(tmp, '/');
    base = base ? base + 1 : tmp;
    return snprintf(out, n, "%s/%s", dir, base) >= (int)n ? -1 : 0;
}

int main(int argc, char *argv[])
{
    Options     opt = { DEFAULT_BUF_SIZE, 0, 0, 0, 0, 0, 0 };
    const char *log_file = NULL;
    const char *src, *dst;
    struct stat src_st, dst_st;
    char        target[PATH_MAX_LEN];
    int         c, rc;

    while ((c = getopt(argc, argv, "b:ipcrRtl:h")) != -1) {
        switch (c) {
        case 'b': {
            char *end;
            unsigned long v = strtoul(optarg, &end, 10);
            if (*end != '\0' || v == 0 || v > MAX_BUF_SIZE) {
                fprintf(stderr, "Invalid buffer size: %s\n", optarg);
                return 1;
            }
            opt.buf_size = v;
            break;
        }
        case 'i': opt.show_info = 1;     break;
        case 'p': opt.show_progress = 1; break;
        case 'c': opt.verify = 1;        break;
        case 'r': opt.recursive = 1;     break;
        case 'R': opt.resume = 1;        break;
        case 't': opt.benchmark = 1;     break;
        case 'l': log_file = optarg;     break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (argc - optind != 2) {
        usage(argv[0]);
        return 1;
    }
    src = argv[optind];
    dst = argv[optind + 1];

    if (log_file && log_open(log_file) < 0)
        perror(log_file);

    if (stat(src, &src_st) < 0) {
        perror(src);
        log_close();
        return 1;
    }

    if (S_ISDIR(src_st.st_mode)) {
        if (!opt.recursive) {
            fprintf(stderr, "%s is a directory (use -r)\n", src);
            log_close();
            return 1;
        }
        rc = copy_directory(src, dst, &opt);
    } else {
        /* if destination is an existing directory, copy into it */
        if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
            if (build_target(dst, src, target, sizeof target) < 0) {
                fprintf(stderr, "path too long\n");
                log_close();
                return 1;
            }
        } else {
            snprintf(target, sizeof target, "%s", dst);
        }
        if (opt.show_info)
            print_file_info(src, &src_st);
        rc = copy_file(src, target, &opt);
    }

    log_close();
    return rc == 0 ? 0 : 1;
}
