# wow Refactoring Implementation Specification

**Status:** In Progress (Session 1 Complete)  
**Scope:** DRY improvements, file splitting, and wow/wowx code sharing  
**Target:** Gradual migration with tests passing at each step

---

## Guiding Principles

1. **Tests pass at every commit** — no big-bang refactors
2. **Create new utilities first** — then migrate call sites
3. **Delete old code last** — once all sites migrated
4. **Follow existing conventions** — `wow_` prefix, en_IE spelling

---

## Phase 1: Foundation Utilities (High Impact, Low Effort)

### 1.1 Create `src/util/fmt.c` + `include/wow/util/fmt.h`

**New Files:**
```c
// include/wow/util/fmt.h
#ifndef WOW_UTIL_FMT_H
#define WOW_UTIL_FMT_H

#include <stddef.h>

/* Format bytes as human-readable string (e.g., "1.5 MiB")
 * buf must be at least 8 bytes (fits "999 MiB\0")
 */
void wow_fmt_bytes(size_t bytes, char *buf, size_t bufsz);

/* Format bytes with spaces instead of unit suffix
 * (e.g., "1572864" -> "1 572 864")
 */
void wow_fmt_bytes_spaced(size_t bytes, char *buf, size_t bufsz);

#endif
```

```c
// src/util/fmt.c
#include "wow/util/fmt.h"
#include <stdio.h>

void
wow_fmt_bytes(size_t bytes, char *buf, size_t bufsz)
{
    const char *units[] = { "B", "KiB", "MiB", "GiB" };
    int unit = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(buf, bufsz, "%.0f %s", size, units[unit]);
    } else {
        snprintf(buf, bufsz, "%.1f %s", size, units[unit]);
    }
}

void
wow_fmt_bytes_spaced(size_t bytes, char *buf, size_t bufsz)
{
    /* Implementation from gems/list.c fmt_size() */
    if (bytes < 1000) {
        snprintf(buf, bufsz, "%zu", bytes);
    } else if (bytes < 1000000) {
        snprintf(buf, bufsz, "%zu %03zu", bytes / 1000, bytes % 1000);
    } else if (bytes < 1000000000) {
        snprintf(buf, bufsz, "%zu %03zu %03zu",
                 bytes / 1000000, (bytes / 1000) % 1000, bytes % 1000);
    } else {
        snprintf(buf, bufsz, "%zu %03zu %03zu %03zu",
                 bytes / 1000000000, (bytes / 1000000) % 1000,
                 (bytes / 1000) % 1000, bytes % 1000);
    }
}
```

**Migration Steps:**
1. Create new files
2. Add `src/util/fmt.c` to Makefile `SHARED_OBJS`
3. Replace `format_bytes()` in `src/download/multibar.c`
4. Replace `format_bytes()` in `src/download/progress.c`
5. Replace `fmt_size()` in `src/gems/list.c`
6. Verify `make test` passes
7. Delete old implementations

---

### 1.2 Create `include/wow/util/buf.h` (Header-Only)

**New File:**
```c
// include/wow/util/buf.h
#ifndef WOW_UTIL_BUF_H
#define WOW_UTIL_BUF_H

#include <string.h>

/* Fill buffer with repeated character */
static inline void
wow_buf_fill(char *buf, size_t bufsz, int *pos, char c, int n)
{
    while (*pos < (int)bufsz - 1 && n-- > 0) {
        buf[(*pos)++] = c;
    }
}

/* Append formatted string to buffer with bounds checking
 * Returns number of characters that would have been written (like snprintf)
 */
#define WOW_BUF_APPEND(buf, bufsz, pos, ...) \
    do { \
        int _n = snprintf((buf) + (pos), (bufsz) - (pos), __VA_ARGS__); \
        if (_n > 0) { \
            (pos) += _n; \
            if ((pos) > (int)(bufsz)) (pos) = (int)(bufsz); \
        } \
    } while (0)

#endif
```

