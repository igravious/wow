# Phase 0a: .gem Binary Format

> **Historical research document (Phase 0a).** This is a non-binding reference from early
> exploration. Paths, APIs, and design decisions described here may be outdated — the
> canonical sources are `CLAUDE.md`, `docs/plan/MASTER_PLAN.md`, and `docs/ERGONOMICS.md`.
>
> Examined sinatra-4.1.1.gem, rack-3.1.12.gem, and nokogiri-1.18.3.gem.

## Overall Structure

A `.gem` file is a **POSIX tar archive** (not gzip-compressed at the outer level) containing exactly three entries:

```
example-1.0.0.gem (tar, ustar format)
├── metadata.gz          # gzip'd YAML gemspec
├── data.tar.gz          # gzip'd tar of the actual gem source files
└── checksums.yaml.gz    # gzip'd YAML with SHA-256 and SHA-512 checksums
```

This structure is consistent across pure Ruby gems (sinatra, rack) and gems with native extensions (nokogiri). The order is always `metadata.gz`, `data.tar.gz`, `checksums.yaml.gz`.

## Tar Format Details

| Field | Value | Notes |
|-------|-------|-------|
| Format | **ustar** (POSIX.1-1988) | Magic: `ustar\0` at offset 257, version `00` |
| `file` output | `POSIX tar archive` | |
| Block size | 512 bytes | Standard tar block |
| Owner/Group | `wheel`/`wheel` | Names in header, UID/GID both 0 |
| Permissions | `0000444` (read-only) | All three entries |
| Typeflag | `0` (regular file) | |
| Device major/minor | `0000000`/`0000000` | |

### ustar Header Layout (512 bytes)

```
Offset  Size  Field
0       100   filename (null-terminated)
100     8     mode (octal ASCII)
108     8     uid (octal ASCII)
116     8     gid (octal ASCII)
124     12    size (octal ASCII)
136     12    mtime (octal ASCII, Unix timestamp)
148     8     checksum (octal ASCII + space + null)
156     1     typeflag ('0' = regular file)
157     100   linkname
257     6     magic ("ustar\0")
263     2     version ("00")
265     32    uname
297     32    gname
329     8     devmajor (octal ASCII)
337     8     devminor (octal ASCII)
345     155   prefix (for paths > 100 chars)
500     12    padding (zeros)
```

**What wow needs:** A minimal ustar tar reader. Only needs to handle typeflag `0` (regular files). No directories, no symlinks, no pax extensions. The three entries are always at fixed names — we can match by filename.

## metadata.gz — Gemspec as YAML

The metadata is a gzip-compressed YAML document using Ruby-specific tags. Full example from sinatra-4.1.1:

```yaml
--- !ruby/object:Gem::Specification
name: sinatra
version: !ruby/object:Gem::Version
  version: 4.1.1
platform: ruby
```

### Fields wow Needs to Parse

| Field | Type | Use |
|-------|------|-----|
| `name` | string | Package identity |
| `version` | `!ruby/object:Gem::Version` → string | Package version |
| `platform` | string | `"ruby"` for pure Ruby; platform string for native |
| `dependencies` | list | **Critical** — each dep has name, version constraints, type |
| `required_ruby_version` | version constraint | Compatibility check |
| `files` | list of strings | Can cross-reference with data.tar.gz |
| `executables` | list of strings | For `wow tool install` |
| `require_paths` | list (usually `["lib"]`) | For `GEM_PATH` / load path setup |
| `bindir` | string (usually `"bin"`) | Where executables live |

### Dependency Structure

Each dependency in the `dependencies` list:

```yaml
- !ruby/object:Gem::Dependency
  name: rack
  requirement: !ruby/object:Gem::Requirement
    requirements:
    - - ">="
      - !ruby/object:Gem::Version
        version: 3.0.0
    - - "<"
      - !ruby/object:Gem::Version
        version: '4'
  type: :runtime          # :runtime or :development
  prerelease: false
  version_requirements: !ruby/object:Gem::Requirement
    requirements:          # same as requirement (duplicated)
    - - ">="
      - !ruby/object:Gem::Version
        version: 3.0.0
    - - "<"
      - !ruby/object:Gem::Version
        version: '4'
```

