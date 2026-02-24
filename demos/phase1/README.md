# Phase 1: Skeleton Build + Dispatch + `wow init`

This directory intentionally contains no demo programs. Phase 1 was focused entirely on infrastructure and plumbing — the framework that everything else plugs into, rather than self-contained features that can be exercised in isolation.

## What Phase 1 Covered

### 1. Build System (`Makefile`, `configure`)

- Out-of-tree compilation using `cosmocc` for ~120 mbedTLS, networking, and HTTPS source files
- Static library creation at `build/libtls.a`
- SSL root CA certificate embedding via `zipcopy`
- Toolchain detection and configuration via `./configure`

### 2. CLI Dispatch Table (`src/main.c`)

- Command dispatch via a `commands[]` array mapping subcommand strings (`"init"`, `"sync"`, `"fetch"`, etc.) to `cmd_fn` function pointers
- Flag handling for `--help`, `--version`, and `--verbose`
- Shim dispatch logic (lines 156–209): when the binary is invoked as `ruby`, `irb`, `gem`, etc. via symlink or hard link, it reads `.ruby-version` and execs the managed Ruby binary

### 3. `wow init` Command (`src/init.c`, `include/wow/init.h`)

- Creates a new project directory with a `Gemfile` and `.ruby-version`
- Queries the GitHub releases API for the latest Ruby version
- Supports `--force` flag to overwrite existing files
- Eagerly installs the Ruby version via `wow_ruby_ensure()`

## Why No Demos?

Unlike Phase 0 and Phase 2, Phase 1 components don't lend themselves to standalone demonstration programs:

| Phase | Nature | Demo Approach |
|-------|--------|---------------|
| **Phase 0** | Research spikes — tar parsing, lockfile parsing, compact index, platform detection | Standalone `.com` programs proving individual concepts |
| **Phase 1** | Infrastructure — build system, CLI skeleton, init command | Framework code that other features depend upon |
| **Phase 2** | Network I/O — HTTPS client, registry API | Standalone programs demonstrating real network calls to rubygems.org |

The build system produces the binary; the dispatch table routes commands; `wow init` bootstraps projects. These are foundational layers — without them, nothing else runs — but they don't produce observable behaviour that can be demonstrated in isolation like parsing a tar file or fetching gem metadata over HTTPS.

## Key Source Files

- `Makefile` — build rules and cosmocc integration
- `configure` — toolchain and environment detection
- `src/main.c` — CLI dispatch table and shim logic
- `src/init.c` — `wow init` implementation
- `include/wow/init.h` — init command header
