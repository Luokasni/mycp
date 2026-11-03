#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mycp.h"

/* Recursively copy the contents of src into dst. Returns 0 if no errors. */
int copy_directory(const char *src, const char *dst, const Options *opt)
{
    DIR           *dir;
    struct dirent *entry;
    struct stat    st;
    int            errors = 0;

    if (stat(src, &st) < 0) {
        perror(src);
        return -1;
    }
    if (mkdir(dst, (st.st_mode & 0777) | 0700) < 0 && errno != EEXIST) {
        perror(dst);
        return -1;
    }

    dir = opendir(src);
    if (!dir) {
        perror(src);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char        s[PATH_MAX_LEN], d[PATH_MAX_LEN];
        struct stat est;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (snprintf(s, sizeof s, "%s/%s", src, entry->d_name) >= (int)sizeof s ||
            snprintf(d, sizeof d, "%s/%s", dst, entry->d_name) >= (int)sizeof d) {
            fprintf(stderr, "path too long: %s/%s\n", src, entry->d_name);
            errors++;
            continue;
        }
        if (lstat(s, &est) < 0) {
            perror(s);
            errors++;
            continue;
        }

        if (S_ISDIR(est.st_mode)) {
            if (copy_directory(s, d, opt) < 0)
                errors++;
        } else if (S_ISREG(est.st_mode)) {
            printf("Copying %s -> %s\n", s, d);
            if (opt->show_info)
                print_file_info(s, &est);
            if (copy_file(s, d, opt) < 0)
                errors++;
        } else {
            fprintf(stderr, "Skipping %s (not a regular file or directory)\n", s);
        }
    }
    closedir(dir);

    if (chmod(dst, st.st_mode & 07777) < 0)
        perror(dst);

    return errors ? -1 : 0;
}
