# wow Ergonomics Design Document

> Internal design reference for wow's user experience model.
> Status: In Progress (Phase 8+)
>
> **Reading guide:** Each section below is marked with its implementation status:
> - **Implemented** — working code, tested
> - **Half-baked** — code exists but incomplete or limited
> - **Stub** — dispatch entry exists, prints "not yet implemented"
> - **Aspirational** — design only, no code yet

---

## Core Philosophy

**wow augments, then replaces.**

wow is designed to coexist with existing Ruby tooling during evaluation, then completely replace rbenv/ruby-build/bundler/gem when the user is ready.

The transition is explicit (`wow decloak` / `wow cloak`), reversible, and non-destructive.

---

## The Cloak Model

> **Status: Aspirational** — no code exists. Not even stubbed in `main.c` dispatch.

### States

| State | PATH | Behavior |
|-------|------|----------|
| **Cloaked** (default) | wow shims NOT on PATH | User must type `wow` explicitly: `wow ruby`, `wow sync`, `wow run rspec` |
| **Decloaked** | wow shims replace rbenv at same position | `ruby`, `gem`, `bundle` work directly; rbenv is bypassed |

### Commands

```bash
wow decloak    # Activate: put wow shims where rbenv was (or at front if no rbenv)
wow cloak      # Deactivate: restore rbenv to its original position, remove wow shims
```

### State Persistence

```json
~/.config/wow/state.json (XDG_CONFIG_HOME)
{
  "replaced_rbenv": true,
  "rbenv_shims_path": "~/.rbenv/shims",
  "rbenv_position": 3
}
```

On `cloak`: restore rbenv to position 3. On `decloak`: put wow at position 3, remember rbenv.

If PATH changed between cloak/decloak, positions are clamped to valid range.

---

## Command Reference (Proposed)

### Native wow commands

These are wow's own vocabulary — the primary interface.

| Command | Status | Project-aware? | Description |
|---------|--------|---------------|-------------|
| `wow init` | **Implemented** | **Yes** | Create Gemfile + .ruby-version + install Ruby |
| `wow sync` | **Implemented** | **Yes** | Resolve, lock, download, install gems to vendor/ |
| `wow lock` | **Implemented** | **Yes** | Update Gemfile.lock without installing |
| `wow resolve` | **Implemented** | **Yes** | Resolve gem dependencies |
| `wow add <gem>` | Stub | **Yes** | Add gem to Gemfile + sync |
| `wow remove <gem>` | Stub | **Yes** | Remove gem from Gemfile + sync |
| `wow run <cmd>` | Stub | **Yes** | Run command with correct Ruby + GEM_PATH (mirrors `uv run`) |
| `wow rubies install` | **Implemented** | No | Download and install Ruby version |
| `wow rubies list` | **Implemented** | No | List installed Ruby versions |
| `wow rubies pin` | Aspirational | **Yes** | Write .ruby-version |
| `wow decloak` | Aspirational | N/A | Activate shims on PATH |
| `wow cloak` | Aspirational | N/A | Deactivate shims, restore rbenv |

Wow also has internal diagnostic commands — these are wow-native and are **not** part of
the emulation layer below:

| Command | Status | Description |
|---------|--------|-------------|
| `gem-info` | **Implemented** | Show gem info from rubygems.org |
| `gem-download` | **Implemented** | Download a .gem file to cache |
| `gem-list` | **Implemented** | List .gem tar contents |
| `gem-meta` | **Implemented** | Show .gem metadata (libyaml) |
| `gem-unpack` | **Implemented** | Unpack .gem to directory |
| `gemfile-parse` | **Implemented** | Parse a Gemfile (re2c + lemon) |
| `gemfile-deps` | **Implemented** | List Gemfile dependencies |

### Emulation layer: `wow bundle`, `wow gem`, `wow rbenv`

> **Status: Half-baked** — `wow bundle install` exists and routes to `wow sync`. Everything
> else in this section is aspirational.

These commands emulate the most common subcommands of Bundler, RubyGems, and rbenv respectively,
using the **exact same subcommand names** but doing things the wow way. The goal is muscle-memory
compatibility — a Rubyist can type what they already know and it works.

wow doesn't try to replicate every feature of these tools, just the most common and useful ones
that map onto what wow does. Subcommands that make no sense in wow's model are rejected with a
clear message explaining the wow equivalent.

