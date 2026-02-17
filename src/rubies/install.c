/*
 * rubies/install.c — Ruby installation, uninstallation, and listing
 *
 * Part of wow's Ruby version manager (replaces rbenv + ruby-build).
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "wow/http.h"
#include "wow/download.h"
#include "wow/rubies.h"
#include "wow/tar.h"
#include "wow/version.h"

/* PATH_MAX extension for composite paths */
#define WPATH  (PATH_MAX + 256)

/* File locking timeout */
#define LOCK_MAX_WAIT_MS  45000

/* ANSI escape sequences */
#define ANSI_BOLD      "\033[1m"
#define ANSI_DIM       "\033[2m"
#define ANSI_CYAN      "\033[36m"
#define ANSI_RESET     "\033[0m"

/* ── Internal helpers ─────────────────────────────────────────────── */

static int use_colour(void) { return isatty(STDERR_FILENO); }

static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int mkdirs(char *path, mode_t mode)
{
    char *p = path;
    if (*p == '/') p++;
    for (; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, mode) != 0 && errno != EEXIST) {
            fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int acquire_lock(const char *base_dir)
{
    char lockpath[WPATH];
    snprintf(lockpath, sizeof(lockpath), "%s/.lock", base_dir);

    int fd = open(lockpath, O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        fprintf(stderr, "wow: cannot create lock %s: %s\n",
                lockpath, strerror(errno));
        return -1;
    }

    /* Write PID for diagnostics */
    char pidbuf[32];
    int plen = snprintf(pidbuf, sizeof(pidbuf), "%d\n", (int)getpid());
    (void)write(fd, pidbuf, (size_t)plen);

    /* Try non-blocking first */
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return fd;

    /* Exponential backoff */
    fprintf(stderr, "wow: waiting for lock...\n");
    int waited_ms = 0;
    int sleep_ms = 100;
    while (waited_ms < LOCK_MAX_WAIT_MS) {
        usleep((unsigned)(sleep_ms * 1000));
        waited_ms += sleep_ms;
        if (flock(fd, LOCK_EX | LOCK_NB) == 0)
            return fd;
        sleep_ms *= 2;
        if (sleep_ms > 5000) sleep_ms = 5000;
    }

    fprintf(stderr, "wow: timed out waiting for lock (%s)\n", lockpath);
    close(fd);
    return -1;
}

static void release_lock(int fd)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static int rmdir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *ent;
    int ret = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char fullpath[WPATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);

        struct stat st;
        if (lstat(fullpath, &st) != 0) {
            ret = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (rmdir_recursive(fullpath) != 0) ret = -1;
        } else {
            if (unlink(fullpath) != 0) ret = -1;
        }
    }
    closedir(d);

    if (rmdir(path) != 0) return -1;
    return ret;
}

static int find_highest_patch(const char *base_dir, const char *minor_ver,
                              const char *plat, char *out_ver, size_t outsz)
{
    DIR *d = opendir(base_dir);
    if (!d) return 0;

    char prefix[64];
    snprintf(prefix, sizeof(prefix), "ruby-%s.", minor_ver);
    size_t prefix_len = strlen(prefix);

    char highest[32] = {0};
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) != 0)
            continue;
        if (!strstr(ent->d_name, plat))
            continue;

        /* Skip symlinks (minor-version aliases) */
        char full_path[WPATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, ent->d_name);
        struct stat st;
        if (lstat(full_path, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        /* Extract version from "ruby-X.Y.Z-plat" */
        const char *ver_start = ent->d_name + 5; /* skip "ruby-" */
        const char *dash = strstr(ver_start, plat);
        if (!dash) continue;
        size_t ver_len = dash - ver_start - 1;
        if (ver_len >= sizeof(highest)) continue;

        char this_ver[32];
        memcpy(this_ver, ver_start, ver_len);
        this_ver[ver_len] = '\0';

        if (highest[0] == 0 || strcmp(this_ver, highest) > 0)
            memcpy(highest, this_ver, ver_len + 1);
    }
    closedir(d);

    if (highest[0]) {
        snprintf(out_ver, outsz, "%s", highest);
        return 1;
    }
    return 0;
}

