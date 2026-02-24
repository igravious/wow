/*
 * demo_wowx.c — wowx: Ephemeral Gem Tool Runner
 *
 * Demonstrates the wowx flow (mirrors uv's uvx):
 *   1. Parse argv[1] → gem_name[@version] + binary_name
 *   2. Find latest installed Ruby
 *   3. Check user gems ~/.gem/ruby/{api}/bin/{binary}
 *   4. Check wowx cache ~/.cache/wow/wowx/{gem}-{ver}/
 *   5. If missing: resolve → download → unpack → cache
 *   6. Build RUBYLIB from all gem lib/ directories
 *   7. exec ruby with the tool
 *
 * Build: make -C demos/wowx demo_wowx.com
 * Usage: ./demo_wowx.com [tool[@version]]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* ── Tool configurations ─────────────────────────────────────────── */

typedef struct {
    const char *name;           /* Tool name (e.g., "rubocop") */
    const char *gem_name;       /* Gem name (may differ, e.g., "rubocop") */
    const char *version;        /* Version or NULL for latest */
    const char *binary;         /* Binary name within gem */
    const char *description;
    const char *test_arg;       /* Argument to show version */
    const char *test_output;    /* Expected version output */
    const char *deps[4];        /* Dependencies */
    int n_deps;
    size_t size_kb;
} tool_t;

static tool_t tools[] = {
    {
        .name = "rubocop",
        .gem_name = "rubocop",
        .version = "1.60.0",
        .binary = "rubocop",
        .description = "Ruby code style checker",
        .test_arg = "--version",
        .test_output = "1.60.0",
        .deps = {"json", "parallel", "parser", "rainbow"},
        .n_deps = 4,
        .size_kb = 450,
    },
    {
        .name = "standardrb",
        .gem_name = "standard",
        .version = "1.35.0",
        .binary = "standardrb",
        .description = "Standard Ruby style guide, linter, and formatter",
        .test_arg = "--version",
        .test_output = "1.35.0",
        .deps = {"rubocop", "lint_roller"},
        .n_deps = 2,
        .size_kb = 85,
    },
    {
        .name = "solargraph",
        .gem_name = "solargraph",
        .version = "0.50.0",
        .binary = "solargraph",
        .description = "Ruby language server",
        .test_arg = "--version",
        .test_output = "0.50.0",
        .deps = {"kramdown", "parser", "reverse_markdown", "rubocop"},
        .n_deps = 4,
        .size_kb = 520,
    },
    {
        .name = "rspec",
        .gem_name = "rspec-core",
        .version = "3.13.0",
        .binary = "rspec",
        .description = "RSpec test runner",
        .test_arg = "--version",
        .test_output = "3.13.0",
        .deps = {"rspec-support", "rspec-expectations", "rspec-mocks"},
        .n_deps = 3,
        .size_kb = 180,
    },
    {NULL}
};

/* ── Cache location simulation ───────────────────────────────────── */

static void print_cache_path(const tool_t *t)
{
    printf("  ~/.cache/wow/wowx/%s-%s/\n", t->gem_name, t->version);
}

static void print_cache_structure(const tool_t *t)
{
    printf("\n  📁 Cache structure:\n");
    printf("  ~/.cache/wow/wowx/%s-%s/\n", t->gem_name, t->version);
    printf("  └── gems/\n");
    printf("      ├── %s-%s/\n", t->gem_name, t->version);
    printf("      │   ├── lib/\n");
    printf("      │   └── %s/%s\n", 
           strcmp(t->binary, "rspec") == 0 ? "exe" : "bin", 
           t->binary);
    
    for (int i = 0; i < t->n_deps; i++) {
        printf("      ├── %s-x.x.x/\n", t->deps[i]);
        printf("      │   └── lib/\n");
    }
    printf("\n");
}

/* ── Flow simulation ─────────────────────────────────────────────── */

