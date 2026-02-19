#ifndef WOW_RESOLVER_LOCKFILE_H
#define WOW_RESOLVER_LOCKFILE_H

/*
 * lockfile.h -- Bundler-format Gemfile.lock writer
 *
 * Extracted from cmd_lock() so that both `wow lock` and `wow sync`
 * can share the same lockfile writing logic.
 */

#include "wow/resolver/pubgrub.h"
#include "wow/gemfile/types.h"

/*
 * Write a Bundler-format Gemfile.lock.
 *
 * path:    output file path (e.g. "Gemfile.lock")
 * solver:  solved PubGrub solver (solution must already be qsort'd)
 * prov:    provider (for re-querying deps — data is cached, no HTTP)
 * gf:      parsed Gemfile (for DEPENDENCIES section)
 * source:  gem source URL (e.g. "https://rubygems.org")
 *
 * Returns 0 on success, -1 on error.
 */
int wow_write_lockfile(const char *path, wow_solver *solver,
                       wow_provider *prov, struct wow_gemfile *gf,
                       const char *source);

/*
 * Join an array of constraint strings with ", " into buf.
 * Shared helper used by both the lockfile writer and cmd_lock.
 */
void wow_join_constraints(char **cs, int n, char *buf, size_t bufsz);

#endif
