# Phase 4: .gem Download + Unpack — COMPLETE

> Download a gem from rubygems.org, inspect its contents, parse gemspec metadata, and unpack to a directory.

## Status: Done

All four sub-phases implemented and tested.

## 4a: Download a .gem File

**Command:** `wow gem-download sinatra 4.1.1`

**Files:**
- `src/gems/download.c` — download + SHA-256 verify + atomic rename
- `include/wow/gems/download.h`

**Implementation:**
- Fetch registry metadata via `wow_gem_info_fetch()` (URL + SHA-256)
- Check global cache (`$XDG_CACHE_HOME/wow/gems/` or `~/.cache/wow/gems/`)
- Download with progress bar to temp file
- Verify SHA-256 (only when requesting the latest version — the registry API only returns the latest version's hash)
- Atomic rename to final cache path
- `goto cleanup` on all error paths (unlinks partial downloads)

## 4b: List .gem Contents

**Command:** `wow gem-list ~/.cache/wow/gems/sinatra-4.1.1.gem`

**Files:**
- `src/gems/list.c` — tar entry listing with formatted sizes
- `include/wow/gems/list.h`

**Implementation:**
- Uses `wow_tar_list()` with callback to iterate plain tar entries
- Formats sizes as B/KiB/MiB
- Validates: a .gem contains `metadata.gz`, `data.tar.gz`, `checksums.yaml.gz`

## 4c: Parse Gemspec Metadata

**Command:** `wow gem-meta ~/.cache/wow/gems/sinatra-4.1.1.gem`

**Files:**
- `src/gems/meta.c` — extract metadata.gz, gunzip, parse YAML
- `include/wow/gems/meta.h` — `struct wow_gemspec`, `struct wow_gem_dep_info`

**Implementation:**
- `wow_tar_read_entry()` extracts `metadata.gz` from outer tar (1 MiB limit)
- `gunzip_mem()` decompresses with zlib (`inflateInit2` with `16+MAX_WBITS`)
- libyaml document API parses the YAML gemspec
- Handles Ruby-specific YAML tags (`!ruby/object:Gem::Specification`, `Gem::Version`, `Gem::Requirement`) — libyaml processes them transparently
- Extracts: name, version, summary, authors, required_ruby_version, runtime dependencies (skips development deps)
- `Gem::Requirement` parsing: handles `[operator, version]` pairs, skips `>= 0` (means "any")

## 4d: Unpack Gem to Directory

**Command:** `wow gem-unpack ~/.cache/wow/gems/sinatra-4.1.1.gem /tmp/sinatra/`

**Files:**
- `src/gems/unpack.c` — stream data.tar.gz to temp, extract gzip tar
- `include/wow/gems/unpack.h`

**Implementation:**
- Streams `data.tar.gz` from outer tar to temp file via `wow_tar_extract_entry_to_fd()` (no large malloc)
- Temp file in `$TMPDIR` (falls back to `/tmp`) — not in dest dir
- Extracts gzip tar via existing `wow_tar_extract_gz()`
- `goto cleanup` unlinks temp file on all paths (success or error)

## Infrastructure: tar.c Refactoring

**Files:**
- `src/tar.c` — added plain (uncompressed) tar support
- `include/wow/tar.h` — new public APIs

**Key changes:**
- `struct tar_reader` gains `compressed` flag
- `tar_reader_init_plain()` (no zlib) alongside existing `tar_reader_init_gz()`
- `tar_reader_read/skip/close` branch on `compressed`
- Extracted `tar_extract_loop()` shared function
- New APIs: `wow_tar_extract()`, `wow_tar_list()`, `wow_tar_read_entry()`, `wow_tar_extract_entry_to_fd()`

## Infrastructure: libyaml Integration

**Files:**
- `Makefile` — compiles 8 libyaml source files from cosmo source tree into `build/libyaml.a`

**Build flags:**
```
-I$(COSMO_SRC)/third_party/libyaml -DYAML_DECLARE_STATIC
-include $(COSMO_SRC)/third_party/libyaml/config.h -Wno-unused-value
```

## Source Layout

```
src/gems/
    cmd.c           — CLI dispatch (gem-download, gem-list, gem-meta, gem-unpack)
    download.c      — Download .gem to cache + SHA-256 verify
    list.c          — List .gem tar entries
    meta.c          — Parse metadata.gz gemspec YAML (libyaml)
    unpack.c        — Extract data.tar.gz to dest dir

include/wow/gems/
    download.h, list.h, meta.h, unpack.h

include/wow/gems.h  — umbrella header
```

## Tests

`tests/gem_test.c` — all offline with embedded fixtures:
- Plain tar extraction (multi-file)
- `tar_list` callback iteration
- `tar_read_entry` with max_size guard
- `tar_extract_entry_to_fd` streaming
- SHA-256 against known digest
- Gzip compress/decompress round-trip
- Gemspec YAML parsing (fake .gem with embedded YAML)
- Cache directory path
