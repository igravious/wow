# Phase 3: Ruby Manager

> Download, install, and activate Ruby — the rbenv + ruby-build replacement.

## 3a: Download Pre-Built Ruby

**Demo:** `./wow.com ruby install 4.0` downloads Ruby to `~/.local/share/wow/ruby/`.

**Files:**
- `src/ruby_mgr.c`
- `include/wow/ruby_mgr.h`

**Implementation:**
- Determine download URL from version + platform (researched in Phase 0d)
- Download with progress bar (reuse http.c)
- Verify SHA-256 hash
- Extract tarball to `~/.local/share/wow/ruby/ruby-{version}-{platform}/`
- Stage in `.temp/` first, atomic rename on completion
- Write `.lock` file for concurrent access safety

**Verify:**
```bash
./build/wow.com ruby install 4.0
ls ~/.local/share/wow/ruby/
# ruby-4.0.1-linux-x86_64/
```

## 3b: Minor-Version Symlink

**Demo:** `ruby-4.0` symlink points to `ruby-4.0.1`.

**Implementation:**
- After install, create symlink: `ruby-4.0-{platform}` → `ruby-4.0.1-{platform}`
- On upgrade (4.0.1 → 4.0.2), update the symlink atomically
- `.ruby-version` contains `4.0` (minor), symlink resolves to patch

**Verify:**
```bash
ls -la ~/.local/share/wow/ruby/
# ruby-4.0-linux-x86_64 -> ruby-4.0.1-linux-x86_64/
```

## 3c: wow ruby list

**Demo:** `./wow.com ruby list` shows all managed Ruby installations.

**Implementation:**
- Scan `~/.local/share/wow/ruby/` for directories matching `ruby-*`
- Print version, platform, path
- Mark active version (from nearest `.ruby-version`)

**Verify:**
```bash
./build/wow.com ruby list
# ruby-4.0.1-linux-x86_64  (active)
```

## 3d: wow init Downloads Ruby Eagerly

**Demo:** `./wow.com init greg` now downloads Ruby before returning.

**Changes to init.c:**
- After writing Gemfile + .ruby-version, call `ruby_mgr_ensure(version)`
- If Ruby is already installed → skip (fast path)
- If not → download + install (with progress bar)
- Print uv-style output: `ruby-4.0.1-linux-x86_64 (download)  2.3 MiB/45.2 MiB`

## 3e: Shims

**Demo:** `cd greg && ruby --version` → uses wow-managed Ruby.

**Implementation:**
- `wow ruby install` creates shims in `~/.local/share/wow/shims/`
- Shims: `ruby`, `irb`, `gem`, `bundle`, `rake`, `rdoc`, `ri`, `erb`
- Each shim is a **symlink to wow.com** with argv[0] dispatch (Kimi review: preferred over tiny shell scripts — one binary, clean, same approach uv uses)
- Shim reads `.ruby-version` (walk up directory tree, like rbenv)
- **Symlink gotcha:** if project is reached via a symlink, wow walks the symlink's directory, not its target. Document this behaviour.
- Dispatches to the correct Ruby in `~/.local/share/wow/ruby/`
- User adds `~/.local/share/wow/shims` to PATH (once, in .bashrc)

**Verify:**
```bash
export PATH="$HOME/.local/share/wow/shims:$PATH"
cd greg
ruby --version    # → ruby 4.0.1
which ruby        # → ~/.local/share/wow/shims/ruby
cd /tmp
ruby --version    # → system ruby (or "no .ruby-version found")
```
