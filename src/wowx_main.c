/*
 * wowx_main.c — Ephemeral gem tool runner (uvx for Ruby)
 *
 * wowx <gem-binary>[@<version>] [args...]
 *
 * Lookup order:
 *   1. User-installed gems  (~/.gem/ruby/X.Y.0/bin/<binary>)
 *   2. wowx cache           (~/.cache/wowx/<gem>-<ver>/...)
 *   3. Auto-install          PubGrub resolve → download → unpack → exec
 *
 * Uses RUBYLIB (not GEM_HOME) for load-path resolution — same as
 * Bundler's --standalone mode.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wow/common.h"
#include "wow/download.h"
#include "wow/gems.h"
#include "wow/http.h"
#include "wow/resolver.h"
#include "wow/rubies.h"
#include "wow/util.h"
#include "wow/version.h"
#include "wow/wowx.h"

/* ── wowx cache directory ────────────────────────────────────────── */

int wow_wowx_cache_dir(const char *ruby_api, char *buf, size_t bufsz)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) {
        int n = snprintf(buf, bufsz, "%s/wowx/%s", xdg, ruby_api);
        if (n < 0 || (size_t)n >= bufsz) return -1;
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "wowx: $HOME not set\n");
            return -1;
        }
        int n = snprintf(buf, bufsz, "%s/.cache/wowx/%s", home, ruby_api);
        if (n < 0 || (size_t)n >= bufsz) return -1;
    }
    return 0;
}

/* Bounded string copy using memcpy instead of snprintf.
 *
 * GCC's -Wformat-truncation fires when snprintf("%s", src) may truncate.
 * For bounded local copies (e.g. d_name[256] → entry[128]) the truncation
 * is intentional — we know gem names/versions are short, and the bounded
 * size lets GCC verify downstream path compositions fit.  Using memcpy
 * avoids the format warning while giving GCC the same size information. */
#define SCOPY(dst, src) do {                              \
    size_t _len = strlen(src);                             \
    if (_len >= sizeof(dst)) _len = sizeof(dst) - 1;       \
    memcpy((dst), (src), _len);                             \
    (dst)[_len] = '\0';                                     \
} while (0)

/* ── Helpers ─────────────────────────────────────────────────────── */

static void print_usage(void)
{
    fprintf(stderr, "wowx %s — run gem binaries without a project\n\n",
            WOW_VERSION);
    fprintf(stderr, "Usage: wowx [--ruby <ver>] <gem>[@<version>] [args...]\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --ruby, -r <ver>  Ruby version to use (e.g. 3.3, 4.0)\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  wowx rubocop               # latest gem, latest Ruby\n");
    fprintf(stderr, "  wowx rubocop@1.60.0        # pinned gem version\n");
    fprintf(stderr, "  wowx --ruby 3.3 rubocop    # specific Ruby version\n");
    fprintf(stderr, "  wowx -r 3.3 rubocop        # short form\n");
    fprintf(stderr, "  wowx rubocop -- --only Style\n");
}

/*
 * Detect the RubyGems platform string for native gem downloads.
 * Returns a static string like "x86_64-linux", "aarch64-linux",
 * "x86_64-darwin", "arm64-darwin", or NULL on failure.
 */
/*
 * Detect RubyGems platform strings for native gem downloads.
 * Returns a NULL-terminated array of platforms to try in order.
 *
 * On Linux, gems use inconsistent conventions:
 *   - nokogiri: x86_64-linux-gnu / x86_64-linux-musl
 *   - grpc:     x86_64-linux
 * We try the more specific variant first, then the bare one.
 */
#define MAX_GEM_PLATFORMS 3

static const char **detect_gem_platforms(void)
{
    static const char *platforms[MAX_GEM_PLATFORMS + 1];
    static char buf[MAX_GEM_PLATFORMS][256];
    static int detected;

    if (detected) return platforms[0] ? platforms : NULL;
    detected = 1;

    struct utsname u;
    if (uname(&u) != 0) return NULL;

    const char *arch = u.machine;  /* x86_64, aarch64, arm64 */
    int n = 0;

    if (strstr(u.sysname, "Linux")) {
        /* Try gnu-suffixed first (nokogiri, etc.), then bare */
        snprintf(buf[n], sizeof(buf[n]), "%s-linux-gnu", arch);
        platforms[n] = buf[n]; n++;
        snprintf(buf[n], sizeof(buf[n]), "%s-linux", arch);
        platforms[n] = buf[n]; n++;
    } else if (strstr(u.sysname, "Darwin")) {
        snprintf(buf[n], sizeof(buf[n]), "%s-darwin", arch);
        platforms[n] = buf[n]; n++;
    } else {
        return NULL;
    }

    platforms[n] = NULL;
    return platforms;
}

/*
 * Build RUBYLIB and exec the binary.
 * Does not return on success (execv replaces the process).
 * Pass env_dir=NULL for direct exec (user gems with binstubs).
 *
 * ruby_api is the API version string (e.g. "4.0.0") used to locate
 * the stdlib under the Ruby prefix.  Pre-built Ruby binaries have
 * hardcoded load paths from their build machine; we fix this by
 * prepending the correct stdlib directories to RUBYLIB.
 */