**Migration Steps:**
1. Create header file
2. Replace `BUF_APPEND` macro and `buf_fill()` in `src/download/multibar.c`
3. Replace in `src/download/progress.c`
4. Update `#include` paths
5. Verify tests pass

---

### 1.3 HTTP/TLS DRY in `src/http/client.c`

**Extract TLS Setup Helper:**
```c
// In src/http/client.c, add static helper:

static int
tls_setup(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf,
          mbedtls_ctr_drbg_context *drbg, int sock, const char *host)
{
    mbedtls_ssl_init(ssl);
    mbedtls_ssl_config_init(conf);
    mbedtls_ctr_drbg_init(drbg);
    
    mbedtls_ssl_config_defaults(conf,
                                MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(conf, &g_ssl_roots, NULL);
    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, drbg);
    
    if (mbedtls_ctr_drbg_seed(drbg, wow_mbedtls_entropy_poll, NULL,
                              (const unsigned char *)host, strlen(host)) != 0) {
        return -1;
    }
    
    if (mbedtls_ssl_setup(ssl, conf) != 0) {
        return -1;
    }
    
    if (mbedtls_ssl_set_hostname(ssl, host) != 0) {
        return -1;
    }
    
    mbedtls_ssl_set_bio(ssl, &sock, mbedtls_net_send, mbedtls_net_recv, NULL);
    
    int ret;
    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }
    
    return 0;
}
```

**Extract HTTP Request Builder:**
```c
static void
build_http_request(char *request, size_t request_sz,
                   const char *path, const char *host, const char *port,
                   bool keep_alive)
{
    snprintf(request, request_sz,
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%s\r\n"
             "User-Agent: wow/" WOW_VERSION "\r\n"
             "Connection: %s\r\n"
             "\r\n",
             path, host, port,
             keep_alive ? "keep-alive" : "close");
}
```

**Migration Steps:**
1. Add static helper declarations at top of `src/http/client.c`
2. Implement helpers
3. Replace TLS setup in `do_get_to_file()` (~lines 92-132)
4. Replace TLS setup in `do_get_to_fd()` (~lines 510-550)
5. Replace request building in both functions + pool.c
6. Verify `make test-tls` and `make test-registry` pass

---

## Phase 2: Centralised Defaults

### 2.1 Create `include/wow/defaults.h`

**New File:**
```c
// include/wow/defaults.h
#ifndef WOW_DEFAULTS_H
#define WOW_DEFAULTS_H

/* Registry and download URLs */
#define WOW_DEFAULT_REGISTRY      "https://rubygems.org"
#define WOW_GEM_DOWNLOAD_URL_FMT  "https://rubygems.org/downloads/%s-%s.gem"
#define WOW_RUBY_BUILDER_URL      "https://github.com/ruby/ruby-builder/releases/download"

/* Default file names */
#define WOW_DEFAULT_GEMFILE       "Gemfile"
#define WOW_DEFAULT_LOCKFILE      "Gemfile.lock"
#define WOW_DEFAULT_RUBY_VERSION_FILE ".ruby-version"

/* Cache directory components */
#define WOW_CACHE_DIR_NAME        "wow"
#define WOWX_CACHE_DIR_NAME       "wowx"
#define WOW_GEM_CACHE_SUBDIR      "gems"

/* Fallback temp directory */
#define WOW_FALLBACK_TMPDIR       "/tmp"

/* Default Ruby version */
#define WOW_DEFAULT_RUBY_VERSION  "3.3.0"

/* HTTP User-Agent */
#define WOW_HTTP_USER_AGENT       "wow/" WOW_VERSION

/* Error prefixes */
#define WOW_ERR_PREFIX            "wow: "
#define WOWX_ERR_PREFIX           "wowx: "

/* Buffer sizes and limits */
#define WOW_MAX_TAR_FILE_SIZE     (100ULL * 1024 * 1024)  /* 100 MiB */
#define WOW_ZLIB_BUFFER_SIZE      65536
#define WOW_MAX_GEM_METADATA      (1024 * 1024)           /* 1 MiB */
#define WOW_MAX_GEMSPEC_YAML      (4 * 1024 * 1024)       /* 4 MiB */
#define WOW_ARENA_INITIAL_CAP     (64 * 1024)             /* 64 KiB */
#define WOW_HTTP_BUFFER_CHUNK     4096

#endif
```

