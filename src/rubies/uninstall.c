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

    char base[WOW_DIR_PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;

    char install_dir[WOW_OS_PATH_MAX];
    snprintf(install_dir, sizeof(install_dir), "%s/%s", base, full_ver);

    struct stat st;
    if (stat(install_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "wow: Ruby %s is not installed (%s)\n",
                full_ver, install_dir);
        return -1;
    }

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

    wow_rubies_release_lock(lockfd);

    double elapsed = wow_now_secs() - t0;

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Uninstalled " WOW_ANSI_BOLD "Ruby %s"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                full_ver, elapsed);
        fprintf(stderr, " " WOW_ANSI_CYAN "-" WOW_ANSI_RESET " "
                WOW_ANSI_BOLD "ruby %s" WOW_ANSI_RESET "\n", full_ver);
    } else {
        fprintf(stderr, "Uninstalled Ruby %s in %.2fs\n", full_ver, elapsed);
        fprintf(stderr, " - ruby %s\n", full_ver);
    }

    return 0;
}