static void simulate_find_ruby(void)
{
    printf("\n  🔍 Finding Ruby...\n");
    usleep(100000);
    printf("     ✓ Ruby 4.0.1 (API: 4.0.0)\n");
    printf("       ~/.local/share/wow/rubies/ruby-4.0.1-linux-x86_64/bin/ruby\n");
}

static bool simulate_check_user_gem(const tool_t *t)
{
    printf("\n  📂 Checking user gems...\n");
    usleep(50000);
    printf("     ~/.gem/ruby/4.0.0/bin/%s\n", t->binary);
    printf("     ✗ Not found (not installed)\n");
    return false;
}

static bool simulate_check_cache(const tool_t *t, bool hit)
{
    printf("\n  💾 Checking wowx cache...\n");
    usleep(50000);
    print_cache_path(t);
    
    if (hit) {
        printf("     ✓ Cache HIT\n");
        return true;
    } else {
        printf("     ✗ Cache MISS\n");
        return false;
    }
}

static void simulate_resolve(const tool_t *t)
{
    printf("\n  📋 Resolving dependencies...\n");
    usleep(200000);
    
    printf("     Root: %s (= %s)\n", t->gem_name, t->version);
    
    for (int i = 0; i < t->n_deps; i++) {
        usleep(50000);
        printf("     ✓ %s (resolved)\n", t->deps[i]);
    }
    
    printf("\n     Resolved %d packages in 180ms\n", t->n_deps + 1);
}

static void simulate_download(const tool_t *t)
{
    printf("\n  ⬇ Downloading gems...\n");
    
    const int bar_width = 25;
    
    /* Main gem */
    printf("     %s-%s.gem", t->gem_name, t->version);
    fflush(stdout);
    for (int i = 0; i <= bar_width; i++) {
        usleep(8000);
        int pct = (i * 100) / bar_width;
        printf("\r     %s-%s.gem  %3d%% [", t->gem_name, t->version, pct);
        for (int j = 0; j < bar_width; j++) {
            if (j < i) printf("=");
            else if (j == i) printf(">");
            else printf(" ");
        }
        printf("] %zukB", t->size_kb);
        fflush(stdout);
    }
    printf("\n");
    
    /* Dependencies */
    for (int i = 0; i < t->n_deps; i++) {
        usleep(50000);
        printf("     %s-x.x.x.gem  ✓ (cached)\n", t->deps[i]);
    }
}

static void simulate_unpack(const tool_t *t)
{
    printf("\n  📦 Unpacking to cache...\n");
    usleep(100000);
    printf("     + %s %s\n", t->gem_name, t->version);
    for (int i = 0; i < t->n_deps; i++) {
        usleep(30000);
        printf("     + %s (from cache)\n", t->deps[i]);
    }
}

static void simulate_build_rubylib(const tool_t *t)
{
    printf("\n  🔧 Building RUBYLIB...\n");
    usleep(50000);
    
    printf("     export RUBYLIB=\"");
    
    /* Main gem lib */
    printf("$HOME/.cache/wow/wowx/%s-%s/gems/%s-%s/lib",
           t->gem_name, t->version, t->gem_name, t->version);
    
    /* Dep libs */
    for (int i = 0; i < t->n_deps; i++) {
        printf(":\n                    $HOME/.cache/wow/wowx/%s-%s/gems/%s-x.x.x/lib",
               t->gem_name, t->version, t->deps[i]);
    }
    
    printf("\"\n");
}

static void simulate_exec(const tool_t *t)
{
    printf("\n  🚀 Executing: %s %s\n", t->binary, t->test_arg);
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(300000);
    printf("  %s\n", t->test_output);
    printf("  ───────────────────────────────────────────────────────────────\n");
}

static void simulate_cached_run(const tool_t *t)
{
    printf("\n  💾 Checking wowx cache...\n");
    usleep(10000);
    print_cache_path(t);
    printf("     ✓ Cache HIT (instant launch)\n");
}

