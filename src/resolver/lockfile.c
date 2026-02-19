/*
 * lockfile.c -- Bundler-format Gemfile.lock writer
 *
 * Writes the four sections of a Gemfile.lock:
 *   GEM          — source remote + resolved gem specs with deps
 *   PLATFORMS    — ruby (platform-specific gems out of scope)
 *   DEPENDENCIES — Gemfile's direct deps with constraints
 *   BUNDLED WITH — wow version string
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/resolver/lockfile.h"
#include "wow/version.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

void wow_join_constraints(char **cs, int n, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < n && off < bufsz - 1; i++) {
        if (i > 0) {
            int w = snprintf(buf + off, bufsz - off, ", ");
            if (w > 0) off += (size_t)w;
        }
        int w = snprintf(buf + off, bufsz - off, "%s", cs[i]);
        if (w > 0) off += (size_t)w;
    }
}

/* qsort comparator for sorting dep indices by name */
struct dep_sort_entry { int idx; const char *name; };
static int dep_sort_cmp(const void *a, const void *b)
{
    const struct dep_sort_entry *ea = a, *eb = b;
    return strcmp(ea->name, eb->name);
}

/* qsort comparator for Gemfile deps by name */
static int gemfile_dep_cmp(const void *a, const void *b)
{
    const struct wow_gemfile_dep *da = a, *db = b;
    return strcmp(da->name, db->name);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int wow_write_lockfile(const char *path, wow_solver *solver,
                       wow_provider *prov, struct wow_gemfile *gf,
                       const char *source)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "wow: cannot write %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    /* GEM section */
    fprintf(f, "GEM\n");
    size_t slen = strlen(source);
    if (slen > 0 && source[slen - 1] == '/')
        fprintf(f, "  remote: %s\n", source);
    else
        fprintf(f, "  remote: %s/\n", source);
    fprintf(f, "  specs:\n");

    for (int i = 0; i < solver->n_solved; i++) {
        fprintf(f, "    %s (%s)\n",
                solver->solution[i].name,
                solver->solution[i].version.raw);

        /* Re-query deps from provider (still cached, no HTTP) */
        const char **dep_names = NULL;
        wow_gem_constraints *dep_cs_out = NULL;
        int n_deps = 0;
        if (prov->get_deps(prov->ctx,
                           solver->solution[i].name,
                           &solver->solution[i].version,
                           &dep_names, &dep_cs_out,
                           &n_deps) == 0 && n_deps > 0) {
            /* Sort deps alphabetically */
            struct dep_sort_entry entries[128];
            int n_ent = n_deps < 128 ? n_deps : 128;
            for (int d = 0; d < n_ent; d++) {
                entries[d].idx = d;
                entries[d].name = dep_names[d];
            }
            qsort(entries, (size_t)n_ent, sizeof(entries[0]),
                  dep_sort_cmp);

            for (int d = 0; d < n_ent; d++) {
                int di = entries[d].idx;
                char cbuf[256];
                wow_gem_constraints_fmt(&dep_cs_out[di], cbuf,
                                        sizeof(cbuf));
                if (cbuf[0] && strcmp(cbuf, ">= 0") != 0)
                    fprintf(f, "      %s (%s)\n",
                            dep_names[di], cbuf);
                else
                    fprintf(f, "      %s\n", dep_names[di]);
            }
        }
    }

    /* PLATFORMS section */
    fprintf(f, "\nPLATFORMS\n");
    fprintf(f, "  ruby\n");

    /* DEPENDENCIES section */
    fprintf(f, "\nDEPENDENCIES\n");
    qsort(gf->deps, gf->n_deps, sizeof(struct wow_gemfile_dep),
          gemfile_dep_cmp);
    for (size_t i = 0; i < gf->n_deps; i++) {
        if (gf->deps[i].n_constraints > 0) {
            char joined[512];
            wow_join_constraints(gf->deps[i].constraints,
                                 gf->deps[i].n_constraints,
                                 joined, sizeof(joined));
            fprintf(f, "  %s (%s)\n", gf->deps[i].name, joined);
        } else {
            fprintf(f, "  %s\n", gf->deps[i].name);
        }
    }

    /* BUNDLED WITH section */
    fprintf(f, "\nBUNDLED WITH\n");
    fprintf(f, "  %s\n", WOW_VERSION);

    fclose(f);
    return 0;
}