static int do_exec(const char *ruby_bin, const char *ruby_api,
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

    /* 1. Shims directory: shadows bundler/setup.rb with a no-op.
     *
     * `require 'bundler/setup'` loads the real stdlib file which calls
     * Bundler.setup → Bundler::Definition.build → Gemfile not found.
     * Our shim intercepts the require and defines a minimal Bundler
     * module.  This must come BEFORE stdlib on RUBYLIB. */
    {
        char shims_dir[WOW_OS_PATH_MAX];
        snprintf(shims_dir, sizeof(shims_dir),
                 "%s/lib/wow_shims", prefix);

        char bundler_shim[WOW_OS_PATH_MAX];
        snprintf(bundler_shim, sizeof(bundler_shim),
                 "%s/lib/wow_shims/bundler/setup.rb", prefix);

        struct stat st;
        if (stat(bundler_shim, &st) != 0) {
            char bundler_dir[WOW_OS_PATH_MAX];
            snprintf(bundler_dir, sizeof(bundler_dir),
                     "%s/lib/wow_shims/bundler", prefix);
            wow_mkdirs(bundler_dir, 0755);

            FILE *f = fopen(bundler_shim, "w");
            if (f) {
                fputs("# wow shim: shadows real bundler/setup.rb\n"
                      "# RUBYLIB already has all gem paths on the load path,\n"
                      "# so Bundler's setup is unnecessary.\n"
                      "unless defined?(::Bundler) && "
                          "::Bundler.respond_to?(:root)\n"
                      "  module Bundler\n"
                      "    def self.setup(*) self end\n"
                      "    def self.require(*) nil end\n"
                      "    def self.root() "
                          "Pathname.new(Dir.pwd) end\n"
                      "    def self.environment() self end\n"
                      "    def self.locked_gems() nil end\n"
                      "  end\n"
                      "end\n", f);
                fclose(f);
            }
        }

        RUBYLIB_APPEND(shims_dir);
    }

    /* 2. Gem require_paths from env_dir.
     *
     * Gems come BEFORE stdlib so installed gems shadow default gems
     * (e.g. prism 1.9.0 shadows bundled prism 0.19 on Ruby 3.3).
     * This matches Bundler's $LOAD_PATH behaviour.
     *
     * Each unpacked gem has a .require_paths marker written during
     * auto_install (from the gemspec's require_paths field).  Most gems
     * use "lib" but some (e.g. concurrent-ruby) use "lib/concurrent-ruby".
     * If the marker is missing, fall back to "lib". */
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

    /* 3. Ruby stdlib: <prefix>/lib/ruby/<api_ver>
     *    Comes AFTER gems so installed gems shadow default gems.
     *    Provides rubygems, error_highlight, did_you_mean, etc. */
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

    /* Stub Kernel#gem so RubyGems activation calls are no-ops.
     *
     * Gems are already on RUBYLIB, so `require` finds them.  But some
     * gems call `gem "name", ">= x.y"` to activate via RubyGems, which
     * fails because we don't have gemspec files.  We override `gem` with
     * a no-op via RUBYOPT=-r<preload>.  RUBYOPT is processed AFTER
     * RubyGems loads, so our stub cleanly replaces the RubyGems version. */
    {
        char preload[WOW_OS_PATH_MAX];
        snprintf(preload, sizeof(preload), "%s/lib/wow_preload.rb", prefix);

        struct stat st;
        if (stat(preload, &st) != 0) {
            char preload_dir[WOW_OS_PATH_MAX];
            snprintf(preload_dir, sizeof(preload_dir), "%s/lib", prefix);
            wow_mkdirs(preload_dir, 0755);

            FILE *f = fopen(preload, "w");
            if (f) {
                fputs("module Kernel\n"
                      "  def gem(name, *requirements)\n"
                      "    true\n"
                      "  end\n"
                      "  private :gem\n"
                      "end\n", f);
                fclose(f);
            }
        }

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
        fprintf(stderr, "wowx: out of memory\n");
        return 1;
    }
    exec_argv[0] = (char *)ruby_bin;
    exec_argv[1] = (char *)exe_path;
    for (int i = 0; i < user_argc; i++)
        exec_argv[2 + i] = user_argv[i];
    exec_argv[nargs] = NULL;

    execv(ruby_bin, exec_argv);
    fprintf(stderr, "wowx: exec failed: %s\n", strerror(errno));
    free(exec_argv);
    return 1;
}

/*
 * Try to find a binary in a gem's standard bindirs (exe/, bin/).
 * Returns 0 on success (exe_path filled), -1 if not found.
 */
static int try_binary_in_gem(const char *gems_dir, const char *gem_entry,
                             const char *binary_name,
                             char *exe_path, size_t exe_path_sz)
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

/*
 * Search for gem binary in env_dir/gems/.
 *
 * Pass 1: search gem dirs matching gem_name (e.g. rails-8.1.2/).
 *         Also tries .executables markers for name-mismatch gems.
 * Pass 2: search ALL gem dirs (handles meta-gems like rails where
 *         the binary lives in a dependency, e.g. railties-8.1.2/).
 *
 * Returns 0 on success (exe_path filled), -1 if not found.
 */
static int find_cached_binary(const char *env_dir, const char *gem_name,
                              const char *binary_name,
                              char *exe_path, size_t exe_path_sz)
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

    /* Pass 1: directories matching gem_name */
    DIR *dir = opendir(gems_dir);
    if (!dir) return -1;

    size_t nlen = strlen(gem_name);
    struct dirent *ent;
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
        SCOPY(entry, ent->d_name);

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

    /* Pass 2: search ALL gem directories (meta-gem support).
     * e.g. `rails` gem has no binary — it's in `railties`. */
    dir = opendir(gems_dir);
    if (!dir) return -1;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        /* Skip dirs already checked in pass 1 */
        if (strncmp(ent->d_name, gem_name, nlen) == 0 &&
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

/*
 * Check wowx cache for a specific version.
 * Returns 0 if found (env_dir + exe_path filled), -1 otherwise.
 */
static int check_cache_pinned(const char *wowx_cache, const char *gem_name,
                              const char *version, const char *binary_name,
                              char *env_dir, size_t env_dir_sz,
                              char *exe_path, size_t exe_path_sz)
{
    /* Bounded copies so GCC can prove env_dir composition fits */
    char cache[WOW_DIR_PATH_MAX];
    snprintf(cache, sizeof(cache), "%s", wowx_cache);
    char name[64];
    SCOPY(name, gem_name);
    char ver[32];
    SCOPY(ver, version);

    snprintf(env_dir, env_dir_sz, "%s/%s-%s", cache, name, ver);
    return find_cached_binary(env_dir, gem_name, binary_name,
                              exe_path, exe_path_sz);
}

/*
 * Scan wowx cache for the latest cached version of a gem.
 * Returns 0 if found (env_dir + exe_path filled), -1 otherwise.
 */
static int check_cache_latest(const char *wowx_cache, const char *gem_name,
                              const char *binary_name,
                              char *env_dir, size_t env_dir_sz,
                              char *exe_path, size_t exe_path_sz)
{
    /* Bounded copy so GCC can track size through compositions */
    char cache[WOW_DIR_PATH_MAX];
    snprintf(cache, sizeof(cache), "%s", wowx_cache);

    DIR *d = opendir(cache);
    if (!d) return -1;

    char best_env[WOW_OS_PATH_MAX] = {0};
    wow_gemver best_ver;
    int found = 0;
    size_t nlen = strlen(gem_name);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strncmp(ent->d_name, gem_name, nlen) != 0) continue;
        if (ent->d_name[nlen] != '-') continue;

        const char *vstr = ent->d_name + nlen + 1;
        wow_gemver v;
        if (wow_gemver_parse(vstr, &v) != 0) continue;

        char entry[128];
        SCOPY(entry, ent->d_name);

        if (!found || wow_gemver_cmp(&v, &best_ver) > 0) {
            best_ver = v;
            snprintf(best_env, sizeof(best_env), "%s/%s",
                     cache, entry);
            found = 1;
        }
    }
    closedir(d);

    if (!found) return -1;

    snprintf(env_dir, env_dir_sz, "%s", best_env);
    return find_cached_binary(env_dir, gem_name, binary_name,
                              exe_path, exe_path_sz);
}

