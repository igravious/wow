# Phase 6: PubGrub Resolver

> SAT-solver-inspired dependency resolution. The algorithm that makes wow fast.

## Background

PubGrub was invented by the Dart/pub team. It's fundamentally better than Bundler's Molinillo backtracking algorithm:
- **Unit propagation** eliminates impossible versions early
- **Conflict-driven learning** avoids re-exploring dead ends
- **Human-readable error messages** explain WHY resolution failed

Reference: https://nex3.medium.com/pubgrub-2fb6470504f

## 6a: Version Parsing + Constraint Matching

**Demo:** Unit-test-style output showing version matching.

**Files:**
- `src/version.c`
- `include/wow/version.h`

**Implementation:**
- Parse gem version strings: `4.1.1`, `3.0.0.beta.2`, `4.0.1.pre`
- Version comparison (gem versions are segment-based, not semver)
- Constraint matching:
  - `= 4.1.1` — exact match
  - `!= 4.1.1` — exclude
  - `> 4.0`, `>= 4.0`, `< 5.0`, `<= 5.0` — comparisons
  - `~> 4.1` — pessimistic (>= 4.1, < 5.0)
  - `~> 4.1.1` — pessimistic (>= 4.1.1, < 4.2.0)
- Multiple constraints (AND): `>= 3.0.0, < 4`

**Verify:**
```bash
./build/wow.com version-test
# 4.1.1 satisfies ~> 4.0     → true
# 5.0.0 satisfies ~> 4.0     → false
# 3.2.1 satisfies >= 3.0, < 4 → true
# 4.0.0 satisfies >= 3.0, < 4 → false
# 4.1.1.pre satisfies ~> 4.1  → true
```

## 6b: PubGrub Core with Hardcoded Data

**Demo:** Resolve a small dependency graph with hardcoded packages.

**Files:**
- `src/pubgrub.c`
- `include/wow/pubgrub.h`

**Memory management — arena allocation (Kimi review):**

PubGrub's conflict-driven learning builds an incompatibility graph that can grow large. All incompatibilities have the same lifetime (the resolution run), so arena allocation is the right approach:
- Allocate incompatibilities, terms, and version ranges from a single arena
- No individual frees during resolution — bulk free at end
- Reference counting not needed — arena lifetime handles it
- Acceptable for MVP: if the arena approach slips, "leak during resolution, free at end" is functionally identical

**Key data structures:**
```c
/* A term: package + version range + positive/negative */
typedef struct {
    char *package;
    wow_version_range range;
    bool positive;  /* true = must be in range, false = must NOT be */
} wow_term;

/* An incompatibility: a set of terms that cannot all be true */
typedef struct {
    wow_term *terms;
    int n_terms;
    /* cause: either "dependency" or "conflict from other incompatibility" */
} wow_incompatibility;

/* Partial solution: assigned package versions */
typedef struct {
    /* ... assignments, decision levels, etc. */
} wow_partial_solution;
```

**Algorithm outline:**
1. Start with root package's dependencies as initial incompatibilities
2. **Unit propagation**: derive forced assignments from incompatibilities
3. **Decision making**: pick the next package to decide, choose a version
4. **Conflict resolution**: if contradiction found, analyse conflict, learn new incompatibility, backtrack
5. Repeat until all packages resolved or conflict is irreconcilable

**Error messages are the whole point (Kimi review):** The user-visible win of PubGrub over Bundler's backtracking is human-readable conflict explanations. Do NOT skip these. When resolution fails, the error must explain the conflict chain:
```
Because sinatra >= 4.0 depends on rack >= 3.0
  and your-legacy-gem depends on rack < 3.0,
  sinatra >= 4.0 is incompatible with your-legacy-gem.
```
This is why we chose PubGrub. A bare "version solving failed" message defeats the purpose.

**Hardcoded test:** packages A, B, C with known deps, verify correct resolution.

**Verify:**
```bash
./build/wow.com pubgrub-test
# Resolving...
#   A 1.0.0 depends on B >= 1.0
#   B 1.1.0 depends on C ~> 2.0
#   C 2.3.0 (no deps)
# Solution: A=1.0.0, B=1.1.0, C=2.3.0
```

## 6c: PubGrub Wired to Registry

**Demo:** `./wow.com resolve sinatra` resolves sinatra's full dependency tree from rubygems.org.

**Implementation:**
- Package version provider: fetches from registry (Phase 2) on demand
- Cache fetched metadata in memory (sqlite3 cache later)
- Feed into PubGrub solver
- Print the resolution tree

**Verify:**
```bash
./build/wow.com resolve sinatra
# Resolved 5 packages:
#   sinatra 4.1.1
#   mustermann 3.0.3
#   rack 3.1.12
#   rack-session 2.1.0
#   tilt 2.6.0
```

## 6d: Generate Gemfile.lock

**Demo:** `./wow.com lock Gemfile` produces a Bundler-compatible Gemfile.lock.

**Files:**
- `src/lockfile.c`
- `include/wow/lockfile.h`

**Implementation:**
- Take resolved package set from PubGrub
- Write Gemfile.lock in exact Bundler format:
  ```
  GEM
    remote: https://rubygems.org/
    specs:
      mustermann (3.0.3)
        ruby2_keywords (~> 0.0.1)
      rack (3.1.12)
      ...

  PLATFORMS
    ruby

  DEPENDENCIES
    sinatra (~> 4.0)

  BUNDLED WITH
     wow-0.1.0
  ```
- Alphabetical ordering of specs (Bundler convention)
- `BUNDLED WITH` shows `wow-{version}` (Bundler shows its version here)

**Verify:**
```bash
cd testproject
echo 'source "https://rubygems.org"' > Gemfile
echo 'gem "sinatra"' >> Gemfile
../build/wow.com lock Gemfile
cat Gemfile.lock
# (valid Bundler-format lockfile)
```
