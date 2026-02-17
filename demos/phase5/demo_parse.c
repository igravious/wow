/*
 * demo_parse.c — Phase 5b demo: Gemfile parser with structured output
 *
 * Parses a Gemfile into a structured wow_gemfile and displays it
 * in a human-readable format with ANSI formatting — similar to
 * how `uv pip compile` shows resolved dependencies.
 *
 * Build:  make -C demos/phase5              (after main 'make')
 * Usage:  ./demos/phase5/demo_parse.com [Gemfile]
 *
 * If no argument is given, uses a built-in sample Gemfile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile.h"

/* ANSI escapes */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_CYAN    "\033[36m"
#define C_GREEN   "\033[32m"
#define C_MAGENTA "\033[35m"
#define C_YELLOW  "\033[33m"
#define C_RED     "\033[31m"

static const char SAMPLE[] =
    "# frozen_string_literal: true\n"
    "\n"
    "source \"https://rubygems.org\"\n"
    "\n"
    "ruby \"3.3.0\"\n"
    "\n"
    "gem \"sinatra\", \"~> 4.0\"\n"
    "gem \"rack\", \">= 3.0.0\", \"< 4\"\n"
    "gem \"pry\", require: false\n"
    "gem \"puma\", \"~> 6.0\", \">= 6.0.2\"\n"
    "\n"
    "group :development do\n"
    "  gem \"rspec\", \"~> 3.0\"\n"
    "  gem \"rubocop\", require: false\n"
    "end\n"
    "\n"
    "group :test do\n"
    "  gem \"minitest\"\n"
    "  gem \"simplecov\", require: false\n"
    "end\n"
    "\n"
    "gem \"debug\", require: false, group: :development\n"
    "gemspec\n";

static void print_gemfile(const struct wow_gemfile *gf)
{
    /* Header */
    if (gf->source)
        printf(C_DIM "  source  " C_RESET "%s\n", gf->source);
    if (gf->ruby_version)
        printf(C_DIM "  ruby    " C_RESET C_CYAN "%s" C_RESET "\n",
               gf->ruby_version);
    if (gf->has_gemspec)
        printf(C_DIM "  gemspec " C_RESET C_YELLOW "yes" C_RESET "\n");
    printf("\n");

    /* Count deps by group */
    size_t n_default = 0, n_dev = 0, n_test = 0, n_other = 0;
    for (size_t i = 0; i < gf->n_deps; i++) {
        const struct wow_gemfile_dep *d = &gf->deps[i];
        if (!d->group)                           n_default++;
        else if (strcmp(d->group, "development") == 0) n_dev++;
        else if (strcmp(d->group, "test") == 0)        n_test++;
        else                                           n_other++;
    }

    /* Print by group, uv-style */
    const char *groups[] = { NULL, "development", "test" };
    const char *group_labels[] = {
        "Dependencies",
        "Development dependencies",
        "Test dependencies"
    };
    const size_t counts[] = { n_default, n_dev, n_test };

    for (int g = 0; g < 3; g++) {
        if (counts[g] == 0) continue;

        printf(C_BOLD "  %s" C_RESET C_DIM " (%zu)" C_RESET "\n",
               group_labels[g], counts[g]);

        for (size_t i = 0; i < gf->n_deps; i++) {
            const struct wow_gemfile_dep *d = &gf->deps[i];

            /* Match group */
            if (g == 0 && d->group != NULL) continue;
            if (g > 0 && (d->group == NULL ||
                          strcmp(d->group, groups[g]) != 0)) continue;

            /* Gem name */
            printf("    " C_GREEN "%s" C_RESET, d->name);

            /* Version constraints */
            if (d->n_constraints > 0) {
                printf(C_CYAN);
                for (int j = 0; j < d->n_constraints; j++)
                    printf(" %s%s", d->constraints[j],
                           j + 1 < d->n_constraints ? "," : "");
                printf(C_RESET);
            }

            /* Flags */
            if (!d->require)
                printf(C_DIM " (require: false)" C_RESET);

            printf("\n");
        }
        printf("\n");
    }

    /* Any other groups */
    if (n_other > 0) {
        printf(C_BOLD "  Other groups" C_RESET "\n");
        for (size_t i = 0; i < gf->n_deps; i++) {
            const struct wow_gemfile_dep *d = &gf->deps[i];
            if (!d->group) continue;
            if (strcmp(d->group, "development") == 0) continue;
            if (strcmp(d->group, "test") == 0) continue;
            printf("    " C_GREEN "%s" C_RESET C_DIM " (group: %s)" C_RESET "\n",
                   d->name, d->group);
        }
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    printf(C_BOLD "=== wow Gemfile Parser Demo ===" C_RESET "\n\n");

    struct wow_gemfile gf;
    int rc;

    if (argc >= 2) {
        printf("Parsing: %s\n\n", argv[1]);
        rc = wow_gemfile_parse_file(argv[1], &gf);
    } else {
        printf(C_DIM "Using built-in sample Gemfile" C_RESET "\n\n");
        rc = wow_gemfile_parse_buf(SAMPLE, (int)strlen(SAMPLE), &gf);
    }

    if (rc != 0) {
        fprintf(stderr, "\n" C_RED C_BOLD "Parse failed." C_RESET "\n");
        return 1;
    }

    printf(C_DIM "─── Parsed structure ─────────────────────────────" C_RESET "\n\n");
    print_gemfile(&gf);

    printf(C_DIM "─────────────────────────────────────────────────" C_RESET "\n");
    printf(C_BOLD "%zu" C_RESET " gem%s found\n",
           gf.n_deps, gf.n_deps == 1 ? "" : "s");

    wow_gemfile_free(&gf);
    return 0;
}
