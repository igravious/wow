#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wow/http.h"
#include "wow/internal/util.h"
#include "wow/init.h"
#include "wow/registry.h"
#include "wow/rubies.h"
#include "wow/gems.h"
#include "wow/gemfile.h"

/* External verbose flag from http.c */
extern int wow_http_debug;

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
    double t0 = wow_now_secs();
    for (int i = 0; i < count; i++) {
        struct wow_response resp;
        if (wow_http_get(url, &resp) != 0) {
            fprintf(stderr, "  request %d failed\n", i + 1);
            return 1;
        }
        wow_response_free(&resp);
    }
    double t1 = wow_now_secs();
    printf("  %.3fs (%.1f ms/req)\n", t1 - t0, (t1 - t0) / count * 1000);

    /* Benchmark with pool */
    printf("With pool (%d requests)...\n", count);
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);
    double t2 = wow_now_secs();
    for (int i = 0; i < count; i++) {
        struct wow_response resp;
        if (wow_http_pool_get(&pool, url, &resp) != 0) {
            fprintf(stderr, "  request %d failed\n", i + 1);
            wow_http_pool_cleanup(&pool);
            return 1;
        }
        wow_response_free(&resp);
    }
    double t3 = wow_now_secs();
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
    { "ruby",   "Manage Ruby installations",      cmd_ruby },
    { "bundle", "Bundler compatibility shim",     cmd_stub },
    { "fetch",    "Fetch a URL (debug)",           cmd_fetch },
    { "gem-info",    "Show gem info from rubygems",   cmd_gem_info },
    { "bench-pool",  "Benchmark pool vs no-pool",    cmd_bench_pool },
    { "gem-download", "Download a .gem file",          cmd_gem_download },
    { "gem-list",    "List .gem contents",           cmd_gem_list },
    { "gem-meta",    "Show .gem metadata",           cmd_gem_meta },
    { "gem-unpack",  "Unpack .gem to directory",     cmd_gem_unpack },
    { "gemfile-lex",   "Lex a Gemfile (debug)",      cmd_gemfile_lex },
    { "gemfile-parse", "Parse a Gemfile",             cmd_gemfile_parse },
    { "gemfile-deps",  "List Gemfile dependencies",   cmd_gemfile_deps },
};

#define N_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void print_usage(void) {
    printf("wow %s — a portable Ruby project manager\n\n", WOW_VERSION);
    printf("Usage: wow <command> [args...]\n\n");
    printf("Commands:\n");
    for (size_t i = 0; i < N_COMMANDS; i++)
        printf("  %-10s %s\n", commands[i].name, commands[i].description);
    printf("\nOptions:\n");
    printf("  --help, -h       Show this help\n");
    printf("  --version, -V    Show version\n");
    printf("  --verbose, -v    Enable verbose HTTP debugging\n");
}

int main(int argc, char *argv[]) {
    /*
     * Shim dispatch: if invoked as "ruby", "irb", etc. via symlink
     * or hard link, find .ruby-version and exec the managed Ruby binary.
     */
    const char *progname = strrchr(argv[0], '/');
    progname = progname ? progname + 1 : argv[0];

    /* Strip .com suffix (APE binary) */
    static char basename_buf[64];
    size_t plen = strlen(progname);
    if (plen > 4 && strcmp(progname + plen - 4, ".com") == 0) {
        if (plen - 4 < sizeof(basename_buf)) {
            memcpy(basename_buf, progname, plen - 4);
            basename_buf[plen - 4] = '\0';
            progname = basename_buf;
        }
    }

    if (strcmp(progname, "wow") != 0) {
        /* Shim mode */
        char version[32];
        if (wow_find_ruby_version(version, sizeof(version)) != 0) {
            fprintf(stderr,
                    "wow: no .ruby-version found (looked from cwd to /)\n");
            return 1;
        }

        wow_platform_t plat;
        wow_detect_platform(&plat);
        const char *rb_plat = wow_ruby_builder_platform(&plat);
        if (!rb_plat) {
            fprintf(stderr, "wow: unsupported platform\n");
            return 1;
        }

        char base[PATH_MAX];
        if (wow_ruby_base_dir(base, sizeof(base)) != 0) return 1;

        char bin_path[PATH_MAX + 256];
        snprintf(bin_path, sizeof(bin_path), "%s/ruby-%s-%s/bin/%s",
                 base, version, rb_plat, progname);

        if (access(bin_path, X_OK) != 0) {
            fprintf(stderr, "wow: Ruby %s not installed "
                    "(run: wow ruby install %s)\n", version, version);
            return 1;
        }

        execv(bin_path, argv);
        fprintf(stderr, "wow: exec %s failed: %s\n",
                bin_path, strerror(errno));
        return 1;
    }

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

    /* Handle --verbose / -v flag */
    if (strcmp(cmd, "--verbose") == 0 || strcmp(cmd, "-v") == 0) {
        wow_http_debug = 1;
        if (argc < 3) {
            print_usage();
            return 1;
        }
        /* Shift args: wow -v <command> -> wow <command> */
        argc--;
        argv++;
        cmd = argv[1];
    }

    for (size_t i = 0; i < N_COMMANDS; i++) {
        if (strcmp(cmd, commands[i].name) == 0)
            return commands[i].fn(argc - 1, argv + 1);
    }

    fprintf(stderr, "unknown command: %s\n\n", cmd);
    print_usage();
    return 1;
}
