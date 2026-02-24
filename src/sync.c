/*
 * sync.c -- `wow sync` orchestrator
 *
 * Pipeline:
 *   1. Parse Gemfile
 *   2. Read .ruby-version → derive Ruby API version
 *   3. Resolve deps (PubGrub via compact index)
 *   4. Write Gemfile.lock
 *   5. Diff installed vs solved — find missing gems
 *   6. Download missing .gem files (parallel)
 *   7. Unpack missing gems to vendor/bundle/ruby/<api>/gems/<name>-<ver>/
 *   8. Print uv-style summary
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "wow/common.h"
#include "wow/download.h"
#include "wow/gemfile.h"
#include "wow/gems.h"
#include "wow/http.h"
#include "wow/resolver.h"
#include "wow/rubies.h"
#include "wow/sync.h"
#include "wow/util.h"
#include "wow/version.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Format elapsed time: < 1s → "340ms", >= 1s → "1.23s". */
static void fmt_elapsed(double secs, char *buf, size_t bufsz)
{
    if (secs < 1.0)
        snprintf(buf, bufsz, "%.0fms", secs * 1000.0);
    else
        snprintf(buf, bufsz, "%.2fs", secs);
}

/* qsort comparator for wow_resolved_pkg by name */
static int resolved_cmp(const void *a, const void *b)
{
    const wow_resolved_pkg *pa = a, *pb = b;
    return strcmp(pa->name, pb->name);
}

/* ------------------------------------------------------------------ */
/* cmd_sync                                                            */
/* ------------------------------------------------------------------ */

