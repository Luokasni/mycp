#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "mycp.h"
 
static int log_fd = -1;

/* Open (or create) the log file in append mode. */
int log_open(const char *path)
{
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    return (log_fd < 0) ? -1 : 0;
}

/* Write one timestamped line. Does nothing if logging is off. */
void log_msg(const char *fmt, ...)
{
    char      line[1024];
    time_t    now = time(NULL);
    struct tm tm_now;
    va_list   ap;
    int       n;
    ssize_t   r;

    if (log_fd < 0)
        return;

    localtime_r(&now, &tm_now);
    n = (int)strftime(line, sizeof line, "[%Y-%m-%d %H:%M:%S] ", &tm_now);

    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);

    if (n > (int)sizeof line - 2)
        n = (int)sizeof line - 2;
    line[n++] = '\n';

    r = write(log_fd, line, (size_t)n);
    (void)r;
}

void log_close(void)
{
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}
