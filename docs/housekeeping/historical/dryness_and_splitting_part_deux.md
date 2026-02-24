# Dryness and Splitting — Part Deux

A housekeeping plan to address:
1. Files that are too large or contain mixed concerns
2. Code duplication that can be DRY'd up
3. Hardcoded strings that should be configurable

> Status: **Planning** — Not yet implemented  
> Priority: Medium (nice-to-have refactors)

---

## 1. Files Requiring Splitting

### 1.1 src/wowx_main.c (1,420 lines)
**Concern:** This is an entire program masquerading as a single file. It handles:
- Cache directory management
- Platform detection
- Ruby execution environment setup (RUBYLIB construction)
- Bundler shim generation
- Gem binary discovery
- Native extension compilation
- Auto-install orchestration

**Proposed split:**
```
src/wowx/
├── main.c          # main() and argument parsing only
├── cache.c         # wow_wowx_cache_dir(), cache discovery
├── platform.c      # detect_gem_platforms()
├── exec.c          # do_exec(), RUBYLIB construction
├── shims.c         # bundler/setup.rb shim, preload.rb generation
├── discover.c      # find_cached_binary(), try_binary_in_gem()
├── native.c        # has_native_lib(), build_native_extension()
└── autoinstall.c   # auto_install() orchestration
```

**Rationale:** Each of these is a distinct domain. The exec environment setup alone is ~200 lines of complex path manipulation that deserves its own file.

---

### 1.2 src/gemfile/eval.c (1,366 lines)
**Concern:** The Gemfile evaluator handles multiple distinct phases:
- Token helpers (tok_text, tok_strip)
- Memory management (eval_alloc)
- Block stack management (if/unless/elsif/else/end)
- Variable store (Ruby variable assignments)
- Output queue management
- Expression evaluation (recursive descent parser)
- Method call handling (ENV[], RUBY_VERSION, etc.)
- eval_gemfile processing

**Proposed split:**
```
src/gemfile/eval/
├── eval.c          # Public API: wow_eval_init, wow_eval_next, wow_eval_free
├── helpers.c       # tok_text, tok_strip, eval_alloc, eval_error
├── block.c         # Block stack: push_block, is_suppressed, enclosing_active
├── vars.c          # Variable store: store_var, lookup_var
├── output.c        # Output queue: emit, emit_newline, flush_line
├── expr.c          # Expression evaluator: eval_expr, peek, is_truthy, value helpers
└── builtin.c       # Builtin methods: ENV[], RUBY_VERSION, RUBY_PLATFORM, etc.
```

**Rationale:** The expression evaluator (~300 lines) and builtin method handlers (~200 lines) are self-contained enough to split. This would make the main eval.c focus on orchestration.

---

### 1.3 src/resolver/cmd.c (762 lines)
**Concern:** Contains both CLI commands AND test harnesses:
- `wow resolve` command
- `wow lock` command  
- `wow debug version-test` (hardcoded unit tests)
- `wow debug pubgrub-test` (hardcoded integration tests)

**Proposed split:**
```
src/resolver/
├── cmd.c           # wow resolve, wow lock only
└── test/
    ├── version_test.c   # debug version-test
    └── pubgrub_test.c   # debug pubgrub-test
```

**Rationale:** Test code should live separately from production code. The test commands are substantial (~400 lines combined) and pollute the CLI command file.

---

### 1.4 src/http/client.c (998 lines)
**Concern:** Contains two nearly identical TLS setup blocks and multiple HTTP request builders.

**Observation:** Lines ~92-132 and ~510-550 are nearly identical TLS initialization sequences. The request building at lines ~135-141, ~553-559, and pool.c:239-245 are also duplicated.

**Proposed DRY (not split):**
- Extract `static int tls_setup(...)` helper
- Extract `static void build_http_request(...)` helper

---

### 1.5 src/resolver/pubgrub.c (1,196 lines)
**Concern:** While well-organised with section comments, this file contains:
- Version range operations (range_intersect, range_contains, etc.)
- Term operations (term_relation)
- Unit propagation
- Conflict resolution
- Decision making

**Consideration:** This is a complex algorithm implementation. Splitting might hurt readability. The current structure with clear section comments may be preferable. **Recommendation: Leave as-is** unless it grows beyond 1,500 lines.

---

### 1.6 src/gemfile/parser.c & src/gemfile/lexer.c (Generated files)
**Note:** These are auto-generated (lemon and re2c respectively). They should NOT be manually split. The source files are:
- `src/gemfile/parser.y` → parser.c
- `src/gemfile/lexer.re` → lexer.c

If splitting is needed, it should happen in the source grammar files, not the generated output.

---

## 2. Code Duplication (DRY Opportunities)

### 2.1 Byte Formatting Functions
**Location:** Three implementations exist:
- `src/download/multibar.c:44-56` — `format_bytes()`
- `src/download/progress.c:32-44` — `format_bytes()` (identical)
- `src/gems/list.c:15-24` — `fmt_size()` (slightly different, uses spaces)

