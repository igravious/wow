#ifndef WOW_VERSION_H
#define WOW_VERSION_H

#include <stddef.h>

/*
 * Fetch the latest stable CRuby version from ruby-builder GitHub releases.
 * Writes e.g. "4.0.1" into buf. Returns 0 on success, -1 on failure.
 */
int wow_latest_ruby_version(char *buf, size_t bufsz);

#endif
