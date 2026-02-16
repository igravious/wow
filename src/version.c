#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/version.h"

#define RELEASES_URL \
    "https://api.github.com/repos/ruby/ruby-builder/releases?per_page=30"
#define MAX_RESPONSE (64 * 1024)

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
    FILE *fp = popen(
        "curl -sL -H 'Accept: application/vnd.github+json' "
        "'" RELEASES_URL "'", "r");
    if (!fp) return -1;

    char *resp = malloc(MAX_RESPONSE);
    if (!resp) { pclose(fp); return -1; }

    size_t total = 0;
    size_t n;
    while ((n = fread(resp + total, 1, MAX_RESPONSE - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_RESPONSE - 1) break;
    }
    resp[total] = '\0';
    pclose(fp);

    int best_maj = 0, best_min = 0, best_pat = 0;
    int found = 0;

    const char *p = resp;
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

    free(resp);
    if (!found) return -1;

    int w = snprintf(buf, bufsz, "%d.%d.%d", best_maj, best_min, best_pat);
    if (w < 0 || (size_t)w >= bufsz) return -1;
    return 0;
}
