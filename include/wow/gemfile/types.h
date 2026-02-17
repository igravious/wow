#ifndef WOW_GEMFILE_TYPES_H
#define WOW_GEMFILE_TYPES_H

/*
 * types.h -- data structures for parsed Gemfile content
 *
 * A Gemfile is parsed into a wow_gemfile struct containing the source URL,
 * ruby version, gemspec flag, and a dynamic array of gem dependencies.
 * Version constraints are stored as opaque strings; Phase 6 (PubGrub)
 * will evaluate them.
 */

#include <stdbool.h>
#include <stddef.h>

/* A single dependency from a Gemfile gem declaration */
struct wow_gemfile_dep {
    char  *name;            /* "sinatra"                            */
    char **constraints;     /* ["~> 4.0", ">= 1.0"] -- opaque strs */
    int    n_constraints;
    char  *group;           /* "development" / "test" / NULL        */
    bool   require;         /* false when require: false             */
};

/* Parsed Gemfile */
struct wow_gemfile {
    char  *source;          /* "https://rubygems.org"               */
    char  *ruby_version;    /* "3.3.0" or NULL                      */
    bool   has_gemspec;
    struct wow_gemfile_dep *deps;
    size_t n_deps;
    /* internal */
    size_t _deps_cap;
    char  *_current_group;  /* set during group do...end parsing    */
};

/* Initialise a gemfile struct to safe empty state */
void wow_gemfile_init(struct wow_gemfile *gf);

/* Free all memory owned by a gemfile struct */
void wow_gemfile_free(struct wow_gemfile *gf);

/* Add a dependency (transfers ownership of dep fields to gf) */
int wow_gemfile_add_dep(struct wow_gemfile *gf,
                        struct wow_gemfile_dep *dep);

#endif