**Key points:**
- `requirement` and `version_requirements` are always identical — parse either one
- `type` is `:runtime` (needed at runtime) or `:development` (build/test only) — wow only cares about `:runtime`
- Multiple constraint pairs form an AND (e.g. `>= 3.0.0` AND `< 4`)
- Constraint operators: `=`, `!=`, `>`, `<`, `>=`, `<=`, `~>` (pessimistic/twiddle-wakka)

### YAML Parsing Strategy

The metadata YAML uses Ruby-specific tags (`!ruby/object:Gem::Specification`, `!ruby/object:Gem::Version`, etc.) but the actual data is plain YAML maps and sequences. wow does **not** need a full Ruby YAML deserialiser — it needs a YAML parser that:

1. Ignores `!ruby/object:*` tags (treat as plain maps)
2. Handles maps, sequences, strings, integers, booleans, timestamps
3. Cosmopolitan bundles **libyaml** — use it directly

## data.tar.gz — Gem Source Files

A gzip-compressed tar archive containing the gem's actual files. Paths are relative (no leading `/` or `./`):

```
lib/sinatra.rb
lib/sinatra/base.rb
lib/sinatra/version.rb
sinatra.gemspec
README.md
LICENSE
...
```

The tar format inside data.tar.gz is also ustar. Files are regular files (typeflag `0`) and there may be directory entries (typeflag `5`).

**What wow needs:** Extract all files to `vendor/bundle/ruby/{ruby_version}/gems/{gem_name}-{gem_version}/`. The extraction must preserve the relative path structure.

## checksums.yaml.gz — Integrity Verification

Gzip-compressed YAML containing SHA-256 and SHA-512 hashes of the other two entries:

```yaml
---
SHA256:
  metadata.gz: 491154a9e29e4c218d9245fd73024818e7dfa6c75ba1d74220e46498841bb54e
  data.tar.gz: 42259b9becde7268d9b95abc783d896c864a6c84b3e1dd6d40d7f351a350f626
SHA512:
  metadata.gz: 43a69c7f07afab191eacc80d7837d9bdbd81701a...
  data.tar.gz: f2fb4deeb5f8e44a5a6a59663080f143c2a1dac1...
```

**What wow needs:** After extracting `metadata.gz` and `data.tar.gz` from the outer tar, compute SHA-256 of each and compare against the checksums. Cosmopolitan's mbedTLS provides SHA-256.

## What wow Must Implement

| Component | Notes |
|-----------|-------|
| **ustar tar reader** | Read 512-byte headers, extract by filename. Only typeflag `0` and `5`. |
| **gzip decompressor** | Cosmopolitan provides zlib — use `gzopen`/`gzread` or `inflate`. |
| **YAML parser** | Use Cosmopolitan's libyaml. Ignore `!ruby/object:*` tags. Extract `name`, `version`, `platform`, `dependencies`, `required_ruby_version`, `require_paths`, `executables`. |
| **SHA-256 verification** | Compare computed hash of metadata.gz and data.tar.gz against checksums.yaml.gz values. |

## Edge Cases and Quirks

1. **Three entries only** — all gems examined have exactly three entries in the same order. It's safe to assume this structure, but worth adding a check.
2. **Permissions are always 0444** — read-only. Not meaningful for extraction.
3. **Owner is always `wheel`** — not meaningful for extraction.
4. **`requirement` vs `version_requirements`** — these are always duplicates. Parse only one.
5. **Platform gems** (e.g. `nokogiri-1.18.3-x86_64-linux`) have `platform` set to something other than `"ruby"`. For MVP (pure Ruby only), we can skip these.
6. **No pax extensions observed** — all examined gems use plain ustar. No need for pax header parsing. Tested nokogiri (which has the longest paths due to `patches/` and `ext/` directories) — longest path in data.tar.gz is 79 chars, well under ustar's 100-char filename limit. Even the ustar `prefix` field (155 chars extra) is unused.
7. **File sizes in tar headers are octal ASCII strings** — remember to parse as octal, not decimal.