**Solution:** Move to `src/util/fmt.c`:
```c
// include/wow/util/fmt.h
void wow_fmt_bytes(size_t bytes, char *buf, size_t bufsz);
void wow_fmt_bytes_spaced(size_t bytes, char *buf, size_t bufsz);  // gems/list variant
```

---

### 2.2 Buffer Append Macros
**Location:**
- `src/download/multibar.c:64-68` — `BUF_APPEND` macro
- `src/download/progress.c:58-62` — `BUF_APPEND` macro (identical)
- `src/download/multibar.c:58-62` — `buf_fill()` function
- `src/download/progress.c:49-53` — `buf_fill()` function (identical)

**Solution:** Move to `include/wow/util/buf.h` as inline functions/macros:
```c
static inline void wow_buf_fill(char *buf, size_t bufsz, int *pos, char c, int n);
#define WOW_BUF_APPEND(buf, bufsz, pos, ...) ...
```

---

### 2.3 HTTP Request Building
**Location:** Three nearly identical request builders:
- `src/http/client.c:135-141` — Connection: close
- `src/http/client.c:553-559` — Connection: close (streaming variant)
- `src/http/pool.c:239-245` — Connection: keep-alive

**Current code:**
```c
appendf(&request,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "User-Agent: wow/" WOW_VERSION "\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, port);
```

**Solution:** Extract helper in `src/http/client.c`:
```c
static char *build_request(const char *path, const char *host, 
                           const char *port, const char *connection);
```

---

### 2.4 TLS Setup Duplication
**Location:** `src/http/client.c` lines ~92-132 and ~510-550

**Observation:** Both `do_get_to_file()` and `do_get_to_fd()` contain ~40 lines of identical TLS initialization.

**Solution:** Extract helper:
```c
static int tls_setup(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf,
                     mbedtls_ctr_drbg_context *drbg, int sock, 
                     const char *host);
```

---

### 2.5 Time Formatting
**Location:**
- `src/download/multibar.c:38-42` — `monotonic_secs()`
- `src/download/progress.c:22-28` — `progress_now()` (identical)

**Solution:** Both already use `wow_now_secs()` from `src/util/time.c`. These local wrappers should be removed and `wow_now_secs()` used directly.

---

### 2.6 Error Message Prefixes
**Location:** Throughout codebase

**Observation:** Error messages use inconsistent prefixes:
- `"wow: "` — most common
- `"wowx: "` — in wowx_main.c
- `"error: "` — in init.c

**Solution:** Define in `include/wow/common.h`:
```c
#define WOW_ERR_PREFIX "wow: "
#define WOWX_ERR_PREFIX "wowx: "
```

Or consider a logging utility:
```c
void wow_error(const char *fmt, ...);   // adds "wow: " prefix
void wowx_error(const char *fmt, ...);  // adds "wowx: " prefix
```

---

## 3. Hardcoded Strings That Should Be Configurable

### 3.1 Registry/Source URLs
**Current hardcoding:**
| Location | String |
|----------|--------|
| `src/gemfile/parser.c:1426` | `"https://rubygems.org"` |
| `src/gems/download.c:130,135` | `"https://rubygems.org/downloads/..."` |
| `src/resolver/cmd.c:601,677` | `"https://rubygems.org"` |

**Solution:** Define in `include/wow/defaults.h`:
```c
#define WOW_DEFAULT_REGISTRY "https://rubygems.org"
#define WOW_GEM_DOWNLOAD_URL_FMT "https://rubygems.org/downloads/%s-%s.gem"
```

**Future enhancement:** Support `$WOW_REGISTRY` environment variable override.

---

### 3.2 Ruby Builder URL
**Current:** `src/rubies/install_many.c:72`
```c
"https://github.com/ruby/ruby-builder/releases/download/"
```

**Solution:** Define in `include/wow/defaults.h`:
```c
#define WOW_RUBY_BUILDER_URL "https://github.com/ruby/ruby-builder/releases/download"
```

---

### 3.3 File Paths
**Current hardcoding:**
| Location | String | Context |
|----------|--------|---------|
| `src/rubies/resolve.c:214` | `"/.ruby-version"` | Root fallback |
| `src/sync.c:67` | `"Gemfile"` | Default Gemfile |
| `src/sync.c:151` | `"Gemfile.lock"` | Lockfile output |
| `src/init.c:67` | `"Gemfile"` | Init check |
| `src/gems/download.c:58,66` | `"wow/gems"`, `".cache/wow/gems"` | Cache paths |
| `src/wowx_main.c:46,54` | `"wowx/%s"`, `".cache/wowx/%s"` | wowx cache |
| `src/gems/unpack.c:37` | `"/tmp"` | TMPDIR fallback |

