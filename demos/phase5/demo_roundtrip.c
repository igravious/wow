/*
 * demo_roundtrip.c — Phase 5c demo: parse multiple Gemfile samples
 *
 * Demonstrates wow's Gemfile parser on a gallery of real-world
 * Gemfile patterns — from minimal to complex — showing what wow
 * understands and what it cleanly rejects.
 *
 * Build:  make -C demos/phase5              (after main 'make')
 * Usage:  ./demos/phase5/demo_roundtrip.com
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
#define C_RED     "\033[31m"
#define C_YELLOW  "\033[33m"

/* ── Gallery of Gemfile samples ─────────────────────────────────── */

struct sample {
    const char *title;
    const char *src;
    int         expect_ok;  /* 1 = should parse, 0 = should fail */
};

static const struct sample gallery[] = {
    {
        "Minimal — just a source",
        "source \"https://rubygems.org\"\n",
        1
    },
    {
        "Typical Rails app",
        "source \"https://rubygems.org\"\n"
        "ruby \"3.3.0\"\n"
        "\n"
        "gem \"rails\", \"~> 7.1\"\n"
        "gem \"pg\", \"~> 1.5\"\n"
        "gem \"puma\", \"~> 6.0\"\n"
        "gem \"bootsnap\", require: false\n"
        "\n"
        "group :development, :test do\n"
        "  gem \"rspec-rails\", \"~> 6.0\"\n"
        "  gem \"factory_bot_rails\"\n"
        "end\n"
        "\n"
        "group :development do\n"
        "  gem \"web-console\"\n"
        "  gem \"listen\", \"~> 3.9\"\n"
        "end\n"
        "\n"
        "group :test do\n"
        "  gem \"capybara\"\n"
        "  gem \"selenium-webdriver\"\n"
        "end\n",
        1
    },
    {
        "Sinatra microservice",
        "source 'https://rubygems.org'\n"
        "\n"
        "gem 'sinatra', '~> 4.0'\n"
        "gem 'puma', '~> 6.0'\n"
        "gem 'rack-contrib'\n"
        "\n"
        "group :development do\n"
        "  gem 'rerun'\n"
        "  gem 'pry', require: false\n"
        "end\n",
        1
    },
    {
        "Gem library (gemspec + dev deps)",
        "source \"https://rubygems.org\"\n"
        "gemspec\n"
        "\n"
        "gem \"rake\", \"~> 13.0\", group: :development\n"
        "gem \"rspec\", \"~> 3.0\", group: :test\n"
        "gem \"rubocop\", require: false, group: :development\n",
        1
    },
    {
        "Hashrocket syntax",
        "source \"https://rubygems.org\"\n"
        "gem \"pry\", :require => false\n"
        "gem \"debug\", :require => false, :group => :development\n",
        1
    },
    {
        "Multiple version constraints",
        "source \"https://rubygems.org\"\n"
        "gem \"rack\", \">= 2.0\", \"< 4\"\n"
        "gem \"puma\", \"~> 6.0\", \">= 6.0.2\"\n"
        "gem \"nokogiri\", \">= 1.14\", \"< 2.0\"\n",
        1
    },
    {
        "Unknown keys (accepted, ignored)",
        "source \"https://rubygems.org\"\n"
        "gem \"debug\", platforms: :mri\n"
        "gem \"local-lib\", path: \"./vendor/local-lib\"\n"
        "gem \"my-fork\", git: \"https://github.com/me/my-fork\"\n",
        1
    },
    {
        "Comments and blank lines only",
        "# frozen_string_literal: true\n"
        "\n"
        "# This Gemfile is intentionally left blank\n"
        "\n",
        1
    },
    {
        "Empty file",
        "",
        1
    },
    /* ── These should fail cleanly ──────────────────────────── */
    {
        "REJECTED: if/else (dynamic Ruby)",
        "source \"https://rubygems.org\"\n"
        "if RUBY_VERSION >= \"3.0\"\n"
        "  gem \"new-feature\"\n"
        "end\n",
        0
    },
    {
        "REJECTED: eval_gemfile",
        "source \"https://rubygems.org\"\n"
        "eval_gemfile \"other_gems.rb\"\n",
        0
    },
    {
        "REJECTED: git_source block",
        "source \"https://rubygems.org\"\n"
        "git_source(:github) { |repo| \"https://github.com/#{repo}.git\" }\n",
        0
    },
};

