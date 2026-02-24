# Phase 6 Demos: PubGrub Dependency Resolver

These demos showcase wow's PubGrub-based dependency resolver and demonstrate **parity with Bundler** — wow produces identical resolution results and lockfile format.

## Overview

| Demo | File | Description |
|------|------|-------------|
| 6a | `demo_version.com` | Version parsing & constraint matching |
| 6b | `demo_pubgrub.com` | Core PubGrub algorithm (hardcoded data) |
| 6c | `demo_resolve.com` | Resolve real gems from rubygems.org |
| 6d | `demo_lock.com` | Generate Bundler-compatible Gemfile.lock |

## Quick Start

```bash
# Build all demos (requires main 'make' first)
make -C demos/phase6

# Run all demos
./run_demos.sh

# Run individual demos
./run_demos.sh 6a       # Version parsing
./run_demos.sh 6b       # PubGrub algorithm
./run_demos.sh 6c       # Real gem resolution
./run_demos.sh 6d       # Lockfile generation
./run_demos.sh compare  # Bundler comparison tests
```

## Demo Details

### Demo 6a: Version Parsing (`demo_version.com`)

Tests Ruby gem version constraint satisfaction:

```bash
./demo_version.com
```

**Demonstrates:**
- Segment-based version parsing (`4.1.1`, `3.0.0.beta.2`)
- Constraint operators: `=`, `!=`, `>`, `>=`, `<`, `<=`, `~>`
- Pessimistic operator: `~> 4.0` means `>= 4.0, < 5.0`
- Prerelease semantics: `4.1.1.pre < 4.1.1`

### Demo 6b: PubGrub Core (`demo_pubgrub.com`)

Demonstrates the PubGrub resolution algorithm with hardcoded dependency graphs:

```bash
./demo_pubgrub.com              # Run all tests
./demo_pubgrub.com chain        # Simple chain A → B → C
./demo_pubgrub.com diamond      # Diamond A → B,C → D
./demo_pubgrub.com conflict     # Unsatisfiable (human-readable error)
./demo_pubgrub.com versions     # Version selection
```

**Demonstrates:**
- Unit propagation (forced assignments from constraints)
- Decision making (pick highest compatible version)
- Conflict resolution with human-readable explanations
- Backtracking when contradictions found

### Demo 6c: Registry Resolution (`demo_resolve.com`)

Resolves actual gem dependencies from rubygems.org:

```bash
./demo_resolve.com sinatra           # Resolve sinatra deps
./demo_resolve.com rack 3.1.12       # Specific version
./demo_resolve.com rails             # Large dependency tree
```

**Demonstrates:**
- Real-time registry API queries
- On-demand dependency graph building
- Resolution tree display
- Lockfile format preview

### Demo 6d: Lockfile Generation (`demo_lock.com`)

Generates Bundler-compatible Gemfile.lock:

```bash
./demo_lock.com Gemfile              # Read Gemfile, output lock
./demo_lock.com Gemfile Gemfile.lock # Write to file
```

**Output format** (identical to Bundler):
```
GEM
  remote: https://rubygems.org/
  specs:
    rack (3.1.12)
    sinatra (4.1.1)
      mustermann (~> 3.0)
      rack (~> 3.0)
      ...

PLATFORMS
  ruby

DEPENDENCIES
  sinatra (~> 4.0)!

BUNDLED WITH
   wow-0.1.0
```

## Bundler Parity

The key promise: **wow behaves identically to Bundler for resolution.**

### Same Input → Same Output

| Input | Bundler | wow |
|-------|---------|-----|
| `Gemfile` | `bundle lock` | `wow lock` |
| Constraints | `~> 4.0`, `>= 3.0` | Same |
| Resolution order | Alphabetical | Alphabetical |
| Lockfile format | `GEM`, `PLATFORMS`, `DEPENDENCIES` | Identical |

### Test Fixtures

The `fixtures/` directory contains test cases:

- **`simple/`** — Single gem, no dependencies
- **`diamond/`** — Diamond dependency pattern (A → B,C → D)
- **`conflict/`** — Intentional conflict (tests error messages)
- **`transitive/`** — Deep transitive dependency chain

Run with Bundler and wow, compare outputs — they match.

## PubGrub vs Molinillo

Bundler uses **Molinillo** (backtracking resolver). wow uses **PubGrub** (SAT-solver-inspired).

| Feature | Molinillo (Bundler) | PubGrub (wow) |
|---------|---------------------|---------------|
| Algorithm | Backtracking search | Unit propagation + CDCL |
| Performance | Can explore dead ends | Eliminates impossible versions early |
| Error messages | "Could not find compatible versions" | "Because X depends on Y, and Z depends on not-Y..." |

The human-readable conflict explanation is PubGrub's killer feature.

## Building

```bash
# From project root
make

# Build Phase 6 demos
make -C demos/phase6

# Or build individually
cd demos/phase6
make demo_version.com
make demo_pubgrub.com
make demo_resolve.com
make demo_lock.com
```

## Files

| File | Purpose |
|------|---------|
| `Makefile` | Build configuration |
| `demo_version.c` | Version constraint demo |
| `demo_pubgrub.c` | PubGrub algorithm demo |
| `demo_resolve.c` | Registry resolution demo |
| `demo_lock.c` | Lockfile generation demo |
| `run_demos.sh` | Demo runner script |
| `fixtures/` | Test fixtures for parity testing |
| `README.md` | This file |
