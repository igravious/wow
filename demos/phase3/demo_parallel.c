/*
 * demo_parallel.c — Phase 3 demo: parallel HTTPS downloads with progress bars
 *
 * Demonstrates wow's parallel download infrastructure:
 * - Bounded-concurrency worker pool (8 threads by default)
 * - Multi-bar progress display with ANSI cursor positioning
 * - Thread-safe TLS (each thread has independent mbedTLS context)
 * - Status line showing [completed/total] with throughput
 *
 * Build:  make -C demos/phase3 demo_parallel.com   (after main 'make')
 *
 * Usage:
 *   ./demo_parallel.com --gems100             # Download 100 popular gems
 *   ./demo_parallel.com --gems100 -j16        # Same, 16 concurrent workers
 *   ./demo_parallel.com --gems100 --slow-mo   # Rate-limited so you can watch
 *   ./demo_parallel.com <url1> <url2> ...     # Download specific URLs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wow/parallel.h"

#define MAX_DOWNLOADS 128
#define DEFAULT_WORKERS 8

/* ── 100 popular gems from rubygems.org ──────────────────────────── */

/*
 * The 100 most-depended-upon gems in the Ruby ecosystem.
 * Format: { name, version }.  Each downloads from:
 *   https://rubygems.org/downloads/{name}-{version}.gem
 *
 * Sizes range from ~5 KiB (json) to ~12 MiB (nokogiri) — a realistic
 * mix that exercises the worker pool with varying completion times.
 */
static const struct { const char *name; const char *ver; } popular_gems[] = {
    {"rake",             "13.2.1"},
    {"rack",             "3.1.12"},
    {"bundler",          "2.6.3"},
    {"multi_json",       "1.15.0"},
    {"rack-test",        "2.2.0"},
    {"json",             "2.10.1"},
    {"mime-types-data",  "3.2026.0203"},
    {"mime-types",       "3.6.0"},
    {"diff-lcs",         "1.6.1"},
    {"rspec-support",    "3.13.2"},
    {"rspec-core",       "3.13.3"},
    {"rspec-expectations","3.13.3"},
    {"rspec-mocks",      "3.13.2"},
    {"rspec",            "3.13.0"},
    {"addressable",      "2.8.7"},
    {"i18n",             "1.14.7"},
    {"tzinfo",           "2.0.6"},
    {"concurrent-ruby",  "1.3.5"},
    {"activesupport",    "8.0.2"},
    {"minitest",         "5.25.4"},
    {"builder",          "3.3.0"},
    {"erubi",            "1.13.1"},
    {"racc",             "1.8.1"},
    {"nokogiri",         "1.18.3"},
    {"crass",            "1.0.6"},
    {"loofah",           "2.24.0"},
    {"rails-html-sanitizer","1.6.2"},
    {"rails-dom-testing","2.2.0"},
    {"rack-session",     "2.1.1"},
    {"actionview",       "8.0.2"},
    {"actionpack",       "8.0.2"},
    {"activemodel",      "8.0.2"},
    {"activerecord",     "8.0.2"},
    {"globalid",         "1.2.1"},
    {"activejob",        "8.0.2"},
    {"marcel",           "1.0.4"},
    {"activestorage",    "8.0.2"},
    {"actionmailer",     "8.0.2"},
    {"actioncable",      "8.0.2"},
    {"net-smtp",         "0.5.1"},
    {"net-imap",         "0.5.6"},
    {"net-pop",          "0.1.2"},
    {"actionmailbox",    "8.0.2"},
    {"actiontext",       "8.0.2"},
    {"railties",         "8.0.2"},
    {"rails",            "8.0.2"},
    {"sinatra",          "4.1.1"},
    {"puma",             "6.5.0"},
    {"faraday",          "2.12.2"},
    {"connection_pool",  "2.5.0"},
    {"redis",            "5.4.0"},
    {"sidekiq",          "7.3.9"},
    {"aws-sdk-core",     "3.216.1"},
    {"ffi",              "1.17.1"},
    {"rb-fsevent",       "0.11.2"},
    {"rb-inotify",       "0.11.1"},
    {"listen",           "3.9.0"},
    {"thor",             "1.3.2"},
    {"method_source",    "1.1.0"},
    {"coderay",          "1.1.3"},
    {"pry",              "0.15.2"},
    {"tilt",             "2.6.0"},
    {"temple",           "0.10.3"},
    {"haml",             "6.3.0"},
    {"sass-embedded",    "1.83.4"},
    {"zeitwerk",         "2.7.2"},
    {"bootsnap",         "1.18.6"},
    {"sprockets",        "4.2.1"},
    {"turbo-rails",      "2.0.11"},
    {"stimulus-rails",   "1.3.4"},
    {"jbuilder",         "2.13.0"},
    {"cssbundling-rails","1.4.2"},
    {"jsbundling-rails", "1.3.1"},
    {"importmap-rails",  "2.1.0"},
    {"propshaft",        "1.1.0"},
    {"dotenv",           "3.1.7"},
    {"devise",           "4.9.4"},
    {"cancancan",        "3.6.1"},
    {"pundit",           "2.5.0"},
    {"kaminari",         "1.2.2"},
    {"friendly_id",      "5.5.1"},
    {"ransack",          "4.2.1"},
    {"pg",               "1.5.9"},
    {"mysql2",           "0.5.6"},
    {"sqlite3",          "2.6.0"},
    {"bcrypt",           "3.1.20"},
    {"jwt",              "2.10.1"},
    {"httparty",         "0.22.0"},
    {"typhoeus",         "1.4.1"},
    {"dry-types",        "1.7.2"},
    {"dry-struct",       "1.6.0"},
    {"dry-validation",   "1.10.0"},
    {"oj",               "3.16.9"},
    {"msgpack",          "1.7.5"},
    {"rotp",             "6.3.0"},
    {"rqrcode",          "2.2.0"},
    {"rubyzip",          "2.4.1"},
    {"rubycritic",       "4.9.2"},
    {"rubocop",          "1.72.2"},
    {"simplecov",        "0.22.0"},
};

