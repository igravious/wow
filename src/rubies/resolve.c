/*
 * rubies/resolve.c — Ruby version resolution and platform detection
 *
 * Part of wow's Ruby version manager (replaces rbenv + ruby-build).
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/rubies/resolve.h"
#include "wow/version.h"

/* ── String helper ───────────────────────────────────────────────── */

static void scopy(char *dst, size_t dstsz, const char *src, size_t n)
{
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ── Platform detection ───────────────────────────────────────────── */

static void detect_libc(wow_platform_t *p)
{
    if (strcmp(p->os, "linux") != 0) {
        p->libc[0] = '\0';
        return;
    }
    /* musl uses /lib/ld-musl-*.so.1 as its dynamic linker */
    FILE *fp = fopen("/lib/ld-musl-x86_64.so.1", "r");
    if (!fp) fp = fopen("/lib/ld-musl-aarch64.so.1", "r");
    if (fp) {
        fclose(fp);
        scopy(p->libc, sizeof(p->libc), "musl", 4);
    } else {
        scopy(p->libc, sizeof(p->libc), "gnu", 3);
    }
}

void wow_detect_platform(wow_platform_t *p)
{
    memset(p, 0, sizeof(*p));

    struct utsname u;
    if (uname(&u) != 0) {
        fprintf(stderr, "wow: uname() failed\n");
        scopy(p->os, sizeof(p->os), "unknown", 7);
        scopy(p->arch, sizeof(p->arch), "unknown", 7);
        return;
    }

    /* OS */
    if (strcmp(u.sysname, "Linux") == 0)
        scopy(p->os, sizeof(p->os), "linux", 5);
    else if (strcmp(u.sysname, "Darwin") == 0)
        scopy(p->os, sizeof(p->os), "darwin", 6);
    else
        scopy(p->os, sizeof(p->os), u.sysname, strlen(u.sysname));

    /* Architecture — normalise aarch64 → arm64 */
    if (strcmp(u.machine, "aarch64") == 0)
        scopy(p->arch, sizeof(p->arch), "arm64", 5);
    else
        scopy(p->arch, sizeof(p->arch), u.machine, strlen(u.machine));

    detect_libc(p);

    /* Compose platform identifier */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (p->libc[0])
        snprintf(p->wow_id, sizeof(p->wow_id), "%s-%s-%s",
                 p->os, p->arch, p->libc);
    else
        snprintf(p->wow_id, sizeof(p->wow_id), "%s-%s",
                 p->os, p->arch);
#pragma GCC diagnostic pop
}

const char *wow_ruby_builder_platform(const wow_platform_t *p)
{
    if (strcmp(p->os, "linux") == 0 && strcmp(p->arch, "x86_64") == 0
        && strcmp(p->libc, "gnu") == 0)
        return "ubuntu-22.04";

    if (strcmp(p->os, "linux") == 0 && strcmp(p->arch, "arm64") == 0
        && strcmp(p->libc, "gnu") == 0)
        return "ubuntu-22.04-arm64";

    if (strcmp(p->os, "darwin") == 0 && strcmp(p->arch, "arm64") == 0)
        return "macos-14-arm64";

    if (strcmp(p->os, "darwin") == 0 && strcmp(p->arch, "x86_64") == 0)
        return "macos-14-x64";

    return NULL;
}

/* ── Directory helpers ────────────────────────────────────────────── */

static int wow_data_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) {
        int n = snprintf(buf, bufsz, "%s/wow", xdg);
        if (n < 0 || (size_t)n >= bufsz) return -1;
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "wow: HOME not set\n");
        return -1;
    }
    int n = snprintf(buf, bufsz, "%s/.local/share/wow", home);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return 0;
}

int wow_ruby_base_dir(char *buf, size_t bufsz)
{
    char data[PATH_MAX];
    if (wow_data_dir(data, sizeof(data)) != 0) return -1;
    int n = snprintf(buf, bufsz, "%s/ruby", data);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return 0;
}

int wow_shims_dir(char *buf, size_t bufsz)
{
    char data[PATH_MAX];
    if (wow_data_dir(data, sizeof(data)) != 0) return -1;
    int n = snprintf(buf, bufsz, "%s/shims", data);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return 0;
}

/* ── Version resolution ───────────────────────────────────────────── */

int wow_resolve_ruby_version(const char *input, char *full_ver, size_t bufsz)
{
    /* Count dots to distinguish X.Y from X.Y.Z */
    int dots = 0;
    for (const char *p = input; *p; p++)
        if (*p == '.') dots++;

    if (dots >= 2) {
        snprintf(full_ver, bufsz, "%s", input);
        return 0;
    }

    /* Minor version only — resolve via GitHub API */
    char latest[32];
    if (wow_latest_ruby_version(latest, sizeof(latest)) != 0) {
        fprintf(stderr, "wow: could not determine latest Ruby version\n");
        return -1;
    }

    /* Check the latest matches the requested minor */
    size_t minor_len = strlen(input);
    if (strncmp(latest, input, minor_len) == 0 && latest[minor_len] == '.') {
        snprintf(full_ver, bufsz, "%s", latest);
        return 0;
    }

    fprintf(stderr, "wow: cannot resolve version %s (latest is %s)\n",
            input, latest);
    return -1;
}

/* ── Find .ruby-version ──────────────────────────────────────────── */

int wow_find_ruby_version(char *buf, size_t bufsz)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return -1;

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", cwd);

    for (;;) {
        char path[WOW_WPATH];
        snprintf(path, sizeof(path), "%s/.ruby-version", dir);

        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(buf, (int)bufsz, f)) {
                /* Trim trailing whitespace */
                size_t len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' ||
                       buf[len - 1] == '\r' || buf[len - 1] == ' '))
                    buf[--len] = '\0';
                fclose(f);
                return 0;
            }
            fclose(f);
        }

        /* Walk up */
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) break;
        *slash = '\0';
    }

    /* Check root */
    FILE *f = fopen("/.ruby-version", "r");
    if (f) {
        if (fgets(buf, (int)bufsz, f)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' ||
                   buf[len - 1] == '\r' || buf[len - 1] == ' '))
                buf[--len] = '\0';
            fclose(f);
            return 0;
        }
        fclose(f);
    }

    return -1;
}

/* ── Path resolution ─────────────────────────────────────────────── */

int wow_ruby_bin_path(const char *version, char *buf, size_t bufsz)
{
    wow_platform_t plat;
    wow_detect_platform(&plat);
    const char *rb_plat = wow_ruby_builder_platform(&plat);
    if (!rb_plat) return -1;

    char base[PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;

    int n = snprintf(buf, bufsz, "%s/ruby-%s-%s/bin/ruby",
                     base, version, rb_plat);
    if (n < 0 || (size_t)n >= bufsz) return -1;

    if (access(buf, X_OK) == 0) return 0;
    return -1;
}
