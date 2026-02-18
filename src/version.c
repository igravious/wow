/*
 * version.c — Determine latest stable CRuby version
 *
 * Scans definition filenames in vendor/ruby-binary/share/ruby-binary/repos/
 * ruby-builder/ to find the highest stable (non-prerelease) CRuby version.
 * No network calls — everything is local.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/rubies/deffile.h"
#include "wow/version.h"

#define RB_DEFS_DIR WOW_DEFS_BASE "/ruby-builder"

static int parse_version(const char *s, int *maj, int *min, int *pat)
{
    char *end;
    *maj = (int)strtol(s, &end, 10);
    if (*end != '.') return -1;
    *min = (int)strtol(end + 1, &end, 10);
    if (*end != '.') return -1;
    *pat = (int)strtol(end + 1, &end, 10);
    /* Reject pre-release: must end at NUL (e.g. "4.0.1" not "4.0.0-preview2") */
    if (*end != '\0') return -1;
    return 0;
}

int wow_latest_ruby_version(char *buf, size_t bufsz)
{
    DIR *d = opendir(RB_DEFS_DIR);
    if (!d) {
        fprintf(stderr, "wow: cannot open %s\n", RB_DEFS_DIR);
        return -1;
    }

    int best_maj = 0, best_min = 0, best_pat = 0;
    int found = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;

        /* CRuby definitions start with a digit (no prefix) */
        if (name[0] < '0' || name[0] > '9') continue;

        int maj, min, pat;
        if (parse_version(name, &maj, &min, &pat) != 0)
            continue;

        if (!found || maj > best_maj ||
            (maj == best_maj && min > best_min) ||
            (maj == best_maj && min == best_min && pat > best_pat)) {
            best_maj = maj;
            best_min = min;
            best_pat = pat;
            found = 1;
        }
    }

    closedir(d);
    if (!found) return -1;

    int w = snprintf(buf, bufsz, "%d.%d.%d", best_maj, best_min, best_pat);
    if (w < 0 || (size_t)w >= bufsz) return -1;
    return 0;
}
