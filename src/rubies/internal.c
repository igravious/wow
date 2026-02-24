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
    char lockpath[WOW_OS_PATH_MAX];
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

        char fullpath[WOW_OS_PATH_MAX];
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

