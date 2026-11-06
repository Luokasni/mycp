/*
 * mycp.h - shared types and function declarations for mycp
 */
#ifndef MYCP_H
#define MYCP_H

#include <stddef.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_BUF_SIZE 4096
#define MAX_BUF_SIZE     (64UL * 1024 * 1024)
#define PATH_MAX_LEN     4096
#define MAX_THREADS      64

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime options set from the command line */
typedef struct {
    size_t buf_size;      /* -b : buffer size in bytes        */
    int    show_info;     /* -i : print source file metadata  */
    int    show_progress; /* -p : progress bar                */
    int    recursive;     /* -r : recursive directory copy    */
    int    verify;        /* -c : verify copy after finishing */
    int    resume;        /* -R : resume interrupted copy     */
    int    benchmark;     /* -t : print time and speed        */
    int    threads;       /* -j : number of threads (1 = off) */
    int    sync;          /* -s : skip files that are up to date */
} Options;

/* copy.c */
int  copy_file(const char *src, const char *dst, const Options *opt);

/* parallel.c */
int  parallel_copy_file(const char *src, const char *dst, const Options *opt);

/* dir.c */
int  copy_directory(const char *src, const char *dst, const Options *opt);

/* utils.c */
int    verify_files(const char *path_a, const char *path_b);
void   print_file_info(const char *path, const struct stat *st);
void   print_progress(off_t done, off_t total);
int    is_up_to_date(off_t src_size, time_t src_mtime, const char *dst);
double now_seconds(void);

/* log.c */
int  log_open(const char *path);
void log_msg(const char *fmt, ...);
void log_close(void);

/* One queue node: a file or directory waiting to be processed */
typedef struct Task {
    char        *src;
    char        *dst;
    off_t        size;
    time_t       mtime;
    mode_t       mode;
    struct Task *next;
} Task;

/* FIFO queue implemented as a singly linked list */
typedef struct {
    Task   *head;
    Task   *tail;
    size_t  count;
} Queue;

/* Totals printed after a directory copy */
typedef struct {
    size_t copied;
    size_t skipped;
    size_t failed;
    off_t  bytes;
} Stats;

/* queue.c */
void  queue_init(Queue *q);
int   queue_push(Queue *q, const char *src, const char *dst,
                 off_t size, time_t mtime, mode_t mode);
Task *queue_pop(Queue *q);
void  task_free(Task *t);
void  queue_clear(Queue *q);

#ifdef __cplusplus
}
#endif

#endif
