/*
 * resolver/test/version_test.c — Version parsing and constraint tests
 *
 *   wow debug version-test
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/resolver.h"
#include "internal.h"

/* Shared test counters */
int test_count = 0;
int pass_count = 0;
int fail_count = 0;

static void check_parse(const char *input, bool expect_ok)
{
    test_count++;
    wow_gemver v;
    int rc = wow_gemver_parse(input, &v);
    bool ok = (rc == 0) == expect_ok;
    if (ok) {
        pass_count++;
    } else {
        fail_count++;
        fprintf(stderr, "  FAIL parse \"%s\": expected %s, got %s\n",
                input, expect_ok ? "ok" : "error",
                rc == 0 ? "ok" : "error");
    }
}

static void check_cmp(const char *a, const char *b, int expect)
{
    test_count++;
    wow_gemver va, vb;
    if (wow_gemver_parse(a, &va) != 0 || wow_gemver_parse(b, &vb) != 0) {
        fail_count++;
        fprintf(stderr, "  FAIL cmp \"%s\" vs \"%s\": parse error\n", a, b);
        return;
    }

    int got = wow_gemver_cmp(&va, &vb);
    /* Normalise to -1/0/1 */
    int norm = (got < 0) ? -1 : (got > 0) ? 1 : 0;
    if (norm == expect) {
        pass_count++;
    } else {
        fail_count++;
        fprintf(stderr, "  FAIL cmp \"%s\" vs \"%s\": expected %d, got %d\n",
                a, b, expect, norm);
    }
}

static void check_match(const char *constraint, const char *version,
                         bool expect)
{
    test_count++;
    wow_gem_constraints cs;
    wow_gemver v;
    if (wow_gem_constraints_parse(constraint, &cs) != 0) {
        fail_count++;
        fprintf(stderr, "  FAIL match: parse constraint \"%s\" failed\n",
                constraint);
        return;
    }
    if (wow_gemver_parse(version, &v) != 0) {
        fail_count++;
        fprintf(stderr, "  FAIL match: parse version \"%s\" failed\n",
                version);
        return;
    }

    bool got = wow_gemver_match(&cs, &v);
    if (got == expect) {
        pass_count++;
    } else {
        fail_count++;
        fprintf(stderr, "  FAIL match \"%s\" vs \"%s\": expected %s, got %s\n",
                constraint, version,
                expect ? "true" : "false",
                got ? "true" : "false");
    }
}

int cmd_debug_version_test(int argc, char *argv[])
{
    (void)argc; (void)argv;
    test_count = pass_count = fail_count = 0;

    printf("=== Gem Version Tests ===\n\n");

    /* --- Parsing --- */
    printf("Parsing:\n");
    check_parse("4.1.1", 1);
    check_parse("3.0.0.beta.2", 1);
    check_parse("4.0.1.pre", 1);
    check_parse("0.9.1.1", 1);
    check_parse("1", 1);
    check_parse("0", 1);
    check_parse("1.0.0.alpha", 1);
    check_parse("", 0);
    check_parse("   ", 0);

    /* --- Comparison --- */
    printf("Comparison:\n");
    check_cmp("4.1.1", "4.1.1", 0);
    check_cmp("4.1.1", "4.1.0", 1);
    check_cmp("4.1.0", "4.1.1", -1);
    check_cmp("4.0", "4.0.0", 0);          /* trailing zeros */
    check_cmp("4.0.0", "4.0.0.0", 0);
    check_cmp("1.0", "2.0", -1);
    check_cmp("10.0", "9.0", 1);

    /* Pre-release ordering */
    check_cmp("4.0.0.beta.2", "4.0.0", -1);   /* pre < release */
    check_cmp("4.0.0", "4.0.0.beta.2", 1);
    check_cmp("4.0.0.alpha", "4.0.0.beta", -1); /* alpha < beta */
    check_cmp("4.0.0.beta.1", "4.0.0.beta.2", -1);
    check_cmp("4.0.0.rc1", "4.0.0", -1);
    check_cmp("1.0.0.pre", "1.0.0", -1);

    /* --- Constraint matching --- */
    printf("Constraints:\n");
    check_match(">= 4.0", "4.1.1", 1);
    check_match(">= 4.0", "3.9.9", 0);
    check_match("> 4.0", "4.0.0", 0);
    check_match("> 4.0", "4.0.1", 1);
    check_match("< 5.0", "4.9.9", 1);
    check_match("< 5.0", "5.0.0", 0);
    check_match("<= 4.1.1", "4.1.1", 1);
    check_match("<= 4.1.1", "4.1.2", 0);
    check_match("= 4.1.1", "4.1.1", 1);
    check_match("= 4.1.1", "4.1.0", 0);
    check_match("!= 4.1.1", "4.1.1", 0);
    check_match("!= 4.1.1", "4.1.0", 1);

    /* Pessimistic (~>) */
    check_match("~> 4.1", "4.1.0", 1);
    check_match("~> 4.1", "4.9.9", 1);
    check_match("~> 4.1", "5.0.0", 0);    /* upper bound: < 5.0 */
    check_match("~> 4.1", "4.0.9", 0);    /* lower bound: >= 4.1 */
    check_match("~> 4.1.1", "4.1.1", 1);
    check_match("~> 4.1.1", "4.1.9", 1);
    check_match("~> 4.1.1", "4.2.0", 0);  /* upper bound: < 4.2.0 */
    check_match("~> 4.1.1", "4.1.0", 0);  /* lower bound: >= 4.1.1 */

    /* Combined constraints */
    check_match(">= 3.0, < 4.0", "3.5.0", 1);
    check_match(">= 3.0, < 4.0", "4.0.0", 0);
    check_match(">= 3.0, < 4.0", "2.9.9", 0);
    check_match(">= 3.0.0, < 4, != 3.5.0", "3.4.0", 1);
    check_match(">= 3.0.0, < 4, != 3.5.0", "3.5.0", 0);

    /* Pre-release matching semantics */
    check_match("~> 4.0.0.beta", "4.0.0.beta.2", 1);  /* constraint has pre → matches pre */
    check_match("~> 4.0", "4.0.0.beta", 0);           /* no pre in constraint → skip pre */
    check_match(">= 4.0.0.beta", "4.0.0.beta", 1);
    check_match(">= 4.0.0.beta", "4.0.0.beta.2", 1);
    check_match(">= 4.0.0.beta", "4.0.0", 1);          /* release > pre, constraint has pre */
    check_match(">= 4.0", "4.0.0.beta", 0);            /* no pre in constraint */

    printf("\n%d tests: %d passed, %d failed\n",
           test_count, pass_count, fail_count);

    return fail_count > 0 ? 1 : 0;
}
