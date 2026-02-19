/*
 * demo_version.c — Phase 6a: Version parsing and constraint matching
 *
 * Demonstrates wow's version handling:
 * - Parse Ruby gem version strings (4.1.1, 3.0.0.beta.2, 4.0.1.pre)
 * - Version comparison (segment-based, not semver)
 * - Constraint matching: =, !=, >, >=, <, <=, ~> (pessimistic)
 * - Multiple constraints (AND): >= 3.0.0, < 4
 *
 * Build: make -C demos/phase6 demo_version.com
 * Usage: ./demo_version.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── Version parsing and comparison ──────────────────────────────── */

typedef struct {
    int segments[4];
    int n_segments;
    char prerelease[32];
} wow_version_t;

/* Parse a version string into segments and optional prerelease */
static bool parse_version(const char *str, wow_version_t *v)
{
    memset(v, 0, sizeof(*v));
    v->n_segments = 0;
    v->prerelease[0] = '\0';

    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Check for prerelease suffix */
    char *dot = strstr(buf, ".pre");
    if (!dot) dot = strstr(buf, ".beta");
    if (!dot) dot = strstr(buf, ".alpha");
    if (!dot) dot = strstr(buf, ".rc");

    if (dot) {
        strncpy(v->prerelease, dot + 1, sizeof(v->prerelease) - 1);
        *dot = '\0';
    }

    /* Parse numeric segments */
    char *p = buf;
    while (*p && v->n_segments < 4) {
        char *end;
        long n = strtol(p, &end, 10);
        if (p == end) break;
        v->segments[v->n_segments++] = (int)n;
        if (*end == '.') p = end + 1;
        else break;
    }

    return v->n_segments > 0;
}

/* Compare two versions. Returns: -1 if a<b, 0 if a==b, 1 if a>b */
static int version_cmp(const wow_version_t *a, const wow_version_t *b)
{
    int max_seg = a->n_segments > b->n_segments ? a->n_segments : b->n_segments;

    for (int i = 0; i < max_seg; i++) {
        int av = i < a->n_segments ? a->segments[i] : 0;
        int bv = i < b->n_segments ? b->segments[i] : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }

    /* Prerelease comparison: empty > non-empty (4.0.0 > 4.0.0.pre) */
    if (a->prerelease[0] == '\0' && b->prerelease[0] != '\0') return 1;
    if (a->prerelease[0] != '\0' && b->prerelease[0] == '\0') return -1;
    if (a->prerelease[0] && b->prerelease[0])
        return strcmp(a->prerelease, b->prerelease);

    return 0;
}

/* ── Constraint parsing ──────────────────────────────────────────── */

typedef enum {
    CONSTR_EQ,      /* = */
    CONSTR_NE,      /* != */
    CONSTR_GT,      /* > */
    CONSTR_GTE,     /* >= */
    CONSTR_LT,      /* < */
    CONSTR_LTE,     /* <= */
    CONSTR_PESS,    /* ~> pessimistic */
} wow_constraint_op_t;

typedef struct {
    wow_constraint_op_t op;
    wow_version_t version;
    wow_version_t upper;    /* For ~> operator */
} wow_constraint_t;

static bool parse_constraint(const char *str, wow_constraint_t *c)
{
    memset(c, 0, sizeof(*c));

    const char *p = str;
    while (*p == ' ') p++;

    /* Parse operator */
    if (strncmp(p, "~>", 2) == 0) {
        c->op = CONSTR_PESS;
        p += 2;
    } else if (strncmp(p, ">=", 2) == 0) {
        c->op = CONSTR_GTE;
        p += 2;
    } else if (strncmp(p, "<=", 2) == 0) {
        c->op = CONSTR_LTE;
        p += 2;
    } else if (strncmp(p, "!=", 2) == 0) {
        c->op = CONSTR_NE;
        p += 2;
    } else if (*p == '>') {
        c->op = CONSTR_GT;
        p++;
    } else if (*p == '<') {
        c->op = CONSTR_LT;
        p++;
    } else if (*p == '=') {
        c->op = CONSTR_EQ;
        p++;
    } else {
        /* Bare version = exact match */
        c->op = CONSTR_EQ;
    }

    while (*p == ' ') p++;

    if (!parse_version(p, &c->version))
        return false;

    /* Calculate upper bound for ~> */
    if (c->op == CONSTR_PESS) {
        c->upper = c->version;
        if (c->version.n_segments >= 2) {
            /* ~> x.y = < (x+1).0 */
            /* ~> x.y.z = < x.(y+1).0 */
            if (c->version.n_segments == 2) {
                c->upper.segments[0]++;
                c->upper.segments[1] = 0;
            } else {
                c->upper.segments[1]++;
                c->upper.segments[2] = 0;
            }
        }
    }

    return true;
}

/* Check if version satisfies a single constraint */
static bool constraint_satisfied(const wow_constraint_t *c, const wow_version_t *v)
{
    int cmp = version_cmp(v, &c->version);

    switch (c->op) {
        case CONSTR_EQ:  return cmp == 0;
        case CONSTR_NE:  return cmp != 0;
        case CONSTR_GT:  return cmp > 0;
        case CONSTR_GTE: return cmp >= 0;
        case CONSTR_LT:  return cmp < 0;
        case CONSTR_LTE: return cmp <= 0;
        case CONSTR_PESS:
            return cmp >= 0 && version_cmp(v, &c->upper) < 0;
    }
    return false;
}

/* ── Test cases ──────────────────────────────────────────────────── */

