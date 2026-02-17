/*
 * rubies/cmd.c — Ruby subcommand handler
 */

#include <stdio.h>
#include <string.h>

#include "wow/rubies.h"
#include "wow/version.h"

int cmd_ruby(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow ruby <command>\n\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  install [version...]  Download and install Ruby (parallel if multiple)\n");
        fprintf(stderr, "  uninstall <version>   Remove an installed Ruby version\n");
        fprintf(stderr, "  list                  List installed Ruby versions\n");
        return 1;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "install") == 0) {
        if (argc >= 4) {
            /* Multiple versions — parallel install */
            const char *vers[64];
            int nv = argc - 2;
            if (nv > 64) nv = 64;
            for (int i = 0; i < nv; i++)
                vers[i] = argv[i + 2];
            return wow_ruby_install_many(vers, nv) == 0 ? 0 : 1;
        }

        const char *version = NULL;
        if (argc >= 3) {
            version = argv[2];
        } else {
            static char latest[32];
            if (wow_latest_ruby_version(latest, sizeof(latest)) != 0) {
                fprintf(stderr, "wow: could not determine latest Ruby version\n");
                return 1;
            }
            version = latest;
            printf("Latest Ruby: %s\n", version);
        }
        return wow_ruby_install(version) == 0 ? 0 : 1;
    }

    if (strcmp(sub, "uninstall") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: wow ruby uninstall <version>\n");
            return 1;
        }
        return wow_ruby_uninstall(argv[2]) == 0 ? 0 : 1;
    }

    if (strcmp(sub, "list") == 0) {
        char active[32] = {0};
        wow_find_ruby_version(active, sizeof(active));
        return wow_ruby_list(active[0] ? active : NULL);
    }

    fprintf(stderr, "wow ruby: unknown subcommand '%s'\n", sub);
    return 1;
}
