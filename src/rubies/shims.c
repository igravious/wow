/*
 * rubies/shims.c — Shim creation for argv[0] dispatch
 *
 * Creates hardlinks/copies of wow binary for ruby, irb, gem, etc.
 * Part of wow's Ruby version manager.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/rubies/shims.h"
#include "wow/rubies/resolve.h"

/* PATH_MAX extension for composite paths */
#define WPATH  (PATH_MAX + 256)

/* ── Shim names ───────────────────────────────────────────────────── */

static const char *shim_names[] = {
    "ruby", "irb", "gem", "bundle", "bundler",
    "rake", "rdoc", "ri", "erb",
    NULL
};

/* ── Directory helper (also in install.c) ─────────────────────────── */

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

/* ── Shim creation ────────────────────────────────────────────────── */

int wow_create_shims(const char *wow_binary_path)
{
    char shims[PATH_MAX];
    if (wow_shims_dir(shims, sizeof(shims)) != 0) return -1;
    if (mkdirs(shims, 0755) != 0) return -1;

    for (const char **name = shim_names; *name; name++) {
        char shim_path[WPATH];
        snprintf(shim_path, sizeof(shim_path), "%s/%s", shims, *name);

        /* Remove existing shim */
        unlink(shim_path);

        /* Try hard link first (survives wow.com moves, same filesystem) */
        if (link(wow_binary_path, shim_path) == 0)
            continue;

        /* Fall back to copy */
        FILE *src = fopen(wow_binary_path, "rb");
        if (!src) {
            fprintf(stderr, "wow: warning: cannot read %s: %s\n",
                    wow_binary_path, strerror(errno));
            return -1;
        }
        FILE *dst = fopen(shim_path, "wb");
        if (!dst) {
            fprintf(stderr, "wow: warning: cannot create shim %s: %s\n",
                    shim_path, strerror(errno));
            fclose(src);
            return -1;
        }

        char cpbuf[8192];
        size_t n;
        while ((n = fread(cpbuf, 1, sizeof(cpbuf), src)) > 0)
            fwrite(cpbuf, 1, n, dst);

        fclose(src);
        fclose(dst);
        chmod(shim_path, 0755);
    }

    return 0;
}