#define N_SAMPLES (sizeof(gallery) / sizeof(gallery[0]))

static void print_deps(const struct wow_gemfile *gf)
{
    for (size_t i = 0; i < gf->n_deps; i++) {
        const struct wow_gemfile_dep *d = &gf->deps[i];
        printf("      " C_GREEN "%s" C_RESET, d->name);
        for (int j = 0; j < d->n_constraints; j++)
            printf(C_CYAN " %s" C_RESET, d->constraints[j]);
        if (d->group)
            printf(C_DIM " (%s)" C_RESET, d->group);
        if (!d->require)
            printf(C_DIM " [no require]" C_RESET);
        printf("\n");
    }
}

int main(void)
{
    printf(C_BOLD "=== wow Gemfile Roundtrip Demo ===" C_RESET "\n");
    printf(C_DIM "Parsing %d sample Gemfiles\n" C_RESET "\n",
           (int)N_SAMPLES);

    int ok = 0, rejected = 0, unexpected = 0;

    for (size_t i = 0; i < N_SAMPLES; i++) {
        const struct sample *s = &gallery[i];

        printf(C_BOLD "  [%zu/%zu] %s" C_RESET "\n",
               i + 1, N_SAMPLES, s->title);

        /* Suppress stderr for expected failures */
        FILE *saved = NULL;
        if (!s->expect_ok) {
            saved = stderr;
            stderr = fopen("/dev/null", "w");
        }

        struct wow_gemfile gf;
        int rc = wow_gemfile_parse_buf(s->src, (int)strlen(s->src), &gf);

        if (saved) {
            fclose(stderr);
            stderr = saved;
        }

        if (s->expect_ok && rc == 0) {
            printf("    " C_GREEN "OK" C_RESET " — %zu dep%s",
                   gf.n_deps, gf.n_deps == 1 ? "" : "s");
            if (gf.source)
                printf(", source=%s", gf.source);
            if (gf.ruby_version)
                printf(", ruby=%s", gf.ruby_version);
            if (gf.has_gemspec)
                printf(", gemspec");
            printf("\n");
            if (gf.n_deps > 0) print_deps(&gf);
            ok++;
            wow_gemfile_free(&gf);
        } else if (!s->expect_ok && rc != 0) {
            printf("    " C_YELLOW "REJECTED" C_RESET
                   " — correctly refused (unsupported syntax)\n");
            rejected++;
        } else if (s->expect_ok && rc != 0) {
            printf("    " C_RED "UNEXPECTED FAILURE" C_RESET
                   " — should have parsed!\n");
            unexpected++;
        } else {
            printf("    " C_RED "UNEXPECTED SUCCESS" C_RESET
                   " — should have been rejected!\n");
            unexpected++;
            wow_gemfile_free(&gf);
        }
        printf("\n");
    }

    printf(C_DIM "─────────────────────────────────────────────────" C_RESET "\n");
    printf(C_BOLD "Results:" C_RESET " %d parsed, %d rejected, %d unexpected\n",
           ok, rejected, unexpected);

    if (unexpected > 0) {
        printf(C_RED C_BOLD "FAIL" C_RESET " — %d unexpected result%s\n",
               unexpected, unexpected == 1 ? "" : "s");
        return 1;
    }

    printf(C_GREEN C_BOLD "ALL OK" C_RESET "\n");
    return 0;
}
