/*
 * demo_lockparse.c — Phase 0b demo: Gemfile.lock state-machine parser
 *
 * Reads a Gemfile.lock, parses every section, and prints
 * the structured result. Pure C, no dependencies.
 *
 * Build:  cosmocc -o demo_lockparse.com demo_lockparse.c
 * Usage:  ./demo_lockparse.com Gemfile.lock
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 1024
#define MAX_GEMS 512
#define MAX_DEPS 32
#define MAX_PLATFORMS 32

/* Safe string copy via snprintf — no truncation warnings */
static void scopy(char *dst, size_t dstsz, const char *src, size_t n)
{
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ── Data structures ────────────────────────────────────── */

typedef struct {
    char name[128];
    char constraint[128];
} dep_t;

typedef struct {
    char name[128];
    char version[64];
    char platform[64];
    dep_t deps[MAX_DEPS];
    int dep_count;
} gem_spec_t;

typedef struct {
    char name[128];
    char constraint[128];
    int pinned;
} top_dep_t;

typedef struct {
    char gem_remote[256];
    gem_spec_t gems[MAX_GEMS];
    int gem_count;

    char platforms[MAX_PLATFORMS][64];
    int platform_count;

    top_dep_t dependencies[MAX_GEMS];
    int dep_count;

    char ruby_version[128];
    char bundled_with[64];
} lockfile_t;

/* ── Parser state ───────────────────────────────────────── */

typedef enum {
    SEC_NONE,
    SEC_GEM,
    SEC_GIT,
    SEC_PATH,
    SEC_PLATFORMS,
    SEC_DEPENDENCIES,
    SEC_RUBY_VERSION,
    SEC_CHECKSUMS,
    SEC_BUNDLED_WITH,
    SEC_UNKNOWN,
} section_t;

static int leading_spaces(const char *line)
{
    int n = 0;
    while (line[n] == ' ') n++;
    return n;
}

static void chomp(char *line)
{
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' '))
        line[--len] = '\0';
}

static section_t detect_section(const char *line)
{
    if (strcmp(line, "GEM") == 0)            return SEC_GEM;
    if (strcmp(line, "GIT") == 0)            return SEC_GIT;
    if (strcmp(line, "PATH") == 0)           return SEC_PATH;
    if (strcmp(line, "PLATFORMS") == 0)      return SEC_PLATFORMS;
    if (strcmp(line, "DEPENDENCIES") == 0)   return SEC_DEPENDENCIES;
    if (strcmp(line, "RUBY VERSION") == 0)   return SEC_RUBY_VERSION;
    if (strcmp(line, "CHECKSUMS") == 0)      return SEC_CHECKSUMS;
    if (strcmp(line, "BUNDLED WITH") == 0)   return SEC_BUNDLED_WITH;
    return SEC_UNKNOWN;
}

/* Parse "name (version)" or "name (version-platform)" at 4-space indent */
static void parse_gem_entry(const char *text, gem_spec_t *gem)
{
    memset(gem, 0, sizeof(*gem));
    const char *paren = strchr(text, '(');
    if (!paren) {
        scopy(gem->name, sizeof(gem->name), text, strlen(text));
        return;
    }

    int name_len = (int)(paren - text);
    while (name_len > 0 && text[name_len - 1] == ' ') name_len--;
    scopy(gem->name, sizeof(gem->name), text, (size_t)name_len);

    paren++;
    const char *close = strchr(paren, ')');
    if (!close) return;

    char ver_plat[128] = {0};
    int vp_len = (int)(close - paren);
    scopy(ver_plat, sizeof(ver_plat), paren, (size_t)vp_len);

    /* split on first '-' followed by a letter (platform) */
    char *dash = ver_plat;
    while ((dash = strchr(dash, '-')) != NULL) {
        if (isalpha((unsigned char)dash[1])) {
            *dash = '\0';
            scopy(gem->version, sizeof(gem->version), ver_plat, strlen(ver_plat));
            scopy(gem->platform, sizeof(gem->platform), dash + 1, strlen(dash + 1));
            return;
        }
        dash++;
    }
    scopy(gem->version, sizeof(gem->version), ver_plat, strlen(ver_plat));
}

/* Parse a dependency line: "name (constraint)" or "name" */
static void parse_dep(const char *text, dep_t *dep)
{
    memset(dep, 0, sizeof(*dep));
    const char *paren = strchr(text, '(');
    if (!paren) {
        scopy(dep->name, sizeof(dep->name), text, strlen(text));
        return;
    }

    int name_len = (int)(paren - text);
    while (name_len > 0 && text[name_len - 1] == ' ') name_len--;
    scopy(dep->name, sizeof(dep->name), text, (size_t)name_len);

    paren++;
    const char *close = strchr(paren, ')');
    if (close)
        scopy(dep->constraint, sizeof(dep->constraint), paren, (size_t)(close - paren));
}