**Migration Strategy (Gradual):**
1. Create `defaults.h`
2. For each hardcoded string:
   - Replace with `#include "wow/defaults.h"` + constant
   - Run tests
   - Commit
3. Priority order:
   - Registry URLs (resolver/cmd.c, gems/download.c, gemfile/parser.c)
   - Default filenames (sync.c, init.c, rubies/resolve.c)
   - Cache paths (gems/download.c, wowx_main.c)
   - Limits (tar.c, gems/meta.c, resolver/arena.c, http/client.c)

---

## Phase 3: wow/wowx Code Sharing

### 3.1 Create `src/exec/` Module Structure

**New Directories:**
```
src/exec/
├── env.c           # RUBYLIB construction, exec
├── shims.c         # Bundler shim, preload.rb
├── discover.c      # Gem binary discovery
└── platform.c      # Platform detection (if needed)

include/wow/
├── exec.h          # Public API umbrella header
└── exec/
    ├── env.h       # Environment setup
    ├── shims.h     # Shim generation
    └── discover.h  # Binary discovery
```

### 3.2 Extract Shim Generation (`src/exec/shims.c`)

**New File:**
```c
// include/wow/exec/shims.h
#ifndef WOW_EXEC_SHIMS_H
#define WOW_EXEC_SHIMS_H

/* Ensure lib/wow_shims/bundler/setup.rb exists
 * Returns 0 on success, -1 on error
 */
int wow_ensure_bundler_shim(const char *ruby_prefix);

/* Ensure lib/wow_preload.rb exists
 * Returns 0 on success, -1 on error
 */
int wow_ensure_gem_preload(const char *ruby_prefix);

#endif
```

```c
// src/exec/shims.c
#include "wow/exec/shims.h"
#include "wow/common.h"
#include <stdio.h>
#include <string.h>

/* Bundler shim content - prevents Bundler from loading Gemfile */
static const char *BUNDLER_SHIM_CONTENT =
    "# Generated by wow - no-op Bundler shim\n"
    "module Bundler\n"
    "  def self.setup(*); end\n"
    "  def self.require(*); end\n"
    "  class << self\n"
    "    def method_missing(m, *); end\n"
    "  end\n"
    "end\n";

/* Preload stub content - makes Kernel#gem a no-op */
static const char *PRELOAD_CONTENT =
    "# Generated by wow - no-op gem activation\n"
    "module Kernel\n"
    "  alias_method :__wow_original_gem, :gem\n"
    "  def gem(*); end\n"
    "end\n";

int
wow_ensure_bundler_shim(const char *ruby_prefix)
{
    char path[WOW_OS_PATH_MAX];
    snprintf(path, sizeof(path), "%s/lib/wow_shims/bundler", ruby_prefix);
    
    if (wow_mkdirs(path) != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/lib/wow_shims/bundler/setup.rb", ruby_prefix);
    
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    
    fputs(BUNDLER_SHIM_CONTENT, f);
    fclose(f);
    return 0;
}

int
wow_ensure_gem_preload(const char *ruby_prefix)
{
    char path[WOW_OS_PATH_MAX];
    snprintf(path, sizeof(path), "%s/lib/wow_preload.rb", ruby_prefix);
    
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    
    fputs(PRELOAD_CONTENT, f);
    fclose(f);
    return 0;
}
```

**Migration Steps:**
1. Create new files
2. Add `src/exec/shims.c` to Makefile
3. Replace inline shim generation in `src/wowx_main.c:173-210`
4. Replace inline preload generation in `src/wowx_main.c:329-350`
5. Verify wowx still works: `make && ./build/wowx.com --help`

---

