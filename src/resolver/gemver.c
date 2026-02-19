/*
 * gemver.c -- RubyGems-compatible version parsing + constraint matching
 *
 * Implements the full RubyGems versioning spec:
 *   - Arbitrary segment count (dot-separated)
 *   - Pre-release segments (letters sort before release)
 *   - Pessimistic constraints (~>)
 *   - Trailing-zero equivalence (4.0 == 4.0.0)
 *
 * Reference: https://guides.rubygems.org/patterns/#semantic-versioning
 */

#include "wow/resolver/gemver.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Version parsing                                                     */
/* ------------------------------------------------------------------ */

int wow_gemver_parse(const char *s, wow_gemver *v)
{
    if (!s || !v) return -1;

    memset(v, 0, sizeof(*v));
    snprintf(v->raw, sizeof(v->raw), "%s", s);

    /* Skip leading whitespace */
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return -1;

    while (*s && v->n_segs < WOW_VER_MAX_SEGS) {
        wow_ver_seg *seg = &v->segs[v->n_segs];

        if (isdigit((unsigned char)*s)) {
            /* Numeric segment */
            seg->is_str = false;
            seg->num = 0;
            while (isdigit((unsigned char)*s)) {
                seg->num = seg->num * 10 + (*s - '0');
                s++;
            }
        } else if (isalpha((unsigned char)*s)) {
            /* Pre-release string segment */
            seg->is_str = true;
            v->prerelease = true;
            int i = 0;
            while (isalnum((unsigned char)*s) &&
                   i < WOW_VER_SEG_STRSZ - 1) {
                seg->str[i++] = *s++;
            }
            seg->str[i] = '\0';
        } else {
            return -1;
        }

        v->n_segs++;

        /* Skip separator (dot) */
        if (*s == '.') {
            s++;
        } else if (*s == '\0') {
            break;
        } else if (isalpha((unsigned char)*s)) {
            /* Transition from numeric to alpha without dot:
             * e.g. "4.0.0beta2" — treat as new segment */
            continue;
        } else if (isdigit((unsigned char)*s)) {
            /* Transition from alpha to numeric without dot */
            continue;
        } else {
            /* Unexpected character */
            break;
        }
    }

    return (v->n_segs > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Version comparison                                                  */
/* ------------------------------------------------------------------ */

/*
 * Compare a single segment at position idx in each version.
 * Missing segments are treated as 0 (numeric) for trailing-zero equivalence.
 * String segments sort BEFORE numeric: "beta" < 0 (pre-release < release).
 * String segments compare lexicographically against each other.
 */
static int seg_cmp(const wow_gemver *a, int idx_a,
                   const wow_gemver *b, int idx_b)
{
    bool a_exists = (idx_a < a->n_segs);
    bool b_exists = (idx_b < b->n_segs);

    /* Both missing: equal */
    if (!a_exists && !b_exists) return 0;

    bool a_str = a_exists && a->segs[idx_a].is_str;
    bool b_str = b_exists && b->segs[idx_b].is_str;

    /*
     * RubyGems rules for missing segments:
     *   - If A has a string segment and B is missing at this position,
     *     A is pre-release → A < B.
     *   - If A is missing and B has a string segment, B < A.
     *   - If both are numeric or missing, treat missing as 0.
     */
    if (a_str && !b_exists) return -1;  /* pre-release < release */
    if (!a_exists && b_str) return 1;

    if (a_str && b_str) {
        return strcmp(a->segs[idx_a].str, b->segs[idx_b].str);
    }

    if (a_str && !b_str) return -1;  /* string < numeric */
    if (!a_str && b_str) return 1;

    /* Both numeric (or missing → 0) */
    int na = a_exists ? a->segs[idx_a].num : 0;
    int nb = b_exists ? b->segs[idx_b].num : 0;

    return (na < nb) ? -1 : (na > nb) ? 1 : 0;
}

int wow_gemver_cmp(const wow_gemver *a, const wow_gemver *b)
{
    int max = a->n_segs > b->n_segs ? a->n_segs : b->n_segs;

    for (int i = 0; i < max; i++) {
        int c = seg_cmp(a, i, b, i);
        if (c != 0) return c;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Constraint parsing                                                  */
/* ------------------------------------------------------------------ */

/*
 * Parse operator prefix from s, advance *pp past it.
 * Returns the operator enum value.
 */
static enum wow_ver_op parse_op(const char **pp)
{
    const char *p = *pp;

    while (isspace((unsigned char)*p)) p++;

    if (p[0] == '~' && p[1] == '>') {
        *pp = p + 2;
        return WOW_OP_PESSIMISTIC;
    }
    if (p[0] == '>' && p[1] == '=') {
        *pp = p + 2;
        return WOW_OP_GTE;
    }
    if (p[0] == '<' && p[1] == '=') {
        *pp = p + 2;
        return WOW_OP_LTE;
    }
    if (p[0] == '!' && p[1] == '=') {
        *pp = p + 2;
        return WOW_OP_NEQ;
    }
    if (p[0] == '>') {
        *pp = p + 1;
        return WOW_OP_GT;
    }
    if (p[0] == '<') {
        *pp = p + 1;
        return WOW_OP_LT;
    }
    if (p[0] == '=') {
        *pp = p + 1;
        return WOW_OP_EQ;
    }

    /* No operator → defaults to = */
    *pp = p;
    return WOW_OP_EQ;
}

int wow_gem_constraints_parse(const char *s, wow_gem_constraints *cs)
{
    if (!s || !cs) return -1;

    memset(cs, 0, sizeof(*cs));

    /* Work on a mutable copy */
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", s);

    char *tok = buf;
    while (tok && *tok && cs->count < WOW_MAX_CONSTRAINTS) {
        /* Find next comma (constraint separator) */
        char *comma = strchr(tok, ',');
        if (comma) *comma = '\0';

        /* Skip leading whitespace */
        while (isspace((unsigned char)*tok)) tok++;
        if (*tok == '\0') {
            tok = comma ? comma + 1 : NULL;
            continue;
        }

        wow_gem_constraint *c = &cs->items[cs->count];
        const char *p = tok;
        c->op = parse_op(&p);

        /* Skip whitespace between operator and version */
        while (isspace((unsigned char)*p)) p++;

        if (wow_gemver_parse(p, &c->ver) != 0)
            return -1;

        cs->count++;
        tok = comma ? comma + 1 : NULL;
    }

    return (cs->count > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Constraint matching                                                 */
/* ------------------------------------------------------------------ */

/*
 * Check if constraint c has a pre-release version.
 * Used to determine whether pre-release candidates should be matched.
 */
static bool constraint_has_prerelease(const wow_gem_constraint *c)
{
    return c->ver.prerelease;
}

/*
 * Match a single constraint against a version.
 */
static bool match_one(const wow_gem_constraint *c, const wow_gemver *v)
{
    int cmp = wow_gemver_cmp(v, &c->ver);

    switch (c->op) {
    case WOW_OP_EQ:         return cmp == 0;
    case WOW_OP_NEQ:        return cmp != 0;
    case WOW_OP_GT:         return cmp > 0;
    case WOW_OP_GTE:        return cmp >= 0;
    case WOW_OP_LT:         return cmp < 0;
    case WOW_OP_LTE:        return cmp <= 0;
    case WOW_OP_PESSIMISTIC: {
        /*
         * ~> X.Y.Z means >= X.Y.Z AND < X.(Y+1).0
         * ~> X.Y   means >= X.Y   AND < (X+1).0
         *
         * General rule: bump the second-to-last segment, drop the last.
         */
        if (cmp < 0) return false;  /* must be >= constraint version */

        /* Build upper bound: copy segments, bump penultimate, drop last */
        wow_gemver upper;
        memset(&upper, 0, sizeof(upper));

        int n = c->ver.n_segs;
        if (n < 2) {
            /* ~> 4 is unusual but means >= 4, < 5 */
            upper.segs[0].num = c->ver.segs[0].num + 1;
            upper.segs[0].is_str = false;
            upper.n_segs = 1;
        } else {
            /* Copy all but the last segment */
            for (int i = 0; i < n - 1; i++)
                upper.segs[i] = c->ver.segs[i];
            upper.n_segs = n - 1;

            /* Bump the last copied segment (now penultimate of original) */
            wow_ver_seg *bump = &upper.segs[n - 2];
            if (!bump->is_str) {
                bump->num++;
            }
            /* If it's a string segment we can't really bump it;
             * this would be a weird constraint like ~> 1.beta.
             * In practice this doesn't occur. */
        }

        /* v must be < upper */
        return wow_gemver_cmp(v, &upper) < 0;
    }
    }
    return false;
}

bool wow_gemver_match(const wow_gem_constraints *cs, const wow_gemver *v)
{
    if (!cs || !v || cs->count == 0) return false;

    /*
     * Pre-release gate: if v is a pre-release, at least one constraint
     * must explicitly reference a pre-release version. This matches
     * RubyGems behaviour where ~> 4.0 does NOT match 4.0.0.beta.1,
     * but ~> 4.0.0.beta DOES.
     */
    if (v->prerelease) {
        bool any_prerelease = false;
        for (int i = 0; i < cs->count; i++) {
            if (constraint_has_prerelease(&cs->items[i])) {
                any_prerelease = true;
                break;
            }
        }
        if (!any_prerelease) return false;
    }

    /* All constraints must match (AND semantics) */
    for (int i = 0; i < cs->count; i++) {
        if (!match_one(&cs->items[i], v))
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Display formatting                                                  */
/* ------------------------------------------------------------------ */

static const char *op_str(enum wow_ver_op op)
{
    switch (op) {
    case WOW_OP_EQ:         return "= ";
    case WOW_OP_NEQ:        return "!= ";
    case WOW_OP_GT:         return "> ";
    case WOW_OP_GTE:        return ">= ";
    case WOW_OP_LT:         return "< ";
    case WOW_OP_LTE:        return "<= ";
    case WOW_OP_PESSIMISTIC: return "~> ";
    }
    return "";
}

char *wow_gem_constraints_fmt(const wow_gem_constraints *cs,
                              char *buf, size_t bufsz)
{
    if (!cs || !buf || bufsz == 0) return buf;
    buf[0] = '\0';

    size_t off = 0;
    for (int i = 0; i < cs->count && off < bufsz - 1; i++) {
        if (i > 0) {
            int n = snprintf(buf + off, bufsz - off, ", ");
            if (n > 0) off += (size_t)n;
        }
        int n = snprintf(buf + off, bufsz - off, "%s%s",
                         op_str(cs->items[i].op),
                         cs->items[i].ver.raw);
        if (n > 0) off += (size_t)n;
    }

    return buf;
}
