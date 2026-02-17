#ifndef WOW_UTIL_PATH_H
#define WOW_UTIL_PATH_H

#include <sys/stat.h>

/* Recursive mkdir -p. Returns 0 on success, -1 on error. */
int wow_mkdirs(char *path, mode_t mode);

#endif
