/*
 * demo_run.c — Phase 8d: wow run demonstration
 *
 * Demonstrates running commands with the correct Ruby environment:
 *   1. Read .ruby-version
 *   2. Find managed Ruby in ~/.local/share/wow/rubies/
 *   3. Set GEM_HOME/GEM_PATH to vendor/bundle/
 *   4. Set PATH to include Ruby's bin/ directory
 *   5. exec the command
 *
 * Build: make -C demos/phase8 demo_run.com
 * Usage: ./demo_run.com [command]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* ── Mock environment state ──────────────────────────────────────── */

typedef struct {
    const char *ruby_version;
    const char *ruby_path;
    const char *gem_home;
    const char *bundled_gems[8];
    int n_bundled;
} mock_env_t;

static mock_env_t env = {
    .ruby_version = "4.0.1",
    .ruby_path = "~/.local/share/wow/rubies/ruby-4.0.1-linux-x86_64/bin/ruby",
    .gem_home = "./vendor/bundle/ruby/4.0.0",
    .bundled_gems = {"sinatra", "rack", "tilt", "mustermann"},
    .n_bundled = 4,
};

/* ── Environment setup simulation ───────────────────────────────── */

static void print_env_var(const char *name, const char *value, bool is_path)
{
    printf("  export %s=", name);
    if (is_path && strchr(value, '/')) {
        /* Abbreviate home directory */
        if (strncmp(value, "~/.local", 8) == 0) {
            printf("$HOME%s\n", value + 1);
        } else if (strncmp(value, "./", 2) == 0) {
            printf("%s\n", value);
        } else {
            printf("%s\n", value);
        }
    } else {
        printf("\"%s\"\n", value);
    }
}

static void simulate_env_setup(void)
{
    printf("\n  🔧 Setting up environment...\n\n");
    
    /* 1. Read .ruby-version */
    printf("  📄 Reading .ruby-version...\n");
    usleep(100000);
    printf("     Found: %s\n\n", env.ruby_version);
    
    /* 2. Find managed Ruby */
    printf("  🔍 Locating Ruby %s in wow rubies...\n", env.ruby_version);
    usleep(150000);
    printf("     ✓ %s\n\n", env.ruby_path);
    
    /* 3. Set GEM_HOME and GEM_PATH */
    printf("  📦 Configuring bundler environment...\n");
    usleep(50000);
    print_env_var("GEM_HOME", env.gem_home, true);
    print_env_var("GEM_PATH", env.gem_home, true);
    printf("     ✓ Bundled gems: ");
    for (int i = 0; i < env.n_bundled; i++) {
        if (i > 0) printf(", ");
        printf("%s", env.bundled_gems[i]);
    }
    printf("\n\n");
    
    /* 4. Set PATH */
    printf("  🛤  Updating PATH...\n");
    usleep(50000);
    printf("     export PATH=\"");
    printf("$HOME/.local/share/wow/rubies/ruby-4.0.1-linux-x86_64/bin:");
    printf("$PATH\"\n\n");
    
    /* 5. BUNDLE_GEMFILE */
    printf("  📝 Setting BUNDLE_GEMFILE...\n");
    usleep(30000);
    print_env_var("BUNDLE_GEMFILE", "./Gemfile", false);
}

/* ── Command simulations ─────────────────────────────────────────── */

