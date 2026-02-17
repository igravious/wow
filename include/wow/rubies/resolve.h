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
    char wow_id[32];  /* Composite: "linux-x86_64-gnu" */
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

#endif
