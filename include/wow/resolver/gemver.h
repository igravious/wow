#ifndef WOW_RESOLVER_GEMVER_H
#define WOW_RESOLVER_GEMVER_H

/*
 * gemver.h -- RubyGems-compatible version parsing + constraint matching
 *
 * RubyGems versions are segment-based (NOT semver). Key differences:
 *   - Arbitrary segment count: 4.0.1.1 is valid
 *   - Pre-release: segment starting with a letter (beta, rc1) sorts
 *     BEFORE the corresponding release: 4.0.0.beta.2 < 4.0.0
 *   - Trailing zeros: 4.0 == 4.0.0 for comparison
 *   - Mixed segments: 4.0.0.beta.2 = [4, 0, 0, "beta", 2]
 */

#include <stdbool.h>

#define WOW_VER_MAX_SEGS    16
#define WOW_VER_SEG_STRSZ   64
#define WOW_VER_RAW_SZ      128
#define WOW_MAX_CONSTRAINTS  8

/* A single version segment: numeric or string (pre-release) */
typedef struct {
    int  num;                       /* numeric value (if !is_str) */
    char str[WOW_VER_SEG_STRSZ];   /* string value (if is_str)   */
    bool is_str;
} wow_ver_seg;

/* Parsed gem version */
typedef struct {
    wow_ver_seg segs[WOW_VER_MAX_SEGS];
    int         n_segs;
    bool        prerelease;         /* true if any segment is a string */
    char        raw[WOW_VER_RAW_SZ]; /* original string for display   */
} wow_gemver;

/* Comparison operators */
enum wow_ver_op {
    WOW_OP_EQ,         /* =  (or no operator) */
    WOW_OP_NEQ,        /* != */
    WOW_OP_GT,         /* >  */
    WOW_OP_GTE,        /* >= */
    WOW_OP_LT,         /* <  */
    WOW_OP_LTE,        /* <= */
    WOW_OP_PESSIMISTIC, /* ~> */
};

/* A single constraint: operator + version */
typedef struct {
    enum wow_ver_op op;
    wow_gemver      ver;
} wow_gem_constraint;

/* A constraint set (AND of constraints): ">= 3.0, < 4" */
typedef struct {
    wow_gem_constraint items[WOW_MAX_CONSTRAINTS];
    int                count;
} wow_gem_constraints;

/*
 * Parse a version string into a wow_gemver.
 * Returns 0 on success, -1 on parse error.
 */
int wow_gemver_parse(const char *s, wow_gemver *v);

/*
 * Compare two parsed versions.
 * Returns <0 if a < b, 0 if a == b, >0 if a > b.
 * Trailing zeros are equivalent: 4.0 == 4.0.0.
 * Pre-release segments sort BEFORE the release: 4.0.0.beta < 4.0.0.
 */
int wow_gemver_cmp(const wow_gemver *a, const wow_gemver *b);

/*
 * Test whether version v satisfies ALL constraints in cs.
 * Pre-release versions are only matched if the constraint itself
 * contains a pre-release version (RubyGems behaviour).
 * Returns true if all constraints match, false otherwise.
 */
bool wow_gemver_match(const wow_gem_constraints *cs, const wow_gemver *v);

/*
 * Parse a constraint string like "~> 4.0, >= 3.1" into a constraint set.
 * Constraints are comma-separated. Each is: [operator] version.
 * Returns 0 on success, -1 on parse error.
 */
int wow_gem_constraints_parse(const char *s, wow_gem_constraints *cs);

/*
 * Format a constraint set back to string for display.
 * Writes to buf (max bufsz chars). Returns buf.
 */
char *wow_gem_constraints_fmt(const wow_gem_constraints *cs,
                              char *buf, size_t bufsz);

#endif
