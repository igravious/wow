#ifndef WOW_RESOLVER_PROVIDER_H
#define WOW_RESOLVER_PROVIDER_H

/*
 * provider.h -- Compact index package provider for PubGrub
 *
 * Fetches package metadata from rubygems.org's compact index endpoint
 * (GET /info/{name}) and implements the wow_provider callbacks so the
 * PubGrub solver can discover packages and their dependencies.
 *
 * Each package is fetched at most once and cached in memory for the
 * duration of the resolve.
 *
 * All persistent pointers into the provider arena are stored as
 * wow_aoff offsets — see arena.h for rationale.
 */

#include "wow/resolver/pubgrub.h"

/* ------------------------------------------------------------------ */
/* Per-version dependency info                                         */
/* ------------------------------------------------------------------ */

struct wow_ci_dep {
    wow_aoff             name;         /* offset to name string in arena */
    wow_gem_constraints  constraints;
};

struct wow_ci_ver_deps {
    wow_aoff deps_offset;   /* offset to wow_ci_dep[] in arena */
    int      n_deps;
};

/* ------------------------------------------------------------------ */
/* Cached compact index data for one package                           */
/* ------------------------------------------------------------------ */

struct wow_ci_pkg {
    wow_aoff  name;              /* offset to name string in arena */
    wow_aoff  versions_offset;   /* offset to wow_gemver[] in arena */
    int       n_versions;
    wow_aoff  ver_deps_offset;   /* offset to wow_ci_ver_deps[] in arena */
};

/* ------------------------------------------------------------------ */
/* Compact index provider context                                      */
/* ------------------------------------------------------------------ */

#define WOW_CI_MAX_PKGS 1024

typedef struct {
    struct wow_ci_pkg  pkgs[WOW_CI_MAX_PKGS];
    int                n_pkgs;
    wow_arena          arena;
    wow_aoff           source_url;   /* offset to "https://rubygems.org" */

    /* Target Ruby version for metadata filtering.
     * When has_ruby_ver is true, versions whose compact index ruby:
     * constraint is not satisfied are excluded during parsing. */
    wow_gemver         ruby_ver;
    bool               has_ruby_ver;

    /* Connection pool for HTTP Keep-Alive */
    struct wow_http_pool *pool;
} wow_ci_provider;

/*
 * Initialise a compact index provider.
 * source_url:    gem source (e.g. "https://rubygems.org").
 * pool:          HTTP connection pool (caller-owned, must outlive provider).
 *                Pass NULL to use individual connections (slower).
 * ruby_version:  target Ruby version string (e.g. "3.4.8") for filtering
 *                versions by compact index ruby: metadata.  Pass NULL to
 *                disable metadata filtering.
 */
void wow_ci_provider_init(wow_ci_provider *p, const char *source_url,
                           struct wow_http_pool *pool,
                           const char *ruby_version);

/*
 * Build a wow_provider struct pointing at this compact index provider.
 * The returned struct's .ctx points at p, with list_versions and get_deps
 * bound to the compact index callbacks.
 */
wow_provider wow_ci_provider_as_provider(wow_ci_provider *p);

/* Free all resources owned by the provider. */
void wow_ci_provider_destroy(wow_ci_provider *p);

#endif