#define N_POPULAR_GEMS (sizeof(popular_gems) / sizeof(popular_gems[0]))

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *url_filename(const char *url)
{
    const char *slash = strrchr(url, '/');
    return slash ? slash + 1 : url;
}

static double demo_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void format_bytes(size_t bytes, char *buf, size_t bufsz)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, bufsz, "%.1f GiB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1f MiB",
                 (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1f KiB", (double)bytes / 1024.0);
    else
        snprintf(buf, bufsz, "%zu B", bytes);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    wow_download_spec_t specs[MAX_DOWNLOADS];
    wow_download_result_t results[MAX_DOWNLOADS];
    int n = 0;
    int max_workers = DEFAULT_WORKERS;

    /* Static buffers for gem mode (labels and paths must persist) */
    static char gem_urls[MAX_DOWNLOADS][256];
    static char gem_labels[MAX_DOWNLOADS][128];
    static char gem_paths[MAX_DOWNLOADS][256];

    unsigned throttle_us = 0;

    /* Parse flags from any position */
    int gems_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-j", 2) == 0) {
            int j = atoi(argv[i] + 2);
            if (j > 0) max_workers = j;
        } else if (strcmp(argv[i], "--gems100") == 0) {
            gems_mode = 1;
        } else if (strcmp(argv[i], "--slow-mo") == 0) {
            throttle_us = 50000;  /* 50ms per chunk — visible bar progress */
        }
    }

    if (gems_mode) {
        n = (int)N_POPULAR_GEMS;
        if (n > MAX_DOWNLOADS) n = MAX_DOWNLOADS;

        for (int i = 0; i < n; i++) {
            snprintf(gem_urls[i], sizeof(gem_urls[i]),
                     "https://rubygems.org/downloads/%s-%s.gem",
                     popular_gems[i].name, popular_gems[i].ver);
            snprintf(gem_labels[i], sizeof(gem_labels[i]),
                     "%s-%s.gem", popular_gems[i].name, popular_gems[i].ver);
            snprintf(gem_paths[i], sizeof(gem_paths[i]),
                     "/tmp/wow-demo-%s-%s.gem",
                     popular_gems[i].name, popular_gems[i].ver);

            specs[i].url       = gem_urls[i];
            specs[i].label     = gem_labels[i];
            specs[i].dest_path = gem_paths[i];
        }
    } else {
        /* URL argument mode */
        static char url_paths[MAX_DOWNLOADS][256];

        for (int i = 1; i < argc && n < MAX_DOWNLOADS; i++) {
            if (strncmp(argv[i], "-j", 2) == 0) continue;
            if (strncmp(argv[i], "http", 4) != 0) continue;

            specs[n].url = argv[i];

            /* Check if next arg is a label */
            if (i + 1 < argc && strncmp(argv[i + 1], "http", 4) != 0 &&
                strncmp(argv[i + 1], "-j", 2) != 0) {
                specs[n].label = argv[i + 1];
                i++;
            } else {
                specs[n].label = url_filename(argv[i]);
            }

            snprintf(url_paths[n], sizeof(url_paths[n]),
                     "/tmp/wow-demo-parallel-%d-%s",
                     n, url_filename(specs[n].url));
            specs[n].dest_path = url_paths[n];
            n++;
        }
    }

    if (n == 0) {
        fprintf(stderr, "Usage: %s --gems100 [-jN]\n", argv[0]);
        fprintf(stderr, "       %s <url1> [label1] <url2> [label2] ... [-jN]\n",
                argv[0]);
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  --gems100   Download 100 popular Ruby gems\n");
        fprintf(stderr, "  --slow-mo   Rate-limit downloads so you can watch the bars\n");
        fprintf(stderr, "  -jN         Use N concurrent worker threads "
                "(default: %d)\n", DEFAULT_WORKERS);
        return 1;
    }

    printf("=== wow Parallel Download Demo ===\n\n");
    printf("Downloading %d file%s with %d concurrent workers",
           n, n > 1 ? "s" : "", max_workers < n ? max_workers : n);
    if (throttle_us > 0)
        printf(" (slow-mo: %uus/chunk)", throttle_us);
    printf("...\n\n");

    double t0 = demo_now();

    int ok = wow_parallel_download(specs, results, n, max_workers, throttle_us);

    double elapsed = demo_now() - t0;

    /* Summary */
    printf("\n--- Results ---\n\n");

    size_t total_bytes = 0;
    int n_failed = 0;
    for (int i = 0; i < n; i++) {
        if (results[i].ok) {
            total_bytes += results[i].bytes;
        } else {
            n_failed++;
        }
    }

    /* Don't print all 100 individually — just the summary */
    if (n > 20) {
        if (n_failed > 0) {
            printf("  Failed downloads:\n");
            for (int i = 0; i < n; i++) {
                if (!results[i].ok)
                    printf("    \033[31m\xe2\x9c\x97\033[0m %s\n",
                           specs[i].label);
            }
            printf("\n");
        }
    } else {
        for (int i = 0; i < n; i++) {
            char sz[32];
            if (results[i].ok) {
                format_bytes(results[i].bytes, sz, sizeof(sz));
                printf("  \033[32m\xe2\x9c\x93\033[0m %s \xe2\x80\x94 %s\n",
                       specs[i].label, sz);
            } else {
                printf("  \033[31m\xe2\x9c\x97\033[0m %s \xe2\x80\x94 failed\n",
                       specs[i].label);
            }
        }
    }

    char total_str[32], rate_str[32];
    format_bytes(total_bytes, total_str, sizeof(total_str));
    printf("  %d/%d succeeded, %s total in %.2fs\n", ok, n, total_str, elapsed);

    if (total_bytes > 0 && elapsed > 0.001) {
        format_bytes((size_t)((double)total_bytes / elapsed), rate_str,
                     sizeof(rate_str));
        printf("  Effective throughput: %s/s\n", rate_str);
    }

    /* Clean up temp files */
    for (int i = 0; i < n; i++)
        unlink(specs[i].dest_path);

    return ok == n ? 0 : 1;
}
