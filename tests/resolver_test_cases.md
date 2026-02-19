# PubGrub Resolver Test Cases

Complete test case definitions for `tests/resolver_test.c`.

## Level 1: Trivial Cases (3 tests)

### test_001_single_no_deps
```c
Universe: A 1.0.0 (no deps)
Root: A >= 0
Expected: A=1.0.0
```

### test_002_single_specific_version
```c
Universe: A 1.0.0, A 2.0.0, A 3.0.0
Root: A = 2.0.0
Expected: A=2.0.0
```

### test_003_no_matching_version
```c
Universe: A 1.0.0, A 2.0.0
Root: A >= 3.0.0
Expected: CONFLICT
```

## Level 2: Linear Chains (3 tests)

### test_004_linear_chain
```c
Universe:
  A 1.0.0 → B >= 1.0
  B 1.0.0 → C >= 1.0
  C 1.0.0 (no deps)
Root: A >= 0
Expected: A=1.0.0, B=1.0.0, C=1.0.0
```

### test_005_linear_multiple_versions
```c
Universe:
  A 1.0.0 → B >= 1.0
  A 2.0.0 → B >= 2.0
  B 1.0.0 → C >= 1.0
  B 2.0.0 → C >= 2.0
  C 1.0.0, C 2.0.0, C 3.0.0
Root: A >= 0
Expected: A=2.0.0, B=2.0.0, C=3.0.0
```

### test_006_linear_chain_failure
```c
Universe:
  A 1.0.0 → B >= 2.0
  B 1.0.0 → C >= 1.0
  B 2.0.0 → C >= 3.0
  C 1.0.0, C 2.0.0
Root: A >= 0
Expected: CONFLICT (C>=3.0 unavailable)
```

## Level 3: Diamond Dependencies (3 tests)

### test_007_diamond_compatible
```c
Universe:
  A 1.0.0 → B >= 1.0, C >= 1.0
  B 1.0.0 → D >= 1.0
  C 1.0.0 → D >= 1.0
  D 1.0.0, D 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=1.0.0, C=1.0.0, D=2.0.0
```

### test_008_diamond_incompatible ⭐
```c
Universe:
  A 1.0.0 → B >= 1.0, C >= 1.0
  B 1.0.0 → D >= 2.0
  C 1.0.0 → D < 2.0
  D 1.0.0, D 2.0.0
Root: A >= 0
Expected: CONFLICT (B needs D>=2.0, C needs D<2.0)
```

### test_009_diamond_partial_overlap
```c
Universe:
  A 1.0.0 → B >= 1.0, C >= 1.0
  B 1.0.0 → D >= 1.0, < 3.0
  C 1.0.0 → D >= 2.0, < 4.0
  D 1.0.0, D 2.0.0, D 2.5.0, D 3.0.0, D 4.0.0
Root: A >= 0
Expected: A=1.0.0, B=1.0.0, C=1.0.0, D=2.5.0
```

## Level 4: Backtracking (3 tests)

### test_010_simple_backtrack ⭐
```c
Universe:
  A 1.0.0 → B >= 2.0
  A 2.0.0 → B >= 1.0, < 2.0
  B 1.0.0 (no deps)
  B 2.0.0 → C >= 3.0
  C 1.0.0, C 2.0.0
Root: A >= 0
Expected: A=2.0.0, B=1.0.0 (A=1.0.0 fails)
```

### test_011_deep_backtrack
```c
Universe:
  A 1.0.0 → B >= 1.0
  A 2.0.0 → B >= 1.0
  B 1.0.0 → C >= 2.0
  B 2.0.0 → C >= 1.0, < 2.0
  C 1.0.0 → D >= 1.0
  C 2.0.0 → D >= 2.0
  D 1.0.0, D 2.0.0
Root: A >= 0
Expected: A=2.0.0, B=2.0.0, C=1.0.0, D=1.0.0 or D=2.0.0
```

### test_012_multiple_backtrack_levels
```c
Universe:
  A 1.0.0 → B >= 1.0, C >= 1.0
  B 1.0.0 → D >= 2.0
  B 2.0.0 → D >= 1.0, < 2.0
  C 1.0.0 → D >= 2.0
  C 2.0.0 → D >= 1.0, < 2.0
  D 1.0.0, D 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=2.0.0, C=2.0.0, D=1.0.0
```

