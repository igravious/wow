#ifndef WOW_TAR_H
#define WOW_TAR_H

#include <stddef.h>

/*
 * Extract a gzipped tar archive to a destination directory.
 *
 * gz_path:          Path to the .tar.gz file on disc.
 * dest_dir:         Directory to extract into (must exist).
 * strip_components: Number of leading path components to strip (like
 *                   tar --strip-components). Use 1 to strip the x64/
 *                   prefix from ruby-builder tarballs.
 *
 * Security: rejects absolute paths, ".." traversal, hard links,
 * devices, FIFOs, and symlinks that escape dest_dir.
 *
 * Returns 0 on success, -1 on error (messages printed to stderr).
 */
int wow_tar_extract_gz(const char *gz_path, const char *dest_dir,
                       int strip_components);

#endif
