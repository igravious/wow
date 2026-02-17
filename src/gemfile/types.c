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
    free(gf->_current_group);

    for (size_t i = 0; i < gf->n_deps; i++) {
        struct wow_gemfile_dep *d = &gf->deps[i];
        free(d->name);
        for (int j = 0; j < d->n_constraints; j++)
            free(d->constraints[j]);
        free(d->constraints);
        free(d->group);
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