**Solution:** Centralise in `include/wow/defaults.h`:
```c
/* Default file names */
#define WOW_DEFAULT_GEMFILE "Gemfile"
#define WOW_DEFAULT_LOCKFILE "Gemfile.lock"
#define WOW_DEFAULT_RUBY_VERSION_FILE ".ruby-version"

/* Cache directory components */
#define WOW_CACHE_DIR_NAME "wow"
#define WOWX_CACHE_DIR_NAME "wowx"
#define WOW_GEM_CACHE_SUBDIR "gems"

/* Fallback temp directory */
#define WOW_FALLBACK_TMPDIR "/tmp"
```

**Note:** The XDG paths (`$XDG_CACHE_HOME`, `$HOME`) are already using environment variables correctly. These defaults are for the subdirectory names only.

---

### 3.4 Buffer Sizes and Limits
**Current hardcoding:**
| Location | Value | Purpose |
|----------|-------|---------|
| `src/tar.c:25` | `100ULL * 1024 * 1024` (100 MiB) | Max file size |
| `src/tar.c:28-29` | `65536` | ZLIB/TAR buffer sizes |
| `src/gems/meta.c:22` | `1024 * 1024` (1 MiB) | Max metadata size |
| `src/gems/meta.c:25` | `4 * 1024 * 1024` (4 MiB) | Max YAML size |
| `src/resolver/arena.c:12` | `64 * 1024` (64 KiB) | Arena initial capacity |
| `src/http/client.c:180,597` | `4096` | HTTP read buffer growth |

**Solution:** These are reasonable internal limits, but could be named:
```c
/* in include/wow/limits.h or defaults.h */
#define WOW_MAX_TAR_FILE_SIZE (100 * 1024 * 1024)
#define WOW_ZLIB_BUFFER_SIZE 65536
#define WOW_MAX_GEM_METADATA (1024 * 1024)
#define WOW_MAX_GEMSPEC_YAML (4 * 1024 * 1024)
#define WOW_ARENA_INITIAL_CAP (64 * 1024)
#define WOW_HTTP_BUFFER_CHUNK 4096
```

---

### 3.5 Version String
**Current:** `include/wow/version.h`
```c
#define WOW_VERSION "0.8.0"
```

**Status:** This is already in a header. Consider if it should be generated at build time from git tags, but this is low priority.

---

### 3.6 User-Agent String
**Current:** Built in 3 places with `"User-Agent: wow/" WOW_VERSION "\r\n"`

**Solution:** Define in `include/wow/defaults.h`:
```c
#define WOW_HTTP_USER_AGENT "wow/" WOW_VERSION
```

---

### 3.7 Ruby Platform Strings
**Current:** `src/gemfile/eval.c:1265-1283`
```c
#if defined(__x86_64__) || defined(_M_X64)
# if defined(__linux__) || defined(__COSMOPOLITAN__)
    ctx->ruby_platform = "x86_64-linux";
# elif defined(__APPLE__)
    ctx->ruby_platform = "x86_64-darwin";
...
```

**Status:** These are compile-time platform detection strings. They're inherently hardcoded based on the target platform. **Recommendation: Leave as-is.**

---

### 3.8 Default Ruby Version
**Current:** `src/gemfile/eval.c:1256`
```c
ctx->ruby_version = env ? env : "3.3.0";
```

**Solution:** Define in `include/wow/defaults.h`:
```c
#define WOW_DEFAULT_RUBY_VERSION "3.3.0"
```

---

## 4. Implementation Priority

### High Impact, Low Effort (Do First)
1. **Byte formatting DRY** — `wow_fmt_bytes()` utility
2. **Buffer append macros** — Move to shared header
3. **HTTP request builder** — Eliminate triplication
4. **TLS setup DRY** — Extract common ~40 lines

### Medium Impact, Medium Effort
5. **Hardcoded URLs** — `include/wow/defaults.h` with registry URL
6. **Hardcoded paths** — Default filename constants
7. **Error message prefixes** — Consistent "wow: " / "wowx: "

### Lower Priority (Nice to Have)
8. **File splitting** — wowx_main.c, gemfile/eval.c, resolver/cmd.c
9. **Buffer size constants** — Named limits in headers
10. **Build-time version** — Generate from git

---

## 5. Files to Create

```
include/wow/
├── defaults.h          # URLs, paths, default versions
└── util/
    ├── fmt.h           # wow_fmt_bytes()
    └── buf.h           # wow_buf_fill(), WOW_BUF_APPEND

src/util/
├── fmt.c               # Byte formatting implementations
└── buf.c               # (if needed, or keep as header-only)
```

---

## 6. Migration Strategy

1. **Create new utilities first** — Add `wow_fmt_bytes()`, `wow_buf_fill()`
2. **Migrate one call site at a time** — Keep tests passing
3. **Remove old code** — Once all sites migrated
4. **Add defaults.h** — Migrate hardcoded strings gradually
5. **File splitting** — Last, as it affects #include paths

---

*Document version: 1.0*  
*Last updated: 2026-02-24*
