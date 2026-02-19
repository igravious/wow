/*
 * provider.c -- Compact index package provider for PubGrub
 *
 * Fetches package metadata from rubygems.org's compact index endpoint
 * (GET /info/{name}) and implements the wow_provider callbacks.
 *
 * Compact index format (each line after "---" header):
 *   version dep1:c1&c2,dep2:c3|checksum:hex[,ruby:req][,rubygems:req]
 *
 * - Deps are comma-separated before the pipe
 * - Multiple constraints per dep are &-separated
 * - Platform versions have a dash suffix: "1.0.0-java"
 * - We filter to ruby-platform-only (no dash, or -ruby)
 */

#include "wow/resolver/provider.h"
#include "wow/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Arena convenience macros (prov must be in scope)                     */
/* ------------------------------------------------------------------ */

#define P_STR(off)     WOW_ARENA_STR(&prov->arena, off)
#define P_PTR(off, T)  WOW_ARENA_PTR(&prov->arena, off, T)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
 * Copy n bytes of s into the arena, NUL-terminate, return the offset.
 * Like wow_arena_strdup_off but for a substring (not NUL-terminated).
 */
static wow_aoff arena_strndup_off(wow_arena *a, const char *s, size_t n)
{
    wow_aoff off = wow_arena_alloc_off(a, n + 1);
    if (off == WOW_AOFF_NULL) return WOW_AOFF_NULL;
    char *p = WOW_ARENA_PTR(a, off, char);
    memcpy(p, s, n);
    p[n] = '\0';
    return off;
}

/* ------------------------------------------------------------------ */
/* Compact index line parser                                           */
/* ------------------------------------------------------------------ */

/*
 * Parse a single compact index version line into a wow_ci_pkg entry.
 * Returns 0 on success, -1 to skip (platform-specific), -2 on error.
 *
 * The line has already been stripped of trailing newline.
 */
