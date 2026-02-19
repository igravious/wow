/*
 * pubgrub.c -- PubGrub dependency resolver
 *
 * Implements the PubGrub algorithm: unit propagation + conflict-driven
 * learning with backjumping. Produces either a complete solution or
 * a human-readable explanation of why resolution failed.
 *
 * Reference: https://nex3.medium.com/pubgrub-2fb6470504f
 *            https://github.com/dart-lang/pub/blob/master/doc/solver.md
 */

#include "wow/resolver/pubgrub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* The "root" pseudo-package */
#define ROOT_PKG "$root"

/* Arena dereference convenience — assumes `s` (wow_solver *) in scope */
#define A_STR(off)     WOW_ARENA_STR(&s->arena, off)
#define A_PTR(off, T)  WOW_ARENA_PTR(&s->arena, off, T)

static bool streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/* ------------------------------------------------------------------ */
/* Version range operations                                            */
/* ------------------------------------------------------------------ */

static bool range_is_any(const wow_ver_range *r)
{
    return !r->has_min && !r->has_max;
}

static bool range_is_empty(const wow_ver_range *r)
{
    if (!r->has_min || !r->has_max) return false;
    int cmp = wow_gemver_cmp(&r->min, &r->max);
    if (cmp > 0) return true;
    if (cmp == 0 && (!r->min_inclusive || !r->max_inclusive)) return true;
    return false;
}

/* Does range r contain version v? */
static bool range_contains(const wow_ver_range *r, const wow_gemver *v)
{
    if (r->has_min) {
        int c = wow_gemver_cmp(v, &r->min);
        if (c < 0) return false;
        if (c == 0 && !r->min_inclusive) return false;
    }
    if (r->has_max) {
        int c = wow_gemver_cmp(v, &r->max);
        if (c > 0) return false;
        if (c == 0 && !r->max_inclusive) return false;
    }
    return true;
}

/* Intersect two ranges. Result may be empty. */
static wow_ver_range range_intersect(const wow_ver_range *a,
                                     const wow_ver_range *b)
{
    wow_ver_range r = *a;

    /* Tighten lower bound */
    if (b->has_min) {
        if (!r.has_min) {
            r.has_min = true;
            r.min = b->min;
            r.min_inclusive = b->min_inclusive;
        } else {
            int c = wow_gemver_cmp(&b->min, &r.min);
            if (c > 0) {
                r.min = b->min;
                r.min_inclusive = b->min_inclusive;
            } else if (c == 0) {
                r.min_inclusive = r.min_inclusive && b->min_inclusive;
            }
        }
    }

    /* Tighten upper bound */
    if (b->has_max) {
        if (!r.has_max) {
            r.has_max = true;
            r.max = b->max;
            r.max_inclusive = b->max_inclusive;
        } else {
            int c = wow_gemver_cmp(&b->max, &r.max);
            if (c < 0) {
                r.max = b->max;
                r.max_inclusive = b->max_inclusive;
            } else if (c == 0) {
                r.max_inclusive = r.max_inclusive && b->max_inclusive;
            }
        }
    }

    return r;
}

/* Does range a fully contain range b? */
static bool range_allows_all(const wow_ver_range *a, const wow_ver_range *b)
{
    /* If b is empty, a trivially allows all of b */
    if (range_is_empty(b)) return true;
    /* If a is any, it allows everything */
    if (range_is_any(a)) return true;

    /* Check lower bound: a's min must be <= b's min */
    if (a->has_min) {
        if (!b->has_min) return false;
        int c = wow_gemver_cmp(&a->min, &b->min);
        if (c > 0) return false;
        if (c == 0 && !a->min_inclusive && b->min_inclusive) return false;
    }

    /* Check upper bound: a's max must be >= b's max */
    if (a->has_max) {
        if (!b->has_max) return false;
        int c = wow_gemver_cmp(&a->max, &b->max);
        if (c < 0) return false;
        if (c == 0 && !a->max_inclusive && b->max_inclusive) return false;
    }

    return true;
}

