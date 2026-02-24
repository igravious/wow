/*
 * exec/discover.c — Gem binary discovery
 *
 * Finds gem executables in gem environment directories.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/exec.h"

/*
 * Try to find a binary in a gem's standard bindirs (exe/, bin/).
 * Returns 0 on success (exe_path filled), -1 if not found.
 */
static int
try_binary_in_gem(const char *gems_dir, const char *gem_entry,
                  const char *binary_name, char *exe_path, size_t exe_path_sz)
{
    /* Bounded copies so GCC can prove path compositions fit */
    char dir[WOW_DIR_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", gems_dir);
    char entry[128];
    snprintf(entry, sizeof(entry), "%s", gem_entry);
    char bin[64];
    snprintf(bin, sizeof(bin), "%s", binary_name);

    static const char *bindirs[] = { "exe", "bin", NULL };
    for (const char **bd = bindirs; *bd; bd++) {
        snprintf(exe_path, exe_path_sz, "%s/%s/%s/%s",
                 dir, entry, *bd, bin);
        if (access(exe_path, R_OK) == 0)
            return 0;
    }
    return -1;
}

int
wow_find_gem_binary(const char *env_dir, const char *gem_name,
                    const char *binary_name, char *exe_path,
                    size_t exe_path_sz)
{
    /* Bounded copy so GCC can track sizes through compositions */
    char env[WOW_DIR_PATH_MAX];
    snprintf(env, sizeof(env), "%s", env_dir);

    /* Require completion marker — a partial env (interrupted install)
     * may have the primary gem's binary but lack transitive deps. */
    char marker[WOW_OS_PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/.installed", env);
    if (access(marker, F_OK) != 0)
        return -1;

    char gems_dir[WOW_OS_PATH_MAX];
    snprintf(gems_dir, sizeof(gems_dir), "%s/gems", env);

    size_t nlen = gem_name ? strlen(gem_name) : 0;
    struct dirent *ent;

    /* Pass 1: directories matching gem_name (skipped if gem_name is NULL) */
    if (gem_name) {
        DIR *dir = opendir(gems_dir);
        if (!dir) return -1;

        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (strncmp(ent->d_name, gem_name, nlen) != 0) continue;
            if (ent->d_name[nlen] != '-') continue;

            /* Try direct lookup with given binary_name */
            if (try_binary_in_gem(gems_dir, ent->d_name, binary_name,
                                  exe_path, exe_path_sz) == 0) {
                closedir(dir);
                return 0;
            }

            /* Fall back to .executables marker from gemspec */
            char entry[128];
            if (strlen(ent->d_name) >= sizeof(entry))
                continue;
            strcpy(entry, ent->d_name);

            char exe_marker[WOW_OS_PATH_MAX];
            snprintf(exe_marker, sizeof(exe_marker),
                     "%s/gems/%s/.executables", env, entry);

            FILE *f = fopen(exe_marker, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    if (!line[0] || strcmp(line, binary_name) == 0)
                        continue;

                    if (try_binary_in_gem(gems_dir, ent->d_name, line,
                                          exe_path, exe_path_sz) == 0) {
                        fclose(f);
                        closedir(dir);
                        return 0;
                    }
                }
                fclose(f);
            }
        }
        closedir(dir);
    }

    /* Pass 2: search ALL gem directories (meta-gem support).
     * e.g. `rails` gem has no binary — it's in `railties`.
     * Also used when gem_name is NULL to search all gems. */
    DIR *dir = opendir(gems_dir);
    if (!dir) return -1;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        /* Skip dirs already checked in pass 1 */
        if (gem_name &&
            strncmp(ent->d_name, gem_name, nlen) == 0 &&
            ent->d_name[nlen] == '-')
            continue;

        if (try_binary_in_gem(gems_dir, ent->d_name, binary_name,
                              exe_path, exe_path_sz) == 0) {
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return -1;
}