### 3.3 Extract Binary Discovery (`src/exec/discover.c`)

**New File:**
```c
// include/wow/exec/discover.h
#ifndef WOW_EXEC_DISCOVER_H
#define WOW_EXEC_DISCOVER_H

#include <stddef.h>

/* Find gem binary in environment directory
 * 
 * env_dir:         Base directory (e.g., ~/.cache/wowx/3.3.0 or vendor/bundle/ruby/3.3.0)
 * gem_name:        Name of the gem (or NULL for any gem)
 * binary_name:     Name of the executable to find
 * exe_path:        Output buffer for found executable path
 * exe_path_sz:     Size of exe_path buffer
 * 
 * Returns 0 if found (exe_path populated), -1 if not found
 * 
 * Search order:
 *   1. If gem_name specified: env_dir/gems/gem_name-*/exe/binary_name
 *   2. If gem_name specified: env_dir/gems/gem_name-*/bin/binary_name
 *   3. Search all gems: env_dir/gems/*/exe/binary_name
 *   4. Search all gems: env_dir/gems/*/bin/binary_name
 */
int wow_find_gem_binary(const char *env_dir, const char *gem_name,
                        const char *binary_name, char *exe_path, size_t exe_path_sz);

#endif
```

```c
// src/exec/discover.c
#include "wow/exec/discover.h"
#include "wow/common.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Try to find binary in specific gem directory */
static int
try_binary_in_gem(const char *gem_dir, const char *binary_name,
                  char *exe_path, size_t exe_path_sz)
{
    const char *subdirs[] = { "exe", "bin" };
    
    for (int i = 0; i < 2; i++) {
        snprintf(exe_path, exe_path_sz, "%s/%s/%s", 
                 gem_dir, subdirs[i], binary_name);
        
        struct stat st;
        if (stat(exe_path, &st) == 0 && S_ISREG(st.st_mode) &&
            (st.st_mode & S_IXUSR)) {
            return 0;
        }
    }
    
    return -1;
}

int
wow_find_gem_binary(const char *env_dir, const char *gem_name,
                    const char *binary_name, char *exe_path, size_t exe_path_sz)
{
    char gems_dir[WOW_OS_PATH_MAX];
    snprintf(gems_dir, sizeof(gems_dir), "%s/gems", env_dir);
    
    DIR *dir = opendir(gems_dir);
    if (!dir) {
        return -1;
    }
    
    struct dirent *entry;
    int found = -1;
    
    /* Pass 1: If gem_name specified, search matching gems first */
    if (gem_name) {
        size_t gem_name_len = strlen(gem_name);
        
        while ((entry = readdir(dir)) != NULL && found != 0) {
            if (entry->d_name[0] == '.') continue;
            
            /* Check if directory starts with "gem_name-" */
            if (strncmp(entry->d_name, gem_name, gem_name_len) == 0 &&
                entry->d_name[gem_name_len] == '-') {
                char gem_path[WOW_OS_PATH_MAX];
                snprintf(gem_path, sizeof(gem_path), "%s/%s", 
                         gems_dir, entry->d_name);
                
                if (try_binary_in_gem(gem_path, binary_name, 
                                      exe_path, exe_path_sz) == 0) {
                    found = 0;
                }
            }
        }
        
        rewinddir(dir);
    }
    
    /* Pass 2: Search all gems (for meta-gems like rails) */
    while ((entry = readdir(dir)) != NULL && found != 0) {
        if (entry->d_name[0] == '.') continue;
        
        char gem_path[WOW_OS_PATH_MAX];
        snprintf(gem_path, sizeof(gem_path), "%s/%s", 
                 gems_dir, entry->d_name);
        
        if (try_binary_in_gem(gem_path, binary_name,
                              exe_path, exe_path_sz) == 0) {
            found = 0;
        }
    }
    
    closedir(dir);
    return found;
}
```

**Migration Steps:**
1. Create new files
2. Add to Makefile
3. Replace `try_binary_in_gem()` and `find_cached_binary()` in wowx_main.c
4. Test wowx binary discovery still works

