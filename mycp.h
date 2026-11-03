#ifndef MYCP_H
#define MYCP_H

#include <sys/stat.h>

#define DEFAULT_BUF_SIZE 4096
#define MAX_BUF_SIZE     (64UL * 1024 * 1024)
#define PATH_MAX_LEN     4096

/* Runtime options set from the command line */
typedef struct {
    size_t buf_size;      /* -b : buffer size in bytes        */
    int    show_info;     /* -i : print source file metadata  */
    int    show_progress; /* -p : progress bar                */
    int    recursive;     /* -r : recursive directory copy    */
    int    verify;        /* -c : verify copy after finishing */
    int    resume;        /* -R : resume interrupted copy     */
    int    benchmark;     /* -t : print time and speed        */
} Options;

/* copy.c */
int  copy_file(const char *src, const char *dst, const Options *opt);

/* dir.c */
int  copy_directory(const char *src, const char *dst, const Options *opt);

/* utils.c */
int    verify_files(const char *path_a, const char *path_b);
void   print_file_info(const char *path, const struct stat *st);
void   print_progress(off_t done, off_t total);
double now_seconds(void);

/* log.c */
int  log_open(const char *path);
void log_msg(const char *fmt, ...);
void log_close(void);

#endif
