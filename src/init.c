#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/init.h"
#include "wow/rubies.h"
#include "wow/version.h"

#define FALLBACK_RUBY_VERSION "4.0.1"

static const char gemfile_contents[] =
    "# frozen_string_literal: true\n"
    "\n"
    "source \"https://rubygems.org\"\n"
    "\n"
    "# gem \"rails\"\n";

static int write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "error: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fputs(contents, f);
    fclose(f);
    return 0;
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int cmd_init(int argc, char *argv[]) {
    int force = 0;
    const char *dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0)
            force = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "init: unknown option: %s\n", argv[i]);
            return 1;
        } else if (!dir)
            dir = argv[i];
        else {
            fprintf(stderr, "init: too many arguments\n");
            return 1;
        }
    }

    /* If a directory name was given, create it and chdir into it */
    if (dir) {
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "error: cannot create directory %s: %s\n",
                    dir, strerror(errno));
            return 1;
        }
        if (chdir(dir) != 0) {
            fprintf(stderr, "error: cannot enter directory %s: %s\n",
                    dir, strerror(errno));
            return 1;
        }
    }

    if (!force && file_exists("Gemfile")) {
        fprintf(stderr, "error: Gemfile already exists (use --force to overwrite)\n");
        return 1;
    }

    if (write_file("Gemfile", gemfile_contents) != 0)
        return 1;

    char ruby_ver[32];
    if (wow_latest_ruby_version(ruby_ver, sizeof(ruby_ver)) != 0) {
        fprintf(stderr, "warning: could not fetch latest Ruby version, "
                "using %s\n", FALLBACK_RUBY_VERSION);
        snprintf(ruby_ver, sizeof(ruby_ver), "%s", FALLBACK_RUBY_VERSION);
    }

    /* Write version with trailing newline */
    char ruby_ver_line[64];
    snprintf(ruby_ver_line, sizeof(ruby_ver_line), "%s\n", ruby_ver);
    if (write_file(".ruby-version", ruby_ver_line) != 0)
        return 1;

    /* Eagerly install the Ruby version (non-fatal if it fails) */
    if (wow_ruby_ensure(ruby_ver) != 0)
        fprintf(stderr, "warning: could not install Ruby %s\n", ruby_ver);

    if (dir)
        printf("Initialised new project in %s/\n", dir);
    else
        printf("Initialised project in current directory\n");

    return 0;
}