---

### 3.4 Extract Environment Setup (`src/exec/env.c`)

This is the most complex extraction - the RUBYLIB construction logic.

**New File:**
```c
// include/wow/exec/env.h
#ifndef WOW_EXEC_ENV_H
#define WOW_EXEC_ENV_H

/* Execute a gem binary with proper Ruby environment
 * 
 * ruby_bin:        Path to Ruby executable
 * ruby_api:        Ruby API version (e.g., "3.3.0")
 * env_dir:         Gem environment directory
 * exe_path:        Path to the gem executable to run
 * argc:            Argument count
 * argv:            Argument vector
 * 
 * This function does not return on success (execve replaces process).
 * Returns -1 on error (environment setup failed).
 */
int wow_exec_gem_binary(const char *ruby_bin, const char *ruby_api,
                        const char *env_dir, const char *exe_path,
                        int argc, char **argv);

/* Build RUBYLIB environment variable value
 * 
 * ruby_bin:        Path to Ruby executable
 * env_dir:         Gem environment directory
 * out_buf:         Output buffer
 * out_bufsz:       Size of output buffer
 * 
 * Returns 0 on success, -1 if buffer too small
 */
int wow_build_rubylib(const char *ruby_bin, const char *env_dir,
                      char *out_buf, size_t out_bufsz);

#endif
```

The implementation of `wow_exec_gem_binary()` will be extracted from `do_exec()` in wowx_main.c (lines 131-384). This involves:
- Deriving Ruby prefix from ruby_bin path
- Building RUBYLIB with specific ordering
- Creating shims via `wow_ensure_bundler_shim()` and `wow_ensure_gem_preload()`
- Setting LD_LIBRARY_PATH
- Executing the final binary

**Migration Steps:**
1. Create `src/exec/env.c` with extracted logic
2. Add to Makefile
3. Replace `do_exec()` body with call to `wow_exec_gem_binary()`
4. Test wowx thoroughly
5. Implement `cmd_run()` in `src/main.c` using same function

---

## Phase 4: File Splitting (Lower Priority)

### 4.1 Split `src/wowx_main.c`

**Current:** Single 1,420-line file

**Target:**
```
src/wowx/
├── main.c          # main() and argument parsing
├── cache.c         # wow_wowx_cache_dir()
├── exec.c          # Uses wow_exec_gem_binary() from Phase 3
├── native.c        # has_native_lib(), build_native_extension()
├── autoinstall.c   # auto_install() orchestration
└── internal.h      # Shared wowx-private declarations
```

**Migration Strategy:**
1. Create `src/wowx/` directory
2. Move `wow_wowx_cache_dir()` to `src/wowx/cache.c` (simplest)
3. After Phase 3, `src/wowx/exec.c` becomes thin wrapper
4. Move native extension logic to `src/wowx/native.c`
5. Move auto-install to `src/wowx/autoinstall.c`
6. Update Makefile to compile `src/wowx/*.c` instead of `src/wowx_main.c`

---

### 4.2 Split `src/gemfile/eval.c`

**Current:** 1,366 lines

**Target:**
```
src/gemfile/eval/
├── eval.c          # Public API: wow_eval_init, wow_eval_next, wow_eval_free
├── block.c         # Block stack: push_block, is_suppressed
├── vars.c          # Variable store: store_var, lookup_var
├── output.c        # Output queue: emit, emit_newline
├── expr.c          # Expression evaluator: eval_expr, is_truthy
└── builtin.c       # Builtin methods: ENV[], RUBY_VERSION
```

**Migration Strategy:**
This is the riskiest split due to tight coupling. Recommend:
1. Start with `builtin.c` - most self-contained
2. Then `block.c` - clear boundary
3. Then `vars.c` and `output.c`
4. Leave `expr.c` in main eval.c unless it grows
5. Test Gemfile parsing thoroughly after each move

---