| Command | Status | Emulates | wow behaviour |
|---------|--------|----------|---------------|
| `wow bundle install` | **Implemented** | `bundle install` | Routes to `wow sync` |
| `wow bundle exec <cmd>` | Aspirational | `bundle exec` | Routes to `wow run` |
| `wow bundle update` | Aspirational | `bundle update` | Routes to `wow sync` (re-resolve) |
| `wow bundler ...` | Aspirational | Same as `wow bundle ...` | Alias |
| `wow gem install <gem>` | Aspirational | `gem install` | Install gem (user-local or project) |
| `wow gem list` | Aspirational | `gem list` | List installed gems |
| `wow gem env` | Aspirational | `gem env` | Show wow's gem paths, Ruby version, platform |
| `wow rbenv install <ver>` | Aspirational | `rbenv install` | Routes to `wow rubies install` |
| `wow rbenv versions` | Aspirational | `rbenv versions` | Routes to `wow rubies list` |
| `wow rbenv version` | Aspirational | `rbenv version` | Show active Ruby version |

This list will grow as we discover more subcommands worth emulating.

### wowx

> **Status: Aspirational** — no binary, no dispatch logic, no code at all.

| Command | Project-aware? | Description |
|---------|---------------|-------------|
| `wowx <gem-binary>` | **No** | Run standalone tool (user gems → cache → download) |

**Critical:** `wowx` NEVER looks at Gemfile, vendor/, or bin/. It is for standalone gem binaries only — not scripts.

**Shebang handling:** Users are responsible for their script shebangs. `#!/usr/bin/env ruby` works if `ruby` resolves correctly. Hardcoded paths like `#!/opt/rubies/3.2.0/bin/ruby` are the user's responsibility.

### Generated bin/ (project-local) — Aspirational

After `wow sync`, `bin/` directory would contain stubs:
```
bin/rspec
bin/rake
bin/rails
```

These stubs:
- Are project-aware (use vendor/bundle)
- Work with wow (even when cloaked)
- Do NOT work with rbenv (intentional)

---

## Shim Management

### Current state (Implemented)

Shims work today but are limited:

- **Static list**: `src/rubies/shims.c` hardcodes 9 names: `ruby`, `irb`, `gem`, `bundle`,
  `bundler`, `rake`, `rdoc`, `ri`, `erb`
- **Single-binary model**: all shims are hardlinks (or copies) of `wow.com` itself
- **argv[0] dispatch**: `main.c` detects when invoked as something other than `wow`, reads
  `.ruby-version`, resolves the platform, constructs `~/.local/share/wow/rubies/ruby-{ver}-{plat}/bin/{name}`,
  and `execv()`s into the real binary
- **Created automatically** when `wow rubies install` runs
- **No dynamic discovery** — gem-installed binaries are not shimmed
- **No shim database** — no JSON metadata tracking which versions provide which binary

This works for the core Ruby toolchain but doesn't cover gem-installed executables.

### Proposed: Dynamic Discovery (Aspirational)

Shims would be discovered rather than hardcoded:

1. **On `wow rubies install <version>`**: Scan `~/.local/share/wow/rubies/ruby-X.Y.Z-*/bin/`
2. **On `wow sync`**: Scan `vendor/bundle/ruby/X.Y.0/gems/` (Bundler convention)
3. **Create/update** shims in `~/.local/share/wow/shims/`

### Proposed: Two Shim Templates (Aspirational)

This would replace the current single-binary argv[0] dispatch model with two purpose-built
shim binaries for speed. The current model routes every shim invocation through the full
`wow.com` binary (CLI parsing, etc.) before dispatching — the proposed model skips all of that.

| Binary | For | Behaviour |
|--------|-----|----------|
| `shim_native.com` | Native executables | If invoked as "ruby": set `LD_LIBRARY_PATH` for libruby.so, then `exec(target, argv)`. Otherwise: `exec(target, argv)` directly. |
| `shim_script.com` | Ruby scripts | Prepend ruby to argv: `[ruby_path, target, ...argv[1:]]`, then `exec(ruby_path, new_argv)` |

**Deployment:**
```bash
# Build once
cosmocc -o shim_native.com shim_native.c
cosmocc -o shim_script.com shim_script.c

# Linux/macOS: Use hardlinks (zero disk overhead)
ln shim_native.com ~/.local/share/wow/shims/ruby
ln shim_native.com ~/.local/share/wow/shims/nokogiri    # native extension
ln shim_script.com ~/.local/share/wow/shims/thor
ln shim_script.com ~/.local/share/wow/shims/rails
ln shim_script.com ~/.local/share/wow/shims/bundle

# Windows: Copy (APE binaries work natively, no symlinks needed)
cp shim_native.com %LOCALAPPDATA%\wow\shims\ruby.exe
# ... etc
```

**Why two binaries?** Minimal runtime branching for speed. The shim is invoked on every Ruby command — microseconds matter.

### Proposed: Ruby Special Case (Aspirational)

The `ruby` binary needs `LD_LIBRARY_PATH` set to find `libruby.so.X.Y`:

