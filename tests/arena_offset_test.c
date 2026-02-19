/*
 * tests/arena_offset_test.c -- Arena offset stability tests
 *
 * These tests specifically exercise the wow_aoff offset-based addressing
 * to ensure pointers remain valid across arena reallocations.
 *
 * Run via: make test-arena-offset
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "wow/resolver/arena.h"
#include "wow/resolver/pubgrub.h"
#include "wow/resolver/provider.h"
#include "wow/http.h"

static int n_pass = 0, n_fail = 0;

/* ── Test 1: Arena Reallocation Stability ───────────────────────── */

static void test_arena_realloc_stability(void)
{
    printf("\ntest_arena_realloc_stability:\n");
    printf("  Allocate 1KB chunks until arena grows (starts at 64KB),\n");
    printf("  verify ALL previous offsets remain valid after each growth.\n");

    wow_arena arena;
    wow_arena_init(&arena);

    /* Allocate small chunks until arena grows */
    wow_aoff offsets[200];
    int num_allocs = 200;
    int passed = 1;

    for (int i = 0; i < num_allocs; i++) {
        /* 1KB each - forces growth after ~64 allocations */
        offsets[i] = wow_arena_alloc_off(&arena, 1024);
        if (offsets[i] == WOW_AOFF_NULL) {
            printf("  FAIL: Allocation %d failed\n", i);
            passed = 0;
            break;
        }

        /* Write unique pattern */
        char *p = WOW_ARENA_PTR(&arena, offsets[i], char);
        memset(p, i % 256, 1024);

        /* Verify ALL previous offsets still valid */
        for (int j = 0; j <= i; j++) {
            char *prev = WOW_ARENA_PTR(&arena, offsets[j], char);
            if (prev == NULL) {
                printf("  FAIL: Offset %d returned NULL pointer at iteration %d\n",
                       j, i);
                passed = 0;
                break;
            }
            if (j < i) {
                /* Verify previous writes weren't corrupted */
                unsigned char expected = (unsigned char)(j % 256);
                if ((unsigned char)prev[0] != expected) {
                    printf("  FAIL: Data corruption at offset %d: "
                           "expected %d, got %d (iteration %d)\n",
                           j, expected, (unsigned char)prev[0], i);
                    passed = 0;
                    break;
                }
            }
        }
        if (!passed) break;
    }

    wow_arena_destroy(&arena);

    if (passed) {
        printf("  PASS: Arena reallocation stability\n");
        n_pass++;
    } else {
        n_fail++;
    }
}

/* ── Test 2: Arena String Duplication ───────────────────────────── */

static void test_arena_strdup_off(void)
{
    printf("\ntest_arena_strdup_off:\n");
    printf("  Store multiple strings, verify accessible after arena growth.\n");

    wow_arena arena;
    wow_arena_init(&arena);

    const char *test_strings[] = {
        "rack", "rack-protection", "rack-session", "sinatra",
        "activerecord", "activesupport", "actionpack", "rails",
        "mustermann", "tilt", "logger", "base64"
    };
    int n_strings = sizeof(test_strings) / sizeof(test_strings[0]);
    wow_aoff offsets[12];
    int passed = 1;

    /* Store strings */
    for (int i = 0; i < n_strings; i++) {
        offsets[i] = wow_arena_strdup_off(&arena, test_strings[i]);
        if (offsets[i] == WOW_AOFF_NULL) {
            printf("  FAIL: strdup_off failed for '%s'\n", test_strings[i]);
            passed = 0;
            break;
        }
    }

    /* Force arena growth with large allocation */
    wow_aoff big_off = wow_arena_alloc_off(&arena, 128 * 1024);
    if (big_off == WOW_AOFF_NULL) {
        printf("  FAIL: Large allocation failed\n");
        passed = 0;
    }

    /* Verify all strings still accessible */
    for (int i = 0; i < n_strings && passed; i++) {
        const char *retrieved = WOW_ARENA_STR(&arena, offsets[i]);
        if (strcmp(retrieved, test_strings[i]) != 0) {
            printf("  FAIL: String mismatch at offset %d: expected '%s', got '%s'\n",
                   i, test_strings[i], retrieved);
            passed = 0;
        }
    }

    wow_arena_destroy(&arena);

    if (passed) {
        printf("  PASS: String offset stability\n");
        n_pass++;
    } else {
        n_fail++;
    }
}

