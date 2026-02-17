# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is wow

wow is "uv for Ruby, in C" — a single Actually Portable Executable (APE) that replaces gem, bundler, rbenv, ruby-build, and gem push. Built with Cosmopolitan libc, the `wow.com` binary runs on Linux, macOS, and Windows (x86-64 + aarch64) from one file with zero external dependencies.

Design mantra: "We are slaves to uv. Whatever uv does, wow does." But we're still Rubyists — standard Gemfile, Gemfile.lock, vendor/, .ruby-version conventions are preserved.

## Build Commands

```bash
./configure                     # detect cosmocc toolchain + cosmo source tree
make                            # build build/wow.com (APE binary with embedded SSL roots)
make test                       # run all tests (tls_test + registry_test)
make test-tls                   # TLS certificate validation tests only
make test-registry              # rubygems.org JSON parsing tests only
make clean                      # remove build/
make distclean                  # remove build/ + config.mk

make -C demos                   # build Phase 2 demos (requires main make first)
make -C demos phase0            # build Phase 0 standalone demos
```

The build requires the Cosmopolitan source tree and cosmocc toolchain. `./configure` writes `config.mk` with `COSMO` (toolchain dir) and `COSMO_SRC` (source tree). If `config.mk` is absent, hardcoded fallback paths are used.

**User preference:** The user runs build commands themselves and pastes output back. Don't automatically run `make` unless explicitly asked.

## Architecture

### Toolchain: cosmocc + cosmoar + zipcopy

cosmocc produces fat binaries (x86-64 + aarch64 in one file). Every `.o` file has an `.aarch64/` companion automatically. `cosmoar` archives them. `zipcopy` embeds zip content into the APE binary's `/zip/` filesystem.

### Two sets of CFLAGS

- **CFLAGS** (`-std=c11 -Werror`) — for wow's own `src/*.c` files
- **COSMO_CFLAGS** (`-std=gnu11 -include stdbool.h -Wa,-W -Wa,--noexecstack`) — for compiling cosmo's source files out-of-tree. Key differences from cosmo's internal `-std=gnu23`: stdbool.h bridges `bool`/`true`/`false`, `-Wa,-W` suppresses assembler warnings from ecp256/ecp384 inline asm (matches cosmo's `build/definitions.mk:143-146`)

### mbedTLS: out-of-tree compilation

All ~97 mbedTLS + ~22 net/https source files from `$(COSMO_SRC)` are compiled with cosmocc into `build/libtls.a`. This avoids:
- Pre-built `.a` files from cosmo's `o/` tree (cause symbol conflicts with libcosmo.a)
- Invoking cosmo's own `make` (triggers 386-second full dependency scan)

`getentropy.c` is excluded from the HTTPS build because it calls `arc4random_buf` which isn't in cosmocc's libcosmo.a. Our `src/entropy.c` provides `GetEntropy()` using `getentropy(2)` instead.

### SSL root CA certificates

Root CAs from `$(COSMO_SRC)/usr/share/ssl/root/*.pem` are zipped and embedded into the APE via `zipcopy` post-link. `GetSslRoots()` reads them from `/zip/usr/share/ssl/root/` at runtime. Without this step, HTTPS fails with "certificate not correctly signed by trusted CA".

### Source layout

| File | Role |
|------|------|
| `src/main.c` | CLI dispatch table — subcommands map to `cmd_fn` function pointers |
| `src/http.c` | HTTPS GET client (mbedTLS handshake, cosmo HTTP parser, redirect following, chunked/content-length/read-to-close) |
| `src/pool.c` | Connection pool with Keep-Alive reuse, `MSG_PEEK` liveness probing, LRU eviction |
| `src/registry.c` | rubygems.org `/api/v1/gems/{name}.json` client (cJSON) |
| `src/version.c` | Fetches latest CRuby version from GitHub releases API |
| `src/init.c` | `wow init` — creates Gemfile + .ruby-version |
| `src/entropy.c` | mbedTLS entropy shim (cosmo out-of-tree build workaround) |

### Phase roadmap

Phases 0-2 are complete. See `docs/plan/MASTER_PLAN.md` for the full plan and `docs/plan/PLAN_PHASE*.md` for per-phase details.

| Phase | What | Status |
|-------|------|--------|
| 0 | Research demos (tar, lockparse, compact index, platform) | Done |
| 1 | Skeleton build + dispatch + `wow init` | Done |
| 2 | Native HTTPS + registry client + connection pool | Done |
| 3 | Ruby version manager (download, shims) | Next |
| 4 | .gem unpack (tar + gzip) | |
| 5 | Gemfile parser (re2c + lemon) | |
| 6 | PubGrub dependency resolver | |
| 7 | Parallel download (pthreads) | |
| 8 | End-to-end `wow sync` | |

## Conventions

- **en_IE** for comments, docs, and identifiers (colour, behaviour, initialise, etc.)
- Headers in `include/wow/`, vendored code in `vendor/`
- Public API: `wow_` prefix (e.g. `wow_http_get`, `wow_gem_info_fetch`)
- All build artefacts go in `build/` — never modify the cosmo source tree
- Demos in `demos/` with per-phase subdirectories (`phase0/`, etc.)
- Test binaries also get SSL roots via zipcopy
- cJSON is vendored (not a submodule) in `vendor/cjson/`

## Agent Relay

This project uses `agent-relay-ftw` for Claude-Kimi collaboration. The relay skill is installed at `.claude/skills/agent-relay-ftw/`. Protocol reference at `.claude/skills/PROTOCOLv2.md`. Use the `agent-relay-ftw` skill (not manual file ops) to send messages. Always use relative paths for relay files (`tmp/relay/...`).
