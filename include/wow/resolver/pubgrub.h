#ifndef WOW_RESOLVER_PUBGRUB_H
#define WOW_RESOLVER_PUBGRUB_H

/*
 * pubgrub.h -- PubGrub dependency resolver
 *
 * PubGrub uses conflict-driven learning: when a conflict is found,
 * it learns a new incompatibility that prevents the same conflict
 * from recurring, then backtracks to the appropriate decision level.
 *
 * The algorithm produces either a complete version assignment for
 * every required package, or a human-readable explanation of why
 * no solution exists.
 *
 * Reference: https://nex3.medium.com/pubgrub-2fb6470504f
 */

#include <stdbool.h>
#include "wow/resolver/gemver.h"
#include "wow/resolver/arena.h"

/* ------------------------------------------------------------------ */
/* Version range                                                       */
/* ------------------------------------------------------------------ */

/* A contiguous version interval. Both bounds are optional. */
typedef struct {
    wow_gemver min, max;
    bool has_min, has_max;
    bool min_inclusive, max_inclusive;
} wow_ver_range;

/* Special ranges */
#define WOW_RANGE_ANY   ((wow_ver_range){ .has_min = false, .has_max = false })
#define WOW_RANGE_NONE  ((wow_ver_range){ .has_min = true, .has_max = true, \
    .min_inclusive = true, .max_inclusive = true })
/* NONE is detected by: has_min && has_max && min > max */

/* ------------------------------------------------------------------ */
/* Terms and incompatibilities                                         */
/* ------------------------------------------------------------------ */

/* A term: package + version range + positive/negative */
typedef struct {
    wow_aoff       package;    /* offset to string in solver arena */
    wow_ver_range  range;
    bool           positive;   /* true = must be in range */
} wow_term;

/* Cause of an incompatibility */
enum wow_incomp_cause {
    CAUSE_ROOT,        /* direct requirement from user's Gemfile */
    CAUSE_DEPENDENCY,  /* package X version V depends on Y */
    CAUSE_CONFLICT,    /* derived from two conflicting incompatibilities */
};

/* An incompatibility: set of terms that cannot all be true simultaneously */
typedef struct wow_incomp {
    wow_aoff              terms;               /* offset to wow_term[] in arena */
    int                   n_terms;
    enum wow_incomp_cause cause_type;
    wow_aoff              cause_a, cause_b;    /* offsets for CAUSE_CONFLICT */
    wow_aoff              dep_package;         /* offset for CAUSE_DEPENDENCY */
    wow_gemver            dep_version;         /* for CAUSE_DEPENDENCY */
} wow_incomp;

/* ------------------------------------------------------------------ */
/* Partial solution                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    wow_aoff      package;        /* offset to string in solver arena */
    wow_ver_range range;
    wow_gemver    version;        /* set when is_decision */
    bool          is_decision;
    bool          positive;       /* true = must be in range, false = must NOT */
    int           decision_level;
    wow_aoff      cause;          /* offset to wow_incomp; WOW_AOFF_NULL = none */
} wow_assignment;

/* ------------------------------------------------------------------ */
/* Package version provider                                            */
/* ------------------------------------------------------------------ */

/*
 * Provider callbacks — the solver calls these to discover packages.
 * All returned data must remain valid for the duration of the solve.
 */
typedef struct {
    /*
     * Return all versions of a package (sorted newest-first).
     * Allocates *out array; caller does NOT free (arena or provider owns it).
     * Returns 0 on success, -1 on error.
     */
    int (*list_versions)(void *ctx, const char *package,
                         const wow_gemver **out, int *n_out);

    /*
     * Return dependencies for a specific package version.
     * dep_names[i] corresponds to dep_constraints[i].
     * Returns 0 on success, -1 on error.
     */
    int (*get_deps)(void *ctx, const char *package, const wow_gemver *version,
                    const char ***dep_names_out,
                    wow_gem_constraints **dep_constraints_out,
                    int *n_deps_out);

    void *ctx;
} wow_provider;

/* ------------------------------------------------------------------ */
/* Resolved solution                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    wow_gemver  version;
} wow_resolved_pkg;

/* ------------------------------------------------------------------ */
/* Solver state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    wow_arena        arena;

    /* Incompatibilities (dynamic array of offsets into arena) */
    wow_aoff        *incomps;
    int              n_incomps, incomps_cap;

    /* Partial solution (dynamic array) */
    wow_assignment  *assignments;
    int              n_assign, assign_cap;

    int              decision_level;
    wow_provider    *provider;

    /* Solution output */
    wow_resolved_pkg *solution;
    int               n_solved;

    /* Error output */
    char              error_msg[4096];
} wow_solver;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Initialise the solver with a provider. */
void wow_solver_init(wow_solver *s, wow_provider *p);

/*
 * Solve dependencies.
 *   root_names[i] / root_constraints[i] are the direct requirements.
 *   Returns 0 on success (solution in s->solution / s->n_solved),
 *   or -1 on conflict (error message in s->error_msg).
 */
int wow_solve(wow_solver *s,
              const char **root_names,
              const wow_gem_constraints *root_constraints,
              int n_roots);

/* Free all solver resources. */
void wow_solver_destroy(wow_solver *s);

#endif
