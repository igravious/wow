#ifndef WOW_REGISTRY_H
#define WOW_REGISTRY_H

#include <stddef.h>

struct wow_dep {
    char *name;
    char *requirements;
};

struct wow_gem_info {
    char *name;
    char *version;
    char *authors;
    char *summary;
    char *gem_uri;
    char *sha;
    struct wow_dep *deps;
    size_t n_deps;
};

/* Fetch gem info from rubygems.org. Returns 0 on success, -1 on error. */
int  wow_gem_info_fetch(const char *name, struct wow_gem_info *info);
void wow_gem_info_free(struct wow_gem_info *info);

#endif
