/*
 * buf.h — buffer manipulation utilities
 */

#ifndef WOW_UTIL_BUF_H
#define WOW_UTIL_BUF_H

#include <stdio.h>
#include <string.h>

/*
 * Fill buffer with repeated character.
 * pos is updated to reflect new position.
 */
static inline void
wow_buf_fill(char *buf, size_t bufsz, int *pos, char c, int n)
{
    for (int i = 0; i < n && (size_t)*pos < bufsz - 1; i++)
        buf[(*pos)++] = c;
}

/*
 * Append formatted string to buffer at position, respecting bounds.
 * pos is updated to reflect new position (may exceed bufsz if truncated).
 */
#define WOW_BUF_APPEND(buf, bufsz, pos, ...) \
    do { \
        int _n = snprintf((buf) + (pos), (bufsz) - (pos), __VA_ARGS__); \
        if (_n > 0) (pos) += _n; \
    } while (0)

#endif
