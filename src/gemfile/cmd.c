/*
 * cmd.c -- Gemfile subcommand CLI handlers
 *
 * Each cmd_gemfile_* function is a thin wrapper that parses argv
 * and calls the corresponding module function. Registered in
 * main.c's commands[] table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile.h"

/* Helper: JSON escape a string */
static void json_escape(const char *s)
{
    if (!s) return;
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"': printf("\\\""); break;
        case '\\': printf("\\\\"); break;
        case '\b': printf("\\b"); break;
        case '\f': printf("\\f"); break;
        case '\n': printf("\\n"); break;
        case '\r': printf("\\r"); break;
        case '\t': printf("\\t"); break;
        default:
            if ((unsigned char)*p < 0x20)
                printf("\\u%04x", *p);
            else
                putchar(*p);
        }
    }
}

/* Helper: print JSON string or null */
static void json_string_or_null(const char *s)
{
    if (s) {
        putchar('"');
        json_escape(s);
        putchar('"');
    } else {
        printf("null");
    }
}

int cmd_gemfile_lex(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow debug gemfile-lex <Gemfile>\n");
        return 1;
    }
    return wow_gemfile_lex_file(argv[1]) == 0 ? 0 : 1;
}

int cmd_gemfile_parse(int argc, char *argv[])
{
    int json_mode = 0;
    const char *path = NULL;
    
    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_mode = 1;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    
    if (!path) {
        fprintf(stderr, "usage: wow gemfile-parse [--json] <Gemfile>\n");
        return 1;
    }

    struct wow_gemfile gf;
    if (wow_gemfile_parse_file(path, &gf) != 0)
        return 1;

    if (json_mode) {
        /* JSON output */
        printf("{\n");
        printf("  \"source\": ");
        json_string_or_null(gf.source);
        printf(",\n");
        printf("  \"ruby_version\": ");
        json_string_or_null(gf.ruby_version);
        printf(",\n");
        printf("  \"gemspec\": %s,\n", gf.has_gemspec ? "true" : "false");
        printf("  \"deps\": [\n");
        
        for (size_t i = 0; i < gf.n_deps; i++) {
            struct wow_gemfile_dep *d = &gf.deps[i];
            printf("    {\n");
            printf("      \"name\": \"%s\",\n", d->name);
            
            /* constraints */
            printf("      \"constraints\": [");
            for (int j = 0; j < d->n_constraints; j++) {
                if (j > 0) printf(", ");
                putchar('"');
                json_escape(d->constraints[j]);
                putchar('"');
            }
            printf("],\n");
            
            /* groups */
            printf("      \"groups\": [");
            for (int j = 0; j < d->n_groups; j++) {
                if (j > 0) printf(", ");
                putchar('"');
                json_escape(d->groups[j]);
                putchar('"');
            }
            printf("],\n");
            
            /* autorequire */
            printf("      \"autorequire\": ");
            if (!d->autorequire_specified) {
                printf("null");
            } else if (d->n_autorequire == 0) {
                printf("[]");
            } else {
                putchar('[');
                for (int j = 0; j < d->n_autorequire; j++) {
                    if (j > 0) printf(", ");
                    putchar('"');
                    json_escape(d->autorequire[j]);
                    putchar('"');
                }
                putchar(']');
            }
            printf(",\n");
            
            /* platforms */
            printf("      \"platforms\": [");
            for (int j = 0; j < d->n_platforms; j++) {
                if (j > 0) printf(", ");
                putchar('"');
                json_escape(d->platforms[j]);
                putchar('"');
            }
            printf("]\n");
            
            printf("    }%s\n", i + 1 < gf.n_deps ? "," : "");
        }
        
        printf("  ]\n");
        printf("}\n");
    } else {
        /* Human-readable output */
        if (gf.source)
            printf("source: %s\n", gf.source);
        if (gf.ruby_version)
            printf("ruby: %s\n", gf.ruby_version);
        if (gf.has_gemspec)
            printf("gemspec\n");

        for (size_t i = 0; i < gf.n_deps; i++) {
            struct wow_gemfile_dep *d = &gf.deps[i];
            printf("gem: %s", d->name);
            for (int j = 0; j < d->n_constraints; j++)
                printf(" %s%s", d->constraints[j],
                       j + 1 < d->n_constraints ? "," : "");
            if (d->n_groups > 0) {
                printf(" (group");
                if (d->n_groups > 1) putchar('s');
                putchar(':');
                for (int j = 0; j < d->n_groups; j++)
                    printf(" %s%s", d->groups[j],
                           j + 1 < d->n_groups ? "," : "");
                putchar(')');
            }
            if (d->autorequire_specified && d->n_autorequire == 0)
                printf(" (require: false)");
            if (d->n_platforms > 0) {
                printf(" (platform");
                if (d->n_platforms > 1) putchar('s');
                putchar(':');
                for (int j = 0; j < d->n_platforms; j++)
                    printf(" %s%s", d->platforms[j],
                           j + 1 < d->n_platforms ? "," : "");
                putchar(')');
            }
            printf("\n");
        }
    }

    wow_gemfile_free(&gf);
    return 0;
}

int cmd_gemfile_deps(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow gemfile-deps <Gemfile>\n");
        return 1;
    }

    struct wow_gemfile gf;
    if (wow_gemfile_parse_file(argv[1], &gf) != 0)
        return 1;

    for (size_t i = 0; i < gf.n_deps; i++) {
        struct wow_gemfile_dep *d = &gf.deps[i];
        printf("%s", d->name);
        for (int j = 0; j < d->n_constraints; j++)
            printf(" %s%s", d->constraints[j],
                   j + 1 < d->n_constraints ? "," : "");
        if (d->n_groups > 0) {
            printf(" (group:");
            for (int j = 0; j < d->n_groups; j++)
                printf(" %s%s", d->groups[j],
                       j + 1 < d->n_groups ? "," : "");
            putchar(')');
        }
        printf("\n");
    }

    wow_gemfile_free(&gf);
    return 0;
}
