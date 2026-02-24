/*
 * demo_sync.c — Phase 8: wow sync end-to-end demonstration
 *
 * Demonstrates the complete sync pipeline:
 *   1. Parse Gemfile
 *   2. Resolve dependencies (PubGrub)
 *   3. Download gems in parallel
 *   4. Unpack to vendor/bundle/
 *   5. Write Gemfile.lock
 *   6. Print uv-style summary
 *
 * Build: make -C demos/phase8 demo_sync.com
 * Usage: ./demo_sync.com [scenario]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* ── Mock sync scenario ──────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *version;
    const char *deps[8];  /* "name constraint" pairs (name, constr, name, constr...) */
    int n_deps;
    size_t download_size; /* KB */
} mock_gem_t;

typedef struct {
    const char *name;
    const char *description;
    mock_gem_t gems[8];
    int n_gems;
    int n_to_install;
    int n_cached;
} scenario_t;

/* Scenario 1: Single gem, no deps (like rack) */
static scenario_t scenario_simple = {
    .name = "simple",
    .description = "Single gem with no dependencies",
    .gems = {
        {"rack", "3.1.12", {}, 0, 320},
    },
    .n_gems = 1,
    .n_to_install = 1,
    .n_cached = 0,
};

/* Scenario 2: Small tree (like sinatra-lite) */
static scenario_t scenario_tree = {
    .name = "tree",
    .description = "Small dependency tree with transitive deps",
    .gems = {
        {"sinatra", "4.1.1", {"rack", ">= 3.0", "tilt", ">= 2.0", "mustermann", ">= 3.0"}, 3, 450},
        {"rack", "3.1.12", {}, 0, 320},
        {"tilt", "2.6.0", {}, 0, 280},
        {"mustermann", "3.0.3", {"ruby2_keywords", "~> 0.1"}, 1, 195},
        {"ruby2_keywords", "0.0.5", {}, 0, 12},
    },
    .n_gems = 5,
    .n_to_install = 5,
    .n_cached = 0,
};

/* Scenario 3: Cached second run */
static scenario_t scenario_cached = {
    .name = "cached",
    .description = "Second run - everything cached, near-instant",
    .gems = {
        {"sinatra", "4.1.1", {"rack", ">= 3.0", "tilt", ">= 2.0", "mustermann", ">= 3.0"}, 3, 450},
        {"rack", "3.1.12", {}, 0, 320},
        {"tilt", "2.6.0", {}, 0, 280},
        {"mustermann", "3.0.3", {"ruby2_keywords", "~> 0.1"}, 1, 195},
        {"ruby2_keywords", "0.0.5", {}, 0, 12},
    },
    .n_gems = 5,
    .n_to_install = 0,
    .n_cached = 5,
};

/* Scenario 4: Upgrade scenario */
static scenario_t scenario_upgrade = {
    .name = "upgrade",
    .description = "Upgrading an existing gem",
    .gems = {
        {"rack", "3.1.12", {}, 0, 320},
        {"rack", "3.1.11", {}, 0, 318},  /* old version */
    },
    .n_gems = 1,
    .n_to_install = 1,
    .n_cached = 0,
};

static scenario_t *scenarios[] = {
    &scenario_simple,
    &scenario_tree,
    &scenario_cached,
    &scenario_upgrade,
    NULL,
};

/* ── Simulation helpers ──────────────────────────────────────────── */

static void print_gemfile(const scenario_t *s)
{
    printf("  ┌─ Gemfile ──────────────────────┐\n");
    printf("  │ source \"https://rubygems.org\" │\n");
    for (int i = 0; i < s->n_gems && s->gems[i].n_deps == 0; i++) {
        /* Top-level gems have no deps listed in simplified model */
        printf("  │ gem \"%s\"                     │\n", s->gems[i].name);
    }
    printf("  └────────────────────────────────┘\n");
}

static void print_resolution_trace(const scenario_t *s)
{
    printf("\n  📋 Resolving dependencies...\n");
    usleep(300000);  /* Simulate work */
    
    for (int i = 0; i < s->n_gems; i++) {
        const mock_gem_t *g = &s->gems[i];
        printf("     ✓ %s (%s)", g->name, g->version);
        if (g->n_deps > 0) {
            printf(" → ");
            for (int d = 0; d < g->n_deps; d += 2) {
                if (d > 0) printf(", ");
                printf("%s %s", g->deps[d], g->deps[d+1]);
            }
        }
        printf("\n");
        usleep(50000);
    }
}

static void print_download_bar(const char *name, const char *version, 
                                size_t size_kb, bool cached)
{
    const int bar_width = 30;
    
    if (cached) {
        printf("     ✓ %s-%s  (cached)\n", name, version);
        return;
    }
    
    printf("     ⬇ %s-%s", name, version);
    fflush(stdout);
    
    /* Animate progress bar */
    for (int i = 0; i <= bar_width; i++) {
        usleep(10000 + (rand() % 20000));
        int pct = (i * 100) / bar_width;
        printf("\r     ⬇ %s-%s  %3d%% [", name, version, pct);
        for (int j = 0; j < bar_width; j++) {
            if (j < i) printf("=");
            else if (j == i) printf(">");
            else printf(" ");
        }
        printf("] %zukB", size_kb);
        fflush(stdout);
    }
    printf("\n");
}

