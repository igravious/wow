/*
 * util.c — Shared utility functions
 *
 * Common operations used across multiple modules.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "wow/internal/util.h"

int wow_use_colour(void)
{
    return isatty(STDERR_FILENO);
}

double wow_now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int wow_mkdirs(char *path, mode_t mode)
{
    char *p = path;
    if (*p == '/') p++;
    for (; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, mode) != 0 && errno != EEXIST) {
            fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}
