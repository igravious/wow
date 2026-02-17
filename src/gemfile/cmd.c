/*
 * cmd.c -- Gemfile subcommand CLI handlers
 *
 * Each cmd_gemfile_* function is a thin wrapper that parses argv
 * and calls the corresponding module function. Registered in
 * main.c's commands[] table.
 */

#include <stdio.h>

#include "wow/gemfile.h"

int cmd_gemfile_lex(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow gemfile-lex <Gemfile>\n");
        return 1;
    }
    return wow_gemfile_lex_file(argv[1]) == 0 ? 0 : 1;
}

int cmd_gemfile_parse(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow gemfile-parse <Gemfile>\n");
        return 1;
    }

    struct wow_gemfile gf;
    if (wow_gemfile_parse_file(argv[1], &gf) != 0)
        return 1;

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
        if (d->group)
            printf(" (group: %s)", d->group);
        if (!d->require)
            printf(" (require: false)");
        printf("\n");
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
        if (d->group)
            printf(" (group: %s)", d->group);
        printf("\n");
    }

    wow_gemfile_free(&gf);
    return 0;
}
