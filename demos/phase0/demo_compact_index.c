/*
 * demo_compact_index.c — Phase 0c demo: parse rubygems.org compact index
 *
 * Reads compact index format (as returned by GET /info/{name}) from
 * a file or stdin, parses version lines, and prints structured output.
 *
 * Build:  cosmocc -o demo_compact_index.com demo_compact_index.c
 * Usage:  curl -s https://rubygems.org/info/sinatra | ./demo_compact_index.com
 *         curl -s https://rubygems.org/info/sinatra > /tmp/sinatra.idx
 *         ./demo_compact_index.com /tmp/sinatra.idx
 *
 * Format reference (each line after "---"):
 *   version dep1:constraint,dep2:c1&c2|checksum:sha256[,ruby:>=2.7]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 8192
#define MAX_DEPS 64

/* Safe string copy */
static void scopy(char *dst, size_t dstsz, const char *src, size_t n)
{
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

typedef struct {
    char name[128];
    char constraints[256];
} ci_dep_t;

typedef struct {
    char version[64];
    char platform[64];
    ci_dep_t deps[MAX_DEPS];
    int dep_count;
    char checksum[128];
    char ruby_req[64];
    char rubygems_req[64];
} ci_version_t;

/*
 * Parse a single compact index version line.
 *
 * Format: "version deps|checksum:hex[,ruby:req][,rubygems:req]"
 * Deps:   "name:c1&c2,name2:c3" — comma-separated, & joins multiple constraints
 * Empty:  " |checksum:hex" (space before pipe = no deps)
 */
static int parse_version_line(const char *line, ci_version_t *v)
{
    memset(v, 0, sizeof(*v));

    const char *sp = strchr(line, ' ');
    if (!sp) return -1;

    size_t vlen = (size_t)(sp - line);
    scopy(v->version, sizeof(v->version), line, vlen);

    /* Check for platform suffix */
    char *dash = v->version;
    while ((dash = strchr(dash, '-')) != NULL) {
        if (dash[1] >= 'a' && dash[1] <= 'z') {
            *dash = '\0';
            scopy(v->platform, sizeof(v->platform), dash + 1, strlen(dash + 1));
            break;
        }
        dash++;
    }

    sp++;

    const char *pipe = strchr(sp, '|');
    if (!pipe) return -1;

    /* Parse deps */
    if (pipe > sp && *sp != '|') {
        char deps_buf[4096] = {0};
        size_t dlen = (size_t)(pipe - sp);
        scopy(deps_buf, sizeof(deps_buf), sp, dlen);

        char *tok = deps_buf;
        char *comma;
        while (tok && *tok && v->dep_count < MAX_DEPS) {
            comma = strchr(tok, ',');
            if (comma) *comma = '\0';

            char *colon = strchr(tok, ':');
            if (colon) {
                *colon = '\0';
                ci_dep_t *d = &v->deps[v->dep_count++];
                scopy(d->name, sizeof(d->name), tok, strlen(tok));
                scopy(d->constraints, sizeof(d->constraints), colon + 1, strlen(colon + 1));
            }

            tok = comma ? comma + 1 : NULL;
        }
    }

    /* Parse metadata after pipe */
    pipe++;
    char meta_buf[2048] = {0};
    scopy(meta_buf, sizeof(meta_buf), pipe, strlen(pipe));

    char *mtok = meta_buf;
    char *mcomma;
    while (mtok && *mtok) {
        mcomma = strchr(mtok, ',');
        if (mcomma) *mcomma = '\0';

        if (strncmp(mtok, "checksum:", 9) == 0)
            scopy(v->checksum, sizeof(v->checksum), mtok + 9, strlen(mtok + 9));
        else if (strncmp(mtok, "ruby:", 5) == 0)
            scopy(v->ruby_req, sizeof(v->ruby_req), mtok + 5, strlen(mtok + 5));
        else if (strncmp(mtok, "rubygems:", 9) == 0)
            scopy(v->rubygems_req, sizeof(v->rubygems_req), mtok + 9, strlen(mtok + 9));

        mtok = mcomma ? mcomma + 1 : NULL;
    }

    return 0;
}

static void print_version(const ci_version_t *v)
{
    if (v->platform[0])
        printf("  %s (%s)\n", v->version, v->platform);
    else
        printf("  %s\n", v->version);

    for (int i = 0; i < v->dep_count; i++) {
        const ci_dep_t *d = &v->deps[i];
        /* Replace & with ", " for readability */
        char pretty[256] = {0};
        const char *s = d->constraints;
        char *p = pretty;
        char *end = pretty + sizeof(pretty) - 3;
        while (*s && p < end) {
            if (*s == '&') { *p++ = ','; *p++ = ' '; }
            else { *p++ = *s; }
            s++;
        }
        *p = '\0';
        printf("    dep: %s (%s)\n", d->name, pretty);
    }

    if (v->checksum[0])
        printf("    sha256: %.16s...\n", v->checksum);
    if (v->ruby_req[0])
        printf("    ruby: %s\n", v->ruby_req);
    if (v->rubygems_req[0])
        printf("    rubygems: %s\n", v->rubygems_req);
}

int main(int argc, char **argv)
{
    FILE *fp = stdin;
    if (argc >= 2) {
        fp = fopen(argv[1], "r");
        if (!fp) { perror(argv[1]); return 1; }
    }

    char line[MAX_LINE];
    int past_header = 0;
    int version_count = 0;
    int total = 0;

    printf("=== Compact Index Parser ===\n\n");

    while (fgets(line, (int)sizeof(line), fp)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (strcmp(line, "---") == 0) { past_header = 1; continue; }
        if (!past_header || line[0] == '\0') continue;

        total++;

        ci_version_t v;
        if (parse_version_line(line, &v) == 0) {
            version_count++;
            if (version_count <= 5)
                print_version(&v);
        }
    }

    /* Show last 5 if more than 10 */
    if (total > 10 && argc >= 2) {
        printf("\n  ... (%d versions omitted) ...\n\n", total - 10);
        rewind(fp);
        past_header = 0;
        int count = 0;
        while (fgets(line, (int)sizeof(line), fp)) {
            int len = (int)strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (strcmp(line, "---") == 0) { past_header = 1; continue; }
            if (!past_header || line[0] == '\0') continue;
            count++;
            if (count > total - 5) {
                ci_version_t v;
                if (parse_version_line(line, &v) == 0)
                    print_version(&v);
            }
        }
    }

    printf("\n--- %d versions parsed ---\n", version_count);

    if (fp != stdin) fclose(fp);
    return 0;
}