/* Build a range from a constraint set (intersect all constraints into one range) */
static wow_ver_range range_from_constraints(const wow_gem_constraints *cs)
{
    wow_ver_range r = WOW_RANGE_ANY;

    for (int i = 0; i < cs->count; i++) {
        const wow_gem_constraint *c = &cs->items[i];
        wow_ver_range cr = WOW_RANGE_ANY;

        switch (c->op) {
        case WOW_OP_EQ:
            cr.has_min = cr.has_max = true;
            cr.min = cr.max = c->ver;
            cr.min_inclusive = cr.max_inclusive = true;
            break;
        case WOW_OP_GTE:
            cr.has_min = true;
            cr.min = c->ver;
            cr.min_inclusive = true;
            break;
        case WOW_OP_GT:
            cr.has_min = true;
            cr.min = c->ver;
            cr.min_inclusive = false;
            break;
        case WOW_OP_LTE:
            cr.has_max = true;
            cr.max = c->ver;
            cr.max_inclusive = true;
            break;
        case WOW_OP_LT:
            cr.has_max = true;
            cr.max = c->ver;
            cr.max_inclusive = false;
            break;
        case WOW_OP_NEQ:
            /* Can't represent != as a single range — skip for now.
             * Real handling would need a range set (union of ranges).
             * For MVP, != constraints are checked post-resolution. */
            continue;
        case WOW_OP_PESSIMISTIC: {
            /* ~> X.Y.Z means >= X.Y.Z, < X.(Y+1).0 */
            cr.has_min = true;
            cr.min = c->ver;
            cr.min_inclusive = true;

            cr.has_max = true;
            cr.max_inclusive = false;
            int n = c->ver.n_segs;
            memset(&cr.max, 0, sizeof(cr.max));
            if (n < 2) {
                cr.max.segs[0].num = c->ver.segs[0].num + 1;
                cr.max.n_segs = 1;
            } else {
                for (int j = 0; j < n - 1; j++)
                    cr.max.segs[j] = c->ver.segs[j];
                cr.max.n_segs = n - 1;
                cr.max.segs[n - 2].num++;
            }
            break;
        }
        }

        r = range_intersect(&r, &cr);
    }

    return r;
}

/* Exact version range: [v, v] */
static wow_ver_range range_exact(const wow_gemver *v)
{
    wow_ver_range r;
    memset(&r, 0, sizeof(r));
    r.has_min = r.has_max = true;
    r.min_inclusive = r.max_inclusive = true;
    r.min = r.max = *v;
    return r;
}

/*
 * Does this range allow pre-release versions?
 *
 * RubyGems rule: pre-release versions are only matched when the
 * constraint itself references a pre-release (e.g. ">= 2.0.0.beta").
 * Since ranges are derived from constraints, we check whether either
 * bound is a pre-release version. If so, pre-releases are allowed.
 * If neither bound is pre-release (or unbounded), pre-releases are
 * excluded.
 */