```c
// In shim_native.com, when invoked as "ruby"
if (strcmp(name, "ruby") == 0) {
    setenv("LD_LIBRARY_PATH", ruby_lib_dir, 1);
}
execv(target, argv);
```

Other native binaries (gem extensions like `nokogiri`) don't need this — they're loaded by ruby, not standalone.

> **Note:** This may become moot if we solve the libruby.so problem via static linking or RPATH
> (see "The Ruby Binary Problem" section below).

### Windows (Implemented by cosmocc, untested for shims)

APE binaries run natively on Windows too. No batch files, no PowerShell scripts, no symlink issues. The shim copy fallback in `shims.c` already handles the no-hardlink case.

### Proposed: Discovery & Classification (Aspirational)

**Scanning Ruby's bin/ directory:**

```c
// Scan ~/.local/share/wow/rubies/ruby-X.Y.Z-*/bin/
for each file in bin/:
    if is_elf(file) || is_ape(file):
        cp("shim_native.com", "~/.local/share/wow/shims/<name>")
    else:
        cp("shim_script.com", "~/.local/share/wow/shims/<name>")
```

In Ruby 4.0.1, only `ruby` is native — everything else is a Ruby script. But gems with native extensions may install binaries.

### Proposed: Shim Database (Aspirational)

Each shim would track which Ruby versions provide that binary:

```json
~/.local/share/wow/shims/thor.json
{
  "name": "thor",
  "provided_by": [
    {"ruby": "3.1.0", "path": "~/.local/share/wow/rubies/ruby-3.1.0-.../bin/thor"},
    {"ruby": "3.2.6", "path": "~/.local/share/wow/rubies/ruby-3.2.6-.../bin/thor"}
  ]
}
```

### Proposed: Error Messages (Aspirational)

Requires shim database to implement:

```bash
$ thor
wow: thor: command not found

The `thor' command exists in these Ruby versions:
  3.1.0
  3.2.6

Install the missing version with: wow rubies install 3.1.0
Or install the gem with: wow gem install thor
```

---

## Gem Installation Strategy

### Default: Project-local (Bundler-style) — Half-baked

`wow sync` installs to `vendor/bundle/ruby/X.Y.0/` — this works today via the lockfile
extraction path. The full resolve → lock → download → install pipeline is in progress
(depends on Phases 5–6 for Gemfile parsing and PubGrub resolution).

```bash
wow sync    # Installs to vendor/bundle/ruby/X.Y.0/
```

### User-local (Optional) — Aspirational

```bash
wow gem config install --user    # Default to ~/.gem/rubies/X.Y.0/
wow gem install pry                # Goes to user dir
```

### System (Not Recommended) — Aspirational

```bash
wow gem config install --system   # Requires write permissions
```

---

## The Ruby Binary Problem

> **Status: Open problem** — affects all ruby-builder pre-built binaries today.
> The current argv[0] shim dispatch does a bare `execv()` without setting
> `LD_LIBRARY_PATH`, so this issue is live whenever the user invokes `ruby`
> directly through a shim.

**Current Issue:**
```bash
~/.local/share/wow/rubies/ruby-4.0.1-ubuntu-22.04-x64/bin/ruby --version
# error: libruby.so.4.0: cannot open shared object file
```

**Root Cause:** Dynamic linking, LD_LIBRARY_PATH not set.

**Solutions:**

| Option | Implementation | Trade-off |
|--------|---------------|-----------|
| A. Set LD_LIBRARY_PATH | Wrapper script or env var before exec | Fragile, env pollution |
| B. Static-link Ruby | Build Ruby with `--disable-shared` | Larger binary, no lib sharing |
| C. RPATH | Build with `-Wl,-rpath,$ORIGIN/../lib` | Requires rebuild |

**Recommendation:** Option B (static) for distributed binaries. Option C (RPATH) if we control the build.

---

## Installation Flow

> **Status: Aspirational** — no install script exists. Today wow is built from source
> and the binary is manually placed on PATH.

### First-time Install

```bash
curl -sSf https://wow.sahos.org/install.sh | sh
```

**What happens:**
1. Download `wow.com` → `~/.local/bin/wow`
2. Download `wowx.com` → `~/.local/bin/wowx` (or single binary with argv[0] dispatch)
3. Create shim directory: `~/.local/share/wow/shims/`
4. Create env script: `~/.local/share/wow/env` (commented out by default)
5. **Cloaked state**: shims NOT added to PATH

### User activates

```bash
wow decloak    # Now shims are on PATH (if rbenv was there, it's replaced)
ruby --version # Uses wow
```

### User deactivates

```bash
wow cloak      # Back to cloaked, rbenv restored
```

---

## Coexistence with rbenv (Aspirational — requires cloak/decloak)

### During Evaluation (Cloaked)

```bash
# rbenv handles ruby, gem, bundle
ruby --version    # rbenv's Ruby

