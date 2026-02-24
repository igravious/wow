# Code Sharing Between wow and wowx

**Status:** Analysis complete — implementation not started  
**Priority:** Medium (would reduce code duplication and ensure consistency)

---

## Current Architecture

```
build/wow.com    ← src/main.c (entry point) + all shared code
build/wowx.com   ← src/wowx_main.c (entry point) + all shared code
```

Both binaries link against the same `SHARED_OBJS` (library code), but `wowx_main.c` contains ~1,400 lines of logic that is currently **private to wowx**.

---

## What Could Be Shared

### 1. Ruby Execution Environment (`do_exec`)

**Current state:** `do_exec()` in `src/wowx_main.c:131-384` is 250+ lines of complex RUBYLIB construction that's **wowx-only**.

**What it does:**
- Derives Ruby prefix from ruby_bin path
- Builds RUBYLIB with specific ordering: shims → gems → stdlib
- Creates bundler/setup.rb shim (no-op Bundler module)
- Creates wow_preload.rb (Kernel#gem no-op)
- Reads `.require_paths` markers from gems
- Scans stdlib for arch-specific directories (rbconfig.rb)
- Sets LD_LIBRARY_PATH for libruby.so
- Executes the final binary

**Sharing opportunity:** 
```c
// include/wow/exec.h
int wow_exec_gem_binary(const char *ruby_bin, const char *ruby_api,
                        const char *env_dir, const char *exe_path,
                        int argc, char **argv);
```

**Use in wow:** `wow run` (currently stubbed as `cmd_stub`) needs this to run gems from the project bundle. The only difference: wow's `env_dir` would be `vendor/bundle/ruby/<api>` instead of wowx's `~/.cache/wowx/<api>/<gem>-<ver>`.

---

### 2. Bundler Shim Generation

**Current state:** Inline in `do_exec()` (lines 173-210)

**What it does:** Creates `lib/wow_shims/bundler/setup.rb` with a no-op Bundler module that prevents Bundler from trying to load a Gemfile.

**Sharing opportunity:** Extract to utility function:
```c
// include/wow/exec.h
int wow_ensure_bundler_shim(const char *ruby_prefix);
```

**Why share:** Both wow and wowx need to prevent Bundler from activating when running gems. Currently wow doesn't have this capability (its `wow run` is stubbed).

---

### 3. Kernel#gem Preload Stub

**Current state:** Inline in `do_exec()` (lines 329-350)

**What it does:** Creates `lib/wow_preload.rb` that stubs `Kernel#gem` as a no-op, since gems are already on RUBYLIB.

**Sharing opportunity:** Extract to utility function:
```c
// include/wow/exec.h  
int wow_ensure_gem_preload(const char *ruby_prefix);
```

---

### 4. Gem Binary Discovery

**Current state:** `try_binary_in_gem()` and `find_cached_binary()` in wowx_main.c (lines 390-498)

**What it does:**
- Search `exe/` and `bin/` directories in gem installations
- Use `.executables` markers for name-mismatch gems (e.g., `haml_lint` → `haml-lint`)
- Pass 1: search directories matching gem name
- Pass 2: search all gem directories (for meta-gems like rails)

**Sharing opportunity:**
```c
// include/wow/exec.h
int wow_find_gem_binary(const char *env_dir, const char *gem_name,
                        const char *binary_name, char *exe_path, size_t exe_path_sz);
```

**Use in wow:** `wow run <binary>` needs to find the binary in vendor/bundle.

---

### 5. Platform Detection

**Current state:** `detect_gem_platforms()` in wowx_main.c (lines 93-123)

**What it does:** Detects RubyGems platform strings for native gem downloads (x86_64-linux-gnu, x86_64-linux, aarch64-darwin, etc.)

**Sharing opportunity:** Already exposed? Check if `wow/gems.h` has this.

---

### 6. Auto-install Orchestration

**Current state:** `auto_install()` in wowx_main.c (lines 807-965)

**What it does:**
- Resolves dependencies using PubGrub
- Downloads missing .gem files (parallel)
- Unpacks gems to env_dir
- Creates `.installed` marker

**Sharing opportunity:** This is essentially a subset of `wow sync`. Could be refactored:
```c
// include/wow/install.h
int wow_install_gem_to_env(const char *gem_name, 
                           const char *constraint,
                           const char *ruby_version,
                           const char *target_dir);
```

**Use in wow:** Could be used for `wow add <gem>` (add to Gemfile + auto-install).

---

### 7. Cache Directory Management

**Current state:** `wow_wowx_cache_dir()` is already exposed in `include/wow/wowx.h`

**Sharing opportunity:** Could generalize:
```c
// include/wow/paths.h
int wow_cache_dir(const char *subdir, const char *ruby_api, 
                  char *buf, size_t bufsz);
// subdir = "wow/gems" for wow, "wowx" for wowx
```

---

## Proposed New Module Structure

```
src/exec/
├── env.c           # RUBYLIB construction, do_exec equivalent
├── shims.c         # Bundler shim, preload.rb generation
├── discover.c      # Gem binary discovery
└── platform.c      # Platform detection (if not already shared)

include/wow/
├── exec.h          # Public API for execution
└── exec/
    ├── env.h       # Environment setup
    ├── shims.h     # Shim generation
    └── discover.h  # Binary discovery
```

---

## Implementation Strategy

### Phase 1: Extract Environment Setup (High Impact)
1. Create `src/exec/env.c` with `wow_exec_gem_binary()`
2. Migrate `do_exec()` logic from wowx_main.c
3. Update wowx_main.c to use the shared function
4. Implement `cmd_run()` in wow's main.c using the same function

### Phase 2: Extract Shim Generation (Medium Impact)
1. Create `src/exec/shims.c` with `wow_ensure_bundler_shim()` and `wow_ensure_gem_preload()`
2. Both wow and wowx use these

### Phase 3: Extract Binary Discovery (Medium Impact)
1. Create `src/exec/discover.c` with `wow_find_gem_binary()`
2. Update wowx to use it
3. Use in wow's `cmd_run()`

### Phase 4: Generalize Auto-install (Lower Priority)
1. Refactor `auto_install()` to work with both wowx and `wow add`
2. This is more complex due to different target directories

---

## Files to Modify

| File | Change |
|------|--------|
| `src/wowx_main.c` | Replace ~600 lines with calls to shared functions |
| `src/main.c` | Implement `cmd_run()` using shared functions |
| `Makefile` | Add `src/exec/*.c` to SHARED_OBJS |
| New: `src/exec/env.c` | Extracted RUBYLIB + exec logic |
| New: `src/exec/shims.c` | Extracted shim generation |
| New: `src/exec/discover.c` | Extracted binary discovery |

---

## Benefits

1. **Consistency:** `wow run` and `wowx` behave identically
2. **Maintainability:** One copy of complex RUBYLIB logic instead of two
3. **Completeness:** wow gets `run` command implementation "for free"
4. **Testing:** Single test suite covers both tools
5. **Binary size:** Slightly smaller (shared code vs duplicated)

---

## Open Questions

1. **API versioning:** Should `wow_exec_gem_binary()` take an options struct for future extensibility?
2. **Error handling:** Consistent error messages between wow and wowx (currently "wowx: " vs "wow: " prefixes)
3. **LD_LIBRARY_PATH:** Is this needed for wow's `run` command or only wowx's ephemeral installs?
4. **Shim paths:** Should shims be stored per-prefix (current) or centralized?

---

*Document version: 1.0*  
*Last updated: 2026-02-24*