static int parse_ci_line(wow_arena *arena, const char *line,
                          wow_gemver *ver_out,
                          wow_aoff *deps_offset_out, int *n_deps_out)
{
    *n_deps_out = 0;
    *deps_offset_out = WOW_AOFF_NULL;

    /* Split: "version deps|metadata" */
    const char *sp = strchr(line, ' ');
    if (!sp) return -2;

    /* Version string (may have platform suffix: "1.0.0-java") */
    size_t vlen = (size_t)(sp - line);
    char vbuf[128];
    if (vlen >= sizeof(vbuf)) return -2;
    memcpy(vbuf, line, vlen);
    vbuf[vlen] = '\0';

    /* Check for platform suffix: a dash followed by a letter */
    size_t effective_len = vlen;
    for (size_t i = 0; i < vlen; i++) {
        if (vbuf[i] == '-' && i + 1 < vlen &&
            ((vbuf[i + 1] >= 'a' && vbuf[i + 1] <= 'z') ||
             (vbuf[i + 1] >= 'A' && vbuf[i + 1] <= 'Z'))) {
            /* Platform-specific version — check if it's "ruby" */
            if (strcmp(vbuf + i + 1, "ruby") != 0)
                return -1;  /* skip non-ruby platforms */
            effective_len = i;
            break;
        }
    }
    vbuf[effective_len] = '\0';

    if (wow_gemver_parse(vbuf, ver_out) != 0)
        return -2;

    /* Find the pipe separator */
    const char *pipe = strchr(sp + 1, '|');
    if (!pipe) return -2;

    /* Parse deps between sp+1 and pipe */
    const char *deps_start = sp + 1;
    size_t deps_len = (size_t)(pipe - deps_start);

    if (deps_len == 0 || (deps_len == 1 && *deps_start == ' ')) {
        /* No deps */
        return 0;
    }

    /* Copy deps section to a mutable buffer — use malloc (NOT arena) so that
     * tok/dep_name/constraints_raw pointers remain stable across arena growth */
    char *deps_buf = malloc(deps_len + 1);
    if (!deps_buf) return -2;
    memcpy(deps_buf, deps_start, deps_len);
    deps_buf[deps_len] = '\0';

    /* Count colons to estimate dep count (each dep has exactly one colon) */
    int max_deps = 0;
    for (size_t i = 0; i < deps_len; i++)
        if (deps_buf[i] == ':') max_deps++;

    if (max_deps == 0) { free(deps_buf); return 0; }

    /* Allocate deps array in arena, remember offset */
    wow_aoff deps_off = wow_arena_alloc_off(
        arena, (size_t)max_deps * sizeof(struct wow_ci_dep));
    if (deps_off == WOW_AOFF_NULL) { free(deps_buf); return -2; }
    int n = 0;

    char *tok = deps_buf;
    while (tok && *tok) {
        /* Find the next comma (dep separator) */
        char *comma = strchr(tok, ',');
        if (comma) *comma = '\0';

        /* Split "name:c1&c2" at the colon */
        char *colon = strchr(tok, ':');
        if (colon && n < max_deps) {
            *colon = '\0';
            const char *dep_name = tok;
            const char *constraints_raw = colon + 1;

            /* Convert & to , for constraint parsing: "~> 4.0&>= 3.1" → "~> 4.0, >= 3.1" */
            size_t clen = strlen(constraints_raw);
            char *cbuf = wow_arena_alloc(arena, clen * 2 + 1);
            if (!cbuf) { free(deps_buf); return -2; }
            char *p = cbuf;
            for (size_t i = 0; i < clen; i++) {
                if (constraints_raw[i] == '&') {
                    *p++ = ',';
                    *p++ = ' ';
                } else {
                    *p++ = constraints_raw[i];
                }
            }
            *p = '\0';

            /* Re-fetch deps pointer — arena may have grown during cbuf alloc */
            struct wow_ci_dep *deps = WOW_ARENA_PTR(arena, deps_off,
                                                     struct wow_ci_dep);

            if (wow_gem_constraints_parse(cbuf, &deps[n].constraints) != 0) {
                /* Skip unparseable constraint — better than failing entirely */
                tok = comma ? comma + 1 : NULL;
                continue;
            }

            /* Re-fetch again — constraint parse may have touched arena
             * (it shouldn't, but belt-and-braces) */
            deps = WOW_ARENA_PTR(arena, deps_off, struct wow_ci_dep);

            /* Store name as offset */
            deps[n].name = arena_strndup_off(arena, dep_name,
                                              strlen(dep_name));
            n++;
        }

        tok = comma ? comma + 1 : NULL;
    }

    free(deps_buf);
    *deps_offset_out = deps_off;
    *n_deps_out = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fetch + parse compact index for a package                           */
/* ------------------------------------------------------------------ */

/*
 * Fetch and parse the compact index for a package.
 * Adds to provider cache. Returns the cached pkg or NULL on error.
 */
static struct wow_ci_pkg *fetch_package(wow_ci_provider *prov,
                                         const char *name)
{
    /* Build URL: source_url/info/name */
    char url[512];
    snprintf(url, sizeof(url), "%s/info/%s", P_STR(prov->source_url), name);

    /* Fetch */
    struct wow_response resp;
    int rc;
    if (prov->pool)
        rc = wow_http_pool_get(prov->pool, url, &resp);
    else
        rc = wow_http_get(url, &resp);

    if (rc != 0) {
        fprintf(stderr, "wow: failed to fetch %s\n", url);
        return NULL;
    }

    if (resp.status == 404) {
        /* Package not found — return empty cache entry */
        wow_response_free(&resp);
        if (prov->n_pkgs >= WOW_CI_MAX_PKGS) return NULL;

        struct wow_ci_pkg *pkg = &prov->pkgs[prov->n_pkgs++];
        memset(pkg, 0, sizeof(*pkg));
        pkg->name = wow_arena_strdup_off(&prov->arena, name);
        return pkg;
    }

    if (resp.status != 200) {
        fprintf(stderr, "wow: %s returned HTTP %d\n", url, resp.status);
        wow_response_free(&resp);
        return NULL;
    }

    if (prov->n_pkgs >= WOW_CI_MAX_PKGS) {
        fprintf(stderr, "wow: package cache full (max %d)\n",
                WOW_CI_MAX_PKGS);
        wow_response_free(&resp);
        return NULL;
    }

    /* Parse the response body line by line */
    struct wow_ci_pkg *pkg = &prov->pkgs[prov->n_pkgs++];
    memset(pkg, 0, sizeof(*pkg));
    pkg->name = wow_arena_strdup_off(&prov->arena, name);

    /* Temporary arrays — we don't know the count yet */
    int ver_cap = 128;
    wow_gemver *vers = malloc((size_t)ver_cap * sizeof(wow_gemver));
    struct wow_ci_ver_deps *vdeps = malloc(
        (size_t)ver_cap * sizeof(struct wow_ci_ver_deps));
    int n_ver = 0;

    if (!vers || !vdeps) {
        free(vers);
        free(vdeps);
        wow_response_free(&resp);
        return NULL;
    }

    /* Walk lines: skip everything before "---" header */
    bool past_header = false;
    char *body = resp.body;
    char *line = body;

    while (line && *line) {
        /* Find end of line */
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        /* Strip trailing \r */
        size_t ll = strlen(line);
        if (ll > 0 && line[ll - 1] == '\r')
            line[ll - 1] = '\0';

        if (!past_header) {
            if (strcmp(line, "---") == 0)
                past_header = true;
            line = eol ? eol + 1 : NULL;
            continue;
        }

        if (line[0] == '\0') {
            line = eol ? eol + 1 : NULL;
            continue;
        }

        /* Parse version line */
        wow_gemver ver;
        wow_aoff deps_offset = WOW_AOFF_NULL;
        int n_deps = 0;
        int prc = parse_ci_line(&prov->arena, line, &ver,
                                 &deps_offset, &n_deps);

        if (prc == 0) {
            /* Grow arrays if needed */
            if (n_ver >= ver_cap) {
                ver_cap *= 2;
                vers = realloc(vers,
                               (size_t)ver_cap * sizeof(wow_gemver));
                vdeps = realloc(vdeps,
                                (size_t)ver_cap * sizeof(struct wow_ci_ver_deps));
                if (!vers || !vdeps) {
                    free(vers);
                    free(vdeps);
                    wow_response_free(&resp);
                    return NULL;
                }
            }

            vers[n_ver] = ver;
            vdeps[n_ver].deps_offset = deps_offset;
            vdeps[n_ver].n_deps = n_deps;
            n_ver++;
        }
        /* prc == -1: skip (platform-specific), prc == -2: parse error */

        line = eol ? eol + 1 : NULL;
    }

    wow_response_free(&resp);

    /* Sort versions newest-first */
    /* We also need to keep ver_deps in sync — build index array */
    if (n_ver > 0) {
        /* Build index array for sorting */
        int *idx = malloc((size_t)n_ver * sizeof(int));
        if (!idx) {
            free(vers);
            free(vdeps);
            return NULL;
        }
        for (int i = 0; i < n_ver; i++) idx[i] = i;

        /* Simple insertion sort (stable, preserves order of equal versions) */
        for (int i = 1; i < n_ver; i++) {
            int key = idx[i];
            int j = i - 1;
            while (j >= 0 && wow_gemver_cmp(&vers[idx[j]], &vers[key]) < 0) {
                idx[j + 1] = idx[j];
                j--;
            }
            idx[j + 1] = key;
        }

        /* Copy to arena in sorted order */
        wow_aoff versions_off = wow_arena_alloc_off(
            &prov->arena, (size_t)n_ver * sizeof(wow_gemver));
        wow_aoff ver_deps_off = wow_arena_alloc_off(
            &prov->arena, (size_t)n_ver * sizeof(struct wow_ci_ver_deps));
        pkg->n_versions = n_ver;

        /* Store offsets — safe across future arena growth */
        pkg->versions_offset = versions_off;
        pkg->ver_deps_offset = ver_deps_off;

        /* Write data via computed pointers (both allocs are done,
         * so these pointers are valid until the next arena alloc) */
        wow_gemver *versions_ptr = P_PTR(versions_off, wow_gemver);
        struct wow_ci_ver_deps *ver_deps_ptr =
            P_PTR(ver_deps_off, struct wow_ci_ver_deps);

        for (int i = 0; i < n_ver; i++) {
            versions_ptr[i] = vers[idx[i]];
            ver_deps_ptr[i] = vdeps[idx[i]];
        }

        free(idx);
    }

    free(vers);
    free(vdeps);

    return pkg;
}

/* ------------------------------------------------------------------ */
/* Provider callbacks                                                  */
/* ------------------------------------------------------------------ */

static struct wow_ci_pkg *find_cached(wow_ci_provider *prov,
                                       const char *name)
{
    for (int i = 0; i < prov->n_pkgs; i++) {
        if (strcmp(P_STR(prov->pkgs[i].name), name) == 0)
            return &prov->pkgs[i];
    }
    return NULL;
}

static struct wow_ci_pkg *ensure_cached(wow_ci_provider *prov,
                                         const char *name)
{
    struct wow_ci_pkg *pkg = find_cached(prov, name);
    if (pkg) return pkg;
    return fetch_package(prov, name);
}

static int ci_list_versions(void *ctx, const char *package,
                             const wow_gemver **out, int *n_out)
{
    wow_ci_provider *prov = ctx;
    struct wow_ci_pkg *pkg = ensure_cached(prov, package);
    if (!pkg) {
        *out = NULL;
        *n_out = 0;
        return -1;
    }
    /* Compute pointer from offset — safe across arena growth */
    *out = P_PTR(pkg->versions_offset, const wow_gemver);
    *n_out = pkg->n_versions;
    return 0;
}

/*
 * Static buffers for get_deps return values.
 * The PubGrub solver calls get_deps sequentially (never concurrently) —
 * each returned pointer is consumed before the next call. If we ever
 * parallelise resolution, these must become per-thread or arena-allocated.
 */
#define CI_MAX_DEPS_PER_VER 64
static const char *ci_dep_names_buf[CI_MAX_DEPS_PER_VER];
static wow_gem_constraints ci_dep_cs_buf[CI_MAX_DEPS_PER_VER];

static int ci_get_deps(void *ctx, const char *package,
                        const wow_gemver *version,
                        const char ***dep_names_out,
                        wow_gem_constraints **dep_constraints_out,
                        int *n_deps_out)
{
    wow_ci_provider *prov = ctx;
    struct wow_ci_pkg *pkg = ensure_cached(prov, package);
    if (!pkg) {
        *n_deps_out = 0;
        return -1;
    }

    /* Compute pointers from offsets — safe across arena growth */
    const wow_gemver *versions = P_PTR(pkg->versions_offset,
                                        const wow_gemver);
    const struct wow_ci_ver_deps *ver_deps =
        P_PTR(pkg->ver_deps_offset, const struct wow_ci_ver_deps);

    /* Find the matching version */
    for (int v = 0; v < pkg->n_versions; v++) {
        if (wow_gemver_cmp(&versions[v], version) == 0) {
            const struct wow_ci_ver_deps *vd = &ver_deps[v];
            int n = vd->n_deps;
            if (n > CI_MAX_DEPS_PER_VER)
                n = CI_MAX_DEPS_PER_VER;

            /* Compute deps pointer from offset */
            const struct wow_ci_dep *deps =
                P_PTR(vd->deps_offset, const struct wow_ci_dep);

            for (int d = 0; d < n; d++) {
                ci_dep_names_buf[d] = P_STR(deps[d].name);
                ci_dep_cs_buf[d] = deps[d].constraints;
            }

            *dep_names_out = ci_dep_names_buf;
            *dep_constraints_out = ci_dep_cs_buf;
            *n_deps_out = n;
            return 0;
        }
    }

    /* Version not found */
    *n_deps_out = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void wow_ci_provider_init(wow_ci_provider *p, const char *source_url,
                           struct wow_http_pool *pool)
{
    memset(p, 0, sizeof(*p));
    wow_arena_init(&p->arena);

    /* Strip trailing slash from source URL */
    size_t slen = strlen(source_url);
    while (slen > 0 && source_url[slen - 1] == '/')
        slen--;
    p->source_url = arena_strndup_off(&p->arena, source_url, slen);
    p->pool = pool;
}

wow_provider wow_ci_provider_as_provider(wow_ci_provider *p)
{
    wow_provider prov;
    prov.list_versions = ci_list_versions;
    prov.get_deps = ci_get_deps;
    prov.ctx = p;
    return prov;
}

void wow_ci_provider_destroy(wow_ci_provider *p)
{
    wow_arena_destroy(&p->arena);
    memset(p, 0, sizeof(*p));
}
