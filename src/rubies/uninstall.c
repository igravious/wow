/*
 * rubies/uninstall.c — Uninstall Ruby versions
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/internal/util.h"
#include "wow/rubies.h"
#include "wow/rubies/internal.h"

int wow_ruby_uninstall(const char *version)
{
    char full_ver[32];
    if (wow_resolve_ruby_version(version, full_ver, sizeof(full_ver)) != 0) {
        snprintf(full_ver, sizeof(full_ver), "%s", version);
    }

    wow_platform_t plat;
    wow_detect_platform(&plat);
    const char *rb_plat = wow_ruby_builder_platform(&plat);
    if (!rb_plat) {
        fprintf(stderr, "wow: no pre-built Ruby for %s\n", plat.wow_id);
        return -1;
    }

    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;

    char install_dir[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(install_dir, sizeof(install_dir), "%s/ruby-%s-%s",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop

    struct stat st;
    if (stat(install_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "wow: Ruby %s is not installed (%s)\n",
                full_ver, install_dir);
        return -1;
    }

    /* Extract minor version for symlink update */
    char minor_ver[16] = {0};
    const char *last_dot = strrchr(full_ver, '.');
    if (last_dot) {
        size_t mlen = (size_t)(last_dot - full_ver);
        if (mlen < sizeof(minor_ver)) {
            memcpy(minor_ver, full_ver, mlen);
            minor_ver[mlen] = '\0';
        }
    }

    char asset_name[128];
    snprintf(asset_name, sizeof(asset_name), "ruby-%s-%s", full_ver, rb_plat);

    double t0 = wow_now_secs();

    int lockfd = wow_rubies_acquire_lock(base);
    if (lockfd < 0) return -1;

    if (stat(install_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "wow: Ruby %s is not installed\n", full_ver);
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    if (wow_rubies_rmdir_recursive(install_dir) != 0) {
        fprintf(stderr, "wow: failed to remove %s: %s\n",
                install_dir, strerror(errno));
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    char short_name[64] = {0};
    if (minor_ver[0]) {
        char sympath[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(sympath, sizeof(sympath), "%s/ruby-%s-%s",
                 base, minor_ver, rb_plat);
#pragma GCC diagnostic pop

        char remaining[32];
        if (wow_rubies_find_highest_patch(base, minor_ver, rb_plat, remaining, sizeof(remaining))) {
            unlink(sympath);
            char symtarget[256];
            snprintf(symtarget, sizeof(symtarget), "ruby-%s-%s", remaining, rb_plat);
            if (symlink(symtarget, sympath) == 0) {
                snprintf(short_name, sizeof(short_name), "ruby%s", minor_ver);
            }
        } else {
            unlink(sympath);
            snprintf(short_name, sizeof(short_name), "ruby%s", minor_ver);
        }
    }

    wow_rubies_release_lock(lockfd);

    double elapsed = wow_now_secs() - t0;

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Uninstalled " WOW_ANSI_BOLD "Ruby %s"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                full_ver, elapsed);
        fprintf(stderr, " " WOW_ANSI_CYAN "-" WOW_ANSI_RESET " "
                WOW_ANSI_BOLD "%s" WOW_ANSI_RESET, asset_name);
        if (short_name[0]) {
            fprintf(stderr, " " WOW_ANSI_DIM "(%s)" WOW_ANSI_RESET, short_name);
        }
        fprintf(stderr, "\n");
    } else {
        fprintf(stderr, "Uninstalled Ruby %s in %.2fs\n", full_ver, elapsed);
        fprintf(stderr, " - %s", asset_name);
        if (short_name[0]) {
            fprintf(stderr, " (%s)", short_name);
        }
        fprintf(stderr, "\n");
    }

    return 0;
}
