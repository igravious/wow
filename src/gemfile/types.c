/*
 * types.c -- wow_gemfile lifecycle management
 */

#include <stdlib.h>
#include <string.h>

#include "wow/gemfile/types.h"

void wow_gemfile_init(struct wow_gemfile *gf)
{
    memset(gf, 0, sizeof(*gf));
}

void wow_gemfile_free(struct wow_gemfile *gf)
{
    free(gf->source);
    free(gf->ruby_version);
    for (int i = 0; i < gf->_n_current_groups; i++)
        free(gf->_current_groups[i]);
    free(gf->_current_groups);
    /* Free any remaining group stack frames (defensive) */
    for (int d = 0; d < gf->_group_depth; d++) {
        for (int i = 0; i < gf->_group_stack[d].n_groups; i++)
            free(gf->_group_stack[d].groups[i]);
        free(gf->_group_stack[d].groups);
    }
    for (int i = 0; i < gf->_n_current_platforms; i++)
        free(gf->_current_platforms[i]);
    free(gf->_current_platforms);

    for (size_t i = 0; i < gf->n_deps; i++) {
        struct wow_gemfile_dep *d = &gf->deps[i];
        free(d->name);
        for (int j = 0; j < d->n_constraints; j++)
            free(d->constraints[j]);
        free(d->constraints);
        for (int j = 0; j < d->n_groups; j++)
            free(d->groups[j]);
        free(d->groups);
        for (int j = 0; j < d->n_autorequire; j++)
            free(d->autorequire[j]);
        free(d->autorequire);
        for (int j = 0; j < d->n_platforms; j++)
            free(d->platforms[j]);
        free(d->platforms);
    }
    free(gf->deps);

    memset(gf, 0, sizeof(*gf));
}

int wow_gemfile_add_dep(struct wow_gemfile *gf, struct wow_gemfile_dep *dep)
{
    if (gf->n_deps >= gf->_deps_cap) {
        size_t new_cap = gf->_deps_cap ? gf->_deps_cap * 2 : 8;
        struct wow_gemfile_dep *p = realloc(
            gf->deps, sizeof(*p) * new_cap);
        if (!p) return -1;
        gf->deps = p;
        gf->_deps_cap = new_cap;
    }
    gf->deps[gf->n_deps++] = *dep;
    return 0;
}
