#ifndef WOW_WOWX_H
#define WOW_WOWX_H

#include <stddef.h>

/*
 * Resolve the wowx cache directory, keyed by Ruby API version.
 * e.g. ~/.cache/wowx/3.3.0/ for Ruby 3.3.x.
 * Respects $XDG_CACHE_HOME, falls back to ~/.cache/wowx/<ruby_api>/.
 * Returns 0 on success, -1 on error.
 */
int wow_wowx_cache_dir(const char *ruby_api, char *buf, size_t bufsz);

#endif
