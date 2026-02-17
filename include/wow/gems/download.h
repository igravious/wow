#ifndef WOW_GEMS_DOWNLOAD_H
#define WOW_GEMS_DOWNLOAD_H

#include <stddef.h>

/*
 * Resolve the gem cache directory.
 * Respects $XDG_CACHE_HOME, falls back to ~/.cache/wow/gems/.
 * Returns 0 on success, -1 on error.
 */
int wow_gem_cache_dir(char *buf, size_t bufsz);

/*
 * Download a .gem file from rubygems.org to the global cache.
 *
 * Fetches registry metadata to obtain the download URL and SHA-256.
 * If the gem is already cached and the hash matches, skips download.
 * Downloads with progress bar, verifies SHA-256, atomic rename.
 *
 * out_path (if non-NULL) receives the cache path of the .gem file.
 *
 * Returns 0 on success, -1 on error.
 */
int wow_gem_download(const char *name, const char *version,
                     char *out_path, size_t out_path_sz);

#endif
