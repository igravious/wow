/*
 * rubies/definitions.c — Scan ruby-binary definitions for version listing
 *
 * Reads filenames from vendor/ruby-binary/share/ruby-binary/repos/ to
 * enumerate known Ruby versions.  Scans both ruby-builder/ (platform-
 * specific tarballs) and cosmoruby/ (APE binaries) directories.
 *
 * No network calls — everything is local.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/rubies/deffile.h"
#include "wow/rubies/definitions.h"

#define RB_DEFS_DIR  WOW_DEFS_BASE "/ruby-builder"
#define CR_DEFS_DIR  WOW_DEFS_BASE "/cosmoruby"
#define MAX_DEFS     1024

/* ── Version parsing ─────────────────────────────────────────────── */

typedef struct {
    char name[256];       /* display name, e.g. "3.3.6" or "cosmoruby-1.2.2" */
    wow_ruby_impl_t impl;
    int v[4];             /* version components (up to 4: e.g. 9.4.14.0) */
    int nv;               /* how many version components parsed */
    int patchlevel;       /* -pNNN for old CRuby, or -1 */
    int prerelease;       /* 1 if -dev, -preview*, -rc* */
} ruby_def_t;

/* Parse a dotted version string, returning the number of components.
 * Stops at end-of-string, '-', or any non-digit-non-dot character.
 * Writes up to maxv components into out[]. */
static int parse_version(const char *s, int *out, int maxv)
{
    int n = 0;
    while (n < maxv) {
        char *end;
        long val = strtol(s, &end, 10);
        if (end == s) break;  /* no digit found */
        out[n++] = (int)val;
        if (*end == '.')
            s = end + 1;
        else
            break;
    }
    return n;
}

/* Return a pointer past the implementation prefix, i.e. the version part.
 * For CRuby (no prefix) returns the name itself. */
static const char *version_part(const char *name, wow_ruby_impl_t impl)
{
    const char *pfx = wow_impl_table[impl].prefix;
    if (!pfx) return name;      /* not in ruby-build */
    if (pfx[0] == '\0') return name;  /* CRuby — no prefix */
    return name + strlen(pfx);
}

static int is_prerelease(const char *name)
{
    return strstr(name, "-dev") != NULL ||
           strstr(name, "-preview") != NULL ||
           strstr(name, "-rc") != NULL ||
           strstr(name, ".pre") != NULL;
}

/* Parse the -pNNN patchlevel suffix from old CRuby versions */
static int parse_patchlevel(const char *name)
{
    const char *p = strstr(name, "-p");
    if (!p) return -1;
    p += 2;
    if (*p < '0' || *p > '9') return -1;
    return (int)strtol(p, NULL, 10);
}

/* ── Sorting ─────────────────────────────────────────────────────── */

static int cmp_defs(const void *a, const void *b)
{
    const ruby_def_t *da = a, *db = b;

    /* Sort by implementation first */
    if (da->impl != db->impl)
        return (int)da->impl - (int)db->impl;

    /* Then by version ascending */
    int n = da->nv < db->nv ? da->nv : db->nv;
    for (int i = 0; i < n; i++) {
        if (da->v[i] != db->v[i])
            return da->v[i] - db->v[i];
    }
    if (da->nv != db->nv)
        return da->nv - db->nv;

    /* Patchlevel (for old CRuby -pNNN) */
    if (da->patchlevel != db->patchlevel)
        return da->patchlevel - db->patchlevel;

    /* Stable before prerelease */
    return da->prerelease - db->prerelease;
}

/* ── Scan a single repo directory ────────────────────────────────── */

