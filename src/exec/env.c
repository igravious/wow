/*
 * exec/env.c — Ruby environment setup and execution
 *
 * Handles RUBYLIB construction, shim setup, and gem binary execution.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/exec.h"

/*
 * Bounded string copy using memcpy instead of snprintf.
 *
 * GCC's -Wformat-truncation fires when snprintf("%s", src) may truncate.
 * For bounded local copies (e.g. d_name[256] → entry[128]) the truncation
 * is intentional — we know gem names/versions are short, and the bounded
 * size lets GCC verify downstream path compositions fit.
 */
#define SCOPY(dst, src) do {                              \
    size_t _len = strlen(src);                             \
    if (_len >= sizeof(dst)) _len = sizeof(dst) - 1;       \
    memcpy((dst), (src), _len);                             \
    (dst)[_len] = '\0';                                     \
} while (0)

/*
 * Build RUBYLIB and exec the binary.
 *
 * Does not return on success (execv replaces the process).
 * Pass env_dir=NULL for direct exec (user gems with binstubs).
 */
int
wow_exec_gem_binary(const char *ruby_bin, const char *ruby_api,
                    const char *env_dir, const char *exe_path,
                    int user_argc, char **user_argv)
{
    /* Derive Ruby prefix: ruby_bin is .../bin/ruby → prefix is ... */
    char prefix[WOW_DIR_PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", ruby_bin);
    {
        char *slash = strrchr(prefix, '/');
        if (slash) {
            *slash = '\0';  /* strip /ruby → .../bin */
            slash = strrchr(prefix, '/');
            if (slash) *slash = '\0';  /* strip /bin → prefix */
        }
    }

    /* Bounded copy: GCC can track api[16] through all downstream snprintfs */
    char api[16];
    snprintf(api, sizeof(api), "%s", ruby_api);

    /* Build RUBYLIB: shims first, then gems, then stdlib.
     *
     * Order matters: shims shadow stdlib (e.g. bundler/setup.rb),
     * gems shadow default gems (e.g. prism 1.9 over bundled 0.19),
     * stdlib provides rubygems/rbconfig/etc. as fallback. */
    char rubylib[32768];
    rubylib[0] = '\0';
    size_t pos = 0;

    /* Helper: append a path to RUBYLIB */
    #define RUBYLIB_APPEND(path) do {                           \
        size_t _plen = strlen(path);                            \
        if (pos + _plen + 2 <= sizeof(rubylib)) {               \
            if (pos > 0) rubylib[pos++] = ':';                  \
            memcpy(rubylib + pos, (path), _plen);               \
            pos += _plen;                                        \
            rubylib[pos] = '\0';                                 \
        }                                                        \
    } while (0)

    /* 1. Shims directory: shadows bundler/setup.rb with a no-op */
    {
        char shims_dir[WOW_OS_PATH_MAX];
        snprintf(shims_dir, sizeof(shims_dir),
                 "%s/lib/wow_shims", prefix);

        /* Ensure shim exists */
        wow_ensure_bundler_shim(prefix);
        RUBYLIB_APPEND(shims_dir);
    }

    /* 2. Gem require_paths from env_dir */
    if (env_dir) {
        /* Bounded copy so GCC can track sizes through compositions */
        char env[WOW_DIR_PATH_MAX];
        snprintf(env, sizeof(env), "%s", env_dir);

        char gems_dir[WOW_OS_PATH_MAX];
        snprintf(gems_dir, sizeof(gems_dir), "%s/gems", env);

        DIR *dir = opendir(gems_dir);
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_name[0] == '.') continue;

                /* Bounded copy of d_name for safe path composition */
                char entry[128];
                SCOPY(entry, ent->d_name);

                char gem_dir[WOW_OS_PATH_MAX];
                snprintf(gem_dir, sizeof(gem_dir), "%s/gems/%s",
                         env, entry);

                /* Read .require_paths marker */
                char rp_file[WOW_OS_PATH_MAX];
                snprintf(rp_file, sizeof(rp_file),
                         "%s/gems/%s/.require_paths", env, entry);

                char rp_buf[1024] = {0};
                FILE *rpf = fopen(rp_file, "r");
                if (rpf) {
                    size_t nr = fread(rp_buf, 1, sizeof(rp_buf) - 1, rpf);
                    rp_buf[nr] = '\0';
                    fclose(rpf);
                }

                /* If no marker, default to "lib" */
                if (!rp_buf[0])
                    snprintf(rp_buf, sizeof(rp_buf), "lib\n");

                /* Add each require_path line */
                char *line = rp_buf;
                while (*line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    /* cppcheck-suppress knownConditionTrueFalse - defensive check for empty lines */
                    if (*line) {
                        char rp_line[64];
                        snprintf(rp_line, sizeof(rp_line), "%s", line);

                        char full_path[WOW_OS_PATH_MAX];
                        snprintf(full_path, sizeof(full_path),
                                 "%s/gems/%s/%s", env, entry, rp_line);

                        struct stat fst;
                        if (stat(full_path, &fst) == 0 &&
                            S_ISDIR(fst.st_mode)) {
                            RUBYLIB_APPEND(full_path);
                        }
                    }
                    line = nl ? nl + 1 : line + strlen(line);
                }
            }
            closedir(dir);
        }
    }

    /* 3. Ruby stdlib: <prefix>/lib/ruby/<api_ver> */
    {
        char stdlib_dir[WOW_OS_PATH_MAX];
        snprintf(stdlib_dir, sizeof(stdlib_dir), "%s/lib/ruby/%s",
                 prefix, api);

        struct stat st;
        if (stat(stdlib_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            RUBYLIB_APPEND(stdlib_dir);

            /* 3b. Arch-specific: <prefix>/lib/ruby/<api_ver>/<arch>
             *     Contains rbconfig.rb — scan for the subdir that has it. */
            DIR *dir = opendir(stdlib_dir);
            if (dir) {
                struct dirent *ent;
                while ((ent = readdir(dir)) != NULL) {
                    if (ent->d_name[0] == '.') continue;

                    char arch[128];
                    SCOPY(arch, ent->d_name);

                    char candidate[WOW_OS_PATH_MAX];
                    snprintf(candidate, sizeof(candidate),
                             "%s/lib/ruby/%s/%s/rbconfig.rb",
                             prefix, api, arch);

                    if (access(candidate, R_OK) == 0) {
                        char arch_dir[WOW_OS_PATH_MAX];
                        snprintf(arch_dir, sizeof(arch_dir),
                                 "%s/lib/ruby/%s/%s",
                                 prefix, api, arch);
                        RUBYLIB_APPEND(arch_dir);
                        break;
                    }
                }
                closedir(dir);
            }
        }
    }

    #undef RUBYLIB_APPEND

    if (pos > 0)
        setenv("RUBYLIB", rubylib, 1);

    /* Stub Kernel#gem so RubyGems activation calls are no-ops */
    {
        wow_ensure_gem_preload(prefix);

        char rubyopt[WOW_OS_PATH_MAX];
        snprintf(rubyopt, sizeof(rubyopt),
                 "-r%s/lib/wow_preload.rb", prefix);
        setenv("RUBYOPT", rubyopt, 1);
    }

    /* Set LD_LIBRARY_PATH so Ruby can find libruby.so */
    {
        char lib_dir[WOW_OS_PATH_MAX];
        snprintf(lib_dir, sizeof(lib_dir), "%s/lib", prefix);

        const char *existing = getenv("LD_LIBRARY_PATH");
        if (existing && existing[0]) {
            char combined[PATH_MAX * 2];
            snprintf(combined, sizeof(combined), "%s:%s", lib_dir, existing);
            setenv("LD_LIBRARY_PATH", combined, 1);
        } else {
            setenv("LD_LIBRARY_PATH", lib_dir, 1);
        }
    }

    /* Build exec argv: ruby <script> [user_args...] */
    int nargs = 2 + user_argc;
    char **exec_argv = calloc((size_t)(nargs + 1), sizeof(char *));
    if (!exec_argv) {
        fprintf(stderr, "wow: out of memory\n");
        return 1;
    }
    exec_argv[0] = (char *)ruby_bin;
    exec_argv[1] = (char *)exe_path;
    for (int i = 0; i < user_argc; i++)
        exec_argv[2 + i] = user_argv[i];
    exec_argv[nargs] = NULL;

    execv(ruby_bin, exec_argv);
    fprintf(stderr, "wow: exec failed: %s\n", strerror(errno));
    free(exec_argv);
    return 1;
}
