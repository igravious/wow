/*
 * cmd.c -- Resolver CLI subcommands
 *
 * Provides:
 *   wow resolve <gem> [<gem>...]     — resolve dependencies
 *   wow lock [Gemfile]               — resolve + write Gemfile.lock
 *   wow debug version-test           — hardcoded version matching tests
 *   wow debug pubgrub-test           — hardcoded PubGrub solver tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/resolver.h"
#include "wow/gemfile.h"
#include "wow/http.h"
#include "wow/version.h"

/* ------------------------------------------------------------------ */
/* debug version-test                                                  */
/* ------------------------------------------------------------------ */

static int test_count, pass_count, fail_count;

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
    check_parse("4.1.1", true);
    check_parse("3.0.0.beta.2", true);
    check_parse("4.0.1.pre", true);
    check_parse("0.9.1.1", true);
    check_parse("1", true);
    check_parse("0", true);
    check_parse("1.0.0.alpha", true);
    check_parse("", false);
    check_parse("   ", false);

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
    check_match(">= 4.0", "4.1.1", true);
    check_match(">= 4.0", "3.9.9", false);
    check_match("> 4.0", "4.0.0", false);
    check_match("> 4.0", "4.0.1", true);
    check_match("< 5.0", "4.9.9", true);
    check_match("< 5.0", "5.0.0", false);
    check_match("<= 4.1.1", "4.1.1", true);
    check_match("<= 4.1.1", "4.1.2", false);
    check_match("= 4.1.1", "4.1.1", true);
    check_match("= 4.1.1", "4.1.0", false);
    check_match("!= 4.1.1", "4.1.1", false);
    check_match("!= 4.1.1", "4.1.0", true);

    /* Pessimistic (~>) */
    check_match("~> 4.1", "4.1.0", true);
    check_match("~> 4.1", "4.9.9", true);
    check_match("~> 4.1", "5.0.0", false);    /* upper bound: < 5.0 */
    check_match("~> 4.1", "4.0.9", false);    /* lower bound: >= 4.1 */
    check_match("~> 4.1.1", "4.1.1", true);
    check_match("~> 4.1.1", "4.1.9", true);
    check_match("~> 4.1.1", "4.2.0", false);  /* upper bound: < 4.2.0 */
    check_match("~> 4.1.1", "4.1.0", false);  /* lower bound: >= 4.1.1 */

    /* Combined constraints */
    check_match(">= 3.0, < 4.0", "3.5.0", true);
    check_match(">= 3.0, < 4.0", "4.0.0", false);
    check_match(">= 3.0, < 4.0", "2.9.9", false);
    check_match(">= 3.0.0, < 4, != 3.5.0", "3.4.0", true);
    check_match(">= 3.0.0, < 4, != 3.5.0", "3.5.0", false);

    /* Pre-release matching semantics */
    check_match("~> 4.0.0.beta", "4.0.0.beta.2", true);  /* constraint has pre → matches pre */
    check_match("~> 4.0", "4.0.0.beta", false);           /* no pre in constraint → skip pre */
    check_match(">= 4.0.0.beta", "4.0.0.beta", true);
    check_match(">= 4.0.0.beta", "4.0.0.beta.2", true);
    check_match(">= 4.0.0.beta", "4.0.0", true);          /* release > pre, constraint has pre */
    check_match(">= 4.0", "4.0.0.beta", false);            /* no pre in constraint */

    printf("\n%d tests: %d passed, %d failed\n",
           test_count, pass_count, fail_count);

    return fail_count > 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* debug pubgrub-test — hardcoded provider                             */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* wow resolve <gem> [<gem>...]                                        */
/* ------------------------------------------------------------------ */

int cmd_resolve(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: wow resolve <gem> [<gem>...]\n");
        return 1;
    }

    /* Collect gem names from argv (skip argv[0] which is "resolve") */
    const char **names = (const char **)(argv + 1);
    int n_gems = argc - 1;

    /* Parse optional version constraints: "sinatra:~>4.0" or just "sinatra" */
    const char **root_names = calloc((size_t)n_gems, sizeof(char *));
    wow_gem_constraints *root_cs = calloc((size_t)n_gems,
                                          sizeof(wow_gem_constraints));
    if (!root_names || !root_cs) {
        fprintf(stderr, "wow: out of memory\n");
        free(root_names);
        free(root_cs);
        return 1;
    }

    for (int i = 0; i < n_gems; i++) {
        const char *arg = names[i];
        const char *colon = strchr(arg, ':');
        if (colon && colon[1]) {
            /* "sinatra:~>4.0" — split at colon */
            size_t nlen = (size_t)(colon - arg);
            char *name = malloc(nlen + 1);
            if (!name) { fprintf(stderr, "wow: out of memory\n"); goto fail; }
            memcpy(name, arg, nlen);
            name[nlen] = '\0';
            root_names[i] = name;
            if (wow_gem_constraints_parse(colon + 1, &root_cs[i]) != 0) {
                fprintf(stderr, "wow: invalid constraint: %s\n", colon + 1);
                goto fail;
            }
        } else {
            /* Just a name — default to >= 0 */
            root_names[i] = arg;
            wow_gem_constraints_parse(">= 0", &root_cs[i]);
        }
    }

    /* Set up compact index provider */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    wow_ci_provider ci;
    wow_ci_provider_init(&ci, "https://rubygems.org", &pool);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    printf("Resolving dependencies...\n");
    fflush(stdout);

    int rc = wow_solve(&solver, root_names, root_cs, n_gems);
    if (rc != 0) {
        fprintf(stderr, "\nResolution failed:\n%s\n", solver.error_msg);
    } else {
        printf("\nResolved %d packages:\n", solver.n_solved);
        for (int i = 0; i < solver.n_solved; i++) {
            printf("  %s %s\n", solver.solution[i].name,
                   solver.solution[i].version.raw);
        }
    }

    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);

    /* Free any allocated names */
    for (int i = 0; i < n_gems; i++) {
        if (root_names[i] != names[i])
            free((char *)root_names[i]);
    }
    free(root_names);
    free(root_cs);
    return rc == 0 ? 0 : 1;

