/*
 * fmt.c — string formatting utilities
 */

#include "wow/util/fmt.h"
#include <stdio.h>

void
wow_fmt_bytes(size_t bytes, char *buf, size_t bufsz)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, bufsz, "%.1fGiB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1fMiB",
                 (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1fKiB", (double)bytes / 1024.0);
    else
        snprintf(buf, bufsz, "%zuB", bytes);
}

void
wow_fmt_bytes_spaced(size_t bytes, char *buf, size_t bufsz)
{
    if (bytes < 1000) {
        snprintf(buf, bufsz, "%zu", bytes);
    } else if (bytes < 1000000) {
        snprintf(buf, bufsz, "%zu %03zu", bytes / 1000, bytes % 1000);
    } else if (bytes < 1000000000) {
        snprintf(buf, bufsz, "%zu %03zu %03zu",
                 bytes / 1000000, (bytes / 1000) % 1000, bytes % 1000);
    } else {
        snprintf(buf, bufsz, "%zu %03zu %03zu %03zu",
                 bytes / 1000000000, (bytes / 1000000) % 1000,
                 (bytes / 1000) % 1000, bytes % 1000);
    }
}
