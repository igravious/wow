/*
 * rubies/deffile.c — Parse vendor/ruby-binary/ definition files
 *
 * Definition file format:
 *
 *   # 4.0.1
 *   url=https://.../${platform}.tar.gz
 *
 *   ubuntu-22.04-x64 sha256:69c0a7ad...
 *   darwin-arm64 sha256:6ce2f146...
 *
 * Lines starting with '#' are comments.  The url= line provides a
 * template with ${platform} or ${binary} placeholder.  Each subsequent
 * non-blank, non-comment line is "name sha256:hex".
 */

#include <stdio.h>
#include <string.h>

#include "wow/rubies/deffile.h"

int wow_def_parse(const char *path, wow_def_t *def)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    memset(def, 0, sizeof(*def));

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip blank lines and comments */
        if (len == 0 || line[0] == '#')
            continue;

        /* URL template line */
        if (strncmp(line, "url=", 4) == 0) {
            size_t ulen = strlen(line + 4);
            if (ulen >= sizeof(def->url_template))
                ulen = sizeof(def->url_template) - 1;
            memcpy(def->url_template, line + 4, ulen);
            def->url_template[ulen] = '\0';
            continue;
        }

        /* Entry line: "name sha256:hex" */
        const char *sha_marker = strstr(line, " sha256:");
        if (!sha_marker)
            continue;

        if (def->n_entries >= WOW_DEF_MAX_ENTRIES)
            continue;

        wow_def_entry_t *e = &def->entries[def->n_entries];

        /* Name is everything before " sha256:" */
        size_t name_len = (size_t)(sha_marker - line);
        if (name_len >= sizeof(e->name))
            name_len = sizeof(e->name) - 1;
        memcpy(e->name, line, name_len);
        e->name[name_len] = '\0';

        /* SHA-256 hex digest follows "sha256:" */
        const char *hex = sha_marker + 8; /* strlen(" sha256:") */
        snprintf(e->sha256, sizeof(e->sha256), "%s", hex);

        def->n_entries++;
    }

    fclose(f);

    if (def->url_template[0] == '\0' || def->n_entries == 0)
        return -1;

    return 0;
}

const wow_def_entry_t *wow_def_find(const wow_def_t *def, const char *name)
{
    for (int i = 0; i < def->n_entries; i++) {
        if (strcmp(def->entries[i].name, name) == 0)
            return &def->entries[i];
    }
    return NULL;
}

int wow_def_url(const wow_def_t *def, const char *name, char *buf, size_t bufsz)
{
    /* Try ${platform} first, then ${binary} */
    const char *placeholder = strstr(def->url_template, "${platform}");
    size_t ph_len = 11; /* strlen("${platform}") */

    if (!placeholder) {
        placeholder = strstr(def->url_template, "${binary}");
        ph_len = 9; /* strlen("${binary}") */
    }

    if (!placeholder) {
        /* No placeholder — use template as-is */
        snprintf(buf, bufsz, "%s", def->url_template);
        return 0;
    }

    /* Substitute */
    size_t prefix_len = (size_t)(placeholder - def->url_template);
    const char *suffix = placeholder + ph_len;

    int n = snprintf(buf, bufsz, "%.*s%s%s",
                     (int)prefix_len, def->url_template,
                     name, suffix);
    if (n < 0 || (size_t)n >= bufsz)
        return -1;

    return 0;
}