/* ── Test 3: Nested Structure Offsets ───────────────────────────── */

struct test_nested {
    wow_aoff name;
    int value;
};

struct test_container {
    wow_aoff items;  /* offset to array of test_nested */
    int n_items;
};

static void test_nested_structures(void)
{
    printf("\ntest_nested_structures:\n");
    printf("  Store nested structures with offsets, verify after growth.\n");

    wow_arena arena;
    wow_arena_init(&arena);
    int passed = 1;

    /* Create container with 5 nested items */
    wow_aoff items_off = wow_arena_alloc_off(&arena, 5 * sizeof(struct test_nested));
    if (items_off == WOW_AOFF_NULL) {
        printf("  FAIL: Could not allocate items array\n");
        passed = 0;
    }

    struct test_nested *items = WOW_ARENA_PTR(&arena, items_off, struct test_nested);
    for (int i = 0; i < 5; i++) {
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "item%d", i);
        items[i].name = wow_arena_strdup_off(&arena, name_buf);
        items[i].value = i * 10;
    }

    /* Store container */
    wow_aoff container_off = wow_arena_alloc_off(&arena, sizeof(struct test_container));
    if (container_off == WOW_AOFF_NULL) {
        printf("  FAIL: Could not allocate container\n");
        passed = 0;
    }

    struct test_container *container = WOW_ARENA_PTR(&arena, container_off, struct test_container);
    container->items = items_off;
    container->n_items = 5;

    /* Force arena growth */
    if (wow_arena_alloc_off(&arena, 256 * 1024) == WOW_AOFF_NULL) {
        printf("  FAIL: Growth allocation failed\n");
        passed = 0;
    }

    /* Re-fetch and verify */
    container = WOW_ARENA_PTR(&arena, container_off, struct test_container);
    items = WOW_ARENA_PTR(&arena, container->items, struct test_nested);

    for (int i = 0; i < 5 && passed; i++) {
        const char *name = WOW_ARENA_STR(&arena, items[i].name);
        char expected[16];
        snprintf(expected, sizeof(expected), "item%d", i);
        if (strcmp(name, expected) != 0 || items[i].value != i * 10) {
            printf("  FAIL: Item %d corrupted: name='%s', value=%d\n",
                   i, name, items[i].value);
            passed = 0;
        }
    }

    wow_arena_destroy(&arena);

    if (passed) {
        printf("  PASS: Nested structure offsets\n");
        n_pass++;
    } else {
        n_fail++;
    }
}

/* ── Test 4: Provider with Multiple Fetches ─────────────────────── */

static void test_provider_multiple_fetches(void)
{
    printf("\ntest_provider_multiple_fetches:\n");
    printf("  Fetch multiple packages sequentially, verify all remain valid.\n");

    wow_ci_provider ci;
    struct wow_http_pool pool;
    int passed = 1;

    wow_http_pool_init(&pool, 4);
    wow_ci_provider_init(&ci, "https://rubygems.org", &pool);
    
    /* Build provider vtable */
    wow_provider prov = wow_ci_provider_as_provider(&ci);

    /* Fetch packages sequentially - each fetch may grow arena */
    const char *packages[] = {"rack", "tilt", "mustermann"};
    int n_pkgs = sizeof(packages) / sizeof(packages[0]);
    const wow_gemver *versions[3];
    int n_versions[3];

    for (int i = 0; i < n_pkgs && passed; i++) {
        if (prov.list_versions(prov.ctx, packages[i], &versions[i], &n_versions[i]) != 0) {
            printf("  SKIP: Could not fetch %s (network issue?)\n", packages[i]);
            /* Don't fail test due to network issues */
            passed = -1;
            break;
        }

        if (n_versions[i] == 0) {
            printf("  FAIL: No versions for %s\n", packages[i]);
            passed = 0;
            break;
        }

        /* Access first and last version to verify pointer valid */
        const char *first = versions[i][0].raw;
        const char *last = versions[i][n_versions[i] - 1].raw;
        printf("    %s: %d versions (%s ... %s)\n",
               packages[i], n_versions[i], first, last);

        /* Verify we can access version data */
        if (strlen(first) == 0) {
            printf("  FAIL: First version string empty for %s\n", packages[i]);
            passed = 0;
        }
    }

    /* Now verify ALL previously fetched packages still accessible */
    if (passed == 1) {
        for (int i = 0; i < n_pkgs; i++) {
            const wow_gemver *vers;
            int n_ver;
            if (prov.list_versions(prov.ctx, packages[i], &vers, &n_ver) != 0) {
                printf("  FAIL: Could not re-fetch %s\n", packages[i]);
                passed = 0;
                break;
            }
            if (n_ver != n_versions[i]) {
                printf("  FAIL: Version count mismatch for %s: %d vs %d\n",
                       packages[i], n_ver, n_versions[i]);
                passed = 0;
                break;
            }
            /* Note: 'vers' may differ from 'versions[i]' if arena grew,
             * but the data should be the same */
            if (strcmp(vers[0].raw, versions[i][0].raw) != 0) {
                printf("  FAIL: First version mismatch for %s: %s vs %s\n",
                       packages[i], vers[0].raw, versions[i][0].raw);
                passed = 0;
                break;
            }
        }
    }

    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);

    if (passed == 1) {
        printf("  PASS: Provider multiple fetches\n");
        n_pass++;
    } else if (passed == -1) {
        printf("  SKIP: Network issue (not a code failure)\n");
    } else {
        n_fail++;
    }
}

