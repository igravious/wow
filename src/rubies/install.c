/*
 * rubies/install.c — Single Ruby installation
 *
 * Downloads, extracts, and installs one Ruby version.
 */

#include <dirent.h>
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
#include "wow/version.h"

int wow_ruby_install(const char *version)
{
    char full_ver[32];
    if (wow_resolve_ruby_version(version, full_ver, sizeof(full_ver)) != 0)
        return -1;

    wow_platform_t plat;
    wow_detect_platform(&plat);
    const char *rb_plat = wow_ruby_builder_platform(&plat);
    if (!rb_plat) {
        if (strcmp(plat.libc, "musl") == 0)
            fprintf(stderr, "wow: no pre-built Ruby for musl — "
                    "source compilation not yet supported\n");
        else
            fprintf(stderr, "wow: no pre-built Ruby for %s\n", plat.wow_id);
        return -1;
    }

    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;
    if (wow_mkdirs(base, 0755) != 0) return -1;

    /* Check if already installed */
    char install_dir[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(install_dir, sizeof(install_dir), "%s/ruby-%s-%s",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop
    struct stat st;
    if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("Ruby %s already installed\n", full_ver);
        return 0;
    }

    /* Acquire lock */
    int lockfd = wow_rubies_acquire_lock(base);
    if (lockfd < 0) return -1;

    /* Double-check after lock */
    if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("Ruby %s already installed\n", full_ver);
        wow_rubies_release_lock(lockfd);
        return 0;
    }

    /* Asset name for status messages */
    char asset_name[128];
    snprintf(asset_name, sizeof(asset_name), "ruby-%s-%s", full_ver, rb_plat);

    double t0 = wow_now_secs();

    /* Download URL */
    char url[512];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(url, sizeof(url),
             "https://github.com/ruby/ruby-builder/releases/download/"
             "toolcache/ruby-%s-%s.tar.gz",
             full_ver, rb_plat);

    /* Download to temp file */
    char tmp_tarball[WOW_WPATH];
    snprintf(tmp_tarball, sizeof(tmp_tarball), "%s/.temp-ruby-%s-%s.tar.gz",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop

    int fd = open(tmp_tarball, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        fprintf(stderr, "wow: cannot create %s: %s\n",
                tmp_tarball, strerror(errno));
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    /* Download with progress tracking */
    wow_progress_state_t prog;
    wow_progress_init(&prog, asset_name, 0, NULL);
    int rc = wow_http_download_to_fd(url, fd, wow_progress_http_callback, &prog);
    close(fd);

    if (rc != 0) {
        wow_progress_cancel(&prog);
        unlink(tmp_tarball);
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    wow_progress_finish(&prog, "Downloaded");

    /* Extract to staging directory */
    char staging[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(staging, sizeof(staging), "%s/.temp-ruby-%s-%s",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop

    if (wow_mkdirs(staging, 0755) != 0) {
        unlink(tmp_tarball);
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    rc = wow_tar_extract_gz(tmp_tarball, staging, 1);
    unlink(tmp_tarball);

    if (rc != 0) {
        fprintf(stderr, "wow: extraction failed\n");
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    /* Atomic rename: staging → final */
    if (rename(staging, install_dir) != 0) {
        fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                staging, install_dir, strerror(errno));
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    /* Minor-version symlink */
    char minor_ver[16] = {0};
    const char *last_dot = strrchr(full_ver, '.');
    if (last_dot) {
        size_t mlen = (size_t)(last_dot - full_ver);
        if (mlen < sizeof(minor_ver)) {
            memcpy(minor_ver, full_ver, mlen);
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
                 full_ver, rb_plat);
        if (symlink(symtarget, sympath) != 0) {
            fprintf(stderr, "wow: warning: could not create symlink "
                    "%s -> %s: %s\n", sympath, symtarget, strerror(errno));
        }
    }

    /* Create shims */
    char self_path[PATH_MAX];
    ssize_t self_len = readlink("/proc/self/exe", self_path,
                                sizeof(self_path) - 1);
    if (self_len > 0) {
        self_path[self_len] = '\0';
        wow_create_shims(self_path);
    }

    wow_rubies_release_lock(lockfd);

    double elapsed = wow_now_secs() - t0;

    /* Minor version for summary */
    char short_name[64];
    if (minor_ver[0])
        snprintf(short_name, sizeof(short_name), "ruby%s", minor_ver);
    else
        snprintf(short_name, sizeof(short_name), "ruby%s", full_ver);

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Installed " WOW_ANSI_BOLD "Ruby %s"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                full_ver, elapsed);
        fprintf(stderr, " " WOW_ANSI_CYAN "+" WOW_ANSI_RESET " "
                WOW_ANSI_BOLD "%s" WOW_ANSI_RESET " " WOW_ANSI_DIM "(%s)"
                WOW_ANSI_RESET "\n", asset_name, short_name);
    } else {
        fprintf(stderr, "Installed Ruby %s in %.2fs\n", full_ver, elapsed);
        fprintf(stderr, " + %s (%s)\n", asset_name, short_name);
    }

    return 0;
}

int wow_ruby_ensure(const char *version)
{
    char bin[WOW_WPATH];
    if (wow_ruby_bin_path(version, bin, sizeof(bin)) == 0)
        return 0;
    return wow_ruby_install(version);
}
