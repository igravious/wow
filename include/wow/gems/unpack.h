#ifndef WOW_GEMS_UNPACK_H
#define WOW_GEMS_UNPACK_H

/*
 * Unpack a .gem file by extracting data.tar.gz to a destination directory.
 *
 * gem_path:  Path to the .gem file (uncompressed tar containing data.tar.gz).
 * dest_dir:  Directory to extract gem contents into (created if needed).
 *
 * Streams data.tar.gz from the outer tar to a temp file (no large malloc),
 * then extracts the gzip tar to dest_dir.  Temp file is cleaned up on all
 * paths including errors.
 *
 * Returns 0 on success, -1 on error.
 */
int wow_gem_unpack(const char *gem_path, const char *dest_dir);

/*
 * Quiet variant — suppresses "Unpacked ..." output when quiet != 0.
 * wow_gem_unpack() is equivalent to wow_gem_unpack_q(path, dest, 0).
 */
int wow_gem_unpack_q(const char *gem_path, const char *dest_dir, int quiet);

#endif