### 4.3 Split Test Harnesses from `src/resolver/cmd.c`

**Current:** 762 lines with CLI commands + test harnesses

**Target:**
```
src/resolver/
├── cmd.c           # wow resolve, wow lock only
└── test/
    ├── version_test.c   # debug version-test
    └── pubgrub_test.c   # debug pubgrub-test
```

**Migration Strategy:**
1. Create `src/resolver/test/` directory
2. Move `cmd_debug_version_test()` to `src/resolver/test/version_test.c`
3. Move `cmd_debug_pubgrub_test()` to `src/resolver/test/pubgrub_test.c`
4. Add new files to Makefile
5. Update command dispatch to call new functions

---

## Build System Updates

### Makefile Changes Required

```makefile
# Add new util sources
UTIL_SRCS := src/util.c src/util/fmt.c
UTIL_OBJS := $(UTIL_SRCS:src/%.c=build/%.o)

# Add exec sources
EXEC_SRCS := src/exec/shims.c src/exec/discover.c src/exec/env.c
EXEC_OBJS := $(EXEC_SRCS:src/%.c=build/%.o)

# Update SHARED_OBJS
SHARED_OBJS := $(UTIL_OBJS) $(EXEC_OBJS) ...

# Pattern rule for util subdirectory
build/util/%.o: src/util/%.c | build/util
	$(COSMO_CC) $(CFLAGS) -c $< -o $@

build/util:
	mkdir -p $@

# Pattern rule for exec subdirectory  
build/exec/%.o: src/exec/%.c | build/exec
	$(COSMO_CC) $(CFLAGS) -c $< -o $@

build/exec:
	mkdir -p $@
```

---

## Testing Strategy

### After Each Phase

1. **Unit tests:** `make test` must pass
2. **Integration tests:** `make test-ruby-mgr` (if applicable)
3. **Manual wowx test:**
   ```bash
   make
   ./build/wowx.com --help
   ./build/wowx.com exec rails --version  # If rails installed
   ```

### Regression Checklist

- [ ] `wow init` works
- [ ] `wow install` works
- [ ] `wow resolve` works
- [ ] `wowx exec <gem>` works
- [ ] `wow run <binary>` works (after Phase 3)
- [ ] Gemfile parsing handles conditionals
- [ ] HTTPS downloads work

---

## Timeline Estimate

| Phase | Duration | Risk | Status |
|-------|----------|------|--------|
| 1.1 fmt.c | 30 min | Low | ✅ Done |
| 1.2 buf.h | 20 min | Low | ✅ Done |
| 1.3 HTTP DRY | 1 hour | Medium (TLS is finicky) | ⏳ Pending |
| 2.0 defaults.h | 2 hours | Low (mechanical) | ⏳ Pending |
| 3.1-3.3 shims/discover | 2 hours | Medium | ⏳ Pending |
| 3.4 env.c (exec) | 4 hours | High (complex logic) | ⏳ Pending |
| 4.1 Split wowx_main.c | 3 hours | Medium | ⏳ Pending |
| 4.2 Split eval.c | 6 hours | High (coupled) | ⏳ Pending |
| 4.3 Split resolver/cmd.c | 1 hour | Low | ✅ Done |

**Total: ~20 hours** (can be spread across multiple sessions)

---

## Recommended Execution Order

**Session 1 (Quick wins):**
1. Phase 1.1 (fmt.c)
2. Phase 1.2 (buf.h)
3. Phase 4.3 (split test harnesses)

**Session 2 (HTTP/defaults):**
1. Phase 1.3 (HTTP DRY)
2. Phase 2.0 (defaults.h) - registry URLs only

**Session 3 (wow/wowx sharing):**
1. Phase 3.1-3.3 (shims, discover)
2. Phase 3.4 (env.c - the big one)

**Session 4+ (File splitting):**
1. Phase 4.1 (wowx_main.c split)
2. Phase 4.2 (eval.c split - if needed)

---

*Document version: 1.0*  
*Last updated: 2026-02-24*
