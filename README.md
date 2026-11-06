# mycp
# mycp – File Copy Utility using Low-Level I/O

A Linux command-line file copy utility written in C that uses **system calls**
(`open`, `read`, `write`, `lseek`, `stat`, `chmod`, `pread`, `pwrite`) instead of
standard library functions like `fopen`/`fread`. Built as a backup & recovery
tool: it copies, verifies, resumes interrupted copies, and logs every operation.

## Features

| Level | Feature | Option |
|-------|---------|--------|
| 1 | Basic file copy (source → destination) | – |
| 2 | Configurable buffer size | `-b <bytes>` |
| 3 | File information display (size, permissions, owner, mtime) | `-i` |
| 4 | Progress bar | `-p` |
| 5 | Preserve file permissions | automatic |

**Advanced extensions**

| Feature | Option |
|---------|--------|
| Recursive directory copy | `-r` |
| Copy performance benchmark (time and MB/s) | `-t` |
| Copy verification (byte-by-byte compare) | `-c` |
| Resume interrupted copy | `-R` |
| Logging system (timestamped log file) | `-l <file>` |
| Parallel copy using pthreads | `-j <threads>` |
| Incremental sync (skip files already up to date) | `-s` |

## Build

Requires `gcc` and `make` (Linux, WSL, or GitHub Codespaces).

```bash
make          # builds ./mycp
make clean    # removes build files
```

## Usage

```
./mycp [options] <source> <destination>

  -b <bytes>  buffer size (default 4096)
  -i          show source file info
  -p          show progress bar
  -r          recursive directory copy
  -c          verify copy after finishing
  -R          resume an interrupted copy
  -s          sync: skip files that are already up to date
  -t          show time and speed (benchmark)
  -j <n>      parallel copy with n threads (1-64)
  -l <file>   write a log file
  -h          help
```

### Examples

```bash
# basic copy
./mycp a.txt b.txt

# 64 KB buffer, file info, progress bar, verify, benchmark
./mycp -b 65536 -i -p -c -t big.bin backup.bin

# copy a whole directory tree with logging
./mycp -r -l backup.log mydir mydir_backup

# resume a copy that was interrupted
./mycp -R big.bin partial.bin

# parallel copy with 4 threads
./mycp -j 4 -c -t big.bin backup.bin

# incremental backup: only copy new or changed files
./mycp -r -s mydir mydir_backup
```

If the destination is an existing directory, the file is copied into it
(`./mycp file.txt backups/` creates `backups/file.txt`).

## How it works

- **Copy loop:** `read()` into a buffer, then `write()` until all bytes are out
  (handles partial writes and `EINTR`).
- **Permissions:** `fstat()` the source, then `fchmod()` the destination.
- **Resume:** `stat()` the destination. If it is smaller than the source, both
  files are `lseek()`ed to that offset and copying continues from there.
- **Recursive copy:** three phases. (1) `opendir()` / `readdir()` scan the tree,
  `mkdir()` creates directories and every regular file is added to a
  linked-list queue. (2) Files are dequeued and copied one by one. (3) Directory
  permissions are restored with `chmod()`. A summary (copied / skipped / failed /
  bytes) is printed at the end. Symbolic links and special files are skipped, and
  copying a folder into itself is detected and skipped.
- **Incremental sync (`-s`):** a file is skipped when the destination exists, has
  the same size and is not older than the source.
- **Verification:** both files are re-read in 64 KB blocks and compared with `memcmp()`.
- **Parallel copy:** the file is split into equal chunks, one per thread. Each
  thread uses `pread()`/`pwrite()` with explicit offsets, so threads never share
  a file position. The destination is pre-sized with `ftruncate()`, and a mutex
  protects the shared progress counter. `-j` cannot be combined with `-R`.
- **Error handling:** every system call is checked and reported with `perror()`;
  failures are also written to the log when `-l` is used.

## Data structures

- **Arrays:** I/O buffers, thread and chunk tables, path buffers.
- **Linked list / queue:** `Queue` and `Task` (in `mycp.h`, `queue.c`) hold the
  files and directories found during a recursive copy (FIFO order).

## Project structure

```
mycp/
├── main.c      argument parsing, entry point
├── copy.c      core copy routine (Levels 1-5, resume, verify)
├── parallel.c  multithreaded copy (pthreads)
├── dir.c       recursive directory copy (scan, copy, summary)
├── queue.c     linked-list queue
├── utils.c     file info, progress bar, verification, timer
├── log.c       logging system
├── mycp.h      shared declarations and options
├── Makefile
└── tests/
    ├── test_mycp.c      C unit tests
    ├── gtest_mycp.cpp   Google Test suite
    ├── test_cli.sh      command-line tests
    ├── run_valgrind.sh  Valgrind (and Helgrind) runs
    └── run_coverage.sh  gcov / lcov coverage
```

## Tools and technologies

C, GCC, Makefile, POSIX system calls, pthreads. Data storage is plain file
handling. Target platform: Linux.

## Testing

Install the tools once (Ubuntu / Codespaces):
```bash
sudo apt-get update
sudo apt-get install -y valgrind libgtest-dev lcov
```

| Command | What it does |
|---------|--------------|
| `make test` | C unit tests (`tests/test_mycp.c`, 43 tests) + command-line tests (`tests/test_cli.sh`, 46 checks) |
| `make gtest` | Google Test suite (`tests/gtest_mycp.cpp`, 53 tests) |
| `make valgrind` | Runs the CLI scenarios and the unit tests under Valgrind (leaks, invalid reads/writes, uninitialised values). `HELGRIND=1 make valgrind` also checks the threads for data races |
| `make coverage` | Builds with `--coverage`, runs all three test sets and prints line/branch coverage per file. HTML report: `cov/html/index.html` |

What is covered: queue (FIFO, reuse, clear), `is_up_to_date`, `verify_files`,
logging, copy with many buffer sizes, text / binary / image-like files,
permissions, empty files, same-file and missing-file errors, resume (including
a corrupted prefix caught by `-c`), parallel copy with 2-64 threads, recursive
copy, symlinks, `-s` sync, copying a folder into itself, and every
command-line error.

Manual checks:
```bash
head -c 5000000 /dev/urandom > big.bin
./mycp -i -p -c -t big.bin copy.bin               # copy + verify
./mycp -j 4 -c big.bin copy_p.bin                 # parallel
mkdir -p d/sub && echo hi > d/a.txt && echo yo > d/sub/b.txt
./mycp -r d d_backup && diff -r d d_backup && echo SAME
head -c 2000000 big.bin > part.bin
./mycp -R -c big.bin part.bin                     # resume
valgrind --leak-check=full --trace-children=yes ./mycp -j 4 big.bin v.bin
```
