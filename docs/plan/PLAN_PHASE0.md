# Phase 0: Research

> No code. Understand the formats and APIs we need to implement.

## 0a: .gem Binary Format

**Demo:** Examine a real .gem file, document its structure.

A `.gem` file is a **tar archive** (not gzip'd at the outer level) containing:

```
sinatra-4.1.1.gem (tar)
├── metadata.gz          # gzip'd YAML gemspec
├── data.tar.gz          # gzip'd tar of the actual gem files
└── checksums.yaml.gz    # gzip'd YAML with SHA-256 checksums
```

**Tasks:**
- Download a .gem manually: `curl -O https://rubygems.org/downloads/sinatra-4.1.1.gem`
- List contents: `tar tf sinatra-4.1.1.gem`
- Extract and examine metadata: `tar xf sinatra-4.1.1.gem metadata.gz && gunzip metadata.gz && cat metadata`
- Extract and list data: `tar xf sinatra-4.1.1.gem data.tar.gz && tar tzf data.tar.gz`
- Document the exact tar format (ustar? gnu? pax?)

**Deliverable:** `docs/research/gem_format.md`

## 0b: Gemfile.lock Format

**Demo:** Examine a real Gemfile.lock, document the text format.

```
GEM
  remote: https://rubygems.org/
  specs:
    rack (3.1.12)
    sinatra (4.1.1)
      mustermann (~> 3.0)
      rack (>= 3.0.0, < 4)
      rack-session (>= 2.0.0, < 3)
      tilt (~> 2.0)

PLATFORMS
  ruby
  x86_64-linux

DEPENDENCIES
  sinatra

BUNDLED WITH
   2.5.22
```

**Tasks:**
- Collect Gemfile.lock samples from several real projects (Rails, Sinatra, small gems)
- Document each section: GEM, PLATFORMS, DEPENDENCIES, RUBY VERSION, BUNDLED WITH
- Document the indentation-based structure (2-space indent for specs, 4-space for deps)
- Note edge cases: git sources, path sources, platform-specific gems

**Deliverable:** `docs/research/lockfile_format.md`

## 0c: rubygems.org API

**Demo:** Document every API endpoint we need.

**Endpoints:**
- `GET /api/v1/gems/{name}.json` — gem info + latest version
- `GET /api/v1/versions/{name}.json` — all versions
- `GET /api/v1/dependencies?gems={name1,name2,...}` — marshalled dependency info (fast, batched)
- Compact index: `GET /info/{name}` — all versions + deps in one text response
- `GET /downloads/{name}-{version}.gem` — download the .gem file

**Tasks:**
- curl each endpoint for sinatra, document the response format
- Determine which endpoint is best for resolution (compact index is what Bundler 2.x uses)
- Document rate limits, caching headers (ETag, Last-Modified)
- Check if rubygems.org supports HTTP/2 or requires HTTP/1.1

**Deliverable:** `docs/research/rubygems_api.md`

## 0d: Ruby Pre-Built Binaries

**Demo:** Document where to download pre-built Ruby.

**Sources:**
- ruby-lang.org source tarballs (require compilation)
- ruby-build definitions (URL patterns for source + pre-built)
- GitHub releases for pre-compiled Rubies (e.g. ruby/ruby-builder)
- User's own Cosmopolitan Ruby builds (`ruby.com`)

**Tasks:**
- Research available pre-built Ruby binary sources
- Document URL patterns: `https://.../{version}/ruby-{version}-{platform}.tar.gz`
- Document supported platforms and architectures
- Determine SHA-256 hash verification strategy
- Consider: should wow download source and compile, or require pre-built binaries?

**Deliverable:** `docs/research/ruby_binaries.md`
