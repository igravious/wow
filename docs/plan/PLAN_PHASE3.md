# Phase 3: Ruby Manager + Parallel Downloads — COMPLETE

> Download, install, and activate Ruby — the rbenv + ruby-build replacement.
> Phase 7 (parallel downloads) was pulled forward and merged here.

## Status: COMPLETE

All sub-phases implemented and tested.

## Source files

| File | Role |
|------|------|
| `src/rubies/resolve.c` | Platform detection, version resolution, .ruby-version walk-up, directory helpers |
| `src/rubies/install.c` | Download, tar.gz extraction, atomic rename, file locking |
| `src/rubies/install_many.c` | Parallel multi-version install via bounded-concurrency worker pool |
| `src/rubies/uninstall.c` | Remove an installed Ruby version |
| `src/rubies/list.c` | List installed Ruby versions, mark active |
| `src/rubies/cmd.c` | `wow rubies` subcommand dispatch |
| `src/rubies/shims.c` | Create symlink shims for ruby, irb, gem, etc. |
| `src/rubies/internal.c` | Shared internal helpers (mkdirs, colour, timing) |
| `src/tar.c` | Streaming tar.gz extraction with security hardening |
| `src/download/multibar.c` | uv-style multi-bar progress display |
| `src/download/parallel.c` | Bounded-concurrency worker pool (default 50) |
| `src/download/progress.c` | Single-download progress bar |

Headers: `include/wow/rubies/`, `include/wow/download/`, `include/wow/tar.h`
Umbrella headers: `include/wow/rubies.h`, `include/wow/download.h`

## 3a: Download Pre-Built Ruby — DONE

`./wow.com ruby install 3.3.6` downloads Ruby to `~/.local/share/wow/ruby/`.

- Determines download URL from version + platform (ruby-builder GitHub releases)
- Downloads with progress bar (streaming to fd, reuses `src/http/client.c`)
- Extracts tarball via `src/tar.c` with `strip_components=1` (strips `x64/` prefix)
- Stages in `.temp/` first, atomic rename on completion
- File locking via `flock()` on `.lock` for concurrent access safety

## 3b: Minor-Version Symlink — DONE

`ruby-3.3` symlink points to `ruby-3.3.6-{platform}`.

## 3c: wow rubies list — DONE

`./wow.com ruby list` scans `~/.local/share/wow/ruby/` and prints installed versions, marking the active one from nearest `.ruby-version`.

## 3d: wow init Downloads Ruby Eagerly — DONE

`./wow.com init` now calls `wow_ruby_ensure(version)` after writing Gemfile + .ruby-version.

## 3e: Shims — DONE

Shims are symlinks to wow.com in `~/.local/share/wow/shims/`. argv[0] dispatch in `src/main.c` detects when invoked as `ruby`, `irb`, etc., reads `.ruby-version` (walking up directory tree), and exec's the managed Ruby binary.

## 3f: Parallel Downloads (merged from Phase 7) — DONE

Bounded-concurrency worker pool with `min(n, max_concurrent)` threads pulling from a mutex-protected queue. Default concurrency of 50 matches uv's semaphore pattern.

uv-style multi-bar progress display with:
- Sorted bars (by completion percentage)
- Dynamic column widths (name padded to max, `%7s/%-7s` byte counters)
- Braille spinner on status line
- Throughput indicator
- `--slow-mo` rate limiting for demos

Two modes:
- **Fixed mode** (n_bars == n_total): one row per download, good for 2-8 files
- **Worker mode** (n_bars < n_total): one row per worker thread + status line, good for 100+ files

### Demo

`demos/phase3/demo_parallel.c` — downloads multiple files in parallel with multibar progress. Supports `--slow-mo` flag and worker mode for large batches.

### Tests

`tests/ruby_mgr_test.c` — platform detection, directory helpers, .ruby-version resolution, tar security (path traversal, symlink escape, corruption).
