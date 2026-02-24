/*
 * resolver/test/pubgrub_test.c — PubGrub solver tests
 *
 *   wow debug pubgrub-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/resolver.h"
#include "internal.h"

/* Shared test counters (defined in version_test.c) */
extern int test_count;
extern int pass_count;
extern int fail_count;

/*
 * Hardcoded package universe for testing:
 *
 * Test 1 (happy path):
 *   A 1.0.0 depends on B >= 1.0
 *   B 1.1.0 depends on C ~> 2.0
 *   C 2.3.0 (no deps)
 *   Expected: A=1.0.0, B=1.1.0, C=2.3.0
 *
 * Test 2 (conflict):
 *   X 1.0.0 depends on Y >= 2.0
 *   X 1.0.0 depends on Z >= 1.0
 *   Z 1.0.0 depends on Y < 2.0
 *   Expected: conflict (no solution)
 *
 * Test 3 (backtracking):
 *   P 1.0.0 depends on Q >= 1.0
 *   Q 2.0.0 depends on R >= 2.0
 *   Q 1.0.0 depends on R >= 1.0
 *   R 1.5.0 (no deps)
 *   R 2.0.0 not available
 *   Expected: P=1.0.0, Q=1.0.0, R=1.5.0 (backtracks from Q 2.0.0)
 */

#define MAX_HARDCODED_PKGS 8
#define MAX_HARDCODED_VERS 4
#define MAX_HARDCODED_DEPS 4

struct hc_dep {
    const char          *name;
    wow_gem_constraints  cs;
};

struct hc_ver {
    wow_gemver     ver;
    struct hc_dep  deps[MAX_HARDCODED_DEPS];
    int            n_deps;
};

struct hc_pkg {
    const char    *name;
    struct hc_ver  versions[MAX_HARDCODED_VERS];  /* newest first */
    int            n_versions;
};

struct hc_universe {
    struct hc_pkg pkgs[MAX_HARDCODED_PKGS];
    int           n_pkgs;
};

static struct hc_pkg *hc_find(struct hc_universe *u, const char *name)
{
    for (int i = 0; i < u->n_pkgs; i++) {
        if (strcmp(u->pkgs[i].name, name) == 0)
            return &u->pkgs[i];
    }
    return NULL;
}

static int hc_list_versions(void *ctx, const char *package,
                             const wow_gemver **out, int *n_out)
{
    struct hc_universe *u = ctx;
    struct hc_pkg *pkg = hc_find(u, package);
    if (!pkg) {
        *out = NULL;
        *n_out = 0;
        return 0;
    }
    /* Return pointers to the embedded versions */
    static wow_gemver buf[MAX_HARDCODED_VERS];
    for (int i = 0; i < pkg->n_versions; i++)
        buf[i] = pkg->versions[i].ver;
    *out = buf;
    *n_out = pkg->n_versions;
    return 0;
}

static const char *dep_name_buf[MAX_HARDCODED_DEPS];
static wow_gem_constraints dep_cs_buf[MAX_HARDCODED_DEPS];

static int hc_get_deps(void *ctx, const char *package,
                        const wow_gemver *version,
                        const char ***dep_names_out,
                        wow_gem_constraints **dep_constraints_out,
                        int *n_deps_out)
{
    struct hc_universe *u = ctx;
    struct hc_pkg *pkg = hc_find(u, package);
    if (!pkg) { *n_deps_out = 0; return 0; }

    for (int v = 0; v < pkg->n_versions; v++) {
        if (wow_gemver_cmp(&pkg->versions[v].ver, version) == 0) {
            struct hc_ver *hv = &pkg->versions[v];
            for (int d = 0; d < hv->n_deps; d++) {
                dep_name_buf[d] = hv->deps[d].name;
                dep_cs_buf[d] = hv->deps[d].cs;
            }
            *dep_names_out = dep_name_buf;
            *dep_constraints_out = dep_cs_buf;
            *n_deps_out = hv->n_deps;
            return 0;
        }
    }

    *n_deps_out = 0;
    return 0;
}

static void hc_add_pkg(struct hc_universe *u, const char *name)
{
    struct hc_pkg *p = &u->pkgs[u->n_pkgs++];
    memset(p, 0, sizeof(*p));
    p->name = name;
}

