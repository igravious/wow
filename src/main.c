#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wow/http.h"
#include "wow/init.h"
#include "wow/pool.h"
#include "wow/registry.h"

#define WOW_VERSION "0.1.0"

typedef int (*cmd_fn)(int argc, char *argv[]);

static int cmd_stub(int argc, char *argv[]) {
    (void)argc;
    printf("wow %s: not yet implemented\n", argv[0]);
    return 1;
}

static int cmd_fetch(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: wow fetch <url>\n");
        return 1;
    }
    struct wow_response resp;
    if (wow_http_get(argv[1], &resp) != 0)
        return 1;
    fwrite(resp.body, 1, resp.body_len, stdout);
    if (resp.body_len && resp.body[resp.body_len - 1] != '\n')
        putchar('\n');
    wow_response_free(&resp);
    return 0;
}

static int cmd_gem_info(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: wow gem-info <gem-name>\n");
        return 1;
    }
    struct wow_gem_info info;
    if (wow_gem_info_fetch(argv[1], &info) != 0)
        return 1;

    printf("%s %s\n", info.name, info.version);
    if (info.authors)
        printf("  Authors: %s\n", info.authors);
    if (info.summary)
        printf("  %s\n", info.summary);
    if (info.n_deps > 0) {
        printf("  Dependencies:\n");
        for (size_t i = 0; i < info.n_deps; i++) {
            printf("    %s (%s)\n",
                   info.deps[i].name,
                   info.deps[i].requirements ? info.deps[i].requirements : "*");
        }
    }

    wow_gem_info_free(&info);
    return 0;
}

static double now_secs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int cmd_bench_pool(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: wow bench-pool <url> <count>\n");
        return 1;
    }
    const char *url = argv[1];
    int count = atoi(argv[2]);
    if (count <= 0) count = 5;

    /* Benchmark without pool (new connection each time) */
    printf("Without pool (%d requests)...\n", count);
    double t0 = now_secs();
    for (int i = 0; i < count; i++) {
        struct wow_response resp;
        if (wow_http_get(url, &resp) != 0) {
            fprintf(stderr, "  request %d failed\n", i + 1);
            return 1;
        }
        wow_response_free(&resp);
    }
    double t1 = now_secs();
    printf("  %.3fs (%.1f ms/req)\n", t1 - t0, (t1 - t0) / count * 1000);

    /* Benchmark with pool */
    printf("With pool (%d requests)...\n", count);
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);
    double t2 = now_secs();
    for (int i = 0; i < count; i++) {
        struct wow_response resp;
        if (wow_http_pool_get(&pool, url, &resp) != 0) {
            fprintf(stderr, "  request %d failed\n", i + 1);
            wow_http_pool_cleanup(&pool);
            return 1;
        }
        wow_response_free(&resp);
    }
    double t3 = now_secs();
    printf("  %.3fs (%.1f ms/req)\n", t3 - t2, (t3 - t2) / count * 1000);
    printf("  connections: %d new, %d reused\n", pool.new_count, pool.reuse_count);

    wow_http_pool_cleanup(&pool);

    double speedup = (t1 - t0) / (t3 - t2);
    printf("\nSpeedup: %.2fx\n", speedup);
    return 0;
}

static const struct {
    const char *name;
    const char *description;
    cmd_fn      fn;
} commands[] = {
    { "init",   "Create a new project",          cmd_init },
    { "sync",   "Install gems from Gemfile.lock", cmd_stub },
    { "lock",   "Resolve and lock dependencies",  cmd_stub },
    { "add",    "Add a gem to Gemfile",           cmd_stub },
    { "remove", "Remove a gem from Gemfile",      cmd_stub },
    { "run",    "Run a command with bundled gems", cmd_stub },
    { "ruby",   "Manage Ruby installations",      cmd_stub },
    { "bundle", "Bundler compatibility shim",     cmd_stub },
    { "fetch",    "Fetch a URL (debug)",           cmd_fetch },
    { "gem-info",    "Show gem info from rubygems",   cmd_gem_info },
    { "bench-pool",  "Benchmark pool vs no-pool",    cmd_bench_pool },
};

#define N_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void print_usage(void) {
    printf("wow %s — a portable Ruby project manager\n\n", WOW_VERSION);
    printf("Usage: wow <command> [args...]\n\n");
    printf("Commands:\n");
    for (size_t i = 0; i < N_COMMANDS; i++)
        printf("  %-10s %s\n", commands[i].name, commands[i].description);
    printf("\nOptions:\n");
    printf("  --help     Show this help\n");
    printf("  --version  Show version\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        printf("wow %s\n", WOW_VERSION);
        return 0;
    }

    for (size_t i = 0; i < N_COMMANDS; i++) {
        if (strcmp(cmd, commands[i].name) == 0)
            return commands[i].fn(argc - 1, argv + 1);
    }

    fprintf(stderr, "unknown command: %s\n\n", cmd);
    print_usage();
    return 1;
}
