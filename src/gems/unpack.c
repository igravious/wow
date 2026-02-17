/*
 * gems/unpack.c — extract data.tar.gz from a .gem to a destination directory
 *
 * A .gem file is an uncompressed tar containing:
 *   metadata.gz, data.tar.gz, checksums.yaml.gz
 *
 * Steps:
 *   1. Stream data.tar.gz from outer tar → temp file (no large malloc)
 *   2. Extract temp file (gzip tar) → dest_dir
 *   3. Clean up temp file on all paths
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/gems/unpack.h"
#include "wow/internal/util.h"
#include "wow/tar.h"

int wow_gem_unpack(const char *gem_path, const char *dest_dir)
{
    int ret = -1;
    int fd = -1;
    char tmp_path[256];
    tmp_path[0] = '\0';

    /* 1. Build temp file path in $TMPDIR (falls back to /tmp) */
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0])
        tmpdir = "/tmp";

    snprintf(tmp_path, sizeof(tmp_path),
             "%s/wow-data-XXXXXX.tar.gz", tmpdir);

    fd = mkstemps(tmp_path, 7);  /* 7 = strlen(".tar.gz") */
    if (fd == -1) {
        fprintf(stderr, "wow: cannot create temp file in %s: %s\n",
                tmpdir, strerror(errno));
        return -1;
    }

    /* 2. Stream data.tar.gz from the outer .gem tar to the temp file */
    if (wow_tar_extract_entry_to_fd(gem_path, "data.tar.gz", fd) != 0) {
        fprintf(stderr, "wow: cannot extract data.tar.gz from %s\n", gem_path);
        goto cleanup;
    }

    close(fd);
    fd = -1;

    /* 3. Create destination directory (wow_mkdirs needs non-const) */
    char dir_buf[PATH_MAX];
    snprintf(dir_buf, sizeof(dir_buf), "%s", dest_dir);
    if (wow_mkdirs(dir_buf, 0755) != 0)
        goto cleanup;

    /* 4. Extract the gzip tar to dest_dir */
    if (wow_tar_extract_gz(tmp_path, dest_dir, 0) != 0) {
        fprintf(stderr, "wow: cannot extract gem contents to %s\n", dest_dir);
        goto cleanup;
    }

    const char *base = strrchr(gem_path, '/');
    base = base ? base + 1 : gem_path;

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Unpacked " WOW_ANSI_BOLD "%s"
                WOW_ANSI_RESET WOW_ANSI_DIM " to %s" WOW_ANSI_RESET "\n",
                base, dest_dir);
    } else {
        fprintf(stderr, "Unpacked %s to %s\n", base, dest_dir);
    }

    ret = 0;

cleanup:
    if (fd >= 0)
        close(fd);
    if (tmp_path[0] != '\0')
        unlink(tmp_path);
    return ret;
}
