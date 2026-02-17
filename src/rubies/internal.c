/*
 * rubies/internal.c — Shared helpers for Ruby version management
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
#include <unistd.h>

#include "wow/common.h"
#include "wow/rubies/internal.h"

#define LOCK_MAX_WAIT_MS  45000

int wow_rubies_acquire_lock(const char *base_dir)
{
    char lockpath[WOW_WPATH];
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

void wow_rubies_release_lock(int fd)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

int wow_rubies_rmdir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *ent;
    int ret = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char fullpath[WOW_WPATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);

        struct stat st;
        if (lstat(fullpath, &st) != 0) {
            ret = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (wow_rubies_rmdir_recursive(fullpath) != 0) ret = -1;
        } else {
            if (unlink(fullpath) != 0) ret = -1;
        }
    }
    closedir(d);

    if (rmdir(path) != 0) return -1;
    return ret;
}

int wow_rubies_find_highest_patch(const char *base_dir, const char *minor_ver,
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
        char full_path[WOW_WPATH];
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
