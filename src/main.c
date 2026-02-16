#include <stdio.h>
#include <string.h>

#include "wow/init.h"

#define WOW_VERSION "0.1.0"

typedef int (*cmd_fn)(int argc, char *argv[]);

static int cmd_stub(int argc, char *argv[]) {
    (void)argc;
    printf("wow %s: not yet implemented\n", argv[0]);
    return 1;
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
