/*
 * rubies/install.c — Single Ruby installation
 *
 * Downloads, extracts, and installs one Ruby version using definition
 * files from vendor/ruby-binary/ for URL resolution and SHA-256
 * verification.
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
#include "wow/util/sha256.h"
#include "wow/version.h"

/* ── SHA-256 verification ────────────────────────────────────────── */

static int verify_sha256(const char *path, const char *expected_hex)
{
    char hex[65];
    if (wow_sha256_file(path, hex, sizeof(hex)) != 0)
        return -1;

    if (strcmp(hex, expected_hex) != 0) {
        fprintf(stderr, "wow: SHA-256 mismatch for %s\n"
                "  expected: %s\n"
                "  got:      %s\n", path, expected_hex, hex);
        return -1;
    }
    return 0;
}

/* ── Ruby-builder install (platform-specific tarballs) ───────────── */

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

    /* Parse definition file */
    char def_path[WOW_WPATH];
    snprintf(def_path, sizeof(def_path), "%s/ruby-builder/%s",
             WOW_DEFS_BASE, full_ver);

    wow_def_t def;
    if (wow_def_parse(def_path, &def) != 0) {
        fprintf(stderr, "wow: no definition for Ruby %s\n"
                "  (looked in %s)\n", full_ver, def_path);
        return -1;
    }

    /* Look up platform */
    const wow_def_entry_t *entry = wow_def_find(&def, rb_plat);
    if (!entry) {
        fprintf(stderr, "wow: Ruby %s has no binary for platform %s\n",
                full_ver, rb_plat);
        fprintf(stderr, "  available:");
        for (int i = 0; i < def.n_entries; i++)
            fprintf(stderr, " %s", def.entries[i].name);
        fprintf(stderr, "\n");
        return -1;
    }

    /* Build download URL from template */
    char url[512];
    if (wow_def_url(&def, rb_plat, url, sizeof(url)) != 0) {
        fprintf(stderr, "wow: URL template substitution failed\n");
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

    /* Download to temp file */
    char tmp_tarball[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
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

    /* Verify SHA-256 checksum */
    if (verify_sha256(tmp_tarball, entry->sha256) != 0) {
        fprintf(stderr, "wow: checksum verification failed — aborting install\n");
        unlink(tmp_tarball);
        wow_rubies_release_lock(lockfd);
        return -1;
    }

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

/* ── CosmoRuby install (APE binaries) ────────────────────────────── */

int wow_cosmoruby_install(const char *version)
{
    /* Parse definition file */
    char def_path[WOW_WPATH];
    snprintf(def_path, sizeof(def_path), "%s/cosmoruby/%s",
             WOW_DEFS_BASE, version);

    wow_def_t def;
    if (wow_def_parse(def_path, &def) != 0) {
        fprintf(stderr, "wow: no definition for CosmoRuby %s\n"
                "  (looked in %s)\n", version, def_path);
        return -1;
    }

    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;
    if (wow_mkdirs(base, 0755) != 0) return -1;

    /* Install directory: cosmoruby-{ver}/bin/ */
    char install_dir[WOW_WPATH];
    char bin_dir[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(install_dir, sizeof(install_dir), "%s/cosmoruby-%s",
             base, version);
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin", install_dir);
#pragma GCC diagnostic pop

    struct stat st;
    if (stat(bin_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("CosmoRuby %s already installed\n", version);
        return 0;
    }

    /* Acquire lock */
    int lockfd = wow_rubies_acquire_lock(base);
    if (lockfd < 0) return -1;

    /* Double-check after lock */
    if (stat(bin_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("CosmoRuby %s already installed\n", version);
        wow_rubies_release_lock(lockfd);
        return 0;
    }

    if (wow_mkdirs(bin_dir, 0755) != 0) {
        wow_rubies_release_lock(lockfd);
        return -1;
    }

    double t0 = wow_now_secs();
    int failed = 0;

    /* Download each binary (ruby.com, irb.com) */
    for (int i = 0; i < def.n_entries; i++) {
        const wow_def_entry_t *entry = &def.entries[i];

        char url[512];
        if (wow_def_url(&def, entry->name, url, sizeof(url)) != 0) {
            fprintf(stderr, "wow: URL template substitution failed for %s\n",
                    entry->name);
            failed = 1;
            break;
        }

        char tmp_path[WOW_WPATH];
        char final_path[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(tmp_path, sizeof(tmp_path), "%s/.temp-%s", bin_dir,
                 entry->name);
        snprintf(final_path, sizeof(final_path), "%s/%s", bin_dir,
                 entry->name);
#pragma GCC diagnostic pop

        int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fprintf(stderr, "wow: cannot create %s: %s\n",
                    tmp_path, strerror(errno));
            failed = 1;
            break;
        }

        /* Download with progress tracking */
        char label[128];
        snprintf(label, sizeof(label), "cosmoruby-%s/%s", version,
                 entry->name);
        wow_progress_state_t prog;
        wow_progress_init(&prog, label, 0, NULL);
        int rc = wow_http_download_to_fd(url, fd,
                                          wow_progress_http_callback, &prog);
        close(fd);

        if (rc != 0) {
            wow_progress_cancel(&prog);
            unlink(tmp_path);
            failed = 1;
            break;
        }

        wow_progress_finish(&prog, "Downloaded");

        /* Verify SHA-256 */
        if (verify_sha256(tmp_path, entry->sha256) != 0) {
            fprintf(stderr, "wow: checksum verification failed for %s"
                    " — aborting install\n", entry->name);
            unlink(tmp_path);
            failed = 1;
            break;
        }

        /* Make executable and move into place */
        chmod(tmp_path, 0755);
        if (rename(tmp_path, final_path) != 0) {
            fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                    tmp_path, final_path, strerror(errno));
            unlink(tmp_path);
            failed = 1;
            break;
        }
    }

    if (failed) {
        /* Clean up partial install */
        wow_rubies_rmdir_recursive(install_dir);
        wow_rubies_release_lock(lockfd);
        return -1;
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

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Installed " WOW_ANSI_BOLD "CosmoRuby %s"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                version, elapsed);
        for (int i = 0; i < def.n_entries; i++)
            fprintf(stderr, " " WOW_ANSI_CYAN "+" WOW_ANSI_RESET " "
                    WOW_ANSI_BOLD "%s" WOW_ANSI_RESET "\n",
                    def.entries[i].name);
    } else {
        fprintf(stderr, "Installed CosmoRuby %s in %.2fs\n", version, elapsed);
        for (int i = 0; i < def.n_entries; i++)
            fprintf(stderr, " + %s\n", def.entries[i].name);
    }

    return 0;
}

/* ── Ensure helpers ──────────────────────────────────────────────── */

int wow_ruby_ensure(const char *version)
{
    char bin[WOW_WPATH];
    if (wow_ruby_bin_path(version, bin, sizeof(bin)) == 0)
        return 0;
    return wow_ruby_install(version);
}
