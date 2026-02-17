/*
 * util/path.c — Path and filesystem utilities
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "wow/util/path.h"

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
