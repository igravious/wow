/*
 * gems/download.c — download .gem files to the global cache
 *
 * Pattern follows src/rubies/install.c:
 *   1. Fetch registry metadata (URL + SHA-256)
 *   2. Check if already cached
 *   3. Download to temp file with progress bar
 *   4. Verify SHA-256
 *   5. Atomic rename to final cache path
 *   6. goto cleanup on all error paths
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
#include "wow/download.h"
#include "wow/gems/download.h"
#include "wow/http.h"
#include "wow/internal/util.h"
#include "wow/registry.h"
#include "wow/util/sha256.h"

/* ── SHA-256 verification ────────────────────────────────────────── */

/*
 * Compute SHA-256 of a file and compare against expected hex digest.
 * Returns 0 on match, -1 on mismatch or error.
 */
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

/* ── Cache directory ─────────────────────────────────────────────── */

int wow_gem_cache_dir(char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) {
        int n = snprintf(buf, bufsz, "%s/wow/gems", xdg);
        if (n < 0 || (size_t)n >= bufsz) return -1;
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "wow: $HOME not set\n");
            return -1;
        }
        int n = snprintf(buf, bufsz, "%s/.cache/wow/gems", home);
        if (n < 0 || (size_t)n >= bufsz) return -1;
    }
    return 0;
}

/* ── Download ────────────────────────────────────────────────────── */

int wow_gem_download(const char *name, const char *version,
                     char *out_path, size_t out_path_sz)
{
    int ret = -1;
    int fd = -1;
    char tmp_path[WOW_WPATH];
    tmp_path[0] = '\0';

    /* 1. Fetch registry metadata for URL + SHA-256 */
    struct wow_gem_info info;
    if (wow_gem_info_fetch(name, &info) != 0)
        return -1;

    /*
     * The /api/v1/gems/ endpoint returns the LATEST version's metadata.
     * If the user asked for a different version, the SHA-256 from the
     * registry is for the wrong version — we can't use it for verification.
     */
    const char *verified_sha = NULL;
    if (!version || !info.version || strcmp(info.version, version) == 0)
        verified_sha = info.sha;

    /* 2. Build cache path */
    char cache_dir[WOW_WPATH];
    if (wow_gem_cache_dir(cache_dir, sizeof(cache_dir)) != 0)
        goto cleanup;

    if (wow_mkdirs(cache_dir, 0755) != 0)
        goto cleanup;

    char gem_path[WOW_WPATH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(gem_path, sizeof(gem_path), "%s/%s-%s.gem",
             cache_dir, name, version ? version : info.version);
#pragma GCC diagnostic pop

    /* 3. Check if already cached */
    struct stat st;
    if (stat(gem_path, &st) == 0 && S_ISREG(st.st_mode)) {
        if (!verified_sha || verify_sha256(gem_path, verified_sha) == 0) {
            fprintf(stderr, "wow: %s-%s.gem already cached\n",
                    name, version ? version : info.version);
            if (out_path)
                snprintf(out_path, out_path_sz, "%s", gem_path);
            ret = 0;
            goto cleanup;
        }
        /* Hash mismatch — re-download */
        unlink(gem_path);
    }

    /* 4. Build download URL */
    char url[512];
    if (version) {
        snprintf(url, sizeof(url),
                 "https://rubygems.org/downloads/%s-%s.gem", name, version);
    } else if (info.gem_uri) {
        snprintf(url, sizeof(url), "%s", info.gem_uri);
    } else {
        snprintf(url, sizeof(url),
                 "https://rubygems.org/downloads/%s-%s.gem",
                 name, info.version);
    }

    /* 5. Download to temp file (mkstemps avoids race with concurrent downloads) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp-XXXXXX.gem", cache_dir);
#pragma GCC diagnostic pop

    fd = mkstemps(tmp_path, 4);  /* 4 = strlen(".gem") */
    if (fd == -1) {
        fprintf(stderr, "wow: cannot create temp file in %s: %s\n",
                cache_dir, strerror(errno));
        goto cleanup;
    }

    double t0 = wow_now_secs();

    char label[128];
    snprintf(label, sizeof(label), "%s-%s.gem",
             name, version ? version : info.version);

    wow_progress_state_t prog;
    wow_progress_init(&prog, label, 0, NULL);
    int rc = wow_http_download_to_fd(url, fd, wow_progress_http_callback, &prog);
    close(fd);
    fd = -1;

    if (rc != 0) {
        wow_progress_cancel(&prog);
        goto cleanup;
    }

    wow_progress_finish(&prog, "Downloaded");

    /* 6. Verify SHA-256 (only when we have a hash for this exact version) */
    if (verified_sha) {
        if (verify_sha256(tmp_path, verified_sha) != 0)
            goto cleanup;
    }

    /* 7. Atomic rename to final cache path */
    if (rename(tmp_path, gem_path) != 0) {
        fprintf(stderr, "wow: cannot rename %s to %s: %s\n",
                tmp_path, gem_path, strerror(errno));
        goto cleanup;
    }
    tmp_path[0] = '\0';  /* Renamed — don't unlink in cleanup */

    double elapsed = wow_now_secs() - t0;

    if (wow_use_colour()) {
        fprintf(stderr, WOW_ANSI_DIM "Downloaded " WOW_ANSI_BOLD "%s-%s.gem"
                WOW_ANSI_RESET WOW_ANSI_DIM " in %.2fs" WOW_ANSI_RESET "\n",
                name, version ? version : info.version, elapsed);
    } else {
        fprintf(stderr, "Downloaded %s-%s.gem in %.2fs\n",
                name, version ? version : info.version, elapsed);
    }

    if (out_path)
        snprintf(out_path, out_path_sz, "%s", gem_path);

    ret = 0;

cleanup:
    if (tmp_path[0] != '\0')
        unlink(tmp_path);
    if (fd >= 0)
        close(fd);
    wow_gem_info_free(&info);
    return ret;
}