static void hc_add_ver(struct hc_universe *u, const char *pkg_name,
                        const char *ver_str)
{
    struct hc_pkg *p = hc_find(u, pkg_name);
    struct hc_ver *v = &p->versions[p->n_versions++];
    memset(v, 0, sizeof(*v));
    wow_gemver_parse(ver_str, &v->ver);
}

static void hc_add_dep(struct hc_universe *u, const char *pkg_name,
                        const char *ver_str, const char *dep_name,
                        const char *constraint_str)
{
    struct hc_pkg *p = hc_find(u, pkg_name);
    for (int i = 0; i < p->n_versions; i++) {
        if (strcmp(p->versions[i].ver.raw, ver_str) == 0) {
            struct hc_dep *d = &p->versions[i].deps[p->versions[i].n_deps++];
            d->name = dep_name;
            wow_gem_constraints_parse(constraint_str, &d->cs);
            return;
        }
    }
}

static void check_solved(wow_solver *s, const char *name,
                          const char *expected_ver)
{
    test_count++;
    for (int i = 0; i < s->n_solved; i++) {
        if (strcmp(s->solution[i].name, name) == 0) {
            if (strcmp(s->solution[i].version.raw, expected_ver) == 0) {
                pass_count++;
            } else {
                fail_count++;
                fprintf(stderr, "  FAIL: %s expected %s, got %s\n",
                        name, expected_ver, s->solution[i].version.raw);
            }
            return;
        }
    }
    fail_count++;
    fprintf(stderr, "  FAIL: %s not in solution\n", name);
}

