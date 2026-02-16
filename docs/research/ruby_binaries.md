# Phase 0d: Ruby Pre-Built Binaries Research

> Research deliverable for wow (uv for Ruby in C). Documents available pre-built Ruby binary sources, URL patterns, and verification strategies.

## Table of Contents

1. [Overview](#overview)
2. [Binary Sources](#binary-sources)
3. [URL Patterns](#url-patterns)
4. [Platforms and Architectures](#platforms-and-architectures)
5. [Archive Format](#archive-format)
6. [SHA-256 Verification](#sha-256-verification)
7. [Recommendations for wow](#recommendations-for-wow)

---

## Overview

For wow to install Ruby versions without compilation, we need reliable pre-built binary sources. This document evaluates available sources, documents their URL patterns, and outlines verification strategies.

**Key Question:** Should wow download pre-built binaries or compile from source?

**Answer for MVP:** Binary-only approach. The MASTER_PLAN says "download Ruby" not "compile Ruby". Compiling from source requires a C compiler, make, autoconf, and build dependencies (openssl-dev, libyaml-dev, etc.) — that's exactly the kind of heavyweight toolchain wow is supposed to eliminate.

- **MVP:** Pre-built binaries with clear error if no binary is available for the platform
- **Post-MVP:** Source compilation as fallback for unsupported platforms

---

## Binary Sources

### 1. ruby/ruby-builder (GitHub Releases)

**Primary source for GitHub Actions setups.** Maintained by the Ruby core team.

**Repository:** `https://github.com/ruby/ruby-builder`

**Release Tags:**
- `toolcache` — Contains CRuby binaries for GitHub Actions
- `ruby-X.Y.Z` — Individual Ruby releases
- `jruby-X.Y.Z` — JRuby releases
- `truffleruby-X.Y.Z` — TruffleRuby releases

**Asset Naming Pattern:**
```
ruby-{version}-{platform}.tar.gz
```

**Example Assets (ruby-3.3.6):**
```
ruby-3.3.6-macos-13-arm64.tar.gz
ruby-3.3.6-macos-latest.tar.gz
ruby-3.3.6-ubuntu-22.04-arm64.tar.gz
ruby-3.3.6-ubuntu-22.04.tar.gz
ruby-3.3.6-ubuntu-24.04-arm64.tar.gz
ruby-3.3.6-ubuntu-24.04.tar.gz
```

**Download URL Pattern:**
```
https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-{version}-{platform}.tar.gz
```

**Pros:**
- ✅ Official Ruby core team builds
- ✅ Wide version coverage (2.x through 3.4+, JRuby, TruffleRuby)
- ✅ GitHub CDN (fast, reliable)

**Cons:**
- ❌ Limited platform matrix (only GitHub Actions runners)
- ❌ No SHA-256 in release metadata (must compute after download)
- ❌ Linux binaries are Ubuntu-specific (glibc version issues)
- ❌ Archive has `x64/` prefix that must be stripped during extraction

---

### 2. ruby-build Version Catalogue (rbenv)

**Machine-readable list of available Ruby versions.**

**URL Pattern:**
```
https://github.com/rbenv/ruby-build/tree/master/share/ruby-build/
```

**API for listing:**
```bash
# List all available versions
curl -s "https://api.github.com/repos/rbenv/ruby-build/contents/share/ruby-build" | \
  jq -r '.[] | select(.type == "file") | .name'
```

**Example version file (3.3.6):**
```
install_package "openssl-3.0.18" \
  "https://github.com/openssl/openssl/releases/download/openssl-3.0.18/openssl-3.0.18.tar.gz#d80c34f5cf902dccf1f1b5df5ebb86d0392e37049e5d73df1b3abae72e4ffe8b" \
  openssl --if needs_openssl:1.0.2-3.x.x
install_package "ruby-3.3.6" \
  "https://cache.ruby-lang.org/pub/ruby/3.3/ruby-3.3.6.tar.gz#8dc48fffaf270f86f1019053f28e51e4da4cce32a36760a0603a9aee67d7fd8d" \
  enable_shared standard
```

**Use for wow:**
- Scrape version list to know which Ruby versions are available
- Extract SHA-256 checksums for source tarballs (post-MVP)
- Not needed for binary downloads (ruby-builder has different checksums)

---

### 3. Official Ruby-Lang.org (Source)

**Canonical source for Ruby releases.** Always available, always trusted.

**URL Pattern:**
```
https://cache.ruby-lang.org/pub/ruby/{major}.{minor}/ruby-{version}.tar.gz
```

**Examples:**
```
https://cache.ruby-lang.org/pub/ruby/3.3/ruby-3.3.6.tar.gz
https://cache.ruby-lang.org/pub/ruby/3.4/ruby-3.4.2.tar.gz
```

**SHA-256 Verification:**
- Published on ruby-lang.org downloads page
- Embedded in ruby-build scripts
- Example: `8dc48fffaf270f86f1019053f28e51e4da4cce32a36760a0603a9aee67d7fd8d`

**Note:** For MVP, we don't compile from source. This is documented for post-MVP reference.

---

### 4. Other Potential Sources

| Source | Type | Notes |
|--------|------|-------|
| [fullstaq-ruby](https://github.com/fullstaq/fullstaq-ruby-server) | Binary | Optimized for Docker, limited versions |
| [docker-library/ruby](https://hub.docker.com/_/ruby) | Docker | Not directly usable for wow |
| [ruby-install](https://github.com/postmodern/ruby-install) | Build tool | Alternative to ruby-build |

---

## URL Patterns

### Pre-Built Binaries (ruby-builder)

```
https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-{VERSION}-{PLATFORM}.tar.gz
```

| Placeholder | Values |
|-------------|--------|
| `{VERSION}` | `3.3.6`, `3.4.2`, etc. |
| `{PLATFORM}` | `ubuntu-22.04`, `ubuntu-24.04`, `macos-latest`, `macos-13-arm64`, etc. |

### Source Tarballs (Post-MVP)

```
https://cache.ruby-lang.org/pub/ruby/{MAJOR}.{MINOR}/ruby-{VERSION}.tar.gz
```

| Placeholder | Example |
|-------------|---------|
| `{MAJOR}.{MINOR}` | `3.3`, `3.4` |
| `{VERSION}` | `3.3.6`, `3.4.2` |

---

## Platforms and Architectures

### Supported by ruby-builder

| OS | Version | Architectures |
|----|---------|---------------|
| Ubuntu | 22.04 | x86_64, arm64 |
| Ubuntu | 24.04 | x86_64, arm64 |
| macOS | 13 (Ventura) | arm64 (Apple Silicon) |
| macOS | latest | x86_64 |

### Platform Mapping (Host → Binary)

```
Linux x86_64 (glibc 2.35+) → ubuntu-22.04 / ubuntu-24.04
Linux arm64 (glibc 2.35+)  → ubuntu-22.04-arm64 / ubuntu-24.04-arm64
macOS x86_64               → macos-latest
macOS arm64 (M1/M2/M3)     → macos-13-arm64
```

**⚠️ Compatibility Warning:**
- Ubuntu binaries require compatible glibc version
- Ubuntu 22.04 = glibc 2.35
- Ubuntu 24.04 = glibc 2.39
- Binaries may not work on older distros (Debian 11, RHEL 8, etc.)

### Platform Detection (POSIX/uname)

```bash
# Operating System
uname -s  # Linux, Darwin

# Machine architecture  
uname -m  # x86_64, aarch64, arm64

# libc detection (Linux only)
ldd --version  # Look for "glibc" or "musl"
```

**Mapping to wow platform identifier:**

| uname -s | uname -m | ldd | wow platform |
|----------|----------|-----|--------------|
| Linux | x86_64 | glibc | `linux-x86_64-gnu` |
| Linux | aarch64 | glibc | `linux-arm64-gnu` |
| Linux | x86_64 | musl | `linux-x86_64-musl` |
| Darwin | x86_64 | N/A | `darwin-x86_64` |
| Darwin | arm64 | N/A | `darwin-arm64` |

---

## Archive Format

### ruby-builder Tarball Structure

The ruby-builder tarballs have a **prefix directory** that must be stripped during extraction.

**Structure:**
```
ruby-3.3.6-ubuntu-22.04.tar.gz
├── x64/                          ← Prefix directory to strip
│   ├── bin/
│   │   ├── ruby
│   │   ├── gem
│   │   ├── bundle
│   │   └── ...
│   ├── include/
│   │   └── ruby-3.3.0/
│   ├── lib/
│   │   ├── ruby/
│   │   └── libruby.so
│   └── share/
│       └── man/
```

**Extraction Strategy:**
```bash
# Extract and strip the x64/ prefix
tar -xzf ruby-3.3.6-ubuntu-22.04.tar.gz --strip-components=1 -C /opt/wow/ruby/3.3.6/

# Result:
/opt/wow/ruby/3.3.6/
├── bin/
├── include/
├── lib/
└── share/
```

**C Implementation Note:**
When extracting, skip the first path component (`x64/`) and extract remaining paths relative to the destination prefix.

---

## SHA-256 Verification

### Source Tarballs (ruby-build)

**From ruby-build (trusted):**
```
ruby-3.3.6: 8dc48fffaf270f86f1019053f28e51e4da4cce32a36760a0603a9aee67d7fd8d
ruby-3.4.2: 41328ac21f2bfdd7de6b3565ef4f0dd7543cc385bd05312b048ecd86d13f78a8
```

**From ruby-lang.org (verified):**
- Checksums published on: https://www.ruby-lang.org/en/downloads/

### Pre-Built Binaries

**Problem:** ruby-builder releases do **not** include SHA-256 checksums in metadata.

**Solutions:**

1. **Trust-on-first-use (TOFU) — Recommended for MVP:**
   - Download binary
   - Compute SHA-256 after download
   - Store in wow's local cache for future verification
   - On subsequent installs, verify against cached checksum

2. **Verify GitHub release signature:**
   - GitHub signs releases with their key
   - Use GitHub API to verify asset integrity
   - Overkill for MVP

3. **Skip verification:**
   - HTTPS provides transport security
   - Accept the risk for MVP

**Recommended approach:**
```
First install:
  1. Download from ruby-builder
  2. Compute SHA-256
  3. Store in ~/.cache/wow/ruby/checksums/ruby-3.3.6-linux-x86_64.sha256
  4. Extract and use

Subsequent installs:
  1. Check for cached checksum
  2. Download from ruby-builder
  3. Verify SHA-256 matches cached value
  4. If mismatch → re-download or error
```

---

## Recommendations for wow

### MVP Implementation

**Binary-only approach:**
```
1. Detect platform (linux-x86_64-gnu, darwin-arm64, etc.)
2. Map to ruby-builder platform identifier
3. Download: https://github.com/ruby/ruby-builder/releases/...
4. Compute/verify SHA-256 (TOFU)
5. Extract with --strip-components=1
6. If 404/failure → Clear error: "No binary available for your platform"
```

**Platform support matrix for MVP:**

| Platform | ruby-builder ID | Priority |
|----------|-----------------|----------|
| Ubuntu 22.04+ x86_64 | `ubuntu-22.04` | **High** |
| Ubuntu 22.04+ arm64 | `ubuntu-22.04-arm64` | **High** |
| macOS x86_64 | `macos-latest` | **High** |
| macOS arm64 | `macos-13-arm64` | **High** |
| Other Linux | — | Error for MVP |

### Data Structures

**Ruby Version Metadata (JSON):**
```json
{
  "version": "3.3.6",
  "binaries": [
    {
      "platform": "linux-x86_64-gnu",
      "url": "https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-3.3.6-ubuntu-22.04.tar.gz",
      "strip_components": 1
    },
    {
      "platform": "darwin-arm64",
      "url": "https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-3.3.6-macos-13-arm64.tar.gz",
      "strip_components": 1
    }
  ]
}
```

**Platform Detection (C pseudocode):**
```c
typedef struct {
    char* os;        // "linux", "darwin"
    char* arch;      // "x86_64", "arm64", "aarch64"
    char* libc;      // "gnu", "musl", null (darwin)
} platform_t;

platform_t detect_platform() {
    struct utsname buf;
    uname(&buf);
    
    // Normalize architecture
    if (strcmp(buf.machine, "aarch64") == 0) 
        arch = "arm64";
    else 
        arch = buf.machine;  // x86_64, etc.
    
    // Detect libc (Linux only)
    if (strcmp(buf.sysname, "Linux") == 0) {
        libc = detect_libc();  // gnu or musl
    }
}

char* map_to_ruby_builder(platform_t p) {
    if (strcmp(p.os, "linux") == 0 && strcmp(p.libc, "gnu") == 0) {
        if (strcmp(p.arch, "x86_64") == 0) return "ubuntu-22.04";
        if (strcmp(p.arch, "arm64") == 0) return "ubuntu-22.04-arm64";
    }
    if (strcmp(p.os, "darwin") == 0) {
        if (strcmp(p.arch, "x86_64") == 0) return "macos-latest";
        if (strcmp(p.arch, "arm64") == 0) return "macos-13-arm64";
    }
    return NULL;  // Not supported
}
```

### Installation Flow

```
┌─────────────────────────────────────────────────────────────┐
│ wow install ruby 3.3.6                                      │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Detect platform (linux-x86_64-gnu)                       │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Check cache for ruby-3.3.6-linux-x86_64-gnu              │
│    If found → Done                                          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Map platform to ruby-builder identifier                  │
│    linux-x86_64-gnu → ubuntu-22.04                          │
│    If no mapping → Error: "Platform not supported"          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Download pre-built binary                                │
│    URL: github.com/ruby/ruby-builder/releases/...           │
│    If 404 → Error: "Ruby 3.3.6 not available for ubuntu"    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. Verify SHA-256 (TOFU)                                    │
│    - Check cached checksum                                  │
│    - If none, compute and cache                             │
│    - If mismatch, retry once then error                     │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. Extract with strip-components=1                          │
│    - Strip x64/ prefix                                      │
│    - Extract to ~/.wow/ruby/3.3.6/                          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. Write version metadata                                   │
│    - Record installed version                               │
│    - Cache checksum for verification                        │
└─────────────────────────────────────────────────────────────┘
```

### Security Considerations

| Concern | Mitigation |
|---------|------------|
| Binary tampering | TOFU checksums, verify GitHub HTTPS cert |
| Supply chain | Use official sources (ruby/ruby-builder) |
| Build reproducibility | Document build flags used by ruby-builder |
| Malicious gems | SHA-256 verification via compact index |

---

## References

- [ruby/ruby-builder](https://github.com/ruby/ruby-builder) — Pre-built binaries
- [rbenv/ruby-build](https://github.com/rbenv/ruby-build) — Build scripts and version catalogue
- [ruby-lang.org downloads](https://www.ruby-lang.org/en/downloads/) — Official source tarballs
- [fullstaq-ruby](https://github.com/fullstaq/fullstaq-ruby-server) — Alternative binary source
