#ifndef WOW_GEMS_META_H
#define WOW_GEMS_META_H

#include <stddef.h>

/* A single dependency (name + version constraint) */
struct wow_gem_dep_info {
    char *name;
    char *constraint;   /* e.g. "~> 3.0" or ">= 3.0.0, < 4" */
};

/* Parsed gemspec metadata from a .gem file */
struct wow_gemspec {
    char *name;
    char *version;
    char *summary;
    char *authors;                  /* comma-separated author names */
    char *required_ruby_version;    /* e.g. ">= 2.7.8" */
    struct wow_gem_dep_info *deps;
    size_t n_deps;
    char  *bindir;                  /* default "bin", some gems use "exe" */
    char **executables;             /* e.g. ["rubocop"] */
    size_t n_executables;
    char **require_paths;           /* e.g. ["lib"] or ["lib/concurrent-ruby"] */
    size_t n_require_paths;
    char **extensions;              /* e.g. ["ext/prism/extconf.rb"] */
    size_t n_extensions;
};

/*
 * Parse gemspec metadata from a .gem file.
 *
 * Extracts metadata.gz from the outer tar, decompresses with zlib,
 * and parses the YAML with libyaml.
 *
 * Returns 0 on success, -1 on error.
 */
int wow_gemspec_parse(const char *gem_path, struct wow_gemspec *spec);

void wow_gemspec_free(struct wow_gemspec *spec);

#endif