/* ── Single Ruby install ──────────────────────────────────────────── */

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
    if (mkdirs(base, 0755) != 0) return -1;

    /* Check if already installed */
    char install_dir[WPATH];
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
    int lockfd = acquire_lock(base);
    if (lockfd < 0) return -1;

    /* Double-check after lock */
    if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("Ruby %s already installed\n", full_ver);
        release_lock(lockfd);
        return 0;
    }

    /* Asset name for status messages */
    char asset_name[128];
    snprintf(asset_name, sizeof(asset_name), "ruby-%s-%s", full_ver, rb_plat);

    double t0 = now_secs();

    /* Download URL */
    char url[512];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(url, sizeof(url),
             "https://github.com/ruby/ruby-builder/releases/download/"
             "toolcache/ruby-%s-%s.tar.gz",
             full_ver, rb_plat);

    /* Download to temp file */
    char tmp_tarball[WPATH];
    snprintf(tmp_tarball, sizeof(tmp_tarball), "%s/.temp-ruby-%s-%s.tar.gz",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop

    int fd = open(tmp_tarball, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        fprintf(stderr, "wow: cannot create %s: %s\n",
                tmp_tarball, strerror(errno));
        release_lock(lockfd);
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
        release_lock(lockfd);
        return -1;
    }

    wow_progress_finish(&prog, "Downloaded");

    /* Extract to staging directory */
    char staging[WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(staging, sizeof(staging), "%s/.temp-ruby-%s-%s",
             base, full_ver, rb_plat);
#pragma GCC diagnostic pop

    if (mkdirs(staging, 0755) != 0) {
        unlink(tmp_tarball);
        release_lock(lockfd);
        return -1;
    }

    rc = wow_tar_extract_gz(tmp_tarball, staging, 1);
    unlink(tmp_tarball);

    if (rc != 0) {
        fprintf(stderr, "wow: extraction failed\n");
        release_lock(lockfd);
        return -1;
    }

    /* Atomic rename: staging → final */
    if (rename(staging, install_dir) != 0) {
        fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                staging, install_dir, strerror(errno));
        release_lock(lockfd);
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
        char sympath[WPATH];
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

    release_lock(lockfd);

    double elapsed = now_secs() - t0;

    /* Minor version for summary */
    char short_name[64];
    if (minor_ver[0])
        snprintf(short_name, sizeof(short_name), "ruby%s", minor_ver);
    else
        snprintf(short_name, sizeof(short_name), "ruby%s", full_ver);

    if (use_colour()) {
        fprintf(stderr, ANSI_DIM "Installed " ANSI_BOLD "Ruby %s"
                ANSI_RESET ANSI_DIM " in %.2fs" ANSI_RESET "\n",
                full_ver, elapsed);
        fprintf(stderr, " " ANSI_CYAN "+" ANSI_RESET " "
                ANSI_BOLD "%s" ANSI_RESET " " ANSI_DIM "(%s)"
                ANSI_RESET "\n", asset_name, short_name);
    } else {
        fprintf(stderr, "Installed Ruby %s in %.2fs\n", full_ver, elapsed);
        fprintf(stderr, " + %s (%s)\n", asset_name, short_name);
    }

    return 0;
}

/* ── Ruby list ────────────────────────────────────────────────────── */

int wow_ruby_list(const char *active_version)
{
    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;

    DIR *d = opendir(base);
    if (!d) {
        printf("No Ruby installations found.\n");
        return 0;
    }

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "ruby-", 5) != 0) continue;

        /* Skip symlinks (minor-version aliases) */
        char full_path[WPATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", base, ent->d_name);
        struct stat st;
        if (lstat(full_path, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        int active = 0;
        if (active_version) {
            const char *ver_start = ent->d_name + 5;
            if (strstr(ver_start, active_version) == ver_start)
                active = 1;
        }

        printf("  %s%s\n", ent->d_name, active ? "  (active)" : "");
        count++;
    }
    closedir(d);

    if (count == 0)
        printf("No Ruby installations found.\n");

    return 0;
}

/* ── Ruby uninstall ───────────────────────────────────────────────── */

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

    char install_dir[WPATH];
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

    double t0 = now_secs();

    int lockfd = acquire_lock(base);
    if (lockfd < 0) return -1;

    if (stat(install_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "wow: Ruby %s is not installed\n", full_ver);
        release_lock(lockfd);
        return -1;
    }

    if (rmdir_recursive(install_dir) != 0) {
        fprintf(stderr, "wow: failed to remove %s: %s\n",
                install_dir, strerror(errno));
        release_lock(lockfd);
        return -1;
    }

    char short_name[64] = {0};
    if (minor_ver[0]) {
        char sympath[WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(sympath, sizeof(sympath), "%s/ruby-%s-%s",
                 base, minor_ver, rb_plat);
#pragma GCC diagnostic pop

        char remaining[32];
        if (find_highest_patch(base, minor_ver, rb_plat, remaining, sizeof(remaining))) {
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

    release_lock(lockfd);

    double elapsed = now_secs() - t0;

    if (use_colour()) {
        fprintf(stderr, ANSI_DIM "Uninstalled " ANSI_BOLD "Ruby %s"
                ANSI_RESET ANSI_DIM " in %.2fs" ANSI_RESET "\n",
                full_ver, elapsed);
        fprintf(stderr, " " ANSI_CYAN "-" ANSI_RESET " "
                ANSI_BOLD "%s" ANSI_RESET, asset_name);
        if (short_name[0]) {
            fprintf(stderr, " " ANSI_DIM "(%s)" ANSI_RESET, short_name);
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

/* ── Ensure (install if needed) ───────────────────────────────────── */

int wow_ruby_ensure(const char *version)
{
    char bin[WPATH];
    if (wow_ruby_bin_path(version, bin, sizeof(bin)) == 0)
        return 0;
    return wow_ruby_install(version);
}

/* ── Subcommand handler ──────────────────────────────────────────── */

#include <string.h>

int cmd_ruby(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow ruby <command>\n\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  install [version...]  Download and install Ruby (parallel if multiple)\n");
        fprintf(stderr, "  uninstall <version>   Remove an installed Ruby version\n");
        fprintf(stderr, "  list                  List installed Ruby versions\n");
        return 1;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "install") == 0) {
        if (argc >= 4) {
            /* Multiple versions — parallel install */
            const char *vers[64];
            int nv = argc - 2;
            if (nv > 64) nv = 64;
            for (int i = 0; i < nv; i++)
                vers[i] = argv[i + 2];
            return wow_ruby_install_many(vers, nv) == 0 ? 0 : 1;
        }

        const char *version = NULL;
        if (argc >= 3) {
            version = argv[2];
        } else {
            static char latest[32];
            if (wow_latest_ruby_version(latest, sizeof(latest)) != 0) {
                fprintf(stderr, "wow: could not determine latest Ruby version\n");
                return 1;
            }
            version = latest;
            printf("Latest Ruby: %s\n", version);
        }
        return wow_ruby_install(version) == 0 ? 0 : 1;
    }

    if (strcmp(sub, "uninstall") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: wow ruby uninstall <version>\n");
            return 1;
        }
        return wow_ruby_uninstall(argv[2]) == 0 ? 0 : 1;
    }

    if (strcmp(sub, "list") == 0) {
        char active[32] = {0};
        wow_find_ruby_version(active, sizeof(active));
        return wow_ruby_list(active[0] ? active : NULL);
    }

    fprintf(stderr, "wow ruby: unknown subcommand '%s'\n", sub);
    return 1;
}
