#ifndef WOW_RUBIES_RESOLVE_H
#define WOW_RUBIES_RESOLVE_H

#include <stddef.h>

/*
 * Ruby version resolution and platform detection.
 */

/* Platform information */
typedef struct {
    char os[16];      /* "linux", "darwin", etc. */
    char arch[16];    /* "x86_64", "arm64", etc. */
    char libc[8];     /* "gnu", "musl", or empty */
    char wow_id[48];  /* Composite: "linux-x86_64-gnu" */
} wow_platform_t;

/* Detect current platform */
void wow_detect_platform(wow_platform_t *p);

/* Map wow platform to ruby-builder platform string */
const char *wow_ruby_builder_platform(const wow_platform_t *p);

/* Directory helpers */
int wow_ruby_base_dir(char *buf, size_t bufsz);
int wow_shims_dir(char *buf, size_t bufsz);

/* Version resolution */
int wow_resolve_ruby_version(const char *input, char *full_ver, size_t bufsz);

/* Find .ruby-version file walking up directory tree */
int wow_find_ruby_version(char *buf, size_t bufsz);

/* Get path to ruby binary for a version */
int wow_ruby_bin_path(const char *version, char *buf, size_t bufsz);

/* Derive Ruby API version: "3.3.6" → "3.3.0" (major.minor.0).
 * Used by sync and wowx for gem load-path construction. */
void wow_ruby_api_version(const char *full_ver, char *buf, size_t bufsz);

/* Find the latest installed Ruby version.
 * Scans wow_ruby_base_dir() for ruby-{ver}-{platform} directories
 * and returns the highest version string (e.g. "4.0.1").
 * Returns 0 on success, -1 if no rubies installed. */
int wow_ruby_pick_latest(char *version_buf, size_t bufsz);

/* Find the best installed Ruby matching a prefix (e.g. "3.3" → "3.3.10").
 * Matches uvx --python behaviour: partial version selects the highest
 * installed patch release.  An exact version (e.g. "3.3.10") also works.
 * Returns 0 on success, -1 if no matching Ruby is installed. */
int wow_ruby_pick_matching(const char *requested, char *version_buf,
                           size_t bufsz);

#endif
