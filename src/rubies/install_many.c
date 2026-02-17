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

    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;
    if (wow_mkdirs(base, 0755) != 0) return -1;

    /* Phase 1: resolve versions and filter already-installed */
    char full_vers[MAX_BATCH][32];
    char urls[MAX_BATCH][512];
    char tmp_paths[MAX_BATCH][WOW_WPATH];
    char labels[MAX_BATCH][128];
    int  need_download[MAX_BATCH];
    int  n_to_download = 0;

    if (n > MAX_BATCH) n = MAX_BATCH;

    for (int i = 0; i < n; i++) {
        need_download[i] = 0;

        if (wow_resolve_ruby_version(versions[i], full_vers[i],
                                     sizeof(full_vers[i])) != 0)
            continue;

        char install_dir[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(install_dir, sizeof(install_dir), "%s/ruby-%s-%s",
                 base, full_vers[i], rb_plat);
#pragma GCC diagnostic pop
        struct stat st;
        if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("Ruby %s already installed\n", full_vers[i]);
            continue;
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(urls[i], sizeof(urls[i]),
                 "https://github.com/ruby/ruby-builder/releases/download/"
                 "toolcache/ruby-%s-%s.tar.gz", full_vers[i], rb_plat);
        snprintf(tmp_paths[i], sizeof(tmp_paths[i]),
                 "%s/.temp-ruby-%s-%s.tar.gz", base, full_vers[i], rb_plat);
        snprintf(labels[i], sizeof(labels[i]),
                 "ruby-%s-%s", full_vers[i], rb_plat);
#pragma GCC diagnostic pop

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

        if (!results[s].ok) {
            fprintf(stderr, "wow: download failed for Ruby %s\n", full_vers[vi]);
            continue;
        }

        char staging[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(staging, sizeof(staging), "%s/.temp-ruby-%s-%s",
                 base, full_vers[vi], rb_plat);
#pragma GCC diagnostic pop
        if (wow_mkdirs(staging, 0755) != 0) {
            unlink(tmp_paths[vi]);
            continue;
        }

        int rc = wow_tar_extract_gz(tmp_paths[vi], staging, 1);
        unlink(tmp_paths[vi]);

        if (rc != 0) {
            fprintf(stderr, "wow: extraction failed for Ruby %s\n", full_vers[vi]);
            continue;
        }

        char install_dir[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(install_dir, sizeof(install_dir), "%s/ruby-%s-%s",
                 base, full_vers[vi], rb_plat);
#pragma GCC diagnostic pop

        if (rename(staging, install_dir) != 0) {
            fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                    staging, install_dir, strerror(errno));
            continue;
        }

        /* Minor-version symlink */
        char minor_ver[16] = {0};
        const char *last_dot = strrchr(full_vers[vi], '.');
        if (last_dot) {
            size_t mlen = (size_t)(last_dot - full_vers[vi]);
            if (mlen < sizeof(minor_ver)) {
                memcpy(minor_ver, full_vers[vi], mlen);
                minor_ver[mlen] = '\0';
            }
        }
        if (minor_ver[0]) {
            char sympath[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(sympath, sizeof(sympath), "%s/ruby-%s-%s",
                     base, minor_ver, rb_plat);
#pragma GCC diagnostic pop
            unlink(sympath);
            char symtarget[256];
            snprintf(symtarget, sizeof(symtarget), "ruby-%s-%s",
                     full_vers[vi], rb_plat);
            symlink(symtarget, sympath);
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