/* Parse a DEPENDENCIES line: "name" or "name (constraint)!" */
static void parse_top_dep(const char *text, top_dep_t *dep)
{
    memset(dep, 0, sizeof(*dep));
    int len = (int)strlen(text);

    if (len > 0 && text[len - 1] == '!') {
        dep->pinned = 1;
        len--;
    }

    char buf[256] = {0};
    scopy(buf, sizeof(buf), text, (size_t)len);

    const char *paren = strchr(buf, '(');
    if (!paren) {
        while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
        scopy(dep->name, sizeof(dep->name), buf, strlen(buf));
        return;
    }

    int name_len = (int)(paren - buf);
    while (name_len > 0 && buf[name_len - 1] == ' ') name_len--;
    scopy(dep->name, sizeof(dep->name), buf, (size_t)name_len);

    paren++;
    const char *close = strchr(paren, ')');
    if (close)
        scopy(dep->constraint, sizeof(dep->constraint), paren, (size_t)(close - paren));
}

/* ── Main parser ────────────────────────────────────────── */

static int parse_lockfile(const char *path, lockfile_t *lf)
{
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return -1; }

    memset(lf, 0, sizeof(*lf));

    char line[MAX_LINE];
    section_t section = SEC_NONE;
    int in_specs = 0;
    gem_spec_t *cur_gem = NULL;

    while (fgets(line, (int)sizeof(line), fp)) {
        chomp(line);
        int spaces = leading_spaces(line);
        const char *text = line + spaces;

        if (line[0] == '\0') {
            section = SEC_NONE;
            in_specs = 0;
            cur_gem = NULL;
            continue;
        }

        if (spaces == 0) {
            section = detect_section(text);
            in_specs = 0;
            cur_gem = NULL;
            continue;
        }

        switch (section) {
        case SEC_GEM:
        case SEC_GIT:
        case SEC_PATH:
            if (spaces == 2) {
                if (strncmp(text, "remote:", 7) == 0) {
                    const char *val = text + 7;
                    while (*val == ' ') val++;
                    if (section == SEC_GEM)
                        scopy(lf->gem_remote, sizeof(lf->gem_remote), val, strlen(val));
                } else if (strcmp(text, "specs:") == 0) {
                    in_specs = 1;
                }
            } else if (spaces == 4 && in_specs) {
                if (lf->gem_count < MAX_GEMS) {
                    cur_gem = &lf->gems[lf->gem_count++];
                    parse_gem_entry(text, cur_gem);
                }
            } else if (spaces == 6 && in_specs && cur_gem) {
                if (cur_gem->dep_count < MAX_DEPS)
                    parse_dep(text, &cur_gem->deps[cur_gem->dep_count++]);
            }
            break;

        case SEC_PLATFORMS:
            if (spaces == 2 && lf->platform_count < MAX_PLATFORMS)
                scopy(lf->platforms[lf->platform_count++], 64, text, strlen(text));
            break;

        case SEC_DEPENDENCIES:
            if (spaces == 2 && lf->dep_count < MAX_GEMS)
                parse_top_dep(text, &lf->dependencies[lf->dep_count++]);
            break;

        case SEC_RUBY_VERSION:
            if (spaces == 2 || spaces == 3)
                scopy(lf->ruby_version, sizeof(lf->ruby_version), text, strlen(text));
            break;

        case SEC_BUNDLED_WITH:
            if (spaces == 2 || spaces == 3)
                scopy(lf->bundled_with, sizeof(lf->bundled_with), text, strlen(text));
            break;

        default:
            break;
        }
    }

    fclose(fp);
    return 0;
}

/* ── Pretty-print ───────────────────────────────────────── */

static void print_lockfile(const lockfile_t *lf)
{
    printf("=== GEM remote: %s ===\n\n", lf->gem_remote);

    printf("Specs (%d gems):\n", lf->gem_count);
    for (int i = 0; i < lf->gem_count; i++) {
        const gem_spec_t *g = &lf->gems[i];
        if (g->platform[0])
            printf("  %s (%s-%s)\n", g->name, g->version, g->platform);
        else
            printf("  %s (%s)\n", g->name, g->version);
        for (int j = 0; j < g->dep_count; j++) {
            const dep_t *d = &g->deps[j];
            if (d->constraint[0])
                printf("    -> %s (%s)\n", d->name, d->constraint);
            else
                printf("    -> %s\n", d->name);
        }
    }

    printf("\nPlatforms (%d):\n", lf->platform_count);
    for (int i = 0; i < lf->platform_count; i++)
        printf("  %s\n", lf->platforms[i]);

    printf("\nDependencies (%d):\n", lf->dep_count);
    for (int i = 0; i < lf->dep_count; i++) {
        const top_dep_t *d = &lf->dependencies[i];
        printf("  %s", d->name);
        if (d->constraint[0]) printf(" (%s)", d->constraint);
        if (d->pinned) printf(" [pinned]");
        printf("\n");
    }

    if (lf->ruby_version[0])
        printf("\nRuby version: %s\n", lf->ruby_version);
    if (lf->bundled_with[0])
        printf("\nBundled with: %s\n", lf->bundled_with);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Gemfile.lock>\n", argv[0]);
        return 1;
    }

    lockfile_t lf;
    if (parse_lockfile(argv[1], &lf) != 0)
        return 1;

    print_lockfile(&lf);
    return 0;
}
