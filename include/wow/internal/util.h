#ifndef WOW_INTERNAL_UTIL_H
#define WOW_INTERNAL_UTIL_H

/*
 * Internal utility functions shared across wow.
 *
 * These are implementation details — not part of the public API.
 * Used for common operations like timing, coloured output, filesystem.
 */

#include <sys/stat.h>

/* Return 1 if stderr is a TTY (for coloured output), 0 otherwise */
int wow_use_colour(void);

/* Return monotonic time in seconds */
double wow_now_secs(void);

/* Recursive mkdir -p. Returns 0 on success, -1 on error. */
int wow_mkdirs(char *path, mode_t mode);

#endif
