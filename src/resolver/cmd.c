/*
 * cmd.c -- Resolver CLI subcommands
 *
 * Provides:
 *   wow resolve <gem> [<gem>...]     — resolve dependencies
 *   wow lock [Gemfile]               — resolve + write Gemfile.lock
 *   wow debug version-test           — hardcoded version matching tests
 *   wow debug pubgrub-test           — hardcoded PubGrub solver tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/resolver.h"
#include "wow/resolver/test.h"
#include "wow/gemfile.h"
#include "wow/http.h"
#include "wow/version.h"

/* ------------------------------------------------------------------ */
/* wow resolve <gem> [<gem>...]                                        */
/* ------------------------------------------------------------------ */

int cmd_resolve(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow resolve <gem> [<gem>...]\n");
        return 1;
    }

    /* Collect gem names from argv (skip argv[0] which is "resolve") */
    const char **names = (const char **)(argv + 1);
    int n_gems = argc - 1;

    /* Parse optional version constraints: "sinatra:~>4.0" or just "sinatra" */
    const char **root_names = calloc((size_t)n_gems, sizeof(char *));
    wow_gem_constraints *root_cs = calloc((size_t)n_gems,
                                          sizeof(wow_gem_constraints));
    if (!root_names || !root_cs) {
        fprintf(stderr, "wow: out of memory\n");
        free(root_names);
        free(root_cs);
        return 1;
    }

    for (int i = 0; i < n_gems; i++) {
        const char *arg = names[i];
        const char *colon = strchr(arg, ':');
        if (colon && colon[1]) {
            /* "sinatra:~>4.0" — split at colon */
            size_t nlen = (size_t)(colon - arg);
            char *name = malloc(nlen + 1);
            if (!name) { fprintf(stderr, "wow: out of memory\n"); goto fail; }
            memcpy(name, arg, nlen);
            name[nlen] = '\0';
            root_names[i] = name;
            if (wow_gem_constraints_parse(colon + 1, &root_cs[i]) != 0) {
                fprintf(stderr, "wow: invalid constraint: %s\n", colon + 1);
                goto fail;
            }
        } else {
            /* Just a name — default to >= 0 */
            root_names[i] = arg;
            wow_gem_constraints_parse(">= 0", &root_cs[i]);
        }
    }

    /* Set up compact index provider */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    const char *source = "https://rubygems.org";
    wow_ci_provider ci;
    wow_ci_provider_init(&ci, source, &pool, NULL);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    printf("Resolving dependencies...\n");
    fflush(stdout);

    int rc = wow_solve(&solver, root_names, root_cs, n_gems);
    if (rc != 0) {
        fprintf(stderr, "\nResolution failed:\n%s\n", solver.error_msg);
        wow_solver_destroy(&solver);
        wow_ci_provider_destroy(&ci);
        wow_http_pool_cleanup(&pool);
        free(root_names);
        free(root_cs);
        return 1;
    }

    printf("Resolved %d packages:\n", solver.n_solved);
    for (int i = 0; i < solver.n_solved; i++) {
        printf("  %s %s\n", solver.solution[i].name,
               solver.solution[i].version.raw);
    }

    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);
    free(root_names);
    free(root_cs);
    return 0;

fail:
    for (int i = 0; i < n_gems; i++) {
        const char *arg = names[i];
        const char *colon = strchr(arg, ':');
        if (colon && root_names[i] != arg)
            free((void *)root_names[i]);
    }
    free(root_names);
    free(root_cs);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Helper for sorting resolved packages                                */
/* ------------------------------------------------------------------ */

static int resolved_cmp(const void *a, const void *b)
{
    const wow_resolved_pkg *pa = a;
    const wow_resolved_pkg *pb = b;
    return strcmp(pa->name, pb->name);
}

/* ------------------------------------------------------------------ */
/* wow lock [Gemfile]                                                  */
/* ------------------------------------------------------------------ */

int cmd_lock(int argc, char *argv[])
{
    const char *gemfile_path = (argc > 1) ? argv[1] : "Gemfile";

    /* 1. Parse Gemfile */
    struct wow_gemfile gemfile;
    if (wow_gemfile_parse_file(gemfile_path, &gemfile) != 0) {
        fprintf(stderr, "wow: failed to parse %s\n", gemfile_path);
        return 1;
    }

    if (gemfile.n_deps == 0) {
        printf("No dependencies in %s\n", gemfile_path);
        wow_gemfile_free(&gemfile);
        return 0;
    }

    /* 2. Collect root dependencies */
    const char **root_names = calloc((size_t)gemfile.n_deps, sizeof(char *));
    wow_gem_constraints *root_cs = calloc((size_t)gemfile.n_deps,
                                          sizeof(wow_gem_constraints));
    if (!root_names || !root_cs) {
        fprintf(stderr, "wow: out of memory\n");
        free(root_names);
        free(root_cs);
        wow_gemfile_free(&gemfile);
        return 1;
    }

    const char *source = gemfile.source ? gemfile.source : "https://rubygems.org";

    for (int i = 0; i < (int)gemfile.n_deps; i++) {
        root_names[i] = gemfile.deps[i].name;
        if (gemfile.deps[i].n_constraints > 0) {
            /* Join constraints into single string for parsing */
            char joined[256];
            joined[0] = '\0';
            int pos = 0;
            for (int j = 0; j < gemfile.deps[i].n_constraints; j++) {
                if (j > 0) pos += snprintf(joined + pos, sizeof(joined) - pos, ", ");
                pos += snprintf(joined + pos, sizeof(joined) - pos, "%s",
                               gemfile.deps[i].constraints[j]);
            }
            if (wow_gem_constraints_parse(joined, &root_cs[i]) != 0) {
                fprintf(stderr, "wow: invalid constraint for %s: %s\n",
                        gemfile.deps[i].name, joined);
                free(root_names); free(root_cs);
                wow_gemfile_free(&gemfile);
                return 1;
            }
        } else {
            wow_gem_constraints_parse(">= 0", &root_cs[i]);
        }
    }

    /* 3. Resolve */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    wow_ci_provider ci;
    wow_ci_provider_init(&ci, source, &pool, NULL);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    printf("Resolving dependencies for %s...\n", gemfile_path);
    fflush(stdout);

    int rc = wow_solve(&solver, root_names, root_cs, gemfile.n_deps);
    if (rc != 0) {
        fprintf(stderr, "\nResolution failed:\n%s\n", solver.error_msg);
        wow_solver_destroy(&solver);
        wow_ci_provider_destroy(&ci);
        wow_http_pool_cleanup(&pool);
        free(root_names); free(root_cs);
        wow_gemfile_free(&gemfile);
        return 1;
    }

    printf("Resolved %d packages.\n", solver.n_solved);

    /* 4. Sort solution alphabetically */
    qsort(solver.solution, (size_t)solver.n_solved,
          sizeof(wow_resolved_pkg), resolved_cmp);

    /* 5. Write Gemfile.lock */
    if (wow_write_lockfile("Gemfile.lock", &solver, &prov, &gemfile, source) != 0) {
        wow_solver_destroy(&solver);
        wow_ci_provider_destroy(&ci);
        wow_http_pool_cleanup(&pool);
        free(root_names); free(root_cs);
        wow_gemfile_free(&gemfile);
        return 1;
    }

    printf("Wrote Gemfile.lock\n");

    /* Cleanup */
    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);
    free(root_names);
    free(root_cs);
    wow_gemfile_free(&gemfile);
    return 0;
}