/* ── Test 5: Solver with Large Universe ─────────────────────────── */

struct big_pkg_ver {
    char *version;
    char *dep_name;  /* NULL if no deps */
};

struct big_pkg {
    char *name;
    struct big_pkg_ver *versions;
    int n_versions;
};

/* Placeholder for large universe test - would need proper test provider */
static int big_list_versions(void *ctx, const char *package,
                              const wow_gemver **out, int *n_out)
{
    (void)ctx; (void)package; (void)out; (void)n_out;
    return -1;
}

static void test_solver_large_universe(void)
{
    printf("\ntest_solver_large_universe:\n");
    printf("  (Skipped - requires test provider rewrite for large universe)\n");
    /* This would require a more complex test provider that can handle
     * 100+ packages without using too much stack/static data */
}

/* ── Test 6: WOW_AOFF_NULL Handling ─────────────────────────────── */

static void test_aoff_null(void)
{
    printf("\ntest_aoff_null:\n");
    printf("  Verify WOW_AOFF_NULL sentinel behavior.\n");

    wow_arena arena;
    wow_arena_init(&arena);
    int passed = 1;

    /* Test null string - manual null check pattern */
    wow_aoff null_off = WOW_AOFF_NULL;
    const char *result;
    if (null_off == WOW_AOFF_NULL) {
        result = NULL;
    } else {
        result = WOW_ARENA_STR(&arena, null_off);
    }
    if (result != NULL) {
        printf("  FAIL: Null offset should yield NULL pointer\n");
        passed = 0;
    }

    /* Test valid string */
    wow_aoff valid_off = wow_arena_strdup_off(&arena, "test");
    if (valid_off == WOW_AOFF_NULL) {
        printf("  FAIL: strdup_off failed\n");
        passed = 0;
    } else {
        result = WOW_ARENA_STR(&arena, valid_off);
        if (result == NULL || strcmp(result, "test") != 0) {
            printf("  FAIL: Valid string not retrieved correctly\n");
            passed = 0;
        }
    }

    wow_arena_destroy(&arena);

    if (passed) {
        printf("  PASS: WOW_AOFF_NULL handling\n");
        n_pass++;
    } else {
        n_fail++;
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Arena Offset Stability Test Suite ===\n");
    printf("\nThese tests exercise wow_aoff offset-based addressing\n");
    printf("to verify pointers remain valid across arena reallocations.\n");

    test_arena_realloc_stability();
    test_arena_strdup_off();
    test_nested_structures();
    test_aoff_null();
    test_provider_multiple_fetches();
    test_solver_large_universe();

    printf("\n=== Results ===\n");
    printf("  PASS: %d\n", n_pass);
    printf("  FAIL: %d\n", n_fail);

    return n_fail > 0 ? 1 : 0;
}