static int scan_repo(const char *dir, wow_ruby_impl_t force_impl,
                     ruby_def_t *defs, int max, int *count)
{
    DIR *d = opendir(dir);
    if (!d) return 0;  /* missing repo dir is not an error */

    int n = *count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max) {
        if (ent->d_name[0] == '.') continue;

        ruby_def_t *def = &defs[n];

        if (force_impl != WOW_IMPL_UNKNOWN) {
            /* Cosmoruby: files are bare versions (e.g. "1.2.2"),
             * display as "cosmoruby-1.2.2" */
            def->impl = force_impl;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(def->name, sizeof(def->name), "cosmoruby-%s",
                     ent->d_name);
#pragma GCC diagnostic pop
        } else {
            /* Ruby-builder: use impl detection from filename */
            snprintf(def->name, sizeof(def->name), "%s", ent->d_name);
            def->impl = wow_impl_from_definition(def->name);
        }

        def->prerelease = is_prerelease(ent->d_name);
        def->patchlevel = -1;
        memset(def->v, 0, sizeof(def->v));
        def->nv = 0;

        if (def->impl == WOW_IMPL_UNKNOWN) continue;

        const char *vstr;
        if (force_impl != WOW_IMPL_UNKNOWN)
            vstr = ent->d_name;  /* bare version for cosmoruby */
        else
            vstr = version_part(def->name, def->impl);

        def->nv = parse_version(vstr, def->v, 4);
        if (def->nv == 0) continue;

        if (def->impl == WOW_IMPL_CRUBY)
            def->patchlevel = parse_patchlevel(def->name);

        n++;
    }
    closedir(d);

    *count = n;
    return 0;
}

/* ── Scan all repo directories ───────────────────────────────────── */

static int scan_definitions(ruby_def_t *defs, int max, int *out_count)
{
    int n = 0;

    /* Ruby-builder: impl detected from filename prefixes */
    scan_repo(RB_DEFS_DIR, WOW_IMPL_UNKNOWN, defs, max, &n);

    /* CosmoRuby: force impl since filenames are bare versions */
    scan_repo(CR_DEFS_DIR, WOW_IMPL_COSMORUBY, defs, max, &n);

    qsort(defs, (size_t)n, sizeof(ruby_def_t), cmp_defs);
    *out_count = n;
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

int wow_definitions_list(void)
{
    ruby_def_t *defs = malloc(MAX_DEFS * sizeof(ruby_def_t));
    if (!defs) return -1;

    int count = 0;
    if (scan_definitions(defs, MAX_DEFS, &count) != 0) {
        free(defs);
        return -1;
    }

    /* Print latest stable version per group.
     *   CRuby:     one entry per minor series (3.2, 3.3, 3.4, 4.0)
     *   CosmoRuby: one entry (latest)
     *   All others: one entry per implementation (jruby, etc.)
     *
     * Array is sorted ascending, so the last matching entry per group
     * is the highest version. Walk backwards collecting winners, then
     * print in forward order. */
    typedef struct { wow_ruby_impl_t impl; int v0; int v1; int idx; } winner_t;
    winner_t winners[128];
    int nwin = 0;

    for (int i = count - 1; i >= 0; i--) {
        ruby_def_t *def = &defs[i];
        if (def->prerelease) continue;

        /* Check if we've already picked a winner for this group */
        int dup = 0;
        for (int w = 0; w < nwin; w++) {
            if (def->impl == WOW_IMPL_CRUBY) {
                /* CRuby: group by minor series */
                if (winners[w].impl == def->impl &&
                    winners[w].v0 == def->v[0] &&
                    winners[w].v1 == def->v[1]) {
                    dup = 1;
                    break;
                }
            } else {
                /* Everything else: group by implementation */
                if (winners[w].impl == def->impl) {
                    dup = 1;
                    break;
                }
            }
        }
        if (dup) continue;

        if (nwin < 128) {
            winners[nwin].impl = def->impl;
            winners[nwin].v0 = def->v[0];
            winners[nwin].v1 = def->v[1];
            winners[nwin].idx = i;
            nwin++;
        }
    }

    /* Print in ascending order (winners were collected in reverse) */
    for (int w = nwin - 1; w >= 0; w--)
        printf("%s\n", defs[winners[w].idx].name);

    printf("\nOnly latest stable releases for each Ruby implementation are shown.\n"
           "Use `wow rubies install -L' to show all built-in versions.\n");

    free(defs);
    return 0;
}

int wow_definitions_list_all(void)
{
    ruby_def_t *defs = malloc(MAX_DEFS * sizeof(ruby_def_t));
    if (!defs) return -1;

    int count = 0;
    if (scan_definitions(defs, MAX_DEFS, &count) != 0) {
        free(defs);
        return -1;
    }

    for (int i = 0; i < count; i++)
        printf("%s\n", defs[i].name);

    free(defs);
    return 0;
}
