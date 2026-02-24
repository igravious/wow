/*
 * fmt.h — string formatting utilities
 */

#ifndef WOW_UTIL_FMT_H
#define WOW_UTIL_FMT_H

#include <stddef.h>

/*
 * Format bytes as human-readable string (e.g., "1.5 MiB", "100 B").
 * buf must be at least 16 bytes.
 */
void wow_fmt_bytes(size_t bytes, char *buf, size_t bufsz);

/*
 * Format bytes with spaces for thousands separation
 * (e.g., "1572864" -> "1 572 864").
 * buf must be at least 32 bytes.
 */
void wow_fmt_bytes_spaced(size_t bytes, char *buf, size_t bufsz);

#endif