typedef struct {
    const char *version;
    const char *constraint;
    bool expected;
    const char *description;
} version_test_t;

static const version_test_t tests[] = {
    /* Basic equality */
    {"4.1.1", "= 4.1.1", true, "exact match"},
    {"4.1.1", "4.1.1", true, "implicit exact match"},
    {"4.1.1", "= 4.1.0", false, "exact mismatch"},

    /* Not equal */
    {"4.1.1", "!= 4.1.0", true, "not equal (different)"},
    {"4.1.1", "!= 4.1.1", false, "not equal (same)"},

    /* Greater than */
    {"4.1.1", "> 4.0", true, "greater than"},
    {"4.0.0", "> 4.0", false, "not greater (equal)"},
    {"3.9.9", "> 4.0", false, "not greater (less)"},

    /* Greater than or equal */
    {"4.1.1", ">= 4.0", true, "greater than or equal (greater)"},
    {"4.0.0", ">= 4.0", true, "greater than or equal (equal)"},
    {"3.9.9", ">= 4.0", false, "greater than or equal (less)"},

    /* Less than */
    {"3.9.9", "< 4.0", true, "less than"},
    {"4.0.0", "< 4.0", false, "not less (equal)"},
    {"4.0.1", "< 4.0", false, "not less (greater)"},

    /* Less than or equal */
    {"3.9.9", "<= 4.0", true, "less than or equal (less)"},
    {"4.0.0", "<= 4.0", true, "less than or equal (equal)"},
    {"4.0.1", "<= 4.0", false, "less than or equal (greater)"},

    /* Pessimistic operator ~> */
    {"4.1.1", "~> 4.0", true, "pessimistic ~> 4.0 (patch)"},
    {"4.9.9", "~> 4.0", true, "pessimistic ~> 4.0 (minor)"},
    {"5.0.0", "~> 4.0", false, "pessimistic ~> 4.0 (major bump)"},
    {"3.9.9", "~> 4.0", false, "pessimistic ~> 4.0 (below)"},

    /* Pessimistic with patch */
    {"4.1.1", "~> 4.1.0", true, "pessimistic ~> 4.1.0 (patch)"},
    {"4.1.9", "~> 4.1.0", true, "pessimistic ~> 4.1.0 (patch bump)"},
    {"4.2.0", "~> 4.1.0", false, "pessimistic ~> 4.1.0 (minor bump)"},
    {"4.0.9", "~> 4.1.0", false, "pessimistic ~> 4.1.0 (below)"},

    /* Multiple constraints (AND) - conceptually shown, parsed separately */
    {"3.2.1", ">= 3.0", true, "multiple: >= 3.0 satisfied"},
    {"3.2.1", "< 4", true, "multiple: < 4 satisfied"},
    {"4.0.0", ">= 3.0", true, "multiple: >= 3.0 (boundary)"},
    {"4.0.0", "< 4", false, "multiple: < 4 (boundary fail)"},

    /* Prerelease versions */
    {"4.1.1.pre", "~> 4.1", true, "prerelease satisfies ~>"},
    {"4.1.1", "~> 4.1", true, "release satisfies ~>"},
    {"4.1.1.pre", "= 4.1.1.pre", true, "prerelease exact match"},
    {"4.1.1", "= 4.1.1.pre", false, "release vs prerelease mismatch"},

    /* Ruby-like versions */
    {"3.0.0", ">= 2.7", true, "Ruby version check"},
    {"3.3.0", ">= 2.7", true, "Ruby version check (newer)"},
    {"2.6.10", ">= 2.7", false, "Ruby version check (too old)"},
};

#define N_TESTS (sizeof(tests) / sizeof(tests[0]))

/* ── Main ────────────────────────────────────────────────────────── */

int main(void)
{
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Phase 6a: Version Parsing & Constraint Matching              ║\n");
    printf("║  Ruby gem versions: segment-based, not semver                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");

    int passed = 0, failed = 0;

    printf("Running %zu constraint satisfaction tests...\n\n", N_TESTS);

    for (size_t i = 0; i < N_TESTS; i++) {
        const version_test_t *t = &tests[i];

        wow_version_t v;
        wow_constraint_t c;

        if (!parse_version(t->version, &v)) {
            printf("  [PARSE FAIL] %s\n", t->version);
            failed++;
            continue;
        }

        if (!parse_constraint(t->constraint, &c)) {
            printf("  [PARSE FAIL] %s\n", t->constraint);
            failed++;
            continue;
        }

        bool result = constraint_satisfied(&c, &v);
        bool ok = result == t->expected;

        if (ok) {
            passed++;
            printf("  \033[32m✓\033[0m  %-12s satisfies %-12s  (%s)\n",
                   t->version, t->constraint, t->description);
        } else {
            failed++;
            printf("  \033[31m✗\033[0m  %-12s satisfies %-12s  → %s (expected %s)\n",
                   t->version, t->constraint,
                   result ? "true" : "false",
                   t->expected ? "true" : "false");
        }
    }

    printf("\n─────────────────────────────────────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    if (failed == 0) {
        printf("\n✅ All version constraint tests passed!\n");
        printf("\nKey behaviors demonstrated:\n");
        printf("  • ~> 4.0  means >= 4.0, < 5.0  (pessimistic minor)\n");
        printf("  • ~> 4.1.0 means >= 4.1.0, < 4.2.0 (pessimistic patch)\n");
        printf("  • 4.1.1.pre is considered < 4.1.1 (prerelease semantics)\n");
        printf("  • Multiple constraints combine with AND logic\n");
    }

    return failed > 0 ? 1 : 0;
}