static void simulate_ruby_version(void)
{
    printf("\n  🚀 Executing: ruby -v\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(200000);
    printf("  ruby 4.0.1 (2025-01-01 revision 0) [x86_64-linux]\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
}

static void simulate_require_gem(const char *gem_name)
{
    printf("\n  🚀 Executing: ruby -e \"require '%s'; puts %s::VERSION\"\n",
           gem_name, gem_name);
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(300000);
    
    /* Mock version outputs */
    if (strcmp(gem_name, "sinatra") == 0) {
        printf("  4.1.1\n");
    } else if (strcmp(gem_name, "rack") == 0) {
        printf("  3.1.12\n");
    } else {
        printf("  1.0.0\n");
    }
    printf("  ───────────────────────────────────────────────────────────────\n");
}

static void simulate_rake_version(void)
{
    printf("\n  🚀 Executing: rake --version\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(250000);
    printf("  rake, version 13.2.1\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
}

static void simulate_which(const char *binary)
{
    printf("\n  🚀 Executing: which %s\n", binary);
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(100000);
    printf("  %s/.local/share/wow/rubies/ruby-4.0.1-linux-x86_64/bin/%s\n",
           getenv("HOME") ? "" : "~", binary);
    printf("  ───────────────────────────────────────────────────────────────\n");
}

static void simulate_irb(void)
{
    printf("\n  🚀 Executing: irb\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
    usleep(200000);
    printf("  irb(main):001:0> require 'sinatra'\n");
    usleep(100000);
    printf("  => true\n");
    printf("  irb(main):002:0> Sinatra::VERSION\n");
    usleep(100000);
    printf("  => \"4.1.1\"\n");
    printf("  irb(main):003:0> exit\n");
    printf("  ───────────────────────────────────────────────────────────────\n");
}

/* ── Command handlers ────────────────────────────────────────────── */

static void show_usage(void)
{
    printf("\n  Usage: wow run <command> [args...]\n\n");
    printf("  Commands demonstrated:\n");
    printf("    ruby -v              Show Ruby version\n");
    printf("    ruby -e '...'        Run Ruby code\n");
    printf("    rake --version       Run bundled rake\n");
    printf("    which ruby           Show Ruby path\n");
    printf("    irb                  Interactive Ruby\n");
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Phase 8d: wow run — Execute with Bundled Environment          ║\n");
    printf("║  Sets up GEM_HOME, GEM_PATH, PATH, then execs command          ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    /* Environment setup (common to all commands) */
    simulate_env_setup();
    
    /* Determine which command to simulate */
    const char *cmd = (argc > 1) ? argv[1] : "ruby";
    const char *subcmd = (argc > 2) ? argv[2] : NULL;
    
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════════════════╗\n");
    printf("  ║  Environment Ready                                           ║\n");
    printf("  ╠══════════════════════════════════════════════════════════════╣\n");
    printf("  ║  Ruby:    %-51s║\n", env.ruby_version);
    printf("  ║  Gems:    %-51s║\n", "4 bundled (sinatra, rack, tilt, mustermann)");
    printf("  ║  Command: ");
    
    if (subcmd) {
        printf("%-51s║\n", subcmd);
    } else {
        printf("%-51s║\n", cmd);
    }
    printf("  ╚══════════════════════════════════════════════════════════════╝\n");
    
    /* Execute appropriate simulation */
    if (strcmp(cmd, "ruby") == 0) {
        if (subcmd && strcmp(subcmd, "-v") == 0) {
            simulate_ruby_version();
        } else if (subcmd && strcmp(subcmd, "-e") == 0 && argc > 3) {
            /* Extract gem name from require statement */
            char *code = argv[3];
            if (strstr(code, "require")) {
                char gem[32] = {0};
                sscanf(code, "require '%[^']'", gem);
                simulate_require_gem(gem[0] ? gem : "unknown");
            } else {
                simulate_ruby_version();
            }
        } else {
            simulate_ruby_version();
        }
    } else if (strcmp(cmd, "rake") == 0) {
        simulate_rake_version();
    } else if (strcmp(cmd, "which") == 0 && argc > 2) {
        simulate_which(argv[2]);
    } else if (strcmp(cmd, "irb") == 0) {
        simulate_irb();
    } else {
        show_usage();
        return 1;
    }
    
    printf("\n  ✅ Command completed successfully\n");
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Key Concepts Demonstrated                                     ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  • .ruby-version: Pin project to specific Ruby version         ║\n");
    printf("║  • Managed Ruby: wow-installed rubies in ~/.local/share/wow/   ║\n");
    printf("║  • GEM_HOME: Isolated gem directory per project                ║\n");
    printf("║  • PATH injection: Ruby bins available without rbenv           ║\n");
    printf("║  • Bundler parity: Compatible with Bundler's environment       ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}
