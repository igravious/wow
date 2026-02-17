# wow — Master Plan

> **uv for Ruby, in C.**
> A single tool to replace gem, bundler, rbenv, ruby-build, and gem push.
> Single portable binary. Zero dependencies. Download one file, run it anywhere.

## Design Mantras

1. **WE ARE SLAVES TO UV.** Whatever uv does, wow does.
2. **We are still Rubyists.** wow doesn't change how Ruby works. It speeds up the machinery but keeps the conventions. A Rubyist walks into a wow project and sees: `Gemfile`, `Gemfile.lock`, `vendor/`, `.ruby-version`. Everything looks normal.

## What wow Replaces

| Python tool | uv replaces | Ruby equivalent | wow replaces |
|-------------|-------------|-----------------|--------------|
| pip | yes | `gem install` | yes |
| pip-tools | yes | `bundle lock` / `bundle install` | yes |
| pipx | yes | `gem install` (global executables) | yes |
| poetry | yes | `bundler` (Gemfile/Gemfile.lock) | yes |
| pyenv | yes | `rbenv` + `ruby-build` | yes |
| twine | yes | `gem push` | yes (post-MVP) |
| virtualenv | yes | `vendor/bundle` + `bundle exec` | yes |

## How It Looks to a Rubyist

Everything is familiar under the hood:

- **`vendor/bundle/`** for project gems — standard Bundler convention
- **`Gemfile`** and **`Gemfile.lock`** — standard Bundler format, readable by actual Bundler
- **`.ruby-version`** — exact same file, exact same semantics as rbenv
- **Shims** like rbenv — `~/.local/share/wow/shims/{ruby,irb,gem,...}` on PATH
- **`cd` into a project** with `.ruby-version` → right Ruby is active (shim resolves it)
- **`wow run`** sets `GEM_PATH`/`GEM_HOME` to `vendor/bundle/`, `PATH` to include managed Ruby bin, same as `bundle exec`

The difference: none of those tools need to be installed. wow IS rbenv + ruby-build + bundler + gem — from the outside, it looks like all four are there.

## Command Map (mirrors uv)

```
# Project management
wow init [name]             ↔ uv init         # Gemfile + .ruby-version + download Ruby
wow sync                    ↔ uv sync         # resolve → lock → download → install to vendor/
wow lock                    ↔ uv lock         # just update Gemfile.lock
wow add <gem>               ↔ uv add          # add to Gemfile + sync
wow remove <gem>            ↔ uv remove       # remove from Gemfile + sync
wow run <cmd>               ↔ uv run          # run with correct Ruby + GEM_PATH

# Ruby management
wow ruby install [version]  ↔ uv python install
wow ruby list               ↔ uv python list
wow ruby pin <version>      ↔ uv python pin   # writes .ruby-version

# Tools (global gem executables)
wow tool install <gem>      ↔ uv tool install
wowx <cmd>                  ↔ uvx             # ephemeral tool run

# Backwards compat (like uv pip)
wow bundle install          ↔ uv pip install
wow bundle exec <cmd>       ↔ uv pip ...

# Meta
wow self update             ↔ uv self update

# Post-MVP
wow build                   ↔ uv build        # build a .gem
wow publish                 ↔ uv publish      # push to rubygems.org
```

## MVP Workflow

```bash
wow init greg          # creates greg/, Gemfile, .ruby-version, downloads latest Ruby
cd greg                # shim activates — ruby points to managed installation
vi Gemfile             # uncomment gem "sinatra"
wow sync               # resolve → lock → parallel download → install to vendor/bundle/
wow run ruby app.rb    # runs with correct Ruby + gems on GEM_PATH
```

## Technical Stack

| Decision | Choice |
|----------|--------|
| Language | C |
| Libc | Cosmopolitan (single APE binary, all platforms) |
| Build system | Plain Makefile, out-of-tree builds |
| Gemfile parser | re2c (lexer) + lemon (parser) |
| Resolver | PubGrub (from day one) |
| Downloads | Parallel, connection pooling, streaming (pthreads + mbedTLS) |
| MVP scope | Pure Ruby gems only (native extensions deferred) |

## What Cosmopolitan Provides (zero external deps)

mbedTLS (TLS + SHA-256), zlib, bzip2, zstd, libyaml, sqlite3, xxhash, pcre, pthreads, POSIX sockets, zip/unzip.

Only vendored dep: **cJSON** (single .c/.h for rubygems.org JSON API).

## Directory Layout

### Managed Rubies (mirrors uv)

```
~/.local/share/wow/
├── ruby/
│   ├── .lock
│   ├── .temp/
│   ├── ruby-4.0.1-linux-x86_64/
│   └── ruby-4.0-linux-x86_64 -> ruby-4.0.1-linux-x86_64/
├── shims/
│   ├── ruby              # shim → reads .ruby-version, dispatches to correct Ruby
│   ├── irb
│   ├── gem
│   └── ...
└── tools/                # globally installed gem executables (wow tool install)
```

### Global Cache (shared across projects)

```
~/.cache/wow/
├── gems/                 # downloaded .gem files (content-addressed)
├── registry/             # rubygems.org API response cache
└── metadata/             # sqlite3 package metadata index
```

### Per-Project (standard Ruby conventions)

```
greg/
├── Gemfile               # standard Bundler format
├── Gemfile.lock          # standard Bundler format
├── .ruby-version         # standard rbenv format
└── vendor/
    └── bundle/           # installed gems (standard Bundler --path)
```

## Gemfile Parser: Restricted Subset

