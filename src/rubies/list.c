/*
 * rubies/list.c — List installed Ruby versions
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "wow/common.h"
#include "wow/rubies.h"

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
        char full_path[WOW_WPATH];
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
