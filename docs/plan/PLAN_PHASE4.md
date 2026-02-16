# Phase 4: .gem Download + Unpack

> Download a gem from rubygems.org and unpack it into vendor/bundle/.

## 4a: Download a .gem File

**Demo:** `./wow.com gem-download sinatra 4.1.1` saves sinatra-4.1.1.gem to disk.

**Files:**
- `src/gem.c`
- `include/wow/gem.h`

**Implementation:**
- URL: `https://rubygems.org/downloads/{name}-{version}.gem`
- Download to `~/.cache/wow/gems/{name}-{version}.gem` (global cache)
- Verify SHA-256 against registry metadata
- Progress bar for large gems

**Verify:**
```bash
./build/wow.com gem-download sinatra 4.1.1
ls ~/.cache/wow/gems/
# sinatra-4.1.1.gem
file ~/.cache/wow/gems/sinatra-4.1.1.gem
# POSIX tar archive
```

## 4b: Read Tar Headers

**Demo:** `./wow.com gem-list sinatra-4.1.1.gem` lists the .gem contents.

**Files:**
- `src/tar.c`
- `include/wow/tar.h`

**Implementation:**
- Read 512-byte tar headers (ustar format)
- Parse: filename, size, type flag
- Iterate through entries, print each filename + size
- Handle end-of-archive (two consecutive zero blocks)

**Verify:**
```bash
./build/wow.com gem-list ~/.cache/wow/gems/sinatra-4.1.1.gem
# metadata.gz     (4.2 KB)
# data.tar.gz     (52.1 KB)
# checksums.yaml.gz (0.3 KB)
```

## 4c: Extract + Parse Gemspec YAML

**Demo:** `./wow.com gem-meta sinatra-4.1.1.gem` prints parsed gemspec metadata.

**Implementation:**
- Extract `metadata.gz` from outer tar (tar.c)
- Decompress with zlib (cosmo provides)
- Parse YAML with libyaml (cosmo provides)
- Extract: name, version, summary, authors, dependencies (name + version constraint), required_ruby_version

**Verify:**
```bash
./build/wow.com gem-meta ~/.cache/wow/gems/sinatra-4.1.1.gem
# name: sinatra
# version: 4.1.1
# summary: Classy web-development dressed in a DSL
# dependencies:
#   mustermann (~> 3.0)
#   rack (>= 3.0.0, < 4)
#   rack-session (>= 2.0.0, < 3)
#   tilt (~> 2.0)
```

## 4d: Extract Data to vendor/bundle/

**Demo:** `./wow.com gem-unpack sinatra-4.1.1.gem vendor/bundle/` unpacks gem files.

**Implementation:**
- Extract `data.tar.gz` from outer tar
- Decompress with zlib
- Untar inner archive to `vendor/bundle/ruby/{version}/gems/{name}-{version}/`
- Directory layout matches Bundler convention:
  ```
  vendor/bundle/ruby/4.0.0/gems/sinatra-4.1.1/
  ├── lib/
  ├── README.md
  └── ...
  ```

**Verify:**
```bash
./build/wow.com gem-unpack ~/.cache/wow/gems/sinatra-4.1.1.gem vendor/bundle/
ls vendor/bundle/ruby/4.0.0/gems/sinatra-4.1.1/lib/
# sinatra.rb  sinatra/
```