## Level 5: Constraint Types (4 tests)

### test_013_pessimistic_simple ⭐
```c
Universe:
  A 1.0.0 → B ~> 1.2  (means >= 1.2, < 2.0)
  B 1.1.0, B 1.2.0, B 1.9.0, B 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=1.9.0
```

### test_014_pessimistic_deep
```c
Universe:
  A 1.0.0 → B ~> 1.2.3  (means >= 1.2.3, < 1.3.0)
  B 1.2.2, B 1.2.3, B 1.2.9, B 1.3.0
Root: A >= 0
Expected: A=1.0.0, B=1.2.9
```

### test_015_not_equal_constraint
```c
Universe:
  A 1.0.0 → B != 2.0.0
  B 1.0.0, B 2.0.0, B 3.0.0
Root: A >= 0
Expected: A=1.0.0, B=3.0.0
```

### test_016_multiple_constraints_same_dep
```c
Universe:
  A 1.0.0 → B >= 1.0, B < 3.0, B != 2.0.0
  B 1.0.0, B 2.0.0, B 2.5.0, B 3.0.0
Root: A >= 0
Expected: A=1.0.0, B=2.5.0
```

## Level 6: Pre-release Handling (3 tests)

### test_017_prerelease_not_auto ⭐
```c
Universe:
  A 1.0.0 → B >= 1.0
  B 1.0.0, B 2.0.0.beta
Root: A >= 0
Expected: A=1.0.0, B=1.0.0 (NOT 2.0.0.beta)
```

### test_018_prerelease_explicit_ok
```c
Universe:
  A 1.0.0 → B >= 2.0.0.beta
  B 1.0.0, B 2.0.0.beta, B 2.0.0.beta2, B 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=2.0.0 (stable beats prerelease)
```

### test_019_prerelease_ordering
```c
Universe:
  A 1.0.0 → B ~> 2.0.0.beta
  B 2.0.0.beta, B 2.0.0.beta2, B 2.0.0.rc1, B 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=2.0.0
```

## Level 7: Multiple Root Dependencies (3 tests)

### test_020_multiple_roots_compatible
```c
Universe: A 1.0.0, B 1.0.0, C 1.0.0
Root: A >= 0, B >= 0, C >= 0
Expected: A=1.0.0, B=1.0.0, C=1.0.0
```

### test_021_multiple_roots_shared_transitive
```c
Universe:
  A 1.0.0 → C >= 1.0
  B 1.0.0 → C >= 1.0
  C 1.0.0, C 2.0.0
Root: A >= 0, B >= 0
Expected: A=1.0.0, B=1.0.0, C=2.0.0
```

### test_022_multiple_roots_conflict
```c
Universe:
  A 1.0.0 → C >= 2.0
  B 1.0.0 → C < 2.0
  C 1.0.0, C 2.0.0
Root: A >= 0, B >= 0
Expected: CONFLICT
```

## Level 8: Real-World Patterns (2 tests)

### test_023_rails_like
```c
Universe:
  rails 6.0.0 → activerecord ~> 6.0.0, actionpack ~> 6.0.0
  activerecord 6.0.0 → activesupport ~> 6.0.0
  actionpack 6.0.0 → activesupport ~> 6.0.0
  activesupport 6.0.0, 6.0.1
Root: rails >= 0
Expected: rails=6.0.0, activerecord=6.0.0, actionpack=6.0.0, activesupport=6.0.1
```

### test_024_devise_like_with_optional
```c
Universe:
  devise 4.0.0 → ORM_ADAPTER >= 0.1, RAILS >= 4.0
  orm_adapter 0.1.0, 0.5.0
  rails 4.0.0 → activesupport ~> 4.0
  rails 5.0.0 → activesupport ~> 5.0
  activesupport 4.0.0, 5.0.0
Root: devise >= 0, rails >= 5.0
Expected: devise=4.0.0, orm_adapter=0.5.0, rails=5.0.0, activesupport=5.0.0
```

## Level 9: Edge Cases (4 tests)