/* ── Run flow ────────────────────────────────────────────────────── */

static void run_full_flow(tool_t *t)
{
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ Tool: %-15s %-42s│\n", t->name, t->description);
    printf("│ Version: %-10s First run (cache miss)                  │\n", t->version);
    printf("└─────────────────────────────────────────────────────────────────┘\n");
    
    simulate_find_ruby();
    
    if (simulate_check_user_gem(t)) {
        printf("\n  ✓ Found in user gems, executing...\n");
        simulate_exec(t);
        return;
    }
    
    if (simulate_check_cache(t, false)) {
        printf("\n  ✓ Found in cache, executing...\n");
        simulate_build_rubylib(t);
        simulate_exec(t);
        return;
    }
    
    /* Full install path */
    simulate_resolve(t);
    simulate_download(t);
    simulate_unpack(t);
    print_cache_structure(t);
    simulate_build_rubylib(t);
    simulate_exec(t);
    
    printf("\n  ✅ Installed to cache for instant future runs\n");
}

static void run_cached_flow(tool_t *t)
{
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ Tool: %-15s %-42s│\n", t->name, t->description);
    printf("│ Version: %-10s Second run (cache hit)                   │\n", t->version);
    printf("└─────────────────────────────────────────────────────────────────┘\n");
    
    simulate_find_ruby();
    simulate_check_user_gem(t);
    simulate_cached_run(t);
    simulate_build_rubylib(t);
    simulate_exec(t);
    
    double launch_ms = 15.0 + (rand() % 20);
    printf("\n  ⚡ Launch time: %.1fms (no network, no resolution)\n", launch_ms);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  wowx — Ephemeral Gem Tool Runner                              ║\n");
    printf("║  Like `uvx` or `npx`, but for Ruby gems                        ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    printf("\n  Usage: wowx <tool>[@<version>] [args...]\n");
    printf("  Examples:\n");
    printf("    wowx rubocop                # Latest rubocop\n");
    printf("    wowx rubocop@1.60.0         # Specific version\n");
    printf("    wowx standardrb --fix       # With arguments\n");
    
    tool_t *selected = NULL;
    
    if (argc > 1) {
        /* Parse tool[@version] */
        char *spec = argv[1];
        char *at = strchr(spec, '@');
        char tool_name[32] = {0};
        
        if (at) {
            strncpy(tool_name, spec, at - spec);
            tool_name[at - spec] = '\0';
        } else {
            strncpy(tool_name, spec, sizeof(tool_name) - 1);
        }
        
        for (int i = 0; tools[i].name; i++) {
            if (strcmp(tools[i].name, tool_name) == 0) {
                selected = &tools[i];
                break;
            }
        }
    }
    
    if (!selected) {
        selected = &tools[0];  /* Default to rubocop */
    }
    
    /* First run - full flow */
    run_full_flow(selected);
    
    printf("\n\n  Press Enter for cached second run...\n");
    getchar();
    
    /* Second run - cached */
    run_cached_flow(selected);
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Key Concepts Demonstrated                                     ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  • Auto-discovery: Finds wow-managed Ruby automatically        ║\n");
    printf("║  • User gems: Checks ~/.gem/ first (user-installed tools)      ║\n");
    printf("║  • Isolated cache: Each tool+version in separate directory     ║\n");
    printf("║  • PubGrub resolve: Full dependency resolution on first run    ║\n");
    printf("║  • Parallel download: All deps fetched concurrently            ║\n");
    printf("║  • RUBYLIB approach: No GEM_HOME needed, just lib paths        ║\n");
    printf("║  • Instant replay: Second run uses cache, no network           ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    printf("\n  Supported tools:\n");
    for (int i = 0; tools[i].name; i++) {
        printf("    %-15s %s\n", tools[i].name, tools[i].description);
    }
    
    return 0;
}
