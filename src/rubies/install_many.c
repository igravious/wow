/*
 * rubies/install_many.c — Parallel Ruby installation
 *
 * Phase 7→3: Download multiple Ruby versions in parallel,
 * then extract and install sequentially.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/http.h"
#include "wow/download.h"
#include "wow/internal/util.h"
#include "wow/rubies.h"
#include "wow/rubies/internal.h"
#include "wow/tar.h"

#define MAX_BATCH 64

int wow_ruby_install_many(const char **versions, int n)
{
    if (n <= 0) return -1;

    wow_platform_t plat;
    wow_detect_platform(&plat);
    const char *rb_plat = wow_ruby_builder_platform(&plat);
    if (!rb_plat) {
        fprintf(stderr, "wow: no pre-built Ruby for %s\n", plat.wow_id);
        return -1;
    }

    char base[WOW_DIR_PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;
    if (wow_mkdirs(base, 0755) != 0) return -1;

    /* Phase 1: resolve versions and filter already-installed */
    char full_vers[MAX_BATCH][32];
    char urls[MAX_BATCH][512];
    char tmp_paths[MAX_BATCH][WOW_OS_PATH_MAX];
    char labels[MAX_BATCH][128];
    int  need_download[MAX_BATCH];
    int  n_to_download = 0;

    if (n > MAX_BATCH) n = MAX_BATCH;

    for (int i = 0; i < n; i++) {
        need_download[i] = 0;

        if (wow_resolve_ruby_version(versions[i], full_vers[i],
                                     sizeof(full_vers[i])) != 0)
            continue;

        /* Copy to a local char[32] so GCC's aarch64 backend sees a
         * bounded buffer (it otherwise computes 2048 bytes remaining
         * in the 2-D array and warns about truncation). */
        char fv[32];
        memcpy(fv, full_vers[i], sizeof(fv));

        char install_dir[WOW_OS_PATH_MAX];
        snprintf(install_dir, sizeof(install_dir), "%s/%s", base, fv);
        struct stat st;
        if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("Ruby %s already installed\n", fv);
            continue;
        }

        snprintf(urls[i], sizeof(urls[i]),
                 "https://github.com/ruby/ruby-builder/releases/download/"
                 "toolcache/ruby-%s-%s.tar.gz", fv, rb_plat);
        snprintf(tmp_paths[i], sizeof(tmp_paths[i]),
                 "%s/.temp-%s.tar.gz", base, fv);
        snprintf(labels[i], sizeof(labels[i]), "ruby %s", fv);

        need_download[i] = 1;
        n_to_download++;
    }

    if (n_to_download == 0) return 0;

    /* Phase 2: parallel download */
    wow_download_spec_t specs[MAX_BATCH];
    wow_download_result_t results[MAX_BATCH];
    int spec_to_ver[MAX_BATCH];
    int n_specs = 0;

    for (int i = 0; i < n; i++) {
        if (!need_download[i]) continue;
        specs[n_specs].url       = urls[i];
        specs[n_specs].dest_path = tmp_paths[i];
        specs[n_specs].label     = labels[i];
        spec_to_ver[n_specs]     = i;
        n_specs++;
    }

    double t0 = wow_now_secs();

    int n_downloaded = wow_parallel_download(specs, results, n_specs, 0, 0);
    (void)n_downloaded;

    /* Phase 3: extract + install sequentially */
    int lockfd = wow_rubies_acquire_lock(base);
    if (lockfd < 0) {
        for (int s = 0; s < n_specs; s++) unlink(specs[s].dest_path);
        return -1;
    }

    int n_installed = 0;
    for (int s = 0; s < n_specs; s++) {
        int vi = spec_to_ver[s];
        char fv[32];
        memcpy(fv, full_vers[vi], sizeof(fv));

        if (!results[s].ok) {
            fprintf(stderr, "wow: download failed for Ruby %s\n", fv);
            continue;
        }

        char staging[WOW_OS_PATH_MAX];
        snprintf(staging, sizeof(staging), "%s/.temp-%s", base, fv);
        if (wow_mkdirs(staging, 0755) != 0) {
            unlink(tmp_paths[vi]);
            continue;
        }

        int rc = wow_tar_extract_gz(tmp_paths[vi], staging, 1);
        unlink(tmp_paths[vi]);

        if (rc != 0) {
            fprintf(stderr, "wow: extraction failed for Ruby %s\n", fv);
            continue;
        }

        char install_dir[WOW_OS_PATH_MAX];
        snprintf(install_dir, sizeof(install_dir), "%s/%s", base, fv);

        if (rename(staging, install_dir) != 0) {
            fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                    staging, install_dir, strerror(errno));
            continue;
        }

        n_installed++;
    }

    /* Create shims once at the end */
    if (n_installed > 0) {
        char self_path[PATH_MAX];
        ssize_t self_len = readlink("/proc/self/exe", self_path,
                                    sizeof(self_path) - 1);
        if (self_len > 0) {
            self_path[self_len] = '\0';
            wow_create_shims(self_path);
        }
    }

    wow_rubies_release_lock(lockfd);

    double elapsed = wow_now_secs() - t0;

    /* Print summary */
    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Installed " WOW_ANSI_BOLD "%d Ruby version%s"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                n_installed, n_installed == 1 ? "" : "s", elapsed);
        for (int s = 0; s < n_specs; s++) {
            int vi = spec_to_ver[s];
            if (results[s].ok) {
                fprintf(stderr, " " WOW_ANSI_CYAN "+" WOW_ANSI_RESET " "
                        WOW_ANSI_BOLD "%s" WOW_ANSI_RESET "\n", labels[vi]);
            }
        }
    } else {
        fprintf(stderr, "Installed %d Ruby version%s in %.2fs\n",
                n_installed, n_installed == 1 ? "" : "s", elapsed);
        for (int s = 0; s < n_specs; s++) {
            int vi = spec_to_ver[s];
            if (results[s].ok) fprintf(stderr, " + %s\n", labels[vi]);
        }
    }

    return (n_installed == n_to_download) ? 0 : -1;
}