### test_025_circular_dependency
```c
Universe:
  A 1.0.0 → B >= 1.0
  B 1.0.0 → A >= 1.0
Root: A >= 0
Expected: A=1.0.0, B=1.0.0 (should still resolve)
```

### test_026_self_dependency
```c
Universe:
  A 1.0.0 → A >= 1.0
Root: A >= 0
Expected: A=1.0.0
```

### test_027_version_range_exhaustion
```c
Universe:
  A 1.0.0 → B >= 1.0, < 2.0
  B 1.0.0 → C >= 1.0
  B 1.1.0 → C >= 2.0
  B 1.2.0 → C >= 3.0
  C 1.0.0, C 2.0.0
Root: A >= 0
Expected: A=1.0.0, B=1.1.0, C=2.0.0 (B=1.2.0 fails, B=1.1.0 works)
```

### test_028_deep_transitive_fanout
```c
Universe:
  A 1.0.0 → B1 >= 1.0, B2 >= 1.0, B3 >= 1.0, B4 >= 1.0, B5 >= 1.0
  B1-B5 1.0.0 → C >= 1.0
  C 1.0.0, C 2.0.0
Root: A >= 0
Expected: All B's at 1.0.0, C=2.0.0
```

## Level 10: Stress Tests (3 tests)

### test_029_many_versions
```c
Universe:
  A 1.0.0 → B ~> 1.0
  B 1.0.0 through B 1.99.0 (100 versions)
Root: A >= 0
Expected: A=1.0.0, B=1.99.0 (pick highest efficiently)
```

### test_030_wide_dependency_tree
```c
Universe:
  root → P1 >= 1.0, P2 >= 1.0, ..., P50 >= 1.0 (50 direct deps)
  P1-P50 1.0.0 → SHARED >= 1.0
  SHARED 1.0.0 through 5.0.0
Root: P1, P2, ..., P50
Expected: All P's at 1.0.0, SHARED=5.0.0
```

### test_031_very_deep_chain
```c
Universe:
  P0 1.0.0 → P1 >= 1.0
  P1 1.0.0 → P2 >= 1.0
  ... (chain of 100 packages)
  P99 1.0.0 (no deps)
Root: P0 >= 0
Expected: All P0-P99 at 1.0.0
```

---

## Test Case Template

For each test, use this pattern in `resolver_test.c`:

```c
/* Level N: Description */
static struct test_dep tNNN_X_deps[] = {{"DEP", ">= 1.0"}};
static struct test_pkg_ver tNNN_X_vers[] = {
    {"1.0.0", tNNN_X_deps, 1},
    {"2.0.0", NULL, 0}
};
static struct test_pkg tNNN_X_pkgs[] = {
    {"PKG", tNNN_X_vers, 2}
};
static struct test_dep tNNN_X_roots[] = {{"PKG", ">= 0"}};
static struct expect_pkg tNNN_X_exp[] = {{"PKG", "2.0.0"}};

static void test_NNN_name(void) {
    struct test_case tc = {
        .name = "test_NNN_name",
        .description = "What this test checks",
        .universe = tNNN_X_pkgs,
        .n_packages = N,
        .roots = tNNN_X_roots,
        .n_roots = M,
        .expect_type = EXPECT_OK,  /* or EXPECT_CONFLICT */
        .expect.ok = {tNNN_X_exp, K}  /* or .expect.conflict_msg = "..." */
    };
    run_test(&tc);
}
```

## Implementation Status

| Test | Implemented | Notes |
|------|-------------|-------|
| test_001 | ✅ | Single package, no deps |
| test_008 | ✅ | Diamond incompatible (classic conflict) |
| test_010 | ✅ | Simple backtracking |
| test_013 | ✅ | Pessimistic constraint |
| test_017 | ✅ | Pre-release not auto-selected |
| test_002-007 | ⬜ | Trivial + linear + compatible diamond |
| test_009 | ⬜ | Diamond partial overlap |
| test_011-012 | ⬜ | Deep backtracking |
| test_014-016 | ⬜ | More constraint types |
| test_018-019 | ⬜ | Pre-release explicit |
| test_020-024 | ⬜ | Multiple roots, real-world |
| test_025-028 | ⬜ | Edge cases |
| test_029-031 | ⬜ | Stress tests |
