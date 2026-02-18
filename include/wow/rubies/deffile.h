#ifndef WOW_RUBIES_DEFFILE_H
#define WOW_RUBIES_DEFFILE_H

/*
 * Definition file parser for ruby-binary definitions.
 *
 * Definition files are embedded in the APE binary's /zip/ filesystem
 * via zipcopy at build time (from vendor/ruby-binary/share/ruby-binary/repos/).
 * At runtime they're read from /zip/ruby-builder/ and /zip/cosmoruby/.
 *
 * Each definition file contains a URL template and per-entry SHA-256
 * checksums.  For ruby-builder, entries are platforms (e.g. "ubuntu-22.04-x64").
 * For cosmoruby, entries are binaries (e.g. "ruby.com").
 */

#include <stddef.h>  /* size_t */

#define WOW_DEF_MAX_ENTRIES 16

/* Base path for all definition repos (embedded in APE /zip/ filesystem) */
#define WOW_DEFS_BASE "/zip"

typedef struct {
    char name[64];     /* platform or binary name */
    char sha256[65];   /* hex SHA-256 digest */
} wow_def_entry_t;

typedef struct {
    char url_template[512];
    wow_def_entry_t entries[WOW_DEF_MAX_ENTRIES];
    int n_entries;
} wow_def_t;

/* Parse a definition file.  Returns 0 on success. */
int wow_def_parse(const char *path, wow_def_t *def);

/* Look up a specific entry by name.  Returns NULL if not found. */
const wow_def_entry_t *wow_def_find(const wow_def_t *def, const char *name);

/* Build final URL by substituting ${platform} or ${binary} in the template.
 * Writes to buf, returns 0 on success. */
int wow_def_url(const wow_def_t *def, const char *name, char *buf, size_t bufsz);

#endif
