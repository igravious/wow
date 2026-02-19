# wow Ergonomics Design Document

> Internal design reference for wow's user experience model.
> Status: In Progress (Phase 8+)

---

## Core Philosophy

**wow augments, then replaces.**

wow is designed to coexist with existing Ruby tooling during evaluation, then completely replace rbenv/ruby-build/bundler/gem when the user is ready.

The transition is explicit (`wow decloak` / `wow cloak`), reversible, and non-destructive.

---

## The Cloak Model

### States

| State | PATH | Behavior |
|-------|------|----------|
| **Cloaked** (default) | wow shims NOT on PATH | User must type `wow` explicitly: `wow ruby`, `wow sync`, `wow exec rspec` |
| **Decloaked** | wow shims replace rbenv at same position | `ruby`, `gem`, `bundle` work directly; rbenv is bypassed |

### Commands

```bash
wow decloak    # Activate: put wow shims where rbenv was (or at front if no rbenv)
wow cloak      # Deactivate: restore rbenv to its original position, remove wow shims
```

### State Persistence

```json
~/.config/wow/state.json
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

### wow (the binary)

| Command | Project-aware? | Description |
|---------|---------------|-------------|
| `wow ruby` | No | Run wow-managed Ruby explicitly |
| `wow gem` | No | Run gem command with wow-managed Ruby |
| `wow bundle` / `wow sync` | **Yes** | Resolve, download, install gems to vendor/ |
| `wow exec <cmd>` | **Yes** | Run command from vendor/bundle/rubies/X.Y.Z/bin/ |
| `wow run <cmd>` | Alias for exec? | (TBD: naming) |
| `wow decloak` | N/A | Activate shims on PATH |
| `wow cloak` | N/A | Deactivate shims, restore rbenv |
| `wow install <version>` | N/A | Download and install Ruby version |
| `wow list` | N/A | List installed Ruby versions |

### wowx

| Command | Project-aware? | Description |
|---------|---------------|-------------|
| `wowx <gem-binary>` | **No** | Run standalone tool (user gems → ephemeral) |
| `wowx <script.rb>` | **No** | Run Ruby script with correct shebang |

**Critical:** `wowx` NEVER looks at Gemfile, vendor/, or bin/. It is for standalone tools only.

### Generated bin/ (project-local)

After `wow sync`, `bin/` directory contains stubs:
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

### Dynamic Discovery

Unlike wow's current static list, shims are discovered:

1. **On `wow install <ruby-version>`**: Scan `~/.local/share/wow/rubies/ruby-X.Y.Z-*/bin/`
2. **On `wow sync`**: Scan `vendor/bundle/rubies/X.Y.Z/bin/`
3. **Create/update** shims in `~/.local/share/wow/shims/`

### Two Shim Templates (For Speed)

| Binary | For | Behavior |
|--------|-----|----------|
| `shim_native.com` | Native executables | If invoked as "ruby": set `LD_LIBRARY_PATH` for libruby.so, then `exec(target, argv)`. Otherwise: `exec(target, argv)` directly. |
| `shim_script.com` | Ruby scripts | Prepend ruby to argv: `[ruby_path, target, ...argv[1:]]`, then `exec(ruby_path, new_argv)` |

**Deployment:**
```bash
# Build once
cosmocc -o shim_native.com shim_native.c
cosmocc -o shim_script.com shim_script.c

# Copy and rename for each discovered binary
cp shim_native.com ~/.local/share/wow/shims/ruby
cp shim_native.com ~/.local/share/wow/shims/nokogiri    # native extension
cp shim_script.com ~/.local/share/wow/shims/thor
cp shim_script.com ~/.local/share/wow/shims/rails
cp shim_script.com ~/.local/share/wow/shims/bundle
```

**Why two binaries?** Minimal runtime branching for speed. The shim is invoked on every Ruby command—microseconds matter.

### Ruby Special Case

The `ruby` binary needs `LD_LIBRARY_PATH` set to find `libruby.so.X.Y`:

```c
// In shim_native.com, when invoked as "ruby"
if (strcmp(name, "ruby") == 0) {
    setenv("LD_LIBRARY_PATH", ruby_lib_dir, 1);
}
execv(target, argv);
```

Other native binaries (gem extensions like `nokogiri`) don't need this—they're loaded by ruby, not standalone.

### Windows Solved

APE binaries run natively on Windows too. No batch files, no PowerShell scripts, no symlink issues.

### Discovery & Classification

**Scanning Ruby's bin/ directory:**

```c
// Scan ~/.local/share/wow/rubies/ruby-X.Y.Z-*/bin/
for each file in bin/:
    if is_elf(file) || is_ape(file):
        cp("shim_native.com", "~/.local/share/wow/shims/<name>")
    else:
        cp("shim_script.com", "~/.local/share/wow/shims/<name>")