static bool range_allows_prerelease(const wow_ver_range *r)
{
    if (r->has_min && r->min.prerelease) return true;
    if (r->has_max && r->max.prerelease) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Term operations                                                     */
/* ------------------------------------------------------------------ */

/*
 * Relation of term to the partial solution's assignment for the same package.
 * PubGrub needs: is the term satisfied, contradicted, or inconclusive?
 */
enum term_relation {
    TERM_SATISFIED,      /* assignment proves term true */
    TERM_CONTRADICTED,   /* assignment proves term false */
    TERM_INCONCLUSIVE,   /* assignment neither proves nor disproves */
};

/*
 * Determine the relation of a term to the current partial solution.
 *
 * Assignments can be positive ("version in range") or negative
 * ("version NOT in range"). For a package with assignments:
 *   - positive_range = intersection of all positive assignment ranges
 *   - negative_ranges = list of ranges that are excluded
 *
 * When there's a decision, it overrides all derivations (exact version).
 *
 * We compute whether the effective constraint on a package
 * satisfies, contradicts, or is inconclusive w.r.t. the given term.
 */
static enum term_relation term_relation(const wow_solver *s,
                                         const wow_term *t)
{
    const char *t_pkg = A_STR(t->package);

    /* Collect positive range and negative exclusions for this package */
    wow_ver_range pos_range = WOW_RANGE_ANY;
    bool has_pos = false;
    bool has_decision = false;
    wow_gemver decided_ver;

    /* Negative exclusions — track up to 16 */
    wow_ver_range neg_ranges[16];
    int n_neg = 0;

    for (int i = 0; i < s->n_assign; i++) {
        if (!streq(A_STR(s->assignments[i].package), t_pkg)) continue;

        if (s->assignments[i].is_decision) {
            has_decision = true;
            decided_ver = s->assignments[i].version;
            pos_range = range_exact(&s->assignments[i].version);
            has_pos = true;
        } else if (s->assignments[i].positive) {
            pos_range = range_intersect(&pos_range, &s->assignments[i].range);
            has_pos = true;
        } else {
            /* Negative assignment: version must NOT be in this range */
            if (n_neg < 16)
                neg_ranges[n_neg++] = s->assignments[i].range;
        }
    }

    if (!has_pos && n_neg == 0) {
        /* No assignments at all */
        if (t->positive) {
            return range_is_any(&t->range) ? TERM_SATISFIED : TERM_INCONCLUSIVE;
        }
        return TERM_INCONCLUSIVE;
    }

    /*
     * If there's a decision, the package is pinned to exactly that version.
     * This is the simple case — ignore negatives (decision overrides).
     */
    if (has_decision) {
        bool in_term_range = range_contains(&t->range, &decided_ver);
        if (t->positive)
            return in_term_range ? TERM_SATISFIED : TERM_CONTRADICTED;
        else
            return in_term_range ? TERM_CONTRADICTED : TERM_SATISFIED;
    }

    /*
     * No decision yet. We have positive constraints (pos_range) and
     * possibly negative exclusions.
     */
    if (t->positive) {
        /* Term says "version must be in R" */

        /* If pos_range is entirely within R, and no negative range
         * could pull versions out, then SATISFIED */
        if (range_allows_all(&t->range, &pos_range)) {
            /* Check: are there negative ranges that exclude parts of
             * pos_range that are inside R? For satisfaction, we need
             * ALL possible versions to be in R, which they are since
             * pos_range ⊆ R. Negatives only further restrict, which
             * is fine. */
            return TERM_SATISFIED;
        }

        /* If pos_range doesn't overlap R at all → CONTRADICTED */
        wow_ver_range inter = range_intersect(&t->range, &pos_range);
        if (range_is_empty(&inter))
            return TERM_CONTRADICTED;

        /* Check if negative ranges exclude everything in pos_range ∩ R */
        for (int i = 0; i < n_neg; i++) {
            if (range_allows_all(&neg_ranges[i], &inter)) {
                /* The negative range excludes all of the intersection */
                return TERM_CONTRADICTED;
            }
        }

        return TERM_INCONCLUSIVE;
    } else {
        /* Term says "version must NOT be in R" (negative) */

        /* Satisfied if pos_range doesn't overlap R at all */
        wow_ver_range inter = range_intersect(&t->range, &pos_range);
        if (range_is_empty(&inter))
            return TERM_SATISFIED;

        /* Satisfied if a negative range already excludes everything
         * in pos_range ∩ R (those versions are already ruled out) */
        for (int i = 0; i < n_neg; i++) {
            if (range_allows_all(&neg_ranges[i], &inter))
                return TERM_SATISFIED;
        }

        /* Contradicted if pos_range is entirely within R and no
         * negative ranges help */
        if (range_allows_all(&t->range, &pos_range))
            return TERM_CONTRADICTED;

        return TERM_INCONCLUSIVE;
    }
}

/* ------------------------------------------------------------------ */
/* Solver internals                                                    */
/* ------------------------------------------------------------------ */

/* Dynamic array push helpers */
static int push_incomp(wow_solver *s, wow_aoff ic_off)
{
    if (s->n_incomps >= s->incomps_cap) {
        int new_cap = s->incomps_cap ? s->incomps_cap * 2 : 64;
        wow_aoff *nb = realloc(s->incomps,
                               (size_t)new_cap * sizeof(*nb));
        if (!nb) return -1;
        s->incomps = nb;
        s->incomps_cap = new_cap;
    }
    s->incomps[s->n_incomps++] = ic_off;
    return 0;
}

static int push_assignment(wow_solver *s, wow_assignment *a)
{
    if (s->n_assign >= s->assign_cap) {
        int new_cap = s->assign_cap ? s->assign_cap * 2 : 64;
        wow_assignment *nb = realloc(s->assignments,
                                     (size_t)new_cap * sizeof(*nb));
        if (!nb) return -1;
        s->assignments = nb;
        s->assign_cap = new_cap;
    }
    s->assignments[s->n_assign++] = *a;
    return 0;
}

/* Create an incompatibility in the arena. Returns offset (WOW_AOFF_NULL on OOM). */
static wow_aoff make_incomp(wow_solver *s, wow_term *terms, int n,
                              enum wow_incomp_cause cause)
{
    wow_aoff ic_off = wow_arena_alloc_off(&s->arena, sizeof(wow_incomp));
    if (ic_off == WOW_AOFF_NULL) return WOW_AOFF_NULL;

    wow_aoff terms_off = wow_arena_alloc_off(&s->arena,
                                              (size_t)n * sizeof(wow_term));
    if (terms_off == WOW_AOFF_NULL) return WOW_AOFF_NULL;

    /* Re-fetch after possible realloc */
    wow_incomp *ic = A_PTR(ic_off, wow_incomp);
    memset(ic, 0, sizeof(*ic));
    ic->terms = terms_off;
    ic->n_terms = n;
    ic->cause_type = cause;
    ic->cause_a = WOW_AOFF_NULL;
    ic->cause_b = WOW_AOFF_NULL;
    ic->dep_package = WOW_AOFF_NULL;

    memcpy(A_PTR(terms_off, wow_term), terms, (size_t)n * sizeof(wow_term));
    return ic_off;
}

/* ------------------------------------------------------------------ */
/* Unit propagation                                                    */
/* ------------------------------------------------------------------ */

/*
 * Scan all incompatibilities. For each:
 *   - If ALL terms are satisfied → conflict (return the incompatibility)
 *   - If all-but-one term is satisfied and one is inconclusive →
 *     derive the negation of the remaining term (unit propagation)
 *   - Otherwise → skip
 *
 * Returns WOW_AOFF_NULL when no more propagation is possible.
 * Returns the offset of the conflicting incompatibility on conflict.
 */
static wow_aoff unit_propagate(wow_solver *s, wow_aoff changed_pkg)
{
    /*
     * We iterate until no new derivations. To avoid O(n^2), we only
     * check incompatibilities that mention the changed package.
     * On first call (changed_pkg == WOW_AOFF_NULL), check all.
     */
    bool changed = true;
    while (changed) {
        changed = false;

        for (int i = 0; i < s->n_incomps; i++) {
            wow_incomp *ic = A_PTR(s->incomps[i], wow_incomp);
            wow_term *ic_terms = A_PTR(ic->terms, wow_term);

            /* Quick filter: skip if changed_pkg isn't in this incompatibility */
            if (changed_pkg != WOW_AOFF_NULL) {
                bool relevant = false;
                const char *cpkg = A_STR(changed_pkg);
                for (int t = 0; t < ic->n_terms; t++) {
                    if (streq(A_STR(ic_terms[t].package), cpkg)) {
                        relevant = true;
                        break;
                    }
                }
                if (!relevant) continue;
            }

            int n_satisfied = 0;
            int n_inconclusive = 0;
            int inconclusive_idx = -1;

            for (int t = 0; t < ic->n_terms; t++) {
                enum term_relation rel = term_relation(s, &ic_terms[t]);
                if (rel == TERM_SATISFIED) {
                    n_satisfied++;
                } else if (rel == TERM_INCONCLUSIVE) {
                    n_inconclusive++;
                    inconclusive_idx = t;
                }
                /* CONTRADICTED terms are neither satisfied nor inconclusive */
            }

            if (n_satisfied == ic->n_terms) {
                /* Conflict! All terms are satisfied → impossible */
                return s->incomps[i];
            }

            if (n_satisfied == ic->n_terms - 1 && n_inconclusive == 1) {
                /* Unit propagation: derive negation of the inconclusive term.
                 * Flip the polarity: positive → negative, negative → positive.
                 * Keep the range as-is — the assignment's positive flag
                 * encodes the negation semantics. */
                wow_term *ut = &ic_terms[inconclusive_idx];
                wow_assignment deriv;
                memset(&deriv, 0, sizeof(deriv));
                deriv.package = ut->package;
                deriv.is_decision = false;
                deriv.decision_level = s->decision_level;
                deriv.cause = s->incomps[i];
                deriv.range = ut->range;
                deriv.positive = !ut->positive;  /* flip polarity */

                push_assignment(s, &deriv);
                changed = true;
                changed_pkg = ut->package;
                break;  /* restart scan */
            }
        }
    }

    return WOW_AOFF_NULL;  /* no conflict */
}

/* ------------------------------------------------------------------ */
/* Conflict resolution                                                 */
/* ------------------------------------------------------------------ */

/*
 * When unit propagation finds a conflict, we need to learn a new
 * incompatibility and backjump to the right decision level.
 *
 * Returns 0 if we can continue solving (after backjump),
 * or -1 if the conflict is unsolvable (decision_level 0).
 */
static int conflict_resolution(wow_solver *s, wow_aoff conflict)
{
    /*
     * Algorithm:
     * While the conflict incompatibility has more than one term at the
     * current decision level, resolve it with the cause of the most
     * recent assignment for one of those terms.
     */
    wow_aoff ic_off = conflict;

    while (true) {
        wow_incomp *ic = A_PTR(ic_off, wow_incomp);
        wow_term *ic_terms = A_PTR(ic->terms, wow_term);

        /* Count how many terms in ic are decided at the current level */
        int n_at_level = 0;
        int most_recent_idx = -1;
        int most_recent_assign = -1;

        for (int t = 0; t < ic->n_terms; t++) {
            /* Find the most recent assignment for this term's package
             * at the current decision level */
            const char *tpkg = A_STR(ic_terms[t].package);
            for (int a = s->n_assign - 1; a >= 0; a--) {
                if (!streq(A_STR(s->assignments[a].package), tpkg))
                    continue;
                if (s->assignments[a].decision_level ==
                    s->decision_level) {
                    n_at_level++;
                    if (a > most_recent_assign) {
                        most_recent_assign = a;
                        most_recent_idx = t;
                    }
                    break;
                }
            }
        }

        if (n_at_level <= 1) {
            /* We've reduced to at most one term at current level.
             * This is our learned incompatibility. */
            break;
        }

        /* Resolve: combine ic with the cause of the most recent assignment */
        if (most_recent_assign < 0 ||
            s->assignments[most_recent_assign].cause == WOW_AOFF_NULL) {
            /* Cannot resolve further — this shouldn't happen with
             * a correct algorithm, but guard against it */
            break;
        }

        wow_aoff prior_cause_off = s->assignments[most_recent_assign].cause;

        /* Build a new incompatibility from the union of terms in ic and
         * prior_cause, minus the resolved term's package.
         * This is the resolution rule: if ic says {A, B} are incompatible
         * and prior_cause says {¬A, C} are incompatible, then {B, C} are
         * incompatible. */
        wow_term merged[64];
        int n_merged = 0;

        /* Re-fetch pointers (stable within this block — no arena allocs) */
        ic = A_PTR(ic_off, wow_incomp);
        ic_terms = A_PTR(ic->terms, wow_term);
        wow_aoff resolved_pkg = ic_terms[most_recent_idx].package;

        /* Add terms from ic (except the resolved one) */
        for (int t = 0; t < ic->n_terms; t++) {
            if (t == most_recent_idx) continue;
            if (n_merged < 64) merged[n_merged++] = ic_terms[t];
        }

        /* Add terms from prior_cause (except the resolved package) */
        wow_incomp *prior = A_PTR(prior_cause_off, wow_incomp);
        wow_term *prior_terms = A_PTR(prior->terms, wow_term);
        const char *resolved_str = A_STR(resolved_pkg);
        for (int t = 0; t < prior->n_terms; t++) {
            if (streq(A_STR(prior_terms[t].package), resolved_str))
                continue;
            /* Check for duplicate package — merge ranges */
            bool found = false;
            for (int m = 0; m < n_merged; m++) {
                if (streq(A_STR(merged[m].package),
                          A_STR(prior_terms[t].package))) {
                    /* Both positive or both negative: intersect ranges */
                    if (merged[m].positive ==
                        prior_terms[t].positive) {
                        merged[m].range = range_intersect(
                            &merged[m].range,
                            &prior_terms[t].range);
                    }
                    found = true;
                    break;
                }
            }
            if (!found && n_merged < 64)
                merged[n_merged++] = prior_terms[t];
        }

        /* make_incomp may grow the arena — invalidates ic, prior, etc. */
        wow_aoff derived_off = make_incomp(s, merged, n_merged,
                                            CAUSE_CONFLICT);
        if (derived_off == WOW_AOFF_NULL) return -1;

        /* Re-fetch derived after arena may have moved */
        wow_incomp *derived = A_PTR(derived_off, wow_incomp);
        derived->cause_a = ic_off;
        derived->cause_b = prior_cause_off;

        ic_off = derived_off;
    }

    /* Add the learned incompatibility */
    push_incomp(s, ic_off);

    /* Find the decision level to backjump to:
     * the second-highest decision level among ic's terms */
    wow_incomp *ic = A_PTR(ic_off, wow_incomp);
    wow_term *ic_terms = A_PTR(ic->terms, wow_term);
    int target_level = 0;
    for (int t = 0; t < ic->n_terms; t++) {
        const char *tpkg = A_STR(ic_terms[t].package);
        for (int a = s->n_assign - 1; a >= 0; a--) {
            if (!streq(A_STR(s->assignments[a].package), tpkg))
                continue;
            int lvl = s->assignments[a].decision_level;
            if (lvl < s->decision_level && lvl > target_level)
                target_level = lvl;
            break;
        }
    }

    if (s->decision_level == 0) {
        /* Unresolvable conflict at root level */
        return -1;
    }

    /* Backjump: remove all assignments above target_level */
    int new_n = 0;
    for (int a = 0; a < s->n_assign; a++) {
        if (s->assignments[a].decision_level <= target_level) {
            s->assignments[new_n++] = s->assignments[a];
        }
    }
    s->n_assign = new_n;
    s->decision_level = target_level;

    return 0;
}

/* ------------------------------------------------------------------ */
/* Decision (version selection)                                        */
/* ------------------------------------------------------------------ */

/*
 * Find the next package to decide on: one that has assignments
 * (constraints) but no decision yet.
 * Heuristic: pick the package with the fewest available versions.
 * Returns WOW_AOFF_NULL if all packages are decided.
 */
static wow_aoff pick_next_package(wow_solver *s)
{
    /* Collect packages that have assignments but no decision */
    wow_aoff candidates[256];
    int n_cand = 0;

    for (int a = 0; a < s->n_assign; a++) {
        wow_aoff pkg = s->assignments[a].package;
        const char *pkg_str = A_STR(pkg);

        /* Skip root */
        if (streq(pkg_str, ROOT_PKG)) continue;

        /* Check if this package already has a decision */
        bool has_decision = false;
        for (int b = 0; b < s->n_assign; b++) {
            if (streq(A_STR(s->assignments[b].package), pkg_str) &&
                s->assignments[b].is_decision) {
                has_decision = true;
                break;
            }
        }
        if (has_decision) continue;

        /* Check if already in candidates */
        bool dup = false;
        for (int c = 0; c < n_cand; c++) {
            if (streq(A_STR(candidates[c]), pkg_str)) { dup = true; break; }
        }
        if (!dup && n_cand < 256)
            candidates[n_cand++] = pkg;
    }

    if (n_cand == 0) return WOW_AOFF_NULL;

    /* Heuristic: fewest available versions matching current range */
    wow_aoff best = WOW_AOFF_NULL;
    int best_count = INT32_MAX;

    for (int i = 0; i < n_cand; i++) {
        const char *cand_str = A_STR(candidates[i]);
        const wow_gemver *versions;
        int n_ver;
        if (s->provider->list_versions(s->provider->ctx,
                                        cand_str,
                                        &versions, &n_ver) != 0)
            continue;

        /* Count versions matching current constraints */
        wow_ver_range pkg_pos = WOW_RANGE_ANY;
        wow_ver_range pkg_neg[16];
        int pkg_n_neg = 0;
        for (int a = 0; a < s->n_assign; a++) {
            if (!streq(A_STR(s->assignments[a].package), cand_str))
                continue;
            if (s->assignments[a].is_decision) {
                pkg_pos = range_exact(&s->assignments[a].version);
            } else if (s->assignments[a].positive) {
                pkg_pos = range_intersect(&pkg_pos,
                                           &s->assignments[a].range);
            } else if (pkg_n_neg < 16) {
                pkg_neg[pkg_n_neg++] = s->assignments[a].range;
            }
        }

        bool pkg_pre_ok = range_allows_prerelease(&pkg_pos);
        int matching = 0;
        for (int v = 0; v < n_ver; v++) {
            if (!range_contains(&pkg_pos, &versions[v])) continue;
            if (versions[v].prerelease && !pkg_pre_ok) continue;
            bool excl = false;
            for (int ne = 0; ne < pkg_n_neg; ne++) {
                if (range_contains(&pkg_neg[ne], &versions[v])) {
                    excl = true; break;
                }
            }
            if (!excl) matching++;
        }

        if (matching < best_count) {
            best_count = matching;
            best = candidates[i];
        }
    }

    return best;
}

/*
 * Choose a version for the given package that matches the current
 * assignment constraints. Picks the newest matching version.
 * Respects both positive ranges (must be in) and negative ranges
 * (must NOT be in). Returns NULL if no version matches.
 */
static const wow_gemver *choose_version(wow_solver *s, wow_aoff pkg_off)
{
    const char *pkg = A_STR(pkg_off);
    const wow_gemver *versions;
    int n_ver;
    if (s->provider->list_versions(s->provider->ctx, pkg,
                                    &versions, &n_ver) != 0)
        return NULL;

    /* Compute positive range and collect negative exclusions */
    wow_ver_range pos_range = WOW_RANGE_ANY;
    wow_ver_range neg_ranges[16];
    int n_neg = 0;

    for (int a = 0; a < s->n_assign; a++) {
        if (!streq(A_STR(s->assignments[a].package), pkg)) continue;
        if (s->assignments[a].is_decision) {
            pos_range = range_exact(&s->assignments[a].version);
        } else if (s->assignments[a].positive) {
            pos_range = range_intersect(&pos_range,
                                         &s->assignments[a].range);
        } else {
            if (n_neg < 16)
                neg_ranges[n_neg++] = s->assignments[a].range;
        }
    }

    /* Pre-release gate: skip pre-release versions unless the positive
     * range was derived from a constraint that references a pre-release */
    bool prerelease_ok = range_allows_prerelease(&pos_range);

    /* Pick newest matching: must be in pos_range and NOT in any neg_range */
    for (int v = 0; v < n_ver; v++) {
        if (!range_contains(&pos_range, &versions[v]))
            continue;

        /* RubyGems pre-release semantics */
        if (versions[v].prerelease && !prerelease_ok)
            continue;

        bool excluded = false;
        for (int i = 0; i < n_neg; i++) {
            if (range_contains(&neg_ranges[i], &versions[v])) {
                excluded = true;
                break;
            }
        }
        if (!excluded)
            return &versions[v];
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Error message generation                                            */
/* ------------------------------------------------------------------ */

static void fmt_range(char *buf, size_t sz, const wow_ver_range *r)
{
    if (range_is_any(r)) {
        snprintf(buf, sz, "any version");
        return;
    }
    if (r->has_min && r->has_max &&
        wow_gemver_cmp(&r->min, &r->max) == 0 &&
        r->min_inclusive && r->max_inclusive) {
        snprintf(buf, sz, "%s", r->min.raw);
        return;
    }
    size_t off = 0;
    if (r->has_min) {
        off += (size_t)snprintf(buf + off, sz - off, "%s %s",
                                r->min_inclusive ? ">=" : ">",
                                r->min.raw);
    }
    if (r->has_max) {
        if (off > 0) off += (size_t)snprintf(buf + off, sz - off, ", ");
        off += (size_t)snprintf(buf + off, sz - off, "%s %s",
                                r->max_inclusive ? "<=" : "<",
                                r->max.raw);
    }
    (void)off;
}

/*
 * Build a human-readable explanation from an incompatibility cause chain.
 * Uses an iterative approach with an explicit stack to avoid recursion
 * depth issues (per Kimi's feedback).
 */
static void explain_error(wow_solver *s, wow_aoff root_off)
{
    char *msg = s->error_msg;
    size_t sz = sizeof(s->error_msg);
    size_t off = 0;

    off += (size_t)snprintf(msg + off, sz - off,
                            "Because ");

    /* Iterative cause chain walk with explicit stack */
    #define EXPLAIN_STACK_MAX 32
    struct { wow_aoff ic_off; int phase; } stack[EXPLAIN_STACK_MAX];
    int sp = 0;

    stack[sp].ic_off = root_off;
    stack[sp].phase = 0;
    sp++;

    while (sp > 0 && off < sz - 1) {
        wow_incomp *ic = A_PTR(stack[sp - 1].ic_off, wow_incomp);
        wow_term *ic_terms = A_PTR(ic->terms, wow_term);

        if (ic->cause_type == CAUSE_DEPENDENCY) {
            char rbuf[128];
            /* "sinatra >= 4.0 depends on rack >= 3.0" */
            for (int t = 0; t < ic->n_terms; t++) {
                if (streq(A_STR(ic_terms[t].package), ROOT_PKG)) continue;
                if (!streq(A_STR(ic_terms[t].package),
                           A_STR(ic->dep_package))) {
                    fmt_range(rbuf, sizeof(rbuf), &ic_terms[t].range);
                    off += (size_t)snprintf(msg + off, sz - off,
                        "%s %s depends on ",
                        A_STR(ic->dep_package), ic->dep_version.raw);
                    /* Find the dependency target term */
                    for (int u = 0; u < ic->n_terms; u++) {
                        if (u == t) continue;
                        if (streq(A_STR(ic_terms[u].package), ROOT_PKG))
                            continue;
                        fmt_range(rbuf, sizeof(rbuf),
                                  &ic_terms[u].range);
                        off += (size_t)snprintf(msg + off, sz - off,
                            "%s %s", A_STR(ic_terms[u].package), rbuf);
                    }
                    break;
                }
            }
            sp--;
        } else if (ic->cause_type == CAUSE_ROOT) {
            for (int t = 0; t < ic->n_terms; t++) {
                if (streq(A_STR(ic_terms[t].package), ROOT_PKG)) continue;
                char rbuf[128];
                fmt_range(rbuf, sizeof(rbuf), &ic_terms[t].range);
                off += (size_t)snprintf(msg + off, sz - off,
                    "your project requires %s %s",
                    A_STR(ic_terms[t].package), rbuf);
            }
            sp--;
        } else {
            /* CAUSE_CONFLICT: explain both sides */
            if (stack[sp - 1].phase == 0) {
                stack[sp - 1].phase = 1;
                if (ic->cause_a != WOW_AOFF_NULL &&
                    sp < EXPLAIN_STACK_MAX) {
                    stack[sp].ic_off = ic->cause_a;
                    stack[sp].phase = 0;
                    sp++;
                }
            } else if (stack[sp - 1].phase == 1) {
                stack[sp - 1].phase = 2;
                off += (size_t)snprintf(msg + off, sz - off,
                    "\n  and ");
                if (ic->cause_b != WOW_AOFF_NULL &&
                    sp < EXPLAIN_STACK_MAX) {
                    stack[sp].ic_off = ic->cause_b;
                    stack[sp].phase = 0;
                    sp++;
                }
            } else {
                sp--;
            }
        }
    }

    off += (size_t)snprintf(msg + off, sz - off,
        ",\nversion solving failed.");
    (void)off;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void wow_solver_init(wow_solver *s, wow_provider *p)
{
    memset(s, 0, sizeof(*s));
    wow_arena_init(&s->arena);
    s->provider = p;
}

void wow_solver_destroy(wow_solver *s)
{
    wow_arena_destroy(&s->arena);
    free(s->incomps);
    free(s->assignments);
    free(s->solution);
    memset(s, 0, sizeof(*s));
}

int wow_solve(wow_solver *s,
              const char **root_names,
              const wow_gem_constraints *root_constraints,
              int n_roots)
{
    /* Step 1: add root package as a decision at level 0 */
    {
        wow_assignment root_assign;
        memset(&root_assign, 0, sizeof(root_assign));
        root_assign.package = wow_arena_strdup_off(&s->arena, ROOT_PKG);
        root_assign.cause = WOW_AOFF_NULL;
        root_assign.is_decision = true;
        root_assign.positive = true;
        root_assign.decision_level = 0;
        wow_gemver root_ver;
        memset(&root_ver, 0, sizeof(root_ver));
        root_ver.segs[0].num = 1;
        root_ver.n_segs = 1;
        snprintf(root_ver.raw, sizeof(root_ver.raw), "1.0.0");
        root_assign.version = root_ver;
        root_assign.range = range_exact(&root_ver);
        push_assignment(s, &root_assign);
    }

    /* Step 2: add incompatibilities for each root dependency */
    for (int i = 0; i < n_roots; i++) {
        wow_aoff name_off = wow_arena_strdup_off(&s->arena, root_names[i]);
        wow_ver_range dep_range =
            range_from_constraints(&root_constraints[i]);

        /*
         * Incompatibility: { root is 1.0.0, pkg NOT in dep_range }
         * Meaning: it's incompatible for root to exist AND pkg to not
         * satisfy the constraint.
         */
        wow_term terms[2];

        /* Root positive term */
        terms[0].package = wow_arena_strdup_off(&s->arena, ROOT_PKG);
        terms[0].range = range_exact(&s->assignments[0].version);
        terms[0].positive = true;

        /* Dep negative term: "package NOT in required range" */
        terms[1].package = name_off;
        terms[1].range = dep_range;
        terms[1].positive = false;

        wow_aoff ic_off = make_incomp(s, terms, 2, CAUSE_ROOT);
        if (ic_off == WOW_AOFF_NULL) return -1;
        push_incomp(s, ic_off);
    }

    /* Step 3: main solving loop */
    int max_iterations = 10000;
    for (int iter = 0; iter < max_iterations; iter++) {
        /* Unit propagation */
        wow_aoff conflict = unit_propagate(s, WOW_AOFF_NULL);

        if (conflict != WOW_AOFF_NULL) {
            /* Conflict resolution: learn + backjump */
            if (conflict_resolution(s, conflict) != 0) {
                explain_error(s, conflict);
                return -1;
            }
            continue;
        }

        /* Pick next undecided package */
        wow_aoff next_pkg = pick_next_package(s);
        if (next_pkg == WOW_AOFF_NULL) {
            /* All packages decided — build solution */
            break;
        }

        /* Choose a version */
        const wow_gemver *chosen = choose_version(s, next_pkg);

        if (!chosen) {
            /* No version available in the current allowed range.
             * Add incompatibility: "pkg in [current range] is impossible".
             * Use the actual constrained range, not ANY — this lets
             * conflict resolution learn which constraint caused the
             * failure and backtrack appropriately. */
            wow_ver_range eff_range = WOW_RANGE_ANY;
            for (int a = 0; a < s->n_assign; a++) {
                if (!streq(A_STR(s->assignments[a].package),
                           A_STR(next_pkg)))
                    continue;
                if (s->assignments[a].positive) {
                    eff_range = range_intersect(&eff_range,
                                                &s->assignments[a].range);
                }
            }

            wow_term term;
            term.package = next_pkg;
            term.range = eff_range;
            term.positive = true;

            wow_aoff no_ver = make_incomp(s, &term, 1, CAUSE_ROOT);
            if (no_ver == WOW_AOFF_NULL) return -1;
            push_incomp(s, no_ver);
            /* Re-propagate will catch the conflict */
            continue;
        }

        /* Make a decision */
        s->decision_level++;
        wow_assignment decision;
        memset(&decision, 0, sizeof(decision));
        decision.package = next_pkg;
        decision.cause = WOW_AOFF_NULL;
        decision.version = *chosen;
        decision.range = range_exact(chosen);
        decision.is_decision = true;
        decision.positive = true;
        decision.decision_level = s->decision_level;
        push_assignment(s, &decision);

        /* Add incompatibilities from this version's dependencies */
        const char **dep_names;
        wow_gem_constraints *dep_cs;
        int n_deps;
        if (s->provider->get_deps(s->provider->ctx, A_STR(next_pkg),
                                   chosen, &dep_names, &dep_cs,
                                   &n_deps) != 0) {
            snprintf(s->error_msg, sizeof(s->error_msg),
                     "failed to fetch dependencies for %s %s",
                     A_STR(next_pkg), chosen->raw);
            return -1;
        }

        for (int d = 0; d < n_deps; d++) {
            wow_ver_range dep_range = range_from_constraints(&dep_cs[d]);

            wow_term terms[2];
            /* "If next_pkg is chosen version..." */
            terms[0].package = next_pkg;
            terms[0].range = range_exact(chosen);
            terms[0].positive = true;

            /* "...then dep must be in range" (negated: dep NOT in range) */
            terms[1].package =
                wow_arena_strdup_off(&s->arena, dep_names[d]);
            terms[1].range = dep_range;
            terms[1].positive = false;

            wow_aoff dep_ic_off = make_incomp(s, terms, 2,
                                               CAUSE_DEPENDENCY);
            if (dep_ic_off == WOW_AOFF_NULL) return -1;

            /* Re-fetch after arena may have grown */
            wow_incomp *dep_ic = A_PTR(dep_ic_off, wow_incomp);
            dep_ic->dep_package = next_pkg;  /* reuse existing offset */
            dep_ic->dep_version = *chosen;
            push_incomp(s, dep_ic_off);
        }
    }

    /* Build solution from decisions (arena is stable — no more allocs) */
    int n_sol = 0;
    for (int a = 0; a < s->n_assign; a++) {
        if (s->assignments[a].is_decision &&
            !streq(A_STR(s->assignments[a].package), ROOT_PKG))
            n_sol++;
    }

    s->solution = calloc((size_t)n_sol, sizeof(wow_resolved_pkg));
    if (!s->solution && n_sol > 0) return -1;
    s->n_solved = n_sol;

    int idx = 0;
    for (int a = 0; a < s->n_assign; a++) {
        if (s->assignments[a].is_decision &&
            !streq(A_STR(s->assignments[a].package), ROOT_PKG)) {
            s->solution[idx].name = A_STR(s->assignments[a].package);
            s->solution[idx].version = s->assignments[a].version;
            idx++;
        }
    }

    return 0;
}