fail:
    for (int i = 0; i < n_gems; i++) {
        if (root_names[i] && root_names[i] != names[i])
            free((char *)root_names[i]);
    }
    free(root_names);
    free(root_cs);
    return 1;
}

/* ------------------------------------------------------------------ */
/* wow lock [Gemfile] — resolve + write Gemfile.lock                    */
/* ------------------------------------------------------------------ */

/* qsort comparator for wow_resolved_pkg by name */
static int resolved_cmp(const void *a, const void *b)
{
    const wow_resolved_pkg *pa = a, *pb = b;
    return strcmp(pa->name, pb->name);
}

int cmd_lock(int argc, char *argv[])
{
    /* Determine Gemfile path */
    const char *gemfile_path = "Gemfile";
    if (argc >= 2)
        gemfile_path = argv[1];

    /* 1. Parse Gemfile */
    struct wow_gemfile gf;
    wow_gemfile_init(&gf);
    if (wow_gemfile_parse_file(gemfile_path, &gf) != 0) {
        fprintf(stderr, "wow: failed to parse %s\n", gemfile_path);
        wow_gemfile_free(&gf);
        return 1;
    }

    if (gf.n_deps == 0) {
        fprintf(stderr, "wow: no gems in %s\n", gemfile_path);
        wow_gemfile_free(&gf);
        return 1;
    }

    const char *source = gf.source ? gf.source : "https://rubygems.org";

    /* 2. Convert Gemfile deps to solver root requirements */
    int n_roots = (int)gf.n_deps;
    const char **root_names = calloc((size_t)n_roots, sizeof(char *));
    wow_gem_constraints *root_cs = calloc((size_t)n_roots,
                                          sizeof(wow_gem_constraints));
    if (!root_names || !root_cs) {
        fprintf(stderr, "wow: out of memory\n");
        free(root_names); free(root_cs);
        wow_gemfile_free(&gf);
        return 1;
    }

    for (int i = 0; i < n_roots; i++) {
        root_names[i] = gf.deps[i].name;

        if (gf.deps[i].n_constraints > 0) {
            char joined[512];
            wow_join_constraints(gf.deps[i].constraints,
                                 gf.deps[i].n_constraints,
                                 joined, sizeof(joined));
            if (wow_gem_constraints_parse(joined, &root_cs[i]) != 0) {
                fprintf(stderr, "wow: invalid constraint for %s: %s\n",
                        gf.deps[i].name, joined);
                free(root_names); free(root_cs);
                wow_gemfile_free(&gf);
                return 1;
            }
        } else {
            wow_gem_constraints_parse(">= 0", &root_cs[i]);
        }
    }

    /* 3. Resolve */
    struct wow_http_pool pool;
    wow_http_pool_init(&pool, 4);

    wow_ci_provider ci;
    wow_ci_provider_init(&ci, source, &pool);

    wow_provider prov = wow_ci_provider_as_provider(&ci);
    wow_solver solver;
    wow_solver_init(&solver, &prov);

    printf("Resolving dependencies for %s...\n", gemfile_path);
    fflush(stdout);

    int rc = wow_solve(&solver, root_names, root_cs, n_roots);
    if (rc != 0) {
        fprintf(stderr, "\nResolution failed:\n%s\n", solver.error_msg);
        wow_solver_destroy(&solver);
        wow_ci_provider_destroy(&ci);
        wow_http_pool_cleanup(&pool);
        free(root_names); free(root_cs);
        wow_gemfile_free(&gf);
        return 1;
    }

    printf("Resolved %d packages.\n", solver.n_solved);

    /* 4. Sort solution alphabetically */
    qsort(solver.solution, (size_t)solver.n_solved,
          sizeof(wow_resolved_pkg), resolved_cmp);

    /* 5. Write Gemfile.lock */
    if (wow_write_lockfile("Gemfile.lock", &solver, &prov, &gf, source) != 0) {
        wow_solver_destroy(&solver);
        wow_ci_provider_destroy(&ci);
        wow_http_pool_cleanup(&pool);
        free(root_names); free(root_cs);
        wow_gemfile_free(&gf);
        return 1;
    }

    printf("Wrote Gemfile.lock\n");

    /* Cleanup */
    wow_solver_destroy(&solver);
    wow_ci_provider_destroy(&ci);
    wow_http_pool_cleanup(&pool);
    free(root_names);
    free(root_cs);
    wow_gemfile_free(&gf);
    return 0;
}
