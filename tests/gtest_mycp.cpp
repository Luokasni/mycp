/*
 * gtest_mycp.cpp - Google Test suite for mycp
 *
 * Install:   sudo apt-get install -y libgtest-dev
 * Build/run: make gtest
 *
 * The C code is linked in as-is (mycp.h has extern "C" guards).
 * Each test gets its own temp directory, removed afterwards. The utility's
 * own printf/perror output is captured so the Google Test report stays clean.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "mycp.h"

namespace fs = std::filesystem;

/* ------------------------------------------------------------------ */
/* Fixture with a private temp directory and a few helpers            */
/* ------------------------------------------------------------------ */

class MycpTest : public ::testing::Test {
protected:
    fs::path dir;

    void SetUp() override {
        char tmpl[] = "/tmp/mycp_gtest_XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        dir = tmpl;
        testing::internal::CaptureStdout();
        testing::internal::CaptureStderr();
    }

    void TearDown() override {
        fflush(stdout);
        fflush(stderr);
        testing::internal::GetCapturedStdout();
        testing::internal::GetCapturedStderr();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string path(const std::string &name) const { return (dir / name).string(); }

    /* deterministic bytes */
    static std::string pattern(size_t n, unsigned char salt = 0) {
        std::string s(n, '\0');
        for (size_t i = 0; i < n; i++)
            s[i] = static_cast<char>((i * 31 + 7 + salt) & 0xFF);
        return s;
    }

    void write(const std::string &name, const std::string &data) {
        std::ofstream f(path(name), std::ios::binary | std::ios::trunc);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    std::string read(const std::string &name) const {
        std::ifstream f(path(name), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    bool exists(const std::string &name) const {
        struct stat st;
        return lstat(path(name).c_str(), &st) == 0;
    }

    mode_t mode(const std::string &name) const {
        struct stat st;
        if (stat(path(name).c_str(), &st) != 0)
            return static_cast<mode_t>(-1);
        return st.st_mode & 07777;
    }

    static Options opts() {
        Options o = {DEFAULT_BUF_SIZE, 0, 0, 0, 0, 0, 0, 1, 0};
        return o;
    }

    int copy(const std::string &from, const std::string &to, const Options &o) {
        return copy_file(path(from).c_str(), path(to).c_str(), &o);
    }

    int copy_dir(const std::string &from, const std::string &to, const Options &o) {
        return copy_directory(path(from).c_str(), path(to).c_str(), &o);
    }

    void make_tree() {
        fs::create_directories(dir / "src/sub/deep");
        fs::create_directories(dir / "src/empty_dir");
        write("src/a.txt", "hello world\n");
        write("src/sub/b.bin", pattern(50000, 3));
        write("src/sub/deep/c.txt", "deep file\n");
        chmod(path("src/sub").c_str(), 0750);
    }
};

/* ------------------------------------------------------------------ */
/* Queue (linked list)                                                */
/* ------------------------------------------------------------------ */

TEST(QueueTest, KeepsFifoOrderAndFields) {
    Queue q;
    queue_init(&q);
    EXPECT_EQ(q.count, 0u);

    ASSERT_EQ(queue_push(&q, "a", "A", 1, 10, 0644), 0);
    ASSERT_EQ(queue_push(&q, "b", "B", 2, 20, 0600), 0);
    ASSERT_EQ(queue_push(&q, "c", "C", 3, 30, 0755), 0);
    EXPECT_EQ(q.count, 3u);

    Task *t = queue_pop(&q);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->src, "a");
    EXPECT_STREQ(t->dst, "A");
    EXPECT_EQ(t->size, 1);
    EXPECT_EQ(t->mtime, 10);
    EXPECT_EQ(t->mode, 0644u);
    task_free(t);

    t = queue_pop(&q);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->src, "b");
    task_free(t);

    t = queue_pop(&q);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->src, "c");
    task_free(t);

    EXPECT_EQ(queue_pop(&q), nullptr);
    EXPECT_EQ(q.count, 0u);
}

TEST(QueueTest, EmptyPopAndNullFreeAreSafe) {
    Queue q;
    queue_init(&q);
    EXPECT_EQ(queue_pop(&q), nullptr);
    task_free(nullptr);
}

TEST(QueueTest, CanBeReusedAfterBecomingEmpty) {
    Queue q;
    queue_init(&q);
    queue_push(&q, "x", "X", 0, 0, 0);
    task_free(queue_pop(&q));
    EXPECT_EQ(q.head, nullptr);
    EXPECT_EQ(q.tail, nullptr);

    ASSERT_EQ(queue_push(&q, "y", "Y", 0, 0, 0), 0);
    Task *t = queue_pop(&q);
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(t->src, "y");
    task_free(t);
}

TEST(QueueTest, ClearReleasesEverything) {
    Queue q;
    queue_init(&q);
    for (int i = 0; i < 100; i++)
        queue_push(&q, "s", "d", i, 0, 0);
    EXPECT_EQ(q.count, 100u);
    queue_clear(&q);
    EXPECT_EQ(q.count, 0u);
    EXPECT_EQ(q.head, nullptr);
}

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

using UtilsTest = MycpTest;

TEST_F(UtilsTest, IsUpToDateRules) {
    const std::string dst = path("dst.txt");
    EXPECT_FALSE(is_up_to_date(10, 1000, dst.c_str()));      // missing

    write("dst.txt", "0123456789");
    struct utimbuf tb;
    tb.actime = tb.modtime = 2000;
    utime(dst.c_str(), &tb);

    EXPECT_TRUE(is_up_to_date(10, 1000, dst.c_str()));       // dst newer
    EXPECT_TRUE(is_up_to_date(10, 2000, dst.c_str()));       // same time
    EXPECT_FALSE(is_up_to_date(10, 3000, dst.c_str()));      // src newer
    EXPECT_FALSE(is_up_to_date(11, 1000, dst.c_str()));      // size differs
    EXPECT_FALSE(is_up_to_date(0, 0, dir.c_str()));          // a directory
}

TEST_F(UtilsTest, VerifyFilesDetectsDifferences) {
    const std::string data = pattern(200000);
    write("a", data);
    write("b", data);
    EXPECT_EQ(verify_files(path("a").c_str(), path("b").c_str()), 0);

    std::string changed = data;
    changed.back() ^= 0xFF;
    write("c", changed);
    EXPECT_EQ(verify_files(path("a").c_str(), path("c").c_str()), -1);

    write("d", data.substr(0, data.size() - 10));
    EXPECT_EQ(verify_files(path("a").c_str(), path("d").c_str()), -1);

    EXPECT_EQ(verify_files(path("a").c_str(), path("missing").c_str()), -1);

    write("e1", "");
    write("e2", "");
    EXPECT_EQ(verify_files(path("e1").c_str(), path("e2").c_str()), 0);
}

TEST_F(UtilsTest, DisplayHelpersDoNotCrash) {
    write("f", "abc");
    struct stat st;
    ASSERT_EQ(stat(path("f").c_str(), &st), 0);
    print_file_info(path("f").c_str(), &st);
    print_progress(0, 100);
    print_progress(100, 100);
    print_progress(5, 0);
    print_progress(200, 100);
    const double t1 = now_seconds();
    EXPECT_GE(now_seconds(), t1);
}

/* ------------------------------------------------------------------ */
/* Logging                                                            */
/* ------------------------------------------------------------------ */

using LogTest = MycpTest;

TEST_F(LogTest, WritesTimestampedLines) {
    log_msg("not open yet: must be ignored");
    ASSERT_EQ(log_open(path("t.log").c_str()), 0);
    log_msg("hello %d", 42);
    write("src", pattern(100));
    EXPECT_EQ(copy("src", "dst", opts()), 0);
    log_close();
    log_close();

    const std::string text = read("t.log");
    EXPECT_NE(text.find("hello 42"), std::string::npos);
    EXPECT_NE(text.find("OK "), std::string::npos);
    EXPECT_EQ(text.front(), '[');                             // timestamp
    EXPECT_EQ(log_open("/nonexistent_dir_xyz/t.log"), -1);
}

/* ------------------------------------------------------------------ */
/* copy_file: buffer sizes (parameterised)                            */
/* ------------------------------------------------------------------ */

class BufferSizeTest : public MycpTest,
                       public ::testing::WithParamInterface<size_t> {};

TEST_P(BufferSizeTest, CopiesExactBytes) {
    write("src", pattern(20001));                             // odd size on purpose
    Options o = opts();
    o.buf_size = GetParam();
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

INSTANTIATE_TEST_SUITE_P(Sizes, BufferSizeTest,
                         ::testing::Values(size_t{1}, size_t{3}, size_t{7},
                                           size_t{512}, size_t{4096},
                                           size_t{100000}));

/* ------------------------------------------------------------------ */
/* copy_file: content types, permissions, errors                      */
/* ------------------------------------------------------------------ */

using CopyFileTest = MycpTest;

TEST_F(CopyFileTest, CopiesTextFile) {
    write("note.txt", "line one\nline two\n\ttabbed\n");
    ASSERT_EQ(copy("note.txt", "copy.txt", opts()), 0);
    EXPECT_EQ(read("copy.txt"), read("note.txt"));
}

TEST_F(CopyFileTest, CopiesImageLikeBinary) {
    const std::string header("\x89PNG\r\n\x1a\n", 8);
    write("pic.png", header + pattern(30000, 5));
    ASSERT_EQ(copy("pic.png", "pic_copy.png", opts()), 0);
    EXPECT_EQ(read("pic_copy.png"), read("pic.png"));
}

TEST_F(CopyFileTest, CopiesEmptyFile) {
    write("empty", "");
    ASSERT_EQ(copy("empty", "empty_copy", opts()), 0);
    EXPECT_EQ(fs::file_size(dir / "empty_copy"), 0u);
}

TEST_F(CopyFileTest, PreservesPermissions) {
    write("src", pattern(100));
    for (mode_t m : {0600, 0640, 0755, 0444}) {
        chmod(path("src").c_str(), m);
        unlink(path("dst").c_str());
        ASSERT_EQ(copy("src", "dst", opts()), 0);
        EXPECT_EQ(mode("dst"), m);
    }
}

TEST_F(CopyFileTest, MissingSourceFails) {
    EXPECT_EQ(copy("nope", "dst", opts()), -1);
    EXPECT_FALSE(exists("dst"));
}

TEST_F(CopyFileTest, SameFileIsRejectedAndDataSurvives) {
    write("src", pattern(5000));
    EXPECT_EQ(copy("src", "src", opts()), -1);
    ASSERT_EQ(link(path("src").c_str(), path("hard").c_str()), 0);
    EXPECT_EQ(copy("src", "hard", opts()), -1);
    EXPECT_EQ(read("src"), pattern(5000));
}

TEST_F(CopyFileTest, DirectoryAsSourceFails) {
    fs::create_directory(dir / "somedir");
    EXPECT_EQ(copy("somedir", "dst", opts()), -1);
}

TEST_F(CopyFileTest, UnwritableDestinationFails) {
    write("src", pattern(100));
    EXPECT_EQ(copy("src", "no_such_dir/dst", opts()), -1);
}

TEST_F(CopyFileTest, OverwritesLargerDestination) {
    write("src", pattern(3000));
    write("dst", pattern(9000, 9));
    ASSERT_EQ(copy("src", "dst", opts()), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_F(CopyFileTest, VerifyAndDisplayOptions) {
    write("src", pattern(70000));
    Options o = opts();
    o.show_info = o.show_progress = o.benchmark = o.verify = 1;
    EXPECT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

/* ------------------------------------------------------------------ */
/* Resume                                                             */
/* ------------------------------------------------------------------ */

using ResumeTest = MycpTest;

TEST_F(ResumeTest, ContinuesFromPartialFile) {
    const std::string data = pattern(10000);
    write("src", data);
    write("dst", data.substr(0, 4000));
    Options o = opts();
    o.resume = o.verify = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), data);
}

TEST_F(ResumeTest, CompleteDestinationStaysCorrect) {
    write("src", pattern(6000));
    write("dst", pattern(6000));
    Options o = opts();
    o.resume = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_F(ResumeTest, LargerDestinationRestartsFromScratch) {
    write("src", pattern(5000));
    write("dst", pattern(20000, 9));
    Options o = opts();
    o.resume = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_F(ResumeTest, MissingDestinationIsJustACopy) {
    write("src", pattern(5000));
    Options o = opts();
    o.resume = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_F(ResumeTest, VerifyCatchesCorruptPrefix) {
    write("src", pattern(10000));
    write("dst", std::string(4000, '\xAA'));
    Options o = opts();
    o.resume = o.verify = 1;
    EXPECT_EQ(copy("src", "dst", o), -1);
    EXPECT_NE(read("dst"), read("src"));
}

/* ------------------------------------------------------------------ */
/* Parallel copy (parameterised over thread counts)                   */
/* ------------------------------------------------------------------ */

class ParallelTest : public MycpTest,
                     public ::testing::WithParamInterface<int> {};

TEST_P(ParallelTest, CopiesUnevenSizeCorrectly) {
    write("src", pattern(100003));                            // not divisible
    Options o = opts();
    o.threads = GetParam();
    o.verify = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_P(ParallelTest, HandlesTinyAndEmptyFiles) {
    Options o = opts();
    o.threads = GetParam();
    write("tiny", "abc");
    ASSERT_EQ(copy("tiny", "tiny2", o), 0);
    EXPECT_EQ(read("tiny2"), "abc");

    write("empty", "");
    ASSERT_EQ(copy("empty", "empty2", o), 0);
    EXPECT_EQ(fs::file_size(dir / "empty2"), 0u);
}

INSTANTIATE_TEST_SUITE_P(Threads, ParallelTest,
                         ::testing::Values(2, 3, 4, 8, 16, 64));

using ParallelExtraTest = MycpTest;

TEST_F(ParallelExtraTest, PreservesPermissions) {
    write("src", pattern(5000));
    chmod(path("src").c_str(), 0640);
    Options o = opts();
    o.threads = 3;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(mode("dst"), mode_t{0640});
}

TEST_F(ParallelExtraTest, SmallBufferAndDisplayOptions) {
    write("src", pattern(300000));
    Options o = opts();
    o.threads = 4;
    o.buf_size = 1000;
    o.show_progress = o.benchmark = o.verify = 1;
    ASSERT_EQ(copy("src", "dst", o), 0);
    EXPECT_EQ(read("dst"), read("src"));
}

TEST_F(ParallelExtraTest, ErrorPaths) {
    Options o = opts();
    o.threads = 4;
    EXPECT_EQ(copy("nope", "dst", o), -1);

    write("src", pattern(1000));
    EXPECT_EQ(copy("src", "src", o), -1);

    fs::create_directory(dir / "adir");
    EXPECT_EQ(copy("adir", "dst", o), -1);
    EXPECT_EQ(copy("src", "no_such_dir/dst", o), -1);
}

/* ------------------------------------------------------------------ */
/* Recursive directory copy                                           */
/* ------------------------------------------------------------------ */

using DirTest = MycpTest;

TEST_F(DirTest, CopiesWholeTree) {
    make_tree();
    Options o = opts();
    o.verify = 1;
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    EXPECT_EQ(read("dst/a.txt"), read("src/a.txt"));
    EXPECT_EQ(read("dst/sub/b.bin"), read("src/sub/b.bin"));
    EXPECT_EQ(read("dst/sub/deep/c.txt"), read("src/sub/deep/c.txt"));
    EXPECT_TRUE(exists("dst/empty_dir"));
    EXPECT_EQ(mode("dst/sub"), mode_t{0750});
}

TEST_F(DirTest, SkipsSymbolicLinks) {
    make_tree();
    ASSERT_EQ(symlink(path("src/a.txt").c_str(), path("src/link").c_str()), 0);
    ASSERT_EQ(copy_dir("src", "dst", opts()), 0);
    EXPECT_FALSE(exists("dst/link"));
    EXPECT_TRUE(exists("dst/a.txt"));
}

TEST_F(DirTest, SyncSkipsUpToDateFiles) {
    make_tree();
    ASSERT_EQ(copy_dir("src", "dst", opts()), 0);

    write("dst/a.txt", std::string(12, 'X'));                 // same size
    struct utimbuf tb;
    tb.actime = tb.modtime = time(nullptr) + 100;             // newer
    utime(path("dst/a.txt").c_str(), &tb);

    Options o = opts();
    o.sync = 1;
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    EXPECT_EQ(read("dst/a.txt"), std::string(12, 'X'));       // was skipped

    o.sync = 0;
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    EXPECT_EQ(read("dst/a.txt"), "hello world\n");            // restored
}

TEST_F(DirTest, SyncCopiesNewFiles) {
    make_tree();
    Options o = opts();
    o.sync = 1;
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    write("src/new.txt", "new\n");
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    EXPECT_EQ(read("dst/new.txt"), "new\n");
}

TEST_F(DirTest, CopyingIntoItselfTerminates) {
    make_tree();
    ASSERT_EQ(copy_dir("src", "src/inside", opts()), 0);
    EXPECT_TRUE(exists("src/inside/a.txt"));
    EXPECT_FALSE(exists("src/inside/inside"));
}

TEST_F(DirTest, MissingSourceFails) {
    EXPECT_EQ(copy_dir("nope", "dst", opts()), -1);
}

TEST_F(DirTest, DestinationThatIsAFileFails) {
    make_tree();
    write("dst", "i am a file");
    EXPECT_EQ(copy_dir("src", "dst", opts()), -1);
}

TEST_F(DirTest, WorksWithThreadsAndOptions) {
    make_tree();
    Options o = opts();
    o.threads = 4;
    o.verify = o.show_info = 1;
    ASSERT_EQ(copy_dir("src", "dst", o), 0);
    EXPECT_EQ(read("dst/sub/b.bin"), read("src/sub/b.bin"));
}

TEST_F(DirTest, UnreadableFileCountsAsFailure) {
    if (geteuid() == 0)
        GTEST_SKIP() << "root ignores file permissions";
    make_tree();
    chmod(path("src/a.txt").c_str(), 0000);
    EXPECT_EQ(copy_dir("src", "dst", opts()), -1);
    EXPECT_EQ(read("dst/sub/deep/c.txt"), read("src/sub/deep/c.txt"));
    chmod(path("src/a.txt").c_str(), 0644);
}
