# wow — Progress Checkpoint

> Last updated: 2026-02-17

## Phase 0: Research — COMPLETE

All four sub-phases done, cross-reviewed (Claude did 0a/0b, Kimi did 0c/0d, both reviewed each other's work).

| Sub-phase | Deliverable | Status | Key findings |
|-----------|-------------|--------|--------------|
| **0a** | `docs/research/gem_format.md` | Done | .gem is ustar tar (not gzip'd outer). 3 entries: metadata.gz, data.tar.gz, checksums.yaml.gz. No pax extensions needed — longest path tested is 79 chars. |
| **0b** | `docs/research/lockfile_format.md` | Done | Full format spec (1070 lines). All sections documented incl. CHECKSUMS (Bundler 4+). wow writes modern format only (separate GEM blocks, 2-space indent). Reads both legacy and modern. |
| **0c** | `docs/research/rubygems_api.md` | Done | `/api/v1/dependencies` is deprecated. Use Compact Index (`/info/{name}`) — what Bundler 2.x uses. Also need `/versions` endpoint for gem discovery. |
| **0d** | `docs/research/ruby_binaries.md` | Done | MVP is binary-only (ruby-builder GitHub releases). Source compilation is post-MVP. ruby-builder tarballs have `x64/` prefix to strip. |

### Working demos (CosmoCC, `-Werror` clean)

| Demo | Proves |
|------|--------|
| `demos/phase0/demo_tar.c` | ustar parsing + gzip decompression via Cosmopolitan's zlib |
| `demos/phase0/demo_lockparse.c` | Gemfile.lock state-machine parser (all sections) |
| `demos/phase0/demo_compact_index.c` | Compact index text format parser |
| `demos/phase0/demo_platform.c` | Platform detection + ruby-builder URL mapping |

### Key decisions made during Phase 0

1. **Lockfile writer**: Bundler 4+ style (2-space indent), modern format (separate GEM blocks per source)
2. **Lockfile reader**: Accept both legacy and modern formats, both 2-space and 3-space indent
3. **Checksum parsing**: Generic prefix parsing (split on `=`), verify `sha256=` only, warn on unknown
4. **Ruby install (MVP)**: Binary-only from ruby-builder. Clear error if no binary for platform. No source compilation.
5. **Compact Index**: Primary API for dependency resolution. Fallback to `/api/v1/versions/{name}.json`.
6. **YAML metadata parsing**: Use Cosmopolitan's libyaml. Ignore `!ruby/object:*` tags — treat as plain maps.

## Phase 1: Skeleton + Build System — COMPLETE

| Sub-phase | What | Status |
|-----------|------|--------|
| **1a** | Hello world with cosmocc | Done |
| **1b** | Subcommand dispatch (8 commands + `--help`/`--version`) | Done |
| **1c** | `wow init` — creates Gemfile + .ruby-version | Done |

### Key decisions made during Phase 1

1. **Ruby version in .ruby-version**: Fetched at runtime from ruby-builder GitHub API, not hardcoded. Falls back to `4.0.1` if fetch fails.
2. **Build flags**: `-Wall -Wextra -Werror -O2 -std=c17` (bumped from c11 during Phase 3).
3. **cosmocc path**: Configurable via `./configure` (writes `config.mk`), fallback to hardcoded defaults.

## Phase 2: HTTPS Client — COMPLETE

| Sub-phase | What | Status |
|-----------|------|--------|
| **2a** | HTTP GET (plain + chunked + content-length) | Done |
| **2b** | HTTPS via mbedTLS (out-of-tree compilation) | Done |
| **2c** | rubygems.org registry client (cJSON) + connection pool | Done |

### Files added

| File | Purpose |
|------|---------|
| `src/http/client.c` | HTTPS GET client (mbedTLS handshake, cosmo HTTP parser, redirects, chunked/content-length/read-to-close, streaming download) |
| `src/http/pool.c` | Connection pool with Keep-Alive reuse, `MSG_PEEK` liveness probing, LRU eviction |
| `src/http/entropy.c` | mbedTLS entropy shim (`GetEntropy()` via `getentropy(2)`, replaces cosmo's `arc4random_buf`-based version) |
| `src/registry.c` | rubygems.org `/api/v1/gems/{name}.json` client (cJSON) |
| `vendor/cjson/` | Vendored cJSON library |

### Key decisions made during Phase 2

1. **Out-of-tree mbedTLS**: Compile cosmo's ~119 TLS source files directly with cosmocc into `build/libtls.a`. Avoids pre-built `.a` symbol conflicts and cosmo's slow `make`.
2. **ALPN fix for GitHub**: Force HTTP/1.1 via ALPN to avoid GitHub's HTTP/2 response with >8KB headers overflowing cosmo's HTTP parser.
3. **SSL roots**: Embedded via zipcopy into APE's `/zip/` filesystem, read at runtime by `GetSslRoots()`.
4. **COSMO_CFLAGS**: Separate flags for cosmo source compilation (`-std=gnu17 -include stdbool.h -Wa,-W -Wa,--noexecstack`).

## Phase 3: Ruby Manager + Parallel Downloads — COMPLETE

Phase 7 (parallel downloads) was pulled forward and merged into Phase 3, since the download infrastructure is needed for Ruby installs.

| Sub-phase | What | Status |
|-----------|------|--------|
| **3a** | Download pre-built Ruby from ruby-builder | Done |
| **3b** | Minor-version symlink (`ruby-4.0` → `ruby-4.0.1`) | Done |
| **3c** | `wow ruby list` — list installed Rubies | Done |
| **3d** | `wow init` eagerly downloads Ruby | Done |
| **3e** | Shims (argv[0] dispatch via symlinks to wow.com) | Done |
| **7→3** | Parallel download infrastructure (worker pool + multibar) | Done |

### Source layout (after directory restructure)

| File | Purpose |
|------|---------|
| `src/rubies/resolve.c` | Platform detection, version resolution, .ruby-version walk-up, directory helpers |
| `src/rubies/install.c` | Download, extract (tar.gz), atomic rename, file locking |
| `src/rubies/install_many.c` | Parallel multi-version install via bounded-concurrency worker pool |
| `src/rubies/uninstall.c` | Remove an installed Ruby version |
| `src/rubies/list.c` | List installed Ruby versions |
| `src/rubies/cmd.c` | `wow ruby` subcommand dispatch |
| `src/rubies/shims.c` | Create shims (symlinks to wow.com) for ruby, irb, gem, etc. |
| `src/rubies/internal.c` | Shared internal helpers (mkdirs, colour, timing) |
| `src/tar.c` | Streaming tar.gz extraction with security hardening |
| `src/download/multibar.c` | uv-style multi-bar progress display (sorted, braille spinner, throughput) |
| `src/download/parallel.c` | Bounded-concurrency worker pool (default 50, matching uv) |
| `src/download/progress.c` | Single-download progress bar |
| `src/main.c` | Updated: argv[0] shim dispatch, `--verbose` flag, `cmd_ruby` |
| `src/init.c` | Updated: eager Ruby install on `wow init` |

### Key decisions made during Phase 3

1. **Binary-only Ruby install**: Download pre-built tarballs from ruby-builder GitHub releases. No source compilation (post-MVP).
2. **Parallel downloads pulled forward**: Phase 7's worker pool was implemented in Phase 3, since parallel install of multiple Ruby versions was a natural fit.
3. **Bounded concurrency**: Worker pool with `min(n, max_concurrent)` threads pulling from a mutex-protected queue. Default 50 concurrent (matching uv's semaphore pattern).
4. **uv-style progress**: Multi-bar display with sorted bars, dynamic column widths (name padded to max, `%7s/%-7s` byte counters), braille spinner, throughput indicator.
5. **Tar security**: Rejects path traversal, absolute paths, hard links, device nodes, FIFOs, and symlinks escaping dest_dir.
6. **File locking**: `flock()` on `~/.local/share/wow/ruby/.lock` prevents concurrent installs corrupting state.
7. **Directory restructure**: Flat `src/` reorganised into `src/http/`, `src/download/`, `src/rubies/` domain subdirectories with matching `include/wow/` headers. Umbrella convenience headers (`wow/http.h`, `wow/download.h`, `wow/rubies.h`).
8. **C17 bump**: CFLAGS from `-std=c11` to `-std=c17`, COSMO_CFLAGS from `-std=gnu11` to `-std=gnu17`.

### Working demos

| Demo | Proves |
|------|--------|
| `demos/phase3/demo_parallel.c` | Parallel download with multibar progress, `--slow-mo` rate limiting, worker mode for 100+ files |

### Tests

| Test | Covers |
|------|--------|
| `tests/ruby_mgr_test.c` | Platform detection, directory helpers, .ruby-version resolution, tar security (path traversal, symlink escape, corruption) |

## Phases 4–8: NOT STARTED

Phase 7 was merged into Phase 3. Remaining phases:

| Phase | What | Status |
|-------|------|--------|
| 4 | .gem unpack (tar + gzip) | Not started |
| 5 | Gemfile parser (re2c + lemon) | Not started |
| 6 | PubGrub dependency resolver | Not started |
| 8 | End-to-end `wow sync` | Not started |

See `docs/plan/PLAN_PHASE{4..8}.md`.

## Git log

```
f8624ae Add Ruby manager and tar extraction tests with fixtures
8033019 Add Phase 3 parallel download demo with multibar progress
bd33caa Phase 3: restructure src/ into domain subdirectories, add Ruby manager and download infrastructure
53bea19 Add streaming tar.gz extraction with security hardening
f14dbef Add clangd configuration for LSP diagnostics
a7ed1f0 Rename CLAUDE.md to .ai-instructions.md, add symlinks and uv reference
496e6dc Finish demo restructure: fix phase2 paths, add runner scripts
2d194ef Add CLAUDE.md project documentation
f309d8a Restructure demos: move phase0 to subdirectory, add phase2 demos
66c1fdd Add TLS and registry tests
008100a Add rubygems.org registry client, fetch/gem-info/bench-pool subcommands
4bf8a2c Add native HTTPS client with connection pool
dcb6e4b Add configure script, out-of-tree mbedTLS build, vendor cJSON
eff0b3d Phase 1: skeleton build system + subcommand dispatch + wow init
4c5c76c Add Phase 0 progress checkpoint
030baf5 Phase 0: research docs + working CosmoCC demos
3c005df Initial commit: .gitignore and project plan docs
```