Gemfiles are Ruby code (`instance_eval`). wow does NOT evaluate Ruby. It parses a declarative subset via re2c + lemon. Clean error on dynamic constructs.

**Supported:** `source`, `gem` (with version constraints and options), `group do...end`, `ruby`, `gemspec`, comments, blank lines.

**Not supported (clean error with offending line shown):** `if/else/case`, `eval`, `Dir.glob`, `ENV[]`, `git_source`, dynamic gem names/versions. Error messages must include the offending line and a clear explanation — not just "parse error at line 7".

This mirrors uv: uv parses TOML, not arbitrary Python.

## Micro-Phases

Every micro-phase has a working demo. See PLAN_PHASEn.md for details.

| Phase | Name | Micros | Key Demo | Status |
|-------|------|--------|----------|--------|
| 0 | Research | 0a–0d | Documented formats + APIs | Done |
| 1 | Skeleton | 1a–1c | `./wow.com init greg` creates project | Done |
| 2 | HTTPS | 2a–2c | Fetch gem info from rubygems.org | Done |
| 3 | Ruby Manager | 3a–3e + 7 | Download Ruby, shims, parallel downloads | Done |
| 4 | .gem Unpack | 4a–4d | Download + unpack sinatra to vendor/ | Next |
| 5 | Gemfile Parser | 5a–5c | re2c + lemon parse a real Gemfile | |
| 6 | PubGrub | 6a–6d | Resolve sinatra deps, generate Gemfile.lock | |
| 8 | End-to-End | 8a–8c | Full `wow sync` with uv-style output | |

Phase 7 (parallel downloads) was merged into Phase 3.

## Source Layout

```
Code/wow/
├── src/
│   ├── main.c              # CLI entry point, subcommand dispatch, shim argv[0]
│   ├── init.c              # wow init (Gemfile + .ruby-version + Ruby download)
│   ├── registry.c          # rubygems.org API client (cJSON)
│   ├── version.c           # Latest CRuby version from GitHub releases
│   ├── tar.c               # Streaming tar.gz extraction (security-hardened)
│   ├── http/
│   │   ├── client.c        # HTTPS GET (mbedTLS, redirects, streaming download)
│   │   ├── pool.c          # Connection pool (Keep-Alive, MSG_PEEK, LRU)
│   │   └── entropy.c       # mbedTLS entropy shim (getentropy(2))
│   ├── download/
│   │   ├── multibar.c      # uv-style multi-bar progress display
│   │   ├── parallel.c      # Bounded-concurrency worker pool
│   │   └── progress.c      # Single-download progress bar
│   ├── rubies/
│   │   ├── cmd.c           # wow ruby subcommand dispatch
│   │   ├── resolve.c       # Platform detection, version resolution
│   │   ├── install.c       # Download + extract + atomic rename
│   │   ├── install_many.c  # Parallel multi-version install
│   │   ├── uninstall.c     # Remove installed Ruby
│   │   ├── list.c          # List installed Rubies
│   │   ├── shims.c         # Create symlink shims (ruby, irb, gem, ...)
│   │   └── internal.c      # Shared helpers (mkdirs, colour, timing)
│   ├── sync.c              # wow sync orchestrator (Phase 8)
│   ├── gemfile.l.re2c      # re2c lexer (Phase 5)
│   ├── gemfile.y           # lemon grammar (Phase 5)
│   ├── gem.c               # .gem download + unpack (Phase 4)
│   └── pubgrub.c           # PubGrub dependency resolver (Phase 6)
├── include/wow/             # Public headers (mirror src/ layout)
│   ├── http.h, download.h, rubies.h  # Umbrella convenience headers
│   ├── http/, download/, rubies/      # Per-module headers
│   └── tar.h, registry.h, etc.
├── vendor/cjson/
├── demos/                   # Per-phase demos (phase0/, phase2/, phase3/)
├── tests/                   # Test binaries + fixtures
├── docs/plan/
├── Makefile
└── build/
    └── wow.com              # the Actually Portable Executable
```

## Post-MVP Roadmap

- Native extension compilation (the hard problem)
- Pre-built binary gems (Ruby's equivalent of Python wheels)
- sqlite3 package metadata cache
- Content-addressed gem cache with xxhash
- `wow build` / `wow publish`
- Workspaces (multi-gem projects)
- `wow self update`

## Known Risks

1. **PubGrub in C** — the hardest phase. Conflict-driven learning requires careful memory management. Mitigation: arena allocation for incompatibilities (same lifetime), and don't skip human-readable error messages — they're the user-visible win over Bundler.
2. **Platform-specific gem metadata** — even pure Ruby gems can have platform variants. Watch for this in Phase 0 research.
3. **Gemfile.lock format edge cases** — git sources, platforms section, path sources. Phase 0b must collect diverse real-world samples.
4. **Pre-built Ruby binary availability** — document fallback plan if third-party binary sources go away. Cosmopolitan Ruby builds are the long-term answer.

## Review Credits

Plan critiqued by Kimi via agent-relay (2 rounds). Key inputs:
- Phase reorder (gem unpack before parser)
- Phase 0 (research before coding)
- Restricted Gemfile subset (not lock-only — breaks bootstrap)
- Scoped MVP to pure Ruby gems
- Shim implementation: symlink to wow.com with argv[0] dispatch
- PubGrub memory: arena allocation for incompatibilities
- mbedTLS threading: per-thread SSL context + socket, shared read-only config
- wow run: also set PATH to managed Ruby bin, consider RUBYOPT=-rubygems
- Error messages: show offending line on unsupported Gemfile syntax
