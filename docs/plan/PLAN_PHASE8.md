# Phase 8: End-to-End — wow sync

> The moment of truth. Full workflow: parse Gemfile → resolve → download (parallel) → install to vendor/bundle/.

## 8a: wow sync — Single Gem, No Transitive Deps

**Demo:** `wow sync` on a Gemfile with one simple gem.

**Files:**
- `src/sync.c`
- `include/wow/sync.h`

**Implementation — sync.c orchestrates the full pipeline:**
1. Parse Gemfile (Phase 5 — re2c + lemon)
2. Read Gemfile.lock if it exists (skip resolution if lock is fresh)
3. Resolve dependencies via PubGrub (Phase 6)
4. Diff resolved set against currently installed gems
5. Download missing gems in parallel (Phase 7)
6. Unpack .gem files to `vendor/bundle/ruby/{version}/gems/` (Phase 4)
7. Write/update Gemfile.lock (Phase 6d)
8. Print summary

**Test setup:**
```ruby
# Gemfile
source "https://rubygems.org"
gem "rack"
```

`rack` has zero runtime dependencies — simplest possible case.

**Verify:**
```bash
cd testproject
echo 'source "https://rubygems.org"' > Gemfile
echo 'gem "rack"' >> Gemfile
../build/wow.com sync
# Resolved 1 package in 120ms
# Prepared 1 package in 0.3s
# Installed 1 package in 12ms
#  + rack 3.1.12
cat Gemfile.lock
ls vendor/bundle/ruby/4.0.0/gems/rack-3.1.12/lib/
```

## 8b: wow sync — Sinatra (Full Dep Tree)

**Demo:** `wow sync` on sinatra — multiple transitive dependencies, parallel downloads.

**Test setup:**
```ruby
# Gemfile
source "https://rubygems.org"
gem "sinatra"
```

Sinatra pulls in: mustermann, rack, rack-session, tilt (+ transitive).

**Verify:**
```bash
cd testproject
echo 'source "https://rubygems.org"' > Gemfile
echo 'gem "sinatra"' >> Gemfile
../build/wow.com sync
# Resolved 5 packages in 340ms
# Prepared 5 packages in 0.4s
# Installed 5 packages in 45ms
#  + mustermann 3.0.3
#  + rack 3.1.12
#  + rack-session 2.1.0
#  + sinatra 4.1.1
#  + tilt 2.6.0
cat Gemfile.lock
ls vendor/bundle/ruby/4.0.0/gems/
# mustermann-3.0.3/  rack-3.1.12/  rack-session-2.1.0/  sinatra-4.1.1/  tilt-2.6.0/
```

## 8c: uv-Style Output + Timing

**Demo:** Polish the output to match uv's format.

**Implementation:**
- Timing for each phase: resolution, download, install
- Colour output (if terminal supports it):
  - Green `+` for newly installed
  - Red `-` for removed (on re-sync)
  - Yellow `~` for upgraded
- `wow bundle install` alias dispatches to same code path
- Second run (no changes): `Resolved 5 packages in 2ms` (cached — near-instant)

**Verify — second sync is instant:**
```bash
../build/wow.com sync
# Resolved 5 packages in 2ms
# Audited 5 packages in 1ms
# (no changes)
```

**Verify — bundle install alias:**
```bash
../build/wow.com bundle install
# (same output as wow sync)
```

## 8d: wow run

**Demo:** `wow run ruby -e "require 'sinatra'; puts Sinatra::VERSION"`

**Implementation (updated with Kimi review):**
- Read `.ruby-version` → find managed Ruby in `~/.local/share/wow/ruby/`
- Set `GEM_HOME` and `GEM_PATH` to `vendor/bundle/ruby/{version}/`
- Set `BUNDLE_GEMFILE` to `./Gemfile`
- Set `PATH` to include managed Ruby's `bin/` directory — so `wow run rake` works when rake is a bundled gem with a binstub
- Consider `RUBYOPT=-rubygems` if the managed Ruby doesn't load rubygems by default (most modern Rubies do, but check during Phase 3)
- exec the command with the correct environment

**Verify:**
```bash
../build/wow.com run ruby -e "require 'sinatra'; puts Sinatra::VERSION"
# 4.1.1

# Verify PATH includes managed Ruby bin:
../build/wow.com run which ruby
# ~/.local/share/wow/ruby/ruby-4.0-linux-x86_64/bin/ruby

# Verify bundled gem binstubs work:
../build/wow.com run rake --version
# rake, version 13.x.x
```

## The Full MVP Workflow — Start to Finish

```bash
# Install wow (one binary, anywhere)
curl -LsSf https://wow.dev/install.sh | sh

# Create a project — Ruby downloaded automatically
wow init greg
cd greg

# Edit your Gemfile
vi Gemfile   # add: gem "sinatra"

# Sync — resolve, lock, download (parallel), install
wow sync

# Run
wow run ruby -e "require 'sinatra'; puts 'it works'"

# That's it. No rbenv, no ruby-build, no bundler, no gem.
```