```

In Ruby 4.0.1, only `ruby` is native—everything else is a Ruby script. But gems with native extensions may install binaries.

### Shim Database

Each shim tracks which Ruby versions provide that binary:

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

### Error Messages

```bash
$ thor
wow: thor: command not found

The `thor' command exists in these Ruby versions:
  3.1.0
  3.2.6

Install the missing version with: wow install 3.1.0
Or install the gem with: wow gem install thor
```

---

## Gem Installation Strategy

### Default: Project-local (Bundler-style)

```bash
wow sync    # Installs to vendor/bundle/rubies/X.Y.Z/
```

### User-local (Optional)

```bash
wow gem config install --user    # Default to ~/.gem/rubies/X.Y.0/
wow gem install pry                # Goes to user dir
```

### System (Not Recommended)

```bash
wow gem config install --system   # Requires write permissions
```

---

## The Ruby Binary Problem

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

## Coexistence with rbenv

### During Evaluation (Cloaked)

```bash
# rbenv handles ruby, gem, bundle
ruby --version    # rbenv's Ruby

# wow handles wow commands explicitly
wow install 3.3.6
wow sync
wow exec rspec
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

4. **Shebang rewriting for wowx**: How does `wowx script.rb` ensure correct Ruby?
   - Parse shebang, replace with wow's Ruby path?
   - Or: `exec(wow_ruby_path, script.rb, argv)`?
   - *Deferred: cross this bridge when we come to it*

5. **Windows**: How do shims work on Windows without symlinks/hardlinks?
   - Option: `.exe` wrappers that call wow
   - Option: Batch files
   - Option: Skip shims on Windows, use `wow` command only
   - *Deferred: cross this bridge when we come to it*

## Resolved Decisions

### 1. wowx vs bin/ — No Collision

`./bin/rubocop` (project stub) and `wowx rubocop` (standalone) are **different commands**:
- `./bin/rubocop` — explicit path, project-aware, uses `vendor/bundle`
- `wowx rubocop` — global command, NOT project-aware, uses user gems or ephemeral

No collision because:
- Different invocation patterns
- Different use cases (project tool vs standalone tool)
- wowx explicitly does NOT look at project `bin/` or `vendor/`

### 2. wowx Ruby Version

**Rule:** Use the latest wow-installed Ruby. If none installed, prompt to install.

```bash
wowx rubocop
# if Ruby 4.0.1 is latest installed: use it
# if no Ruby installed: "No Ruby installed. Install latest (4.0.1)? [Y/n]"
```

### 3. Gem Caching

| Command | Cache? | Location | Notes |
|---------|--------|----------|-------|
| `wow sync` | **NO** | `vendor/bundle/rubies/X.Y.Z/` | Direct install, no cache layer |
| `wowx` | **YES** | `~/.local/share/wow/ephemeral_gem_cache/` | TTL cleanup needed |

**Rationale:** wow sync is designed to be lightning fast (parallel downloads). Adding a cache layer adds complexity for minimal gain. wowx needs caching because the same ephemeral tool may be requested multiple times.

**Directory structure:**
```
~/.local/share/wow/
├── rubies/                    # Ruby installations (was: rubies/)
├── shims/                     # Dynamic shims
└── ephemeral_gem_cache/       # wowx tools only
    └── rubocop-1.50.0/
        └── ...
```

---

## Implementation Priority

### Phase 1: Fix Current Blockers
- [ ] Fix Ruby binary (libruby.so issue) — static link or RPATH
- [ ] Fix argv[0] dispatch in shims
- [ ] Ensure `wow exec` works

### Phase 2: Shim Management
- [ ] Dynamic shim discovery on install
- [ ] Shim database (JSON)
- [ ] Error messages with version hints

### Phase 3: Cloak/Decloak
- [ ] `wow decloak` command
- [ ] `wow cloak` command
- [ ] State persistence

### Phase 4: wowx
- [ ] Ephemeral gem execution
- [ ] User gem fallback
- [ ] Script runner

### Phase 5: Polish
- [ ] Install script
- [ ] CI/CD integration
- [ ] Documentation

---

## Related Documents

- `docs/plan/MASTER_PLAN.md` — Phase roadmap
- `docs/plan/PLAN_PHASE8.md` — Current implementation details
- `AGENTS.md` — Build conventions

---

*Last updated: 2026-02-19*
*Status: Draft for discussion*
