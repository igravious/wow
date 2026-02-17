/*
 * cmd.c — gem subcommand CLI handlers
 *
 * Each cmd_gem_* function is a thin wrapper that parses argv and
 * calls the corresponding module function.  Registered in main.c's
 * commands[] table.
 */

#include <stdio.h>

#include "wow/gems.h"

int cmd_gem_download(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: wow gem-download <gem-name> <version>\n");
        return 1;
    }
    return wow_gem_download(argv[1], argv[2], NULL, 0) == 0 ? 0 : 1;
}

int cmd_gem_list(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow gem-list <path-to.gem>\n");
        return 1;
    }
    return wow_gem_list(argv[1]) == 0 ? 0 : 1;
}

int cmd_gem_meta(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow gem-meta <path-to.gem>\n");
        return 1;
    }

    struct wow_gemspec spec;
    if (wow_gemspec_parse(argv[1], &spec) != 0)
        return 1;

    printf("%s %s\n", spec.name ? spec.name : "?",
           spec.version ? spec.version : "?");
    if (spec.authors)
        printf("  Authors: %s\n", spec.authors);
    if (spec.summary)
        printf("  %s\n", spec.summary);
    if (spec.required_ruby_version)
        printf("  Ruby: %s\n", spec.required_ruby_version);
    if (spec.n_deps > 0) {
        printf("  Dependencies:\n");
        for (size_t i = 0; i < spec.n_deps; i++) {
            printf("    %s", spec.deps[i].name);
            if (spec.deps[i].constraint)
                printf(" (%s)", spec.deps[i].constraint);
            printf("\n");
        }
    }

    wow_gemspec_free(&spec);
    return 0;
}

int cmd_gem_unpack(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: wow gem-unpack <path-to.gem> <dest-dir>\n");
        return 1;
    }
    return wow_gem_unpack(argv[1], argv[2]) == 0 ? 0 : 1;
}
