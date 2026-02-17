#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/http.h"
#include "wow/version.h"

#define RELEASES_URL \
    "https://api.github.com/repos/ruby/ruby-builder/releases?per_page=30"

static int parse_version(const char *s, int *maj, int *min, int *pat) {
    char *end;
    *maj = (int)strtol(s, &end, 10);
    if (*end != '.') return -1;
    *min = (int)strtol(end + 1, &end, 10);
    if (*end != '.') return -1;
    *pat = (int)strtol(end + 1, &end, 10);
    /* reject pre-release tags like ruby-4.1.0-preview1 */
    if (*end != '"') return -1;
    return 0;
}

int wow_latest_ruby_version(char *buf, size_t bufsz) {
    struct wow_response resp;
    if (wow_http_get(RELEASES_URL, &resp) != 0)
        return -1;

    if (resp.status != 200) {
        fprintf(stderr, "wow: GitHub API returned status %d\n", resp.status);
        wow_response_free(&resp);
        return -1;
    }

    int best_maj = 0, best_min = 0, best_pat = 0;
    int found = 0;

    const char *p = resp.body;
    while ((p = strstr(p, "\"tag_name\"")) != NULL) {
        p += 10; /* skip "tag_name" */
        while (*p == ' ' || *p == ':' || *p == '\t') p++;
        if (*p != '"') continue;
        p++; /* skip opening quote */
        /* match "ruby-" but not "jruby-" or "truffleruby-" */
        if (strncmp(p, "ruby-", 5) != 0) continue;
        p += 5;
        int maj, min, pat;
        if (parse_version(p, &maj, &min, &pat) != 0) continue;
        if (!found || maj > best_maj ||
            (maj == best_maj && min > best_min) ||
            (maj == best_maj && min == best_min && pat > best_pat)) {
            best_maj = maj;
            best_min = min;
            best_pat = pat;
            found = 1;
        }
    }

    wow_response_free(&resp);
    if (!found) return -1;

    int w = snprintf(buf, bufsz, "%d.%d.%d", best_maj, best_min, best_pat);
    if (w < 0 || (size_t)w >= bufsz) return -1;
    return 0;
}
