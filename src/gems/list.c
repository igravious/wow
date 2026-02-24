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
#include "wow/util/fmt.h"

static int print_entry(const char *name, size_t size,
                       char typeflag, void *ctx)
{
    (void)ctx;
    (void)typeflag;

    char szbuf[16];
    wow_fmt_bytes(size, szbuf, sizeof(szbuf));
    printf("  %-30s (%s)\n", name, szbuf);
    return 0;
}

int wow_gem_list(const char *gem_path)
{
    return wow_tar_list(gem_path, print_entry, NULL);
}
