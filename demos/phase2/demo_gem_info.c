/*
 * demo_gem_info.c — Phase 2c demo: rubygems.org registry client
 *
 * Looks up a gem on rubygems.org using wow's registry API
 * (HTTPS + cJSON) and prints structured info.
 *
 * Build:  make -C demos/phase2 demo_gem_info.com   (after main 'make')
 * Usage:  ./demos/phase2/demo_gem_info.com sinatra
 *         ./demos/phase2/demo_gem_info.com rails
 *         ./demos/phase2/demo_gem_info.com nonexistent-gem-xyz
 */

#include <stdio.h>
#include "wow/registry.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <gem-name> [gem-name ...]\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s sinatra\n", argv[0]);
        fprintf(stderr, "  %s rails rack puma\n", argv[0]);
        return 1;
    }

    printf("=== wow Registry Client Demo ===\n");

    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];
        struct wow_gem_info info = {0};

        printf("\n--- %s ---\n\n", name);

        int rc = wow_gem_info_fetch(name, &info);
        if (rc != 0) {
            fprintf(stderr, "  (lookup failed)\n");
            continue;
        }

        printf("  Name:    %s\n", info.name ? info.name : "(unknown)");
        printf("  Version: %s\n", info.version ? info.version : "(unknown)");
        if (info.authors)
            printf("  Authors: %s\n", info.authors);
        if (info.summary)
            printf("  Summary: %s\n", info.summary);
        if (info.gem_uri)
            printf("  Gem URI: %s\n", info.gem_uri);
        if (info.sha)
            printf("  SHA-256: %s\n", info.sha);

        if (info.n_deps > 0) {
            printf("  Runtime dependencies (%zu):\n", info.n_deps);
            for (size_t d = 0; d < info.n_deps; d++) {
                printf("    %s (%s)\n",
                       info.deps[d].name,
                       info.deps[d].requirements);
            }
        } else {
            printf("  Runtime dependencies: (none)\n");
        }

        wow_gem_info_free(&info);
    }

    return 0;
}
