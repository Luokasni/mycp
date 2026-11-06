/*
 * test_mycp.c - plain C unit tests for mycp (no external framework)
 *
 * Build and run:   make test        (or: make tests/test_mycp && ./tests/test_mycp)
 * Show the utility's own output:   MYCP_TEST_VERBOSE=1 ./tests/test_mycp
 *
 * Every test runs in its own temporary directory under /tmp, which is
 * removed afterwards. Exit code is 0 only if every test passes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include "mycp.h"

/* ------------------------------------------------------------------ */
/* Tiny test framework                                                */
/* ------------------------------------------------------------------ */

static FILE *out;            /* where test results are printed          */
static int   tests_run;
static int   tests_failed;
static int   cur_failed;     /* failed checks inside the current test   */
static char  tmp_root[PATH_MAX_LEN];

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(out, "    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fflush(out);                                                 \
            cur_failed++;                                                \
        }                                                                \
    } while (0)

#define RUN(fn)                                                          \
    do {                                                                 \
        cur_failed = 0;                                                  \
        setup();                                                         \
        fn();                                                            \
        teardown();                                                      \
        tests_run++;                                                     \
        if (cur_failed) {                                                \
            tests_failed++;                                              \
            fprintf(out, "[FAIL] %s\n", #fn);                            \
        } else {                                                         \
            fprintf(out, "[ OK ] %s\n", #fn);                            \
        }                                                                \
        fflush(out);                                                     \
    } while (0)

/* Send the utility's own printf/perror output to /dev/null so that the
 * test report stays readable. */
static void silence(void)
{
    int nul;

    fflush(stdout);
    out = fdopen(dup(STDOUT_FILENO), "w");
    nul = open("/dev/null", O_WRONLY);
    if (nul >= 0) {
        dup2(nul, STDOUT_FILENO);
        dup2(nul, STDERR_FILENO);
        close(nul);
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int rm_cb(const char *path, const struct stat *sb, int flag, struct FTW *f)
{
    (void)sb;
    (void)flag;
    (void)f;
    return remove(path);
}

static void setup(void)
{
    snprintf(tmp_root, sizeof tmp_root, "/tmp/mycp_test_XXXXXX");
    if (!mkdtemp(tmp_root)) {
        perror("mkdtemp");
        exit(2);
    }
}

static void teardown(void)
{
    (void)nftw(tmp_root, rm_cb, 16, FTW_DEPTH | FTW_PHYS);
}

/* Path inside the temp directory (rotating static buffers). */
static char *P(const char *name)
{
    static char bufs[16][PATH_MAX_LEN];
    static int  next;
    char       *b = bufs[next++ % 16];

    if (snprintf(b, PATH_MAX_LEN, "%s/%s", tmp_root, name) >= PATH_MAX_LEN)
        b[0] = '\0';
    return b;
}

/* Deterministic test data. */
static unsigned char *make_data(size_t n, unsigned char salt)
{
    unsigned char *b = malloc(n ? n : 1);
    size_t         i;

    for (i = 0; i < n; i++)
        b[i] = (unsigned char)((i * 31 + 7 + salt) & 0xFF);
    return b;
}

static int write_file(const char *path, const void *data, size_t n)
{
    int    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t off = 0;

    if (fd < 0)
        return -1;
    while (off < n) {
        ssize_t w = write(fd, (const char *)data + off, n - off);
        if (w < 0) {
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

/* Read a whole file. The buffer is NUL-terminated for convenience. */
static unsigned char *read_file(const char *path, size_t *len)
{
    struct stat    st;
    unsigned char *buf;
    int            fd;
    size_t         off = 0;

    if (stat(path, &st) < 0)
        return NULL;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    buf = malloc((size_t)st.st_size + 1);
    while (off < (size_t)st.st_size) {
        ssize_t r = read(fd, buf + off, (size_t)st.st_size - off);
        if (r <= 0)
            break;
        off += (size_t)r;
    }
    close(fd);
    buf[off] = '\0';
    if (len)
        *len = off;
    return buf;
}

static int make_pattern_file(const char *path, size_t n)
{
    unsigned char *d = make_data(n, 0);
    int            r = write_file(path, d, n);

    free(d);
    return r;
}

/* 1 if both files exist and have identical content. */
static int same_content(const char *a, const char *b)
{
    size_t         la = 0, lb = 0;
    unsigned char *da = read_file(a, &la);
    unsigned char *db = read_file(b, &lb);
    int            same = da && db && la == lb && memcmp(da, db, la) == 0;

    free(da);
    free(db);
    return same;
}

static mode_t mode_of(const char *path)
{
    struct stat st;

    if (stat(path, &st) < 0)
        return (mode_t)-1;
    return st.st_mode & 07777;
}

static int exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

static Options base_opts(void)
{
    Options o = { DEFAULT_BUF_SIZE, 0, 0, 0, 0, 0, 0, 1, 0 };

    return o;
}

/* ------------------------------------------------------------------ */
/* queue.c                                                            */
/* ------------------------------------------------------------------ */

static void test_queue_fifo_order(void)
{
    Queue q;
    Task *t;

    queue_init(&q);
    CHECK(q.count == 0 && q.head == NULL && q.tail == NULL);

    CHECK(queue_push(&q, "a", "A", 1, 10, 0644) == 0);
    CHECK(queue_push(&q, "b", "B", 2, 20, 0600) == 0);
    CHECK(queue_push(&q, "c", "C", 3, 30, 0755) == 0);
    CHECK(q.count == 3);

    t = queue_pop(&q);
    CHECK(t && strcmp(t->src, "a") == 0 && strcmp(t->dst, "A") == 0);
    CHECK(t && t->size == 1 && t->mtime == 10 && t->mode == 0644);
    task_free(t);

    t = queue_pop(&q);
    CHECK(t && strcmp(t->src, "b") == 0 && t->size == 2 && t->mode == 0600);
    task_free(t);

    t = queue_pop(&q);
    CHECK(t && strcmp(t->src, "c") == 0 && t->mtime == 30);
    task_free(t);

    CHECK(q.count == 0);
    CHECK(queue_pop(&q) == NULL);
}

static void test_queue_pop_empty(void)
{
    Queue q;

    queue_init(&q);
    CHECK(queue_pop(&q) == NULL);
    CHECK(q.count == 0);
    task_free(NULL);            /* must not crash */
}

static void test_queue_reuse_after_empty(void)
{
    Queue q;
    Task *t;

    queue_init(&q);
    queue_push(&q, "x", "X", 0, 0, 0);
    t = queue_pop(&q);
    task_free(t);
    CHECK(q.head == NULL && q.tail == NULL);

    CHECK(queue_push(&q, "y", "Y", 0, 0, 0) == 0);
    CHECK(q.count == 1);
    t = queue_pop(&q);
    CHECK(t && strcmp(t->src, "y") == 0);
    task_free(t);
}

static void test_queue_clear(void)
{
    Queue q;
    int   i;

    queue_init(&q);
    for (i = 0; i < 50; i++)
        queue_push(&q, "s", "d", i, 0, 0);
    CHECK(q.count == 50);

    queue_clear(&q);
    CHECK(q.count == 0 && q.head == NULL && q.tail == NULL);
    CHECK(queue_push(&q, "again", "d", 0, 0, 0) == 0);
    queue_clear(&q);
}

/* ------------------------------------------------------------------ */
/* utils.c                                                            */
/* ------------------------------------------------------------------ */

static void test_is_up_to_date(void)
{
    struct utimbuf tb;
    const char    *dst = P("dst.txt");

    CHECK(!is_up_to_date(10, 1000, dst));          /* missing file */

    write_file(dst, "0123456789", 10);
    tb.actime = tb.modtime = 2000;
    utime(dst, &tb);

    CHECK(is_up_to_date(10, 1000, dst));           /* newer than source  */
    CHECK(is_up_to_date(10, 2000, dst));           /* same time          */
    CHECK(!is_up_to_date(10, 3000, dst));          /* source is newer    */
    CHECK(!is_up_to_date(11, 1000, dst));          /* different size     */
    CHECK(!is_up_to_date(0, 0, tmp_root));         /* a directory        */
}

static void test_verify_files(void)
{
    size_t         n = 200000;                      /* spans several blocks */
    unsigned char *a = make_data(n, 0);
    unsigned char *b = make_data(n, 0);

    write_file(P("a"), a, n);
    write_file(P("b"), b, n);
    CHECK(verify_files(P("a"), P("b")) == 0);

    b[n - 1] ^= 0xFF;                               /* last byte differs */
    write_file(P("c"), b, n);
    CHECK(verify_files(P("a"), P("c")) == -1);

    write_file(P("d"), b, n - 10);                  /* different length  */
    CHECK(verify_files(P("a"), P("d")) == -1);

    CHECK(verify_files(P("a"), P("missing")) == -1);
    CHECK(verify_files(P("missing"), P("a")) == -1);

    write_file(P("e1"), "", 0);                     /* two empty files   */
    write_file(P("e2"), "", 0);
    CHECK(verify_files(P("e1"), P("e2")) == 0);

    free(a);
    free(b);
}

static void test_print_helpers_do_not_crash(void)
{
    struct stat st;
    double      t1, t2;

    write_file(P("f"), "abc", 3);
    stat(P("f"), &st);
    print_file_info(P("f"), &st);

    print_progress(0, 100);
    print_progress(50, 100);
    print_progress(100, 100);
    print_progress(5, 0);                           /* total 0: 100%     */
    print_progress(200, 100);                       /* over 100%: capped */

    t1 = now_seconds();
    t2 = now_seconds();
    CHECK(t2 >= t1);
}

/* ------------------------------------------------------------------ */
/* log.c                                                              */
/* ------------------------------------------------------------------ */

static void test_log_writes_lines(void)
{
    Options        o = base_opts();
    unsigned char *txt;

    log_msg("ignored: log is not open yet");       /* must not crash */

    CHECK(log_open(P("t.log")) == 0);
    log_msg("hello %d", 42);
    make_pattern_file(P("src"), 100);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    log_close();
    log_close();                                    /* double close is safe */
    log_msg("ignored: log is closed again");

    txt = read_file(P("t.log"), NULL);
    CHECK(txt && strstr((char *)txt, "hello 42") != NULL);
    CHECK(txt && strstr((char *)txt, "OK ") != NULL);
    free(txt);

    CHECK(log_open("/nonexistent_dir_xyz/t.log") == -1);
}

static void test_log_truncates_long_messages(void)
{
    char           big[3000];
    unsigned char *txt;

    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    CHECK(log_open(P("long.log")) == 0);
    log_msg("%s", big);
    log_close();

    txt = read_file(P("long.log"), NULL);
    CHECK(txt && strlen((char *)txt) < 1100);       /* line is capped */
    free(txt);
}

/* ------------------------------------------------------------------ */
/* copy.c - single-threaded copy                                      */
/* ------------------------------------------------------------------ */

static void test_copy_basic(void)
{
    Options o = base_opts();

    make_pattern_file(P("src"), 10000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_copy_text_file(void)
{
    Options    o = base_opts();
    const char msg[] = "line one\nline two\n\ttabbed\n";

    write_file(P("note.txt"), msg, sizeof msg - 1);
    CHECK(copy_file(P("note.txt"), P("copy.txt"), &o) == 0);
    CHECK(same_content(P("note.txt"), P("copy.txt")));
}

static void test_copy_image_like_binary(void)
{
    Options       o = base_opts();
    unsigned char png[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    unsigned char *body = make_data(30000, 5);
    unsigned char *all = malloc(sizeof png + 30000);

    memcpy(all, png, sizeof png);
    memcpy(all + sizeof png, body, 30000);
    write_file(P("pic.png"), all, sizeof png + 30000);

    CHECK(copy_file(P("pic.png"), P("pic_copy.png"), &o) == 0);
    CHECK(same_content(P("pic.png"), P("pic_copy.png")));
    free(body);
    free(all);
}

static void test_copy_many_buffer_sizes(void)
{
    static const size_t sizes[] = { 1, 3, 7, 512, 4096, 100000 };
    size_t              i;
    Options             o = base_opts();

    make_pattern_file(P("src"), 20001);             /* not a multiple of any size */
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        o.buf_size = sizes[i];
        unlink(P("dst"));
        CHECK(copy_file(P("src"), P("dst"), &o) == 0);
        CHECK(same_content(P("src"), P("dst")));
    }
}

static void test_copy_empty_file(void)
{
    Options o = base_opts();
    struct stat st;

    write_file(P("empty"), "", 0);
    CHECK(copy_file(P("empty"), P("empty_copy"), &o) == 0);
    CHECK(stat(P("empty_copy"), &st) == 0 && st.st_size == 0);
}

static void test_copy_preserves_permissions(void)
{
    static const mode_t modes[] = { 0600, 0640, 0755, 0444 };
    size_t              i;
    Options             o = base_opts();

    make_pattern_file(P("src"), 100);
    for (i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        chmod(P("src"), modes[i]);
        unlink(P("dst"));
        CHECK(copy_file(P("src"), P("dst"), &o) == 0);
        CHECK(mode_of(P("dst")) == modes[i]);
    }
}

static void test_copy_missing_source(void)
{
    Options o = base_opts();

    CHECK(copy_file(P("nope"), P("dst"), &o) == -1);
    CHECK(!exists(P("dst")));
}

static void test_copy_same_file_is_rejected(void)
{
    Options o = base_opts();

    make_pattern_file(P("src"), 5000);
    CHECK(copy_file(P("src"), P("src"), &o) == -1);
    CHECK(link(P("src"), P("hard")) == 0);          /* hard link = same inode */
    CHECK(copy_file(P("src"), P("hard"), &o) == -1);

    {                                               /* data must be intact */
        unsigned char *want = make_data(5000, 0);
        unsigned char *got = read_file(P("src"), NULL);
        CHECK(got && memcmp(want, got, 5000) == 0);
        free(want);
        free(got);
    }
}

static void test_copy_directory_as_file_fails(void)
{
    Options o = base_opts();

    mkdir(P("somedir"), 0755);
    CHECK(copy_file(P("somedir"), P("dst"), &o) == -1);
}

static void test_copy_unwritable_destination(void)
{
    Options o = base_opts();

    make_pattern_file(P("src"), 100);
    CHECK(copy_file(P("src"), P("no_such_dir/dst"), &o) == -1);
}

static void test_copy_overwrites_larger_destination(void)
{
    Options o = base_opts();

    make_pattern_file(P("src"), 3000);
    make_pattern_file(P("dst"), 9000);              /* bigger than source */
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_copy_with_verify_option(void)
{
    Options o = base_opts();

    o.verify = 1;
    make_pattern_file(P("src"), 70000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
}

static void test_copy_with_all_display_options(void)
{
    Options o = base_opts();

    o.show_info = 1;
    o.show_progress = 1;
    o.benchmark = 1;
    o.verify = 1;
    make_pattern_file(P("src"), 50000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

/* ------------------------------------------------------------------ */
/* copy.c - resume                                                    */
/* ------------------------------------------------------------------ */

static void test_resume_partial_file(void)
{
    Options        o = base_opts();
    unsigned char *d = make_data(10000, 0);

    write_file(P("src"), d, 10000);
    write_file(P("dst"), d, 4000);                  /* first 4000 bytes */
    o.resume = 1;
    o.verify = 1;
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
    free(d);
}

static void test_resume_already_complete(void)
{
    Options o = base_opts();

    o.resume = 1;
    make_pattern_file(P("src"), 6000);
    make_pattern_file(P("dst"), 6000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_resume_with_larger_destination_restarts(void)
{
    Options o = base_opts();

    o.resume = 1;
    make_pattern_file(P("src"), 5000);
    make_pattern_file(P("dst"), 20000);             /* cannot be a prefix */
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_resume_without_destination(void)
{
    Options o = base_opts();

    o.resume = 1;
    make_pattern_file(P("src"), 5000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_resume_detects_corrupt_prefix(void)
{
    Options        o = base_opts();
    unsigned char *bad = malloc(4000);

    memset(bad, 0xAA, 4000);                        /* not the real data */
    make_pattern_file(P("src"), 10000);
    write_file(P("dst"), bad, 4000);

    o.resume = 1;
    o.verify = 1;                                   /* -c must catch it */
    CHECK(copy_file(P("src"), P("dst"), &o) == -1);
    CHECK(!same_content(P("src"), P("dst")));
    free(bad);
}

/* ------------------------------------------------------------------ */
/* parallel.c - pthreads copy                                         */
/* ------------------------------------------------------------------ */

static void test_parallel_many_thread_counts(void)
{
    static const int counts[] = { 2, 3, 4, 8, 16, 64 };
    size_t           i;
    Options          o = base_opts();

    make_pattern_file(P("src"), 100003);            /* does not divide evenly */
    for (i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        o.threads = counts[i];
        unlink(P("dst"));
        CHECK(copy_file(P("src"), P("dst"), &o) == 0);
        CHECK(same_content(P("src"), P("dst")));
    }
}

static void test_parallel_tiny_file_more_threads_than_bytes(void)
{
    Options o = base_opts();

    o.threads = 8;
    o.verify = 1;
    write_file(P("tiny"), "abc", 3);
    CHECK(copy_file(P("tiny"), P("tiny2"), &o) == 0);
    CHECK(same_content(P("tiny"), P("tiny2")));
}

static void test_parallel_empty_file(void)
{
    Options o = base_opts();
    struct stat st;

    o.threads = 4;
    write_file(P("empty"), "", 0);
    CHECK(copy_file(P("empty"), P("empty2"), &o) == 0);
    CHECK(stat(P("empty2"), &st) == 0 && st.st_size == 0);
}

static void test_parallel_preserves_permissions(void)
{
    Options o = base_opts();

    o.threads = 3;
    make_pattern_file(P("src"), 5000);
    chmod(P("src"), 0640);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(mode_of(P("dst")) == 0640);
}

static void test_parallel_small_buffer(void)
{
    Options o = base_opts();

    o.threads = 4;
    o.buf_size = 1000;
    make_pattern_file(P("src"), 50000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src"), P("dst")));
}

static void test_parallel_with_display_options(void)
{
    Options o = base_opts();

    o.threads = 4;
    o.show_progress = 1;
    o.benchmark = 1;
    o.verify = 1;
    make_pattern_file(P("src"), 300000);
    CHECK(copy_file(P("src"), P("dst"), &o) == 0);
}

static void test_parallel_error_paths(void)
{
    Options o = base_opts();

    o.threads = 4;
    CHECK(copy_file(P("nope"), P("dst"), &o) == -1);          /* missing  */

    make_pattern_file(P("src"), 1000);
    CHECK(copy_file(P("src"), P("src"), &o) == -1);           /* same file */

    mkdir(P("adir"), 0755);
    CHECK(copy_file(P("adir"), P("dst"), &o) == -1);          /* directory */

    CHECK(copy_file(P("src"), P("no_such_dir/dst"), &o) == -1); /* bad dst */
}

/* ------------------------------------------------------------------ */
/* dir.c - recursive copy                                             */
/* ------------------------------------------------------------------ */

static void make_tree(void)
{
    unsigned char *big = make_data(50000, 3);

    mkdir(P("src"), 0755);
    mkdir(P("src/sub"), 0755);
    mkdir(P("src/sub/deep"), 0755);
    mkdir(P("src/empty_dir"), 0755);
    write_file(P("src/a.txt"), "hello world\n", 12);
    write_file(P("src/sub/b.bin"), big, 50000);
    write_file(P("src/sub/deep/c.txt"), "deep file\n", 10);
    chmod(P("src/sub"), 0750);
    free(big);
}

static void test_dir_copies_whole_tree(void)
{
    Options o = base_opts();

    make_tree();
    o.verify = 1;
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src/a.txt"), P("dst/a.txt")));
    CHECK(same_content(P("src/sub/b.bin"), P("dst/sub/b.bin")));
    CHECK(same_content(P("src/sub/deep/c.txt"), P("dst/sub/deep/c.txt")));
    CHECK(exists(P("dst/empty_dir")));
    CHECK(mode_of(P("dst/sub")) == 0750);           /* dir permissions kept */
}

static void test_dir_skips_symlinks(void)
{
    Options o = base_opts();

    make_tree();
    CHECK(symlink(P("src/a.txt"), P("src/link")) == 0);
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    CHECK(!exists(P("dst/link")));
    CHECK(exists(P("dst/a.txt")));
}

static void test_dir_sync_skips_up_to_date_files(void)
{
    Options         o = base_opts();
    struct utimbuf  tb;
    unsigned char  *now;
    size_t          n = 12;
    char            junk[12];

    make_tree();
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);

    /* tamper with dst/a.txt: same size, newer time => "up to date" */
    memset(junk, 'X', sizeof junk);
    write_file(P("dst/a.txt"), junk, n);
    tb.actime = tb.modtime = time(NULL) + 100;
    utime(P("dst/a.txt"), &tb);

    o.sync = 1;
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    now = read_file(P("dst/a.txt"), NULL);
    CHECK(now && memcmp(now, junk, n) == 0);        /* skipped, still XXXX */
    free(now);

    o.sync = 0;                                     /* without -s it is restored */
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src/a.txt"), P("dst/a.txt")));
}

static void test_dir_sync_copies_new_files(void)
{
    Options o = base_opts();

    make_tree();
    o.sync = 1;
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    write_file(P("src/new.txt"), "new\n", 4);
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src/new.txt"), P("dst/new.txt")));
}

static void test_dir_copy_into_itself_terminates(void)
{
    Options o = base_opts();

    make_tree();
    CHECK(copy_directory(P("src"), P("src/inside"), &o) == 0);
    CHECK(exists(P("src/inside/a.txt")));
    CHECK(!exists(P("src/inside/inside")));         /* no endless nesting */
}

static void test_dir_missing_source(void)
{
    Options o = base_opts();

    CHECK(copy_directory(P("nope"), P("dst"), &o) == -1);
}

static void test_dir_destination_is_a_file(void)
{
    Options o = base_opts();

    make_tree();
    write_file(P("dst"), "i am a file", 11);
    CHECK(copy_directory(P("src"), P("dst"), &o) == -1);
}

static void test_dir_with_threads_and_options(void)
{
    Options o = base_opts();

    make_tree();
    o.threads = 4;
    o.verify = 1;
    o.show_info = 1;
    CHECK(copy_directory(P("src"), P("dst"), &o) == 0);
    CHECK(same_content(P("src/sub/b.bin"), P("dst/sub/b.bin")));
}

static void test_dir_unreadable_file_is_counted_as_failure(void)
{
    Options o = base_opts();

    if (geteuid() == 0) {                           /* root ignores modes */
        fprintf(out, "    (skipped: running as root)\n");
        return;
    }
    make_tree();
    chmod(P("src/a.txt"), 0000);
    CHECK(copy_directory(P("src"), P("dst"), &o) == -1);
    CHECK(same_content(P("src/sub/deep/c.txt"), P("dst/sub/deep/c.txt")));
    chmod(P("src/a.txt"), 0644);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    if (getenv("MYCP_TEST_VERBOSE"))
        out = stdout;
    else
        silence();

    fprintf(out, "== queue ==\n");
    RUN(test_queue_fifo_order);
    RUN(test_queue_pop_empty);
    RUN(test_queue_reuse_after_empty);
    RUN(test_queue_clear);

    fprintf(out, "== utils ==\n");
    RUN(test_is_up_to_date);
    RUN(test_verify_files);
    RUN(test_print_helpers_do_not_crash);

    fprintf(out, "== log ==\n");
    RUN(test_log_writes_lines);
    RUN(test_log_truncates_long_messages);

    fprintf(out, "== copy_file ==\n");
    RUN(test_copy_basic);
    RUN(test_copy_text_file);
    RUN(test_copy_image_like_binary);
    RUN(test_copy_many_buffer_sizes);
    RUN(test_copy_empty_file);
    RUN(test_copy_preserves_permissions);
    RUN(test_copy_missing_source);
    RUN(test_copy_same_file_is_rejected);
    RUN(test_copy_directory_as_file_fails);
    RUN(test_copy_unwritable_destination);
    RUN(test_copy_overwrites_larger_destination);
    RUN(test_copy_with_verify_option);
    RUN(test_copy_with_all_display_options);

    fprintf(out, "== resume ==\n");
    RUN(test_resume_partial_file);
    RUN(test_resume_already_complete);
    RUN(test_resume_with_larger_destination_restarts);
    RUN(test_resume_without_destination);
    RUN(test_resume_detects_corrupt_prefix);

    fprintf(out, "== parallel ==\n");
    RUN(test_parallel_many_thread_counts);
    RUN(test_parallel_tiny_file_more_threads_than_bytes);
    RUN(test_parallel_empty_file);
    RUN(test_parallel_preserves_permissions);
    RUN(test_parallel_small_buffer);
    RUN(test_parallel_with_display_options);
    RUN(test_parallel_error_paths);

    fprintf(out, "== directory ==\n");
    RUN(test_dir_copies_whole_tree);
    RUN(test_dir_skips_symlinks);
    RUN(test_dir_sync_skips_up_to_date_files);
    RUN(test_dir_sync_copies_new_files);
    RUN(test_dir_copy_into_itself_terminates);
    RUN(test_dir_missing_source);
    RUN(test_dir_destination_is_a_file);
    RUN(test_dir_with_threads_and_options);
    RUN(test_dir_unreadable_file_is_counted_as_failure);

    fprintf(out, "\n%d tests run, %d failed\n", tests_run, tests_failed);
    fflush(out);
    return tests_failed ? 1 : 0;
}