int cmd_sync(int argc, char *argv[])
{
    (void)argc; (void)argv;

    int ret = 1;
    int colour = wow_use_colour();
    double t_start = wow_now_secs();

    /* ---- 1. Parse Gemfile ---- */
    struct wow_gemfile gf;
    wow_gemfile_init(&gf);
    if (wow_gemfile_parse_file("Gemfile", &gf) != 0) {
        fprintf(stderr, "wow: failed to parse Gemfile\n");
        wow_gemfile_free(&gf);
        return 1;
    }

    if (gf.n_deps == 0) {
        fprintf(stderr, "wow: no gems in Gemfile\n");
        wow_gemfile_free(&gf);
        return 1;
    }

    const char *source = gf.source ? gf.source : "https://rubygems.org";

    /* ---- 2. Read .ruby-version ---- */
    char ruby_full[32];
    if (wow_find_ruby_version(ruby_full, sizeof(ruby_full)) != 0) {
        fprintf(stderr, "wow: no .ruby-version found "
                "(looked from cwd to /)\n");
        wow_gemfile_free(&gf);
        return 1;
    }

    char ruby_api[16];
    wow_ruby_api_version(ruby_full, ruby_api, sizeof(ruby_api));

    /* ---- 3. Convert Gemfile deps to solver roots ---- */
    int n_roots = (int)gf.n_deps;
    const char **root_names = calloc((size_t)n_roots, sizeof(char *));
    wow_gem_constraints *root_cs = calloc((size_t)n_roots,
                                          sizeof(wow_gem_constraints));
    if (!root_names || !root_cs) {
        fprintf(stderr, "wow: out of memory\n");
        free(root_names); free(root_cs);
        wow_gemfile_free(&gf);
        return 1;
    }

    for (int i = 0; i < n_roots; i++) {
        root_names[i] = gf.deps[i].name;

        if (gf.deps[i].n_constraints > 0) {
            char joined[512];
            wow_join_constraints(gf.deps[i].constraints,
                                 gf.deps[i].n_constraints,
                                 joined, sizeof(joined));
            if (wow_gem_constraints_parse(joined, &root_cs[i]) != 0) {
                fprintf(stderr, "wow: invalid constraint for %s: %s\n",
                        gf.deps[i].name, joined);
                free(root_names); free(root_cs);
                wow_gemfile_free(&gf);
                return 1;
            }
        } else {
            wow_gem_constraints_parse(">= 0", &root_cs[i]);
        }
    }

    /* ---- 4. Resolve ---- */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    wow_ci_provider ci;
    wow_ci_provider_init(&ci, source, &pool, NULL);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    double t_resolve_start = wow_now_secs();

    int rc = wow_solve(&solver, root_names, root_cs, n_roots);
    if (rc != 0) {
        fprintf(stderr, "Resolution failed:\n%s\n", solver.error_msg);
        goto cleanup;
    }

    double t_resolve_end = wow_now_secs();

    /* Sort solution alphabetically */
    qsort(solver.solution, (size_t)solver.n_solved,
          sizeof(wow_resolved_pkg), resolved_cmp);

    /* ---- 5. Write Gemfile.lock ---- */
    if (wow_write_lockfile("Gemfile.lock", &solver, &prov, &gf, source) != 0)
        goto cleanup;

    /* ---- 6. Diff installed — find missing gems ---- */
    int n_solved = solver.n_solved;
    int *missing = calloc((size_t)n_solved, sizeof(int));
    if (!missing) {
        fprintf(stderr, "wow: out of memory\n");
        goto cleanup;
    }

    int n_missing = 0;
    for (int i = 0; i < n_solved; i++) {
        char gem_dir[WOW_OS_PATH_MAX];
        snprintf(gem_dir, sizeof(gem_dir),
                 "vendor/bundle/ruby/%s/gems/%s-%s",
                 ruby_api, solver.solution[i].name,
                 solver.solution[i].version.raw);

        struct stat st;
        if (stat(gem_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            missing[n_missing++] = i;
        }
    }

    if (n_missing == 0) {
        /* Nothing to do — audit output */
        char elapsed_buf[32];
        fmt_elapsed(wow_now_secs() - t_start, elapsed_buf,
                    sizeof(elapsed_buf));
        if (colour)
            fprintf(stderr,
                    WOW_ANSI_GREEN WOW_ANSI_BOLD "Audited "
                    WOW_ANSI_RESET "%d packages in "
                    WOW_ANSI_DIM "%s" WOW_ANSI_RESET "\n",
                    n_solved, elapsed_buf);
        else
            fprintf(stderr, "Audited %d packages in %s\n",
                    n_solved, elapsed_buf);
        ret = 0;
        free(missing);
        goto cleanup;
    }

    /* ---- 7. Download missing .gem files ---- */

    /* Ensure cache directory exists */
    char cache_dir[WOW_DIR_PATH_MAX];
    if (wow_gem_cache_dir(cache_dir, sizeof(cache_dir)) != 0) {
        free(missing);
        goto cleanup;
    }
    {
        char cache_dir_mut[WOW_DIR_PATH_MAX];
        snprintf(cache_dir_mut, sizeof(cache_dir_mut), "%s", cache_dir);
        wow_mkdirs(cache_dir_mut, 0755);
    }

    /* Build download specs — only for gems not already cached */
    wow_download_spec_t *specs = calloc((size_t)n_missing,
                                         sizeof(wow_download_spec_t));
    wow_download_result_t *results = calloc((size_t)n_missing,
                                             sizeof(wow_download_result_t));
    /* Flat URL/path/label buffers */
    char (*urls)[512] = calloc((size_t)n_missing, 512);
    char (*paths)[WOW_OS_PATH_MAX] = calloc((size_t)n_missing,
                                             WOW_OS_PATH_MAX);
    char (*labels)[256] = calloc((size_t)n_missing, 256);

    if (!specs || !results || !urls || !paths || !labels) {
        fprintf(stderr, "wow: out of memory\n");
        free(specs); free(results); free(urls); free(paths); free(labels);
        free(missing);
        goto cleanup;
    }

    /* Build source URL base, stripping trailing slash */
    char src_base[512];
    snprintf(src_base, sizeof(src_base), "%s", source);
    size_t slen = strlen(src_base);
    if (slen > 0 && src_base[slen - 1] == '/')
        src_base[slen - 1] = '\0';

    int n_to_download = 0;
    int *download_map = calloc((size_t)n_missing, sizeof(int));
    if (!download_map) {
        fprintf(stderr, "wow: out of memory\n");
        free(specs); free(results); free(urls); free(paths); free(labels);
        free(missing);
        goto cleanup;
    }

    for (int m = 0; m < n_missing; m++) {
        int si = missing[m];
        const char *name = solver.solution[si].name;
        const char *ver = solver.solution[si].version.raw;

        /* Check cache first */
        char cached_path[WOW_OS_PATH_MAX];
        snprintf(cached_path, sizeof(cached_path), "%s/%s-%s.gem",
                 cache_dir, name, ver);

        struct stat st;
        if (stat(cached_path, &st) == 0 && st.st_size > 0) {
            /* Already cached — no download needed */
            continue;
        }

        int d = n_to_download;
        snprintf(urls[d], 512, "%s/downloads/%s-%s.gem",
                 src_base, name, ver);
        snprintf(paths[d], WOW_OS_PATH_MAX, "%s/%s-%s.gem",
                 cache_dir, name, ver);
        snprintf(labels[d], 256, "%s-%s.gem", name, ver);

        specs[d].url = urls[d];
        specs[d].dest_path = paths[d];
        specs[d].label = labels[d];
        download_map[d] = m;  /* which missing index this download is for */
        n_to_download++;
    }

    double t_download_start = wow_now_secs();
    double t_download_end = t_download_start;

    if (n_to_download > 0) {
        int ok = wow_parallel_download(specs, results, n_to_download, 0, 0);
        t_download_end = wow_now_secs();

        if (ok < n_to_download) {
            /* Find first failure and report */
            for (int d = 0; d < n_to_download; d++) {
                if (!results[d].ok) {
                    fprintf(stderr, "wow: download failed: %s\n",
                            specs[d].label);
                    break;
                }
            }
            free(specs); free(results); free(urls); free(paths);
            free(labels); free(download_map); free(missing);
            goto cleanup;
        }
    }

    /* ---- 8. Unpack missing gems ---- */
    double t_install_start = wow_now_secs();

    /* Ensure vendor bundle base directory exists */
    {
        char vendor_base[WOW_OS_PATH_MAX];
        snprintf(vendor_base, sizeof(vendor_base),
                 "vendor/bundle/ruby/%s/gems", ruby_api);
        wow_mkdirs(vendor_base, 0755);
    }

    for (int m = 0; m < n_missing; m++) {
        int si = missing[m];
        const char *name = solver.solution[si].name;
        const char *ver = solver.solution[si].version.raw;

        char gem_path[WOW_OS_PATH_MAX];
        snprintf(gem_path, sizeof(gem_path), "%s/%s-%s.gem",
                 cache_dir, name, ver);

        char dest_dir[WOW_OS_PATH_MAX];
        snprintf(dest_dir, sizeof(dest_dir),
                 "vendor/bundle/ruby/%s/gems/%s-%s",
                 ruby_api, name, ver);

        if (wow_gem_unpack_q(gem_path, dest_dir, 1) != 0) {
            fprintf(stderr, "wow: failed to unpack %s-%s\n", name, ver);
            free(specs); free(results); free(urls); free(paths);
            free(labels); free(download_map); free(missing);
            goto cleanup;
        }
    }

    double t_install_end = wow_now_secs();

    /* ---- 9. Print uv-style summary ---- */
    /* IMPORTANT: print before solver_destroy (arena owns name strings) */
    {
        char resolve_buf[32], download_buf[32], install_buf[32];
        fmt_elapsed(t_resolve_end - t_resolve_start, resolve_buf,
                    sizeof(resolve_buf));

        if (colour) {
            fprintf(stderr,
                    WOW_ANSI_GREEN WOW_ANSI_BOLD "Resolved "
                    WOW_ANSI_RESET "%d packages in "
                    WOW_ANSI_DIM "%s" WOW_ANSI_RESET "\n",
                    n_solved, resolve_buf);
        } else {
            fprintf(stderr, "Resolved %d packages in %s\n",
                    n_solved, resolve_buf);
        }

        if (n_to_download > 0) {
            fmt_elapsed(t_download_end - t_download_start, download_buf,
                        sizeof(download_buf));
            if (colour) {
                fprintf(stderr,
                        WOW_ANSI_GREEN WOW_ANSI_BOLD "Downloaded "
                        WOW_ANSI_RESET "%d packages in "
                        WOW_ANSI_DIM "%s" WOW_ANSI_RESET "\n",
                        n_to_download, download_buf);
            } else {
                fprintf(stderr, "Downloaded %d packages in %s\n",
                        n_to_download, download_buf);
            }
        }

        fmt_elapsed(t_install_end - t_install_start, install_buf,
                    sizeof(install_buf));
        if (colour) {
            fprintf(stderr,
                    WOW_ANSI_GREEN WOW_ANSI_BOLD "Installed "
                    WOW_ANSI_RESET "%d packages in "
                    WOW_ANSI_DIM "%s" WOW_ANSI_RESET "\n",
                    n_missing, install_buf);
        } else {
            fprintf(stderr, "Installed %d packages in %s\n",
                    n_missing, install_buf);
        }

        /* List each newly installed gem */
        for (int m = 0; m < n_missing; m++) {
            int si = missing[m];
            if (colour)
                fprintf(stderr,
                        " " WOW_ANSI_GREEN "+" WOW_ANSI_RESET
                        " " WOW_ANSI_BOLD "%s" WOW_ANSI_RESET
                        " (%s)\n",
                        solver.solution[si].name,
                        solver.solution[si].version.raw);
            else
                fprintf(stderr, " + %s (%s)\n",
                        solver.solution[si].name,
                        solver.solution[si].version.raw);
        }
    }

    ret = 0;

    free(specs); free(results); free(urls); free(paths);
    free(labels); free(download_map); free(missing);

cleanup:
    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);
    free(root_names);
    free(root_cs);
    wow_gemfile_free(&gf);
    return ret;
}