int cmd_debug_pubgrub_test(int argc, char *argv[])
{
    (void)argc; (void)argv;
    test_count = pass_count = fail_count = 0;

    printf("=== PubGrub Resolver Tests ===\n\n");

    /* --- Test 1: Happy path --- */
    printf("Test 1: Happy path (A → B → C)\n");
    {
        static struct hc_universe u;
        memset(&u, 0, sizeof(u));

        hc_add_pkg(&u, "A");
        hc_add_ver(&u, "A", "1.0.0");
        hc_add_dep(&u, "A", "1.0.0", "B", ">= 1.0");

        hc_add_pkg(&u, "B");
        hc_add_ver(&u, "B", "1.1.0");
        hc_add_dep(&u, "B", "1.1.0", "C", "~> 2.0");

        hc_add_pkg(&u, "C");
        hc_add_ver(&u, "C", "2.3.0");

        wow_provider prov = {
            .list_versions = hc_list_versions,
            .get_deps = hc_get_deps,
            .ctx = &u,
        };
        wow_solver s;
        wow_solver_init(&s, &prov);

        const char *roots[] = { "A" };
        wow_gem_constraints rcs[1];
        wow_gem_constraints_parse(">= 0", &rcs[0]);

        int rc = wow_solve(&s, roots, rcs, 1);
        test_count++;
        if (rc == 0) {
            pass_count++;
            printf("  Resolved %d packages\n", s.n_solved);
            for (int i = 0; i < s.n_solved; i++)
                printf("    %s %s\n", s.solution[i].name,
                       s.solution[i].version.raw);
        } else {
            fail_count++;
            fprintf(stderr, "  FAIL: expected success, got error: %s\n",
                    s.error_msg);
        }

        check_solved(&s, "A", "1.0.0");
        check_solved(&s, "B", "1.1.0");
        check_solved(&s, "C", "2.3.0");

        wow_solver_destroy(&s);
    }

    /* --- Test 2: Conflict --- */
    printf("\nTest 2: Conflict (X→Y>=2, X→Z→Y<2)\n");
    {
        static struct hc_universe u;
        memset(&u, 0, sizeof(u));

        hc_add_pkg(&u, "X");
        hc_add_ver(&u, "X", "1.0.0");
        hc_add_dep(&u, "X", "1.0.0", "Y", ">= 2.0");
        hc_add_dep(&u, "X", "1.0.0", "Z", ">= 1.0");

        hc_add_pkg(&u, "Y");
        hc_add_ver(&u, "Y", "2.0.0");
        hc_add_ver(&u, "Y", "1.0.0");

        hc_add_pkg(&u, "Z");
        hc_add_ver(&u, "Z", "1.0.0");
        hc_add_dep(&u, "Z", "1.0.0", "Y", "< 2.0");

        wow_provider prov = {
            .list_versions = hc_list_versions,
            .get_deps = hc_get_deps,
            .ctx = &u,
        };
        wow_solver s;
        wow_solver_init(&s, &prov);

        const char *roots[] = { "X" };
        wow_gem_constraints rcs[1];
        wow_gem_constraints_parse(">= 0", &rcs[0]);

        int rc = wow_solve(&s, roots, rcs, 1);
        test_count++;
        if (rc != 0) {
            pass_count++;
            printf("  Conflict detected (expected)\n");
            printf("  Error: %s\n", s.error_msg);
        } else {
            fail_count++;
            fprintf(stderr, "  FAIL: expected conflict, got success\n");
        }

        wow_solver_destroy(&s);
    }

    /* --- Test 3: Backtracking --- */
    printf("\nTest 3: Backtracking (Q 2.0→R>=2, R only 1.5)\n");
    {
        static struct hc_universe u;
        memset(&u, 0, sizeof(u));

        hc_add_pkg(&u, "P");
        hc_add_ver(&u, "P", "1.0.0");
        hc_add_dep(&u, "P", "1.0.0", "Q", ">= 1.0");

        hc_add_pkg(&u, "Q");
        hc_add_ver(&u, "Q", "2.0.0");  /* newest first */
        hc_add_ver(&u, "Q", "1.0.0");
        hc_add_dep(&u, "Q", "2.0.0", "R", ">= 2.0");
        hc_add_dep(&u, "Q", "1.0.0", "R", ">= 1.0");

        hc_add_pkg(&u, "R");
        hc_add_ver(&u, "R", "1.5.0");  /* only version */

        wow_provider prov = {
            .list_versions = hc_list_versions,
            .get_deps = hc_get_deps,
            .ctx = &u,
        };
        wow_solver s;
        wow_solver_init(&s, &prov);

        const char *roots[] = { "P" };
        wow_gem_constraints rcs[1];
        wow_gem_constraints_parse(">= 0", &rcs[0]);

        int rc = wow_solve(&s, roots, rcs, 1);
        test_count++;
        if (rc == 0) {
            pass_count++;
            printf("  Resolved %d packages\n", s.n_solved);
            for (int i = 0; i < s.n_solved; i++)
                printf("    %s %s\n", s.solution[i].name,
                       s.solution[i].version.raw);
        } else {
            fail_count++;
            fprintf(stderr, "  FAIL: expected success, got error: %s\n",
                    s.error_msg);
        }

        check_solved(&s, "P", "1.0.0");
        check_solved(&s, "Q", "1.0.0");
        check_solved(&s, "R", "1.5.0");

        wow_solver_destroy(&s);
    }

    /* --- Test 4: Multiple roots --- */
    printf("\nTest 4: Multiple root dependencies\n");
    {
        static struct hc_universe u;
        memset(&u, 0, sizeof(u));

        hc_add_pkg(&u, "web");
        hc_add_ver(&u, "web", "3.0.0");
        hc_add_ver(&u, "web", "2.0.0");

        hc_add_pkg(&u, "db");
        hc_add_ver(&u, "db", "1.2.0");

        wow_provider prov = {
            .list_versions = hc_list_versions,
            .get_deps = hc_get_deps,
            .ctx = &u,
        };
        wow_solver s;
        wow_solver_init(&s, &prov);

        const char *roots[] = { "web", "db" };
        wow_gem_constraints rcs[2];
        wow_gem_constraints_parse("~> 2.0", &rcs[0]);
        wow_gem_constraints_parse(">= 1.0", &rcs[1]);

        int rc = wow_solve(&s, roots, rcs, 2);
        test_count++;
        if (rc == 0) {
            pass_count++;
            printf("  Resolved %d packages\n", s.n_solved);
            for (int i = 0; i < s.n_solved; i++)
                printf("    %s %s\n", s.solution[i].name,
                       s.solution[i].version.raw);
        } else {
            fail_count++;
            fprintf(stderr, "  FAIL: expected success, got error: %s\n",
                    s.error_msg);
        }

        check_solved(&s, "web", "2.0.0");
        check_solved(&s, "db", "1.2.0");

        wow_solver_destroy(&s);
    }

    printf("\n%d tests: %d passed, %d failed\n",
           test_count, pass_count, fail_count);

    return fail_count > 0 ? 1 : 0;
}
