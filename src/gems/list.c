/*
 * list.c — list entries inside a .gem file
 *
 * A .gem is an uncompressed tar containing metadata.gz, data.tar.gz,
 * and checksums.yaml.gz.  This module iterates the outer tar headers
 * and prints each entry's name and size.
 */

#include <stdio.h>

#include "wow/gems/list.h"
#include "wow/tar.h"

/* Format a byte count as human-readable (e.g. "4.2 KiB", "52.1 KiB") */
static void fmt_size(char *buf, size_t bufsz, size_t bytes)
{
    if (bytes < 1024) {
        snprintf(buf, bufsz, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufsz, "%.1f KiB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, bufsz, "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
    }
}

static int print_entry(const char *name, size_t size,
                       char typeflag, void *ctx)
{
    (void)ctx;
    (void)typeflag;

    char szbuf[32];
    fmt_size(szbuf, sizeof(szbuf), size);
    printf("  %-30s (%s)\n", name, szbuf);
    return 0;
}

int wow_gem_list(const char *gem_path)
{
    return wow_tar_list(gem_path, print_entry, NULL);
}