/* ── Process helper ──────────────────────────────────────────────── */

/*
 * Run a command in a given working directory, wait for completion.
 * Child stdout is redirected to stderr so build noise (extconf checks,
 * compiler output) doesn't pollute the gem's actual stdout output.
 * Returns the exit code, or -1 on fork/exec failure.
 */
static int run_cmd(const char *cwd, const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "wowx: fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* Redirect stdout → stderr so build output doesn't mix
         * with the gem binary's actual output on stdout. */
        dup2(STDERR_FILENO, STDOUT_FILENO);
        if (cwd && chdir(cwd) != 0)
            _exit(127);
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ── Default gem detection ───────────────────────────────────────── */

/*
 * Check if a gem is a "default gem" in the target Ruby whose bundled
 * version matches the version the resolver chose.
 *
 * If the versions match we can skip unpacking — Ruby already has it.
 * If they differ (e.g. bundled prism 0.19 vs resolver chose 1.9) we
 * must install the gem version and let RUBYLIB shadow the bundled one.
 *
 * Default gemspecs live at:
 *   <prefix>/lib/ruby/gems/<api>/specifications/default/<name>-<ver>.gemspec
 */
static int is_default_gem_matching(const char *ruby_bin, const char *ruby_api,
                                   const char *gem_name, const char *gem_ver)
{
    /* Derive prefix from ruby_bin: strip /bin/ruby */
    char prefix[WOW_DIR_PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s", ruby_bin);
    char *sl = strrchr(prefix, '/');
    if (sl) { *sl = '\0'; sl = strrchr(prefix, '/'); if (sl) *sl = '\0'; }

    /* Bounded copies so GCC can prove spec_path composition fits */
    char api[16];
    snprintf(api, sizeof(api), "%s", ruby_api);
    char name[64];
    snprintf(name, sizeof(name), "%s", gem_name);
    char ver[32];
    snprintf(ver, sizeof(ver), "%s", gem_ver);

    /* Look for <name>-<ver>.gemspec — exact version match */
    char spec_path[WOW_OS_PATH_MAX];
    snprintf(spec_path, sizeof(spec_path),
             "%s/lib/ruby/gems/%s/specifications/default/%s-%s.gemspec",
             prefix, api, name, ver);

    return access(spec_path, F_OK) == 0;
}

/* ── Native extension compilation ────────────────────────────────── */

/*
 * Check if a gem already has compiled native code (e.g. from a
 * platform-specific .gem download).  Looks for .so or .bundle files
 * under the gem's lib/ tree.
 */
static int has_native_lib(const char *gem_dir)
{
    /* Bounded copy so GCC can track sizes through compositions */
    char dir[WOW_DIR_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", gem_dir);

    char lib_dir[WOW_OS_PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/lib", dir);

    /* Quick scan: look for any .so or .bundle file recursively.
     * We only go one level deep under lib/ — that covers the common
     * case (lib/prism/prism.so, lib/x86_64-linux/foo.so). */
    DIR *d1 = opendir(lib_dir);
    if (!d1) return 0;

    struct dirent *e1;
    while ((e1 = readdir(d1)) != NULL) {
        if (e1->d_name[0] == '.') continue;
        size_t len = strlen(e1->d_name);
        if ((len > 3 && strcmp(e1->d_name + len - 3, ".so") == 0) ||
            (len > 7 && strcmp(e1->d_name + len - 7, ".bundle") == 0)) {
            closedir(d1);
            return 1;
        }
        /* Check one level deeper */
        char entry[128];
        SCOPY(entry, e1->d_name);

        char sub[WOW_OS_PATH_MAX];
        snprintf(sub, sizeof(sub), "%s/lib/%s", dir, entry);
        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *d2 = opendir(sub);
        if (!d2) continue;
        struct dirent *e2;
        while ((e2 = readdir(d2)) != NULL) {
            size_t l2 = strlen(e2->d_name);
            if ((l2 > 3 && strcmp(e2->d_name + l2 - 3, ".so") == 0) ||
                (l2 > 7 && strcmp(e2->d_name + l2 - 7, ".bundle") == 0)) {
                closedir(d2);
                closedir(d1);
                return 1;
            }
        }
        closedir(d2);
    }
    closedir(d1);
    return 0;
}

/*
 * Build a native extension from source.
 *
 * Three-tier strategy:
 *   1. Platform binary already present (checked before calling this)
 *   2. Cosmo binary (stub — future)
 *   3. Build from source: ruby extconf.rb && make
 *
 * ext_path is relative to gem_dir, e.g. "ext/prism/extconf.rb".
 * ruby_bin is the absolute path to the Ruby binary.
 */
static int build_native_extension(const char *gem_dir, const char *ext_path,
                                   const char *ruby_bin,
                                   const char *ruby_api)
{
    /* Bounded copies so GCC can track sizes through compositions */
    char gdir[WOW_DIR_PATH_MAX];
    snprintf(gdir, sizeof(gdir), "%s", gem_dir);
    char ext[128];
    snprintf(ext, sizeof(ext), "%s", ext_path);

    /* Extract the directory containing extconf.rb */
    char ext_dir[WOW_OS_PATH_MAX];
    snprintf(ext_dir, sizeof(ext_dir), "%s/%s", gdir, ext);
    char *slash = strrchr(ext_dir, '/');
    if (slash) *slash = '\0';

    /* Verify extconf.rb exists */
    char extconf[WOW_OS_PATH_MAX];
    snprintf(extconf, sizeof(extconf), "%s/%s", gdir, ext);
    if (access(extconf, R_OK) != 0) {
        fprintf(stderr, "wowx: extension not found: %s\n", ext_path);
        return -1;
    }

    /* Set up environment for the forked Ruby process.
     * Pre-built rubies have hardcoded load paths from the build machine,
     * so we need LD_LIBRARY_PATH (for libruby.so) and RUBYLIB (for
     * stdlib: mkmf, rubygems, etc.) — same fix as do_exec(). */
    {
        char prefix[WOW_DIR_PATH_MAX];
        snprintf(prefix, sizeof(prefix), "%s", ruby_bin);
        char *sl = strrchr(prefix, '/');
        if (sl) { *sl = '\0'; sl = strrchr(prefix, '/'); if (sl) *sl = '\0'; }

        char api[16];
        snprintf(api, sizeof(api), "%s", ruby_api);

        /* LD_LIBRARY_PATH */
        char lib_dir[WOW_OS_PATH_MAX];
        snprintf(lib_dir, sizeof(lib_dir), "%s/lib", prefix);
        const char *existing_ld = getenv("LD_LIBRARY_PATH");
        if (existing_ld && existing_ld[0]) {
            char combined[PATH_MAX * 2];
            snprintf(combined, sizeof(combined), "%s:%s", lib_dir, existing_ld);
            setenv("LD_LIBRARY_PATH", combined, 1);
        } else {
            setenv("LD_LIBRARY_PATH", lib_dir, 1);
        }

        /* RUBYLIB — stdlib + arch-specific dir (for mkmf, rbconfig, etc.) */
        char stdlib_dir[WOW_OS_PATH_MAX];
        snprintf(stdlib_dir, sizeof(stdlib_dir), "%s/lib/ruby/%s",
                 prefix, api);

        /* Find arch subdir containing rbconfig.rb */
        char rubylib[PATH_MAX * 2];
        snprintf(rubylib, sizeof(rubylib), "%s", stdlib_dir);

        DIR *sd = opendir(stdlib_dir);
        if (sd) {
            struct dirent *se;
            while ((se = readdir(sd)) != NULL) {
                if (se->d_name[0] == '.') continue;

                char arch[128];
                SCOPY(arch, se->d_name);

                char candidate[WOW_OS_PATH_MAX];
                snprintf(candidate, sizeof(candidate),
                         "%s/lib/ruby/%s/%s/rbconfig.rb",
                         prefix, api, arch);
                if (access(candidate, R_OK) == 0) {
                    size_t pos = strlen(rubylib);
                    snprintf(rubylib + pos, sizeof(rubylib) - pos,
                             ":%s/lib/ruby/%s/%s", prefix, api, arch);
                    break;
                }
            }
            closedir(sd);
        }
        setenv("RUBYLIB", rubylib, 1);
    }

    /* Tier 2: Cosmo binary (stub)
     * TODO: check for Cosmopolitan fat binary variant of this extension.
     * Would be fetched from a wow-specific binary cache. */

    /* Tier 3: Build from source */
    int colour = wow_use_colour();
    if (colour)
        fprintf(stderr, WOW_ANSI_DIM "Building native extension: %s..."
                WOW_ANSI_RESET "\n", ext_path);
    else
        fprintf(stderr, "Building native extension: %s...\n", ext_path);

    /* Step 1: ruby extconf.rb */
    {
        const char *argv[] = { ruby_bin, "extconf.rb", NULL };
        int rc = run_cmd(ext_dir, argv);
        if (rc != 0) {
            fprintf(stderr, "wowx: extconf.rb failed (exit %d) in %s\n",
                    rc, ext_dir);
            return -1;
        }
    }

    /* Step 2: make */
    {
        const char *argv[] = { "/usr/bin/make", "-j4", NULL };
        int rc = run_cmd(ext_dir, argv);
        if (rc != 0) {
            fprintf(stderr, "wowx: make failed (exit %d) in %s\n",
                    rc, ext_dir);
            return -1;
        }
    }

    /* Step 3: make install into the gem's own lib/ directory.
     * extconf.rb-generated Makefiles support sitearchdir/sitelibdir
     * overrides to control where .so and .rb files land.
     *
     * NB: this duplicates .so files — the build artefact stays in ext/
     * and the installed copy goes to lib/.  We can't skip the install
     * step because the build layout doesn't match the require layout
     * (e.g. ext/racc/cparse/cparse.so vs lib/racc/cparse.so).
     * TODO: clean ext/ build artefacts after install to reclaim space. */
    {
        char sitearch[WOW_OS_PATH_MAX];
        char sitelib[WOW_OS_PATH_MAX];
        snprintf(sitearch, sizeof(sitearch), "sitearchdir=%s/lib", gdir);
        snprintf(sitelib, sizeof(sitelib), "sitelibdir=%s/lib", gdir);
        const char *argv[] = {
            "/usr/bin/make", "install", sitearch, sitelib, NULL
        };
        int rc = run_cmd(ext_dir, argv);
        if (rc != 0) {
            fprintf(stderr, "wowx: make install failed (exit %d) in %s\n",
                    rc, ext_dir);
            return -1;
        }
    }

    return 0;
}

/* ── Auto-install: resolve + download + unpack ───────────────────── */

static int auto_install(const char *gem_name, const char *constraint_str,
                        const char *ruby_api, const char *ruby_bin,
                        const char *ruby_version,
                        char *env_dir, size_t env_dir_sz)
{
    int ret = -1;

    /* 1. Resolve */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    wow_ci_provider ci;
    wow_ci_provider_init(&ci, "https://rubygems.org", &pool, ruby_version);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    const char *root_names[] = { gem_name };
    wow_gem_constraints root_cs[1];
    if (wow_gem_constraints_parse(constraint_str, &root_cs[0]) != 0) {
        fprintf(stderr, "wowx: invalid version constraint: %s\n",
                constraint_str);
        goto cleanup;
    }

    int colour = wow_use_colour();
    if (colour)
        fprintf(stderr, WOW_ANSI_DIM "Resolving dependencies..."
                WOW_ANSI_RESET "\n");
    else
        fprintf(stderr, "Resolving dependencies...\n");

    int rc = wow_solve(&solver, root_names, root_cs, 1);
    if (rc != 0) {
        fprintf(stderr, "wowx: failed to resolve dependencies for %s:\n%s\n",
                gem_name, solver.error_msg);
        goto cleanup;
    }

    int n_solved = solver.n_solved;

    /* Find the resolved version of the requested gem */
    const char *resolved_ver = NULL;
    for (int i = 0; i < n_solved; i++) {
        if (strcmp(solver.solution[i].name, gem_name) == 0) {
            resolved_ver = solver.solution[i].version.raw;
            break;
        }
    }
    if (!resolved_ver) {
        fprintf(stderr, "wowx: gem '%s' not found in solution\n", gem_name);
        goto cleanup;
    }

    /* Build env dir path: ~/.cache/wowx/<ruby_api>/<gem>-<ver>/ */
    char wowx_cache[WOW_DIR_PATH_MAX];
    if (wow_wowx_cache_dir(ruby_api, wowx_cache, sizeof(wowx_cache)) != 0)
        goto cleanup;

    snprintf(env_dir, env_dir_sz, "%s/%s-%s",
             wowx_cache, gem_name, resolved_ver);

    /* Copy solution names/versions out of solver arena before destroy.
     * Bounded sizes (64/32) give GCC proof that downstream path
     * compositions (cache_dir + name + version + suffix) fit. */
    char (*names)[64] = calloc((size_t)n_solved, 64);
    char (*versions)[32] = calloc((size_t)n_solved, 32);
    if (!names || !versions) {
        fprintf(stderr, "wowx: out of memory\n");
        free(names); free(versions);
        goto cleanup;
    }
    for (int i = 0; i < n_solved; i++) {
        SCOPY(names[i], solver.solution[i].name);
        SCOPY(versions[i], solver.solution[i].version.raw);
    }

    /* Done with solver + provider (arena freed here) */
    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);

    /* 2. Download missing .gem files */
    char cache_dir[WOW_DIR_PATH_MAX];
    if (wow_gem_cache_dir(cache_dir, sizeof(cache_dir)) != 0) {
        free(names); free(versions);
        wow_http_pool_cleanup(&pool);
        return -1;
    }
    {
        char cache_mut[WOW_DIR_PATH_MAX];
        snprintf(cache_mut, sizeof(cache_mut), "%s", cache_dir);
        wow_mkdirs(cache_mut, 0755);
    }

    wow_download_spec_t *specs = calloc((size_t)n_solved,
                                        sizeof(wow_download_spec_t));
    wow_download_result_t *results = calloc((size_t)n_solved,
                                             sizeof(wow_download_result_t));
    char (*urls)[512] = calloc((size_t)n_solved, 512);
    char (*paths)[WOW_OS_PATH_MAX] = calloc((size_t)n_solved,
                                             WOW_OS_PATH_MAX);
    char (*labels)[256] = calloc((size_t)n_solved, 256);

    if (!specs || !results || !urls || !paths || !labels) {
        fprintf(stderr, "wowx: out of memory\n");
        free(specs); free(results); free(urls); free(paths); free(labels);
        free(names); free(versions);
        wow_http_pool_cleanup(&pool);
        return -1;
    }

    const char **platforms = detect_gem_platforms();
    int n_plat = 0;
    if (platforms) while (platforms[n_plat]) n_plat++;

    /* Map download slot → solution index (for retry) */
    int *dl_map = calloc((size_t)n_solved, sizeof(int));
    if (!dl_map) {
        free(specs); free(results); free(urls); free(paths);
        free(labels); free(names); free(versions);
        wow_http_pool_cleanup(&pool);
        return -1;
    }

    /* Check cache: platform gem is preferred over generic.
     * Only use a cached generic gem if no platform variants exist
     * (e.g. macOS with only one platform string, or no platforms at all).
     * A stale generic gem must not prevent us from trying to download
     * a platform gem with pre-compiled native extensions. */
    int n_to_download = 0;
    for (int i = 0; i < n_solved; i++) {
        struct stat st;
        int cached = 0;
        for (int p = 0; p < n_plat && !cached; p++) {
            char path[WOW_OS_PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s-%s-%s.gem",
                     cache_dir, names[i], versions[i], platforms[p]);
            if (stat(path, &st) == 0 && st.st_size > 0) cached = 1;
        }
        if (!cached && n_plat == 0) {
            char path[WOW_OS_PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s-%s.gem",
                     cache_dir, names[i], versions[i]);
            if (stat(path, &st) == 0 && st.st_size > 0) cached = 1;
        }
        if (cached) continue;

        int d = n_to_download;
        dl_map[d] = i;

        /* Start with first platform variant (or generic if no platforms) */
        if (n_plat > 0) {
            snprintf(urls[d], 512,
                     "https://rubygems.org/downloads/%s-%s-%s.gem",
                     names[i], versions[i], platforms[0]);
            snprintf(paths[d], WOW_OS_PATH_MAX, "%s/%s-%s-%s.gem",
                     cache_dir, names[i], versions[i], platforms[0]);
        } else {
            snprintf(urls[d], 512,
                     "https://rubygems.org/downloads/%s-%s.gem",
                     names[i], versions[i]);
            snprintf(paths[d], WOW_OS_PATH_MAX, "%s/%s-%s.gem",
                     cache_dir, names[i], versions[i]);
        }
        snprintf(labels[d], 256, "%s-%s.gem", names[i], versions[i]);

        specs[d].url = urls[d];
        specs[d].dest_path = paths[d];
        specs[d].label = labels[d];
        n_to_download++;
    }

    if (n_to_download > 0) {
        int ok = wow_parallel_download(specs, results, n_to_download, 0, 0);

        /* Retry failures with remaining platform variants, then generic.
         * E.g. on Linux: round 0 tried x86_64-linux-gnu, round 1 tries
         * x86_64-linux, round 2 tries generic (no platform suffix).
         * Pure-Ruby gems 404 on all platform variants and succeed on generic. */
        for (int round = 1; round <= n_plat && ok < n_to_download; round++) {
            const char *try_plat = (round < n_plat) ? platforms[round] : NULL;
            int n_retry = 0;

            for (int d = 0; d < n_to_download; d++) {
                if (results[d].ok) continue;
                unlink(paths[d]);

                int i = dl_map[d];
                if (try_plat) {
                    snprintf(urls[n_retry], 512,
                             "https://rubygems.org/downloads/%s-%s-%s.gem",
                             names[i], versions[i], try_plat);
                    snprintf(paths[n_retry], WOW_OS_PATH_MAX,
                             "%s/%s-%s-%s.gem",
                             cache_dir, names[i], versions[i], try_plat);
                } else {
                    snprintf(urls[n_retry], 512,
                             "https://rubygems.org/downloads/%s-%s.gem",
                             names[i], versions[i]);
                    snprintf(paths[n_retry], WOW_OS_PATH_MAX,
                             "%s/%s-%s.gem",
                             cache_dir, names[i], versions[i]);
                }
                snprintf(labels[n_retry], 256, "%s-%s.gem",
                         names[i], versions[i]);
                specs[n_retry].url = urls[n_retry];
                specs[n_retry].dest_path = paths[n_retry];
                specs[n_retry].label = labels[n_retry];
                dl_map[n_retry] = i;
                n_retry++;
            }
            if (n_retry > 0) {
                memset(results, 0,
                       (size_t)n_retry * sizeof(wow_download_result_t));
                ok = wow_parallel_download(specs, results, n_retry, 0, 0);
                n_to_download = n_retry;
            }
        }

        /* After all rounds, check for remaining failures */
        if (ok < n_to_download) {
            for (int d = 0; d < n_to_download; d++) {
                if (!results[d].ok) {
                    fprintf(stderr, "wowx: download failed: %s\n",
                            specs[d].label);
                    break;
                }
            }
            free(dl_map);
            free(specs); free(results); free(urls); free(paths);
            free(labels); free(names); free(versions);
            wow_http_pool_cleanup(&pool);
            return -1;
        }
    }

    free(dl_map);
    free(specs); free(results); free(urls); free(paths); free(labels);
    wow_http_pool_cleanup(&pool);

    /* 3. Unpack all gems to env dir and write metadata markers.
     *
     * Bounded copy: all deep paths below are built from env[WOW_DIR_PATH_MAX]
     * rather than chaining through intermediate WOW_OS_PATH_MAX buffers,
     * so GCC can statically prove every composition fits in WOW_OS_PATH_MAX. */
    char env[WOW_DIR_PATH_MAX];
    snprintf(env, sizeof(env), "%s", env_dir);

    char gems_base[WOW_OS_PATH_MAX];
    snprintf(gems_base, sizeof(gems_base), "%s/gems", env);
    wow_mkdirs(gems_base, 0755);

    for (int i = 0; i < n_solved; i++) {
        /* Skip default gems when the bundled version matches the
         * resolved version — Ruby already has it, no need to unpack.
         * If versions differ we must install the gem and let RUBYLIB
         * shadow the bundled copy. */
        if (is_default_gem_matching(ruby_bin, ruby_api,
                                    names[i], versions[i]))
            continue;

        /* Find the .gem file: try each platform variant, then generic */
        char gem_path[WOW_OS_PATH_MAX];
        struct stat gst;
        int gem_found = 0;
        for (int p = 0; p < n_plat && !gem_found; p++) {
            snprintf(gem_path, sizeof(gem_path), "%s/%s-%s-%s.gem",
                     cache_dir, names[i], versions[i], platforms[p]);
            if (stat(gem_path, &gst) == 0 && gst.st_size > 0)
                gem_found = 1;
        }
        if (!gem_found)
            snprintf(gem_path, sizeof(gem_path), "%s/%s-%s.gem",
                     cache_dir, names[i], versions[i]);

        /* Build dest_dir from env (not gems_base) to avoid chain */
        char dest_dir[WOW_OS_PATH_MAX];
        snprintf(dest_dir, sizeof(dest_dir), "%s/gems/%s-%s",
                 env, names[i], versions[i]);

        struct stat st;
        if (stat(dest_dir, &st) == 0 && S_ISDIR(st.st_mode))
            continue;

        if (wow_gem_unpack_q(gem_path, dest_dir, 1) != 0) {
            fprintf(stderr, "wowx: failed to unpack %s-%s\n",
                    names[i], versions[i]);
            free(names); free(versions);
            return -1;
        }

        /* Parse gemspec and write marker files.
         * .require_paths — load paths (do_exec uses these for RUBYLIB)
         * .executables   — binary names (find_cached_binary uses these) */
        struct wow_gemspec gspec;
        if (wow_gemspec_parse(gem_path, &gspec) == 0) {
            if (gspec.n_require_paths > 0) {
                char rp_path[WOW_OS_PATH_MAX];
                snprintf(rp_path, sizeof(rp_path),
                         "%s/gems/%s-%s/.require_paths",
                         env, names[i], versions[i]);
                FILE *rpf = fopen(rp_path, "w");
                if (rpf) {
                    for (size_t r = 0; r < gspec.n_require_paths; r++)
                        fprintf(rpf, "%s\n", gspec.require_paths[r]);
                    fclose(rpf);
                }
            }
            if (gspec.n_executables > 0) {
                char ex_path[WOW_OS_PATH_MAX];
                snprintf(ex_path, sizeof(ex_path),
                         "%s/gems/%s-%s/.executables",
                         env, names[i], versions[i]);
                FILE *exf = fopen(ex_path, "w");
                if (exf) {
                    for (size_t e = 0; e < gspec.n_executables; e++)
                        fprintf(exf, "%s\n", gspec.executables[e]);
                    fclose(exf);
                }
            }

            /* Native extensions: three-tier strategy.
             * 1. Platform binary already present → skip
             * 2. Cosmo binary (stub — future)
             * 3. Build from source: ruby extconf.rb && make */
            if (gspec.n_extensions > 0 && !has_native_lib(dest_dir)) {
                for (size_t e = 0; e < gspec.n_extensions; e++) {
                    if (build_native_extension(dest_dir, gspec.extensions[e],
                                               ruby_bin, ruby_api) != 0) {
                        fprintf(stderr,
                                "wowx: native extension build failed for "
                                "%s-%s (%s)\n",
                                names[i], versions[i], gspec.extensions[e]);
                        wow_gemspec_free(&gspec);
                        free(names); free(versions);
                        return -1;
                    }
                }
            }

            wow_gemspec_free(&gspec);
        }
    }

    /* Write completion marker — without this, a partial env (from a
     * timed-out or crashed install) would be treated as a cache hit,
     * causing LoadError for missing transitive dependencies. */
    {
        char marker_path[WOW_OS_PATH_MAX];
        snprintf(marker_path, sizeof(marker_path), "%s/.installed", env);
        FILE *mf = fopen(marker_path, "w");
        if (mf) fclose(mf);
    }

    if (colour) {
        fprintf(stderr, WOW_ANSI_GREEN WOW_ANSI_BOLD "Installed "
                WOW_ANSI_RESET "%d packages" WOW_ANSI_RESET "\n", n_solved);
    } else {
        fprintf(stderr, "Installed %d packages\n", n_solved);
    }

    free(names); free(versions);
    return 0;

cleanup:
    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);
    return ret;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        print_usage();
        return argc < 2 ? 1 : 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("wowx %s\n", WOW_VERSION);
        return 0;
    }

    /* Parse leading options before the gem arg.
     * Currently: --ruby / -r <version> */
    const char *requested_ruby = NULL;
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--ruby") == 0 ||
            strcmp(argv[argi], "-r") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "wowx: %s requires a version argument\n",
                        argv[argi]);
                return 1;
            }
            requested_ruby = argv[argi + 1];
            argi += 2;
        } else if (strcmp(argv[argi], "--") == 0) {
            break;  /* stop option parsing */
        } else {
            break;  /* not a known option — must be the gem arg */
        }
    }

    if (argi >= argc) {
        print_usage();
        return 1;
    }

    /* Parse argv[argi]: gem_name[@version] */
    char gem_name[128];
    char pin_version[128] = {0};

    const char *at = strchr(argv[argi], '@');
    if (at) {
        size_t nlen = (size_t)(at - argv[argi]);
        if (nlen >= sizeof(gem_name)) {
            fprintf(stderr, "wowx: gem name too long\n");
            return 1;
        }
        memcpy(gem_name, argv[argi], nlen);
        gem_name[nlen] = '\0';

        const char *ver = at + 1;
        if (strcmp(ver, "latest") == 0)
            pin_version[0] = '\0';
        else
            snprintf(pin_version, sizeof(pin_version), "%s", ver);
    } else {
        snprintf(gem_name, sizeof(gem_name), "%s", argv[argi]);
    }

    /* Binary name: resolved from gemspec below, defaults to gem_name */
    char binary_buf[128];
    snprintf(binary_buf, sizeof(binary_buf), "%s", gem_name);
    const char *binary_name = binary_buf;

    /* Build user args (everything after gem arg, skip "--" separator) */
    int user_argc = argc - (argi + 1);
    char **user_argv = argv + (argi + 1);
    if (user_argc > 0 && strcmp(user_argv[0], "--") == 0) {
        user_argc--;
        user_argv++;
    }

    /* 1. Find Ruby — use requested version or latest installed.
     *    Auto-install if missing (like uvx auto-fetches Python). */
    char ruby_ver[64];
    if (requested_ruby) {
        if (wow_ruby_pick_matching(requested_ruby, ruby_ver,
                                    sizeof(ruby_ver)) != 0) {
            /* Not installed — auto-install.
             * TODO: support partial versions (X.Y) — resolve to latest
             * patch via definition files.  For now X.Y.Z works reliably;
             * X.Y works only if it happens to be the latest minor. */
            fprintf(stderr, "wowx: Ruby %s not installed — installing...\n",
                    requested_ruby);
            if (wow_ruby_install(requested_ruby) != 0) {
                fprintf(stderr, "wowx: failed to install Ruby %s\n",
                        requested_ruby);
                return 1;
            }
            if (wow_ruby_pick_matching(requested_ruby, ruby_ver,
                                        sizeof(ruby_ver)) != 0) {
                fprintf(stderr, "wowx: Ruby %s not found after install\n",
                        requested_ruby);
                return 1;
            }
        }
    } else {
        if (wow_ruby_pick_latest(ruby_ver, sizeof(ruby_ver)) != 0) {
            /* No Ruby at all — resolve latest and install */
            char latest_ver[32];
            if (wow_latest_ruby_version(latest_ver, sizeof(latest_ver)) != 0) {
                fprintf(stderr, "wowx: cannot determine latest Ruby version\n");
                return 1;
            }
            fprintf(stderr, "wowx: no Ruby installed — installing %s...\n",
                    latest_ver);
            if (wow_ruby_install(latest_ver) != 0) {
                fprintf(stderr, "wowx: failed to install Ruby\n");
                return 1;
            }
            if (wow_ruby_pick_latest(ruby_ver, sizeof(ruby_ver)) != 0) {
                fprintf(stderr, "wowx: no Ruby found after install\n");
                return 1;
            }
        }
    }

    char ruby_bin[PATH_MAX];
    if (wow_ruby_bin_path(ruby_ver, ruby_bin, sizeof(ruby_bin)) != 0) {
        fprintf(stderr, "wowx: Ruby %s binary not found\n", ruby_ver);
        return 1;
    }

    char ruby_api[16];
    wow_ruby_api_version(ruby_ver, ruby_api, sizeof(ruby_api));

    /* 2. Check user-installed gems: ~/.gem/ruby/X.Y.0/bin/<binary>
     *    Skip when --ruby was specified — managed Rubies should use
     *    isolated wowx environments, not system gems (which may have
     *    ABI-incompatible native extensions or stale versions). */
    if (!requested_ruby) {
        const char *home = getenv("HOME");
        if (home) {
            /* Bounded copies so GCC can prove user_bin composition fits */
            char home_dir[WOW_DIR_PATH_MAX];
            snprintf(home_dir, sizeof(home_dir), "%s", home);
            char bin[64];
            SCOPY(bin, binary_name);

            char user_bin[WOW_OS_PATH_MAX];
            snprintf(user_bin, sizeof(user_bin),
                     "%s/.gem/ruby/%s/bin/%s", home_dir, ruby_api, bin);
            if (access(user_bin, R_OK) == 0)
                return do_exec(ruby_bin, ruby_api, NULL, user_bin,
                               user_argc, user_argv);
        }
    }

    /* 3. Check wowx cache */
    {
        char wowx_cache[WOW_DIR_PATH_MAX];
        if (wow_wowx_cache_dir(ruby_api, wowx_cache, sizeof(wowx_cache)) == 0) {
            char env_dir[WOW_OS_PATH_MAX];
            char exe_path[WOW_OS_PATH_MAX];

            int found;
            if (pin_version[0])
                found = check_cache_pinned(wowx_cache, gem_name, pin_version,
                                           binary_name, env_dir,
                                           sizeof(env_dir), exe_path,
                                           sizeof(exe_path));
            else
                found = check_cache_latest(wowx_cache, gem_name, binary_name,
                                           env_dir, sizeof(env_dir),
                                           exe_path, sizeof(exe_path));

            if (found == 0)
                return do_exec(ruby_bin, ruby_api, env_dir, exe_path,
                               user_argc, user_argv);
        }
    }

    /* 4. Auto-install: resolve + download + unpack */
    {
        char cs_str[256];
        if (pin_version[0])
            snprintf(cs_str, sizeof(cs_str), "= %s", pin_version);
        else
            snprintf(cs_str, sizeof(cs_str), ">= 0");

        char env_dir[WOW_OS_PATH_MAX];
        if (auto_install(gem_name, cs_str, ruby_api, ruby_bin,
                         ruby_ver, env_dir, sizeof(env_dir)) != 0)
            return 1;

        /* Resolve binary name from gemspec (handles gems where
         * the executable name differs from the gem name, e.g.
         * haml_lint gem → haml-lint binary). */
        {
            char gem_cache[WOW_DIR_PATH_MAX];
            if (wow_gem_cache_dir(gem_cache, sizeof(gem_cache)) == 0) {
                /* Find the .gem file for the primary gem */
                DIR *gdir = opendir(gem_cache);
                if (gdir) {
                    struct dirent *ge;
                    size_t gnlen = strlen(gem_name);
                    while ((ge = readdir(gdir)) != NULL) {
                        if (strncmp(ge->d_name, gem_name, gnlen) != 0)
                            continue;
                        if (ge->d_name[gnlen] != '-') continue;
                        size_t dlen = strlen(ge->d_name);
                        if (dlen < 5) continue;
                        if (strcmp(ge->d_name + dlen - 4, ".gem") != 0)
                            continue;

                        char ge_entry[128];
                        snprintf(ge_entry, sizeof(ge_entry), "%s",
                                 ge->d_name);

                        char gpath[WOW_OS_PATH_MAX];
                        snprintf(gpath, sizeof(gpath), "%s/%s",
                                 gem_cache, ge_entry);

                        struct wow_gemspec gspec;
                        if (wow_gemspec_parse(gpath, &gspec) == 0) {
                            if (gspec.n_executables > 0) {
                                /* Prefer gem_name if it's one of the
                                 * executables (e.g. reek has both "reek"
                                 * and "code_climate_reek").  Only fall
                                 * back to executables[0] when gem_name
                                 * isn't listed (e.g. haml_lint → haml-lint). */
                                int matched = 0;
                                for (size_t e = 0; e < gspec.n_executables; e++) {
                                    if (strcmp(gspec.executables[e], gem_name) == 0) {
                                        matched = 1;
                                        break;
                                    }
                                }
                                if (!matched && gspec.executables[0]) {
                                    snprintf(binary_buf, sizeof(binary_buf),
                                             "%s", gspec.executables[0]);
                                }
                            }
                            wow_gemspec_free(&gspec);
                        }
                        break;
                    }
                    closedir(gdir);
                }
            }
        }

        char exe_path[WOW_OS_PATH_MAX];
        if (find_cached_binary(env_dir, gem_name, binary_name,
                               exe_path, sizeof(exe_path)) != 0) {
            fprintf(stderr, "wowx: binary '%s' not found in gem '%s'\n",
                    binary_name, gem_name);
            return 1;
        }

        return do_exec(ruby_bin, ruby_api, env_dir, exe_path,
                       user_argc, user_argv);
    }
}