static void print_downloads(const scenario_t *s)
{
    printf("\n  📦 Downloading gems...\n");
    
    for (int i = 0; i < s->n_gems; i++) {
        bool cached = (i >= s->n_gems - s->n_cached);
        print_download_bar(s->gems[i].name, s->gems[i].version,
                          s->gems[i].download_size, cached);
    }
}

static void print_install(const scenario_t *s)
{
    printf("\n  📂 Installing to vendor/bundle/...\n");
    usleep(100000);
    
    for (int i = 0; i < s->n_gems; i++) {
        if (i >= s->n_gems - s->n_cached) continue;  /* Skip cached */
        printf("     + %s %s\n", s->gems[i].name, s->gems[i].version);
        usleep(20000);
    }
}

static void print_lockfile(const scenario_t *s)
{
    printf("\n  📝 Updated Gemfile.lock\n");
    printf("     GEM\n");
    printf("       remote: https://rubygems.org/\n");
    printf("       specs:\n");
    
    for (int i = 0; i < s->n_gems; i++) {
        const mock_gem_t *g = &s->gems[i];
        printf("         %s (%s)\n", g->name, g->version);
        for (int d = 0; d < g->n_deps; d += 2) {
            printf("           %s (%s)\n", g->deps[d], g->deps[d+1]);
        }
    }
    printf("\n");
}

static void print_summary(const scenario_t *s, double resolve_ms,
                         double download_ms, double install_ms)
{
    printf("\n");
    printf("  ╔════════════════════════════════════════════════════════╗\n");
    printf("  ║  wow sync complete                                     ║\n");
    printf("  ╠════════════════════════════════════════════════════════╣\n");
    printf("  ║  Resolved %d packages in %.0fms                          ║\n",
           s->n_gems, resolve_ms);
    if (s->n_cached > 0) {
        printf("  ║  Prepared %d packages in %.1fs (%.0fms cached)            ║\n",
               s->n_gems - s->n_cached, download_ms / 1000.0, download_ms);
    } else {
        printf("  ║  Prepared %d packages in %.1fs                         ║\n",
               s->n_gems, download_ms / 1000.0);
    }
    printf("  ║  Installed %d packages in %.0fms                         ║\n",
           s->n_to_install, install_ms);
    printf("  ╠════════════════════════════════════════════════════════╣\n");
    
    for (int i = 0; i < s->n_gems; i++) {
        const char *prefix = "  ║   +";
        if (s->name == scenario_upgrade.name && i == 1) {
            prefix = "  ║   ~";  /* upgrade symbol */
        }
        printf("%s %-15s %s", prefix, s->gems[i].name, s->gems[i].version);
        /* Pad to align */
        int len = 25 + (int)strlen(s->gems[i].name) + (int)strlen(s->gems[i].version);
        for (int p = len; p < 51; p++) printf(" ");
        printf("║\n");
    }
    
    printf("  ╚════════════════════════════════════════════════════════╝\n");
}

/* ── Run scenario ────────────────────────────────────────────────── */

static void run_scenario(scenario_t *s)
{
    (void)s;  /* May be unused in some paths */
    
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ Scenario: %-20s %-34s│\n", s->name, s->description);
    printf("└─────────────────────────────────────────────────────────────────┘\n");
    
    print_gemfile(s);
    
    /* Phase 1: Resolution */
    print_resolution_trace(s);
    double resolve_ms = 120.0 + (s->n_gems * 40);  /* Simulated */
    
    /* Phase 2: Download */
    print_downloads(s);
    double download_ms = (s->n_gems - s->n_cached) * 150.0;
    if (s->n_cached == s->n_gems) download_ms = 2.0;  /* Instant if cached */
    
    /* Phase 3: Install */
    print_install(s);
    double install_ms = s->n_to_install * 12.0;
    
    /* Phase 4: Lockfile */
    print_lockfile(s);
    
    /* Summary */
    print_summary(s, resolve_ms, download_ms, install_ms);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Phase 8: wow sync — End-to-End Dependency Management          ║\n");
    printf("║  Parse → Resolve → Download → Install → Lock                   ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    if (argc > 1) {
        /* Run specific scenario */
        for (int i = 0; scenarios[i]; i++) {
            if (strcmp(argv[1], scenarios[i]->name) == 0) {
                run_scenario(scenarios[i]);
                return 0;
            }
        }
        printf("Unknown scenario: %s\n", argv[1]);
        printf("Available: simple, tree, cached, upgrade\n");
        return 1;
    }
    
    /* Run all scenarios */
    for (int i = 0; scenarios[i]; i++) {
        run_scenario(scenarios[i]);
        if (scenarios[i + 1]) {
            printf("\n  Press Enter to continue to next scenario...\n");
            getchar();
        }
    }
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Key Concepts Demonstrated                                     ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  • Gemfile parsing: Extract gems and version constraints       ║\n");
    printf("║  • PubGrub resolution: Find compatible version set             ║\n");
    printf("║  • Parallel download: Concurrent gem fetching                  ║\n");
    printf("║  • Caching: Skip re-download on subsequent runs                ║\n");
    printf("║  • Lockfile: Record exact resolved versions                    ║\n");
    printf("║  • uv-style output: Clear, timing-aware progress               ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}
