/*
 * rubies/list.c — List installed Ruby versions
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "wow/common.h"
#include "wow/rubies.h"

int wow_ruby_list(const char *active_version)
{
    char base[WOW_DIR_PATH_MAX];
    if (wow_ruby_base_dir(base, sizeof(base)) != 0) return -1;

    DIR *d = opendir(base);
    if (!d) {
        printf("No Ruby installations found.\n");
        return 0;
    }

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strncmp(ent->d_name, "cosmoruby-", 10) == 0) continue;
        /* Version directories start with a digit */
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;

        int active = 0;
        if (active_version && strcmp(ent->d_name, active_version) == 0)
            active = 1;

        printf("  ruby %s%s\n", ent->d_name, active ? "  (active)" : "");
        count++;
    }
    closedir(d);

    if (count == 0)
        printf("No Ruby installations found.\n");

    return 0;
}