# wow handles wow commands explicitly
wow install 3.3.6
wow sync
wow run rspec
```

### After Decloak

```bash
wow decloak

# wow owns everything
ruby --version    # wow's Ruby
gem list          # wow's gems
bundle install    # wow's bundle (or wow sync)

# rbenv is still installed but not on PATH
```

### Emergency Escape

If wow breaks:
```bash
wow cloak        # Restore rbenv
# or manually: export PATH="..." (remove wow shims)
```

---

## Open Questions (Deferred)

1. **Shebang rewriting**: How does `wowx script.rb` ensure correct Ruby?
   - Parse shebang, replace with wow's Ruby path?
   - Or: `exec(wow_ruby_path, script.rb, argv)`?
   - *Deferred: cross this bridge when we come to it*

2. **PATH manipulation libraries**: Shell-specific PATH handling (bash, zsh, fish) is a solved problem. Research existing libraries before implementing cloak/decloak.
   - Fish uses `fish_user_paths` not `$PATH`
   - tmux/screen inherit PATH at creation
   - User may modify shell config between cloak/decloak
   - *Research: env-paths, directories crate, xdg-base-dirs*

## Resolved Decisions

### 1. wowx vs bin/ — No Collision

`./bin/rubocop` (project stub) and `wowx rubocop` (standalone) are **different commands**:
- `./bin/rubocop` — explicit path, project-aware, uses `vendor/bundle`
- `wowx rubocop` — global command, NOT project-aware, uses user gems or ephemeral

No collision because:
- Different invocation patterns
- Different use cases (project tool vs standalone tool)
- wowx explicitly does NOT look at project `bin/` or `vendor/`

### 2. wowx Gem Lookup

**Lookup order:**
1. User-installed gems (`~/.gem/ruby/X.Y.0/bin/`) — respect user's explicitly installed tools
2. Cache (`~/.cache/wow/gems/`) — previously downloaded ephemeral tools

If not found: download to cache, then run.

### 3. wowx Ruby Version

**Rule:** Use the latest wow-installed Ruby. If none installed, prompt to install.

```bash
wowx rubocop
# if Ruby 4.0.1 is latest installed: use it
# if no Ruby installed: "No Ruby installed. Install latest (4.0.1)? [Y/n]"
```

### 4. Gem Caching

| Command | Cache? | Location | Status |
|---------|--------|----------|--------|
| `wow sync` | **YES** | `~/.cache/wow/gems/` | **Implemented** — `gem-download` uses XDG_CACHE_HOME |
| `wowx` | **YES** | `~/.cache/wow/gems/` | Aspirational — wowx doesn't exist yet |

**Directory structure (XDG compliant):**
```
~/.cache/wow/
└── gems/                      # Downloaded .gem files          [Implemented]
    └── nokogiri-1.16.0.gem

~/.local/share/wow/
├── rubies/                    # Ruby installations             [Implemented]
└── shims/                     # Shims (static list today)      [Half-baked]

~/.config/wow/
└── state.json                 # decloak/cloak state            [Aspirational]

# System-wide (if installed by admin)
/usr/share/wow/
└── rubies/                    # System Ruby installations      [Aspirational]
```

---

## Implementation Priority (Ergonomics Milestones)

These milestones are orthogonal to the master phase roadmap (see `docs/plan/MASTER_PLAN.md`).
They represent UX infrastructure that lands progressively across master Phases 5–8.

### Milestone 1: Fix Current Blockers
- [ ] Fix Ruby binary (libruby.so issue) — static link or RPATH
- [ ] Fix argv[0] dispatch in shims
- [ ] Ensure `wow run` works (master Phase 8)

### Milestone 2: Shim Management
- [ ] Dynamic shim discovery on install
- [ ] Shim database (JSON)
- [ ] Error messages with version hints

### Milestone 3: Cloak/Decloak
- [ ] `wow decloak` command
- [ ] `wow cloak` command
- [ ] State persistence (`~/.config/wow/state.json`)

### Milestone 4: wowx (mirrors `uvx`)
- [ ] Ephemeral gem execution
- [ ] User gem fallback

### Milestone 5: Polish
- [ ] Install script
- [ ] CI/CD integration
- [ ] Documentation

---

## Related Documents

- `docs/plan/MASTER_PLAN.md` — Phase roadmap
- `docs/plan/PLAN_PHASE8.md` — Current implementation details
- `docs/plan/PLAN_PHASE8_4.md` — wowx ephemeral gem tool runner
- `AGENTS.md` — Build conventions

---

*Last updated: 2026-02-20*
*Status: Draft for discussion*
