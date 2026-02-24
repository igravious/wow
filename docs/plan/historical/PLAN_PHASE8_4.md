# Phase 8.4: wowx — Ephemeral Gem Tool Runner

> wowx is to wow as uvx is to uv. A single command to run any gem binary
> without installing it into a project.
>
> Status: Implementation

---

## What It Does

```bash
wowx rubocop                    # run latest rubocop
wowx rubocop@1.60.0             # run pinned version
wowx rubocop@latest             # explicit latest (same as bare name)
wowx rubocop -- --only Style    # pass args after --
```

wowx is NOT project-aware — it never reads Gemfile, vendor/, or bin/.

## Lookup Order

1. **User-installed gems** — `~/.gem/ruby/X.Y.0/bin/<binary>`
2. **wowx cache** — `~/.cache/wow/wowx/<gem>-<version>/gems/<gem>-<version>/<bindir>/<binary>`
3. **Auto-install** — resolve deps (PubGrub), parallel download, unpack to cache, run

## Architecture

Two binaries, one codebase:

| Binary | Entry point | Role |
|--------|-------------|------|
| `wow.com` | `src/main.c` | CLI + shim dispatch (existing) |
| `wowx.com` | `src/wowx_main.c` | Ephemeral gem runner (new) |

Shared objects: everything except `main.o`. Same ASSETS_ZIP (SSL roots + ruby-builder defs).

## Execution Model

wowx uses RUBYLIB (not GEM_HOME) to make gem libraries loadable. This is the same
approach as Bundler's `--standalone` mode:

```
RUBYLIB=.../rubocop-1.60.0/lib:.../ast-2.4.2/lib:.../parser-3.3.0/lib
exec ruby .../rubocop-1.60.0/exe/rubocop [user_args]
```

Ruby version: latest wow-installed Ruby (`wow_ruby_pick_latest()`). If none installed,
error with: `"No Ruby installed. Run: wow rubies install"`

## Cache Structure

```
~/.cache/wow/wowx/
└── rubocop-1.60.0/                # keyed by requested tool + resolved version
    └── gems/
        ├── rubocop-1.60.0/
        │   ├── lib/
        │   └── exe/rubocop
        ├── ast-2.4.2/
        │   └── lib/
        └── parser-3.3.0/
            └── lib/
```

Gem .gem files are downloaded to the shared cache at `~/.cache/wow/gems/` (same
location as `wow sync`). Unpacked environments live in `~/.cache/wow/wowx/`.

## New/Modified Code

| File | Change |
|------|--------|
| `src/wowx_main.c` | **New** — wowx entry point (~250 lines) |
| `include/wow/wowx.h` | **New** — `wow_wowx_cache_dir()` |
| `src/gems/meta.c` | **Modify** — parse `executables` + `bindir` from gemspec YAML |
| `include/wow/gems/meta.h` | **Modify** — add fields to `wow_gemspec` struct |
| `src/rubies/resolve.c` | **Modify** — add `wow_ruby_pick_latest()` |
| `include/wow/rubies/resolve.h` | **Modify** — declare `wow_ruby_pick_latest()` |
| `Makefile` | **Modify** — add `wowx.com` target, `all` goal |

## Known Limitations

### Functional

- **No GEM_HOME** — gems that call `Gem::Specification.find_by_name` at runtime will
  fail. RUBYLIB provides load-path resolution only. Upgrade to proper GEM_HOME when
  native extensions demand it. Known affected: gems that look up asset paths via the
  gem specification API at runtime (uncommon for CLI tools).
- **Native extension gems** — won't work (need compilation). Post-MVP scope.
- **No `--from` flag** — uvx supports `--from 'pkg>=1.0'` for complex version constraints
  and for running a binary from a differently-named package. `@version` only for MVP.
- **No `--with` flag** — uvx supports `--with <extra-dep>` to inject additional packages.

### Operational

- **Cache eviction** — `~/.cache/wow/wowx/` grows unbounded. No automatic cleanup.
  Users can `rm -rf ~/.cache/wow/wowx/` safely. A `wowx --clean` or `wow cache clean`
  command is post-MVP.
- **Binary name collisions** — if `gem-a` and `gem-b` both provide a `foo` binary,
  wowx assumes the gem name matches the binary name. Full disambiguation
  (`wowx --from gem-a foo`) requires `--from` flag (post-MVP).
- **`wow_ruby_pick_latest()` scans on every invocation** — acceptable with <20 installed
  rubies. Could cache result in a file if performance becomes an issue.

### Error Messages (key ones)

| Situation | Message |
|-----------|---------|
| No Ruby installed | `wowx: no Ruby installed. Run: wow rubies install` |
| Binary not in gem | `wowx: binary 'foo' not found in gem 'bar' (executables: baz, qux)` |
| Gem not found | `wowx: gem 'foo' not found on rubygems.org` |
| Resolution failure | `wowx: failed to resolve dependencies for foo@1.0:\n<solver error>` |
| Network error | Inherited from HTTP client error messages |

## Post-MVP Roadmap

- `--from` flag for complex constraints and cross-package binaries
- `--with` flag for additional dependencies
- `wowx --clean` / `wow cache clean` for cache eviction
- Proper GEM_HOME structure (enables `Gem::Specification` API)
- Native extension compilation support
- Binary name disambiguation
- Performance: cache latest-ruby result

## Review Credits

Plan reviewed by Kimi via agent-relay. Key inputs:
- Cache eviction strategy (document limitation, defer cleanup command)
- Binary name collision handling (first-found wins for MVP)
- Error message granularity (enumerate key failure modes)
- Gemspec executables memory management (gems can have 10+ binaries)
- RUBYLIB approach endorsed as correct MVP shortcut
