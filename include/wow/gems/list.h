#ifndef WOW_GEMS_LIST_H
#define WOW_GEMS_LIST_H

/*
 * List the entries inside a .gem file (outer uncompressed tar).
 * Prints entry name + size to stdout.
 * Returns 0 on success, -1 on error.
 */
int wow_gem_list(const char *gem_path);

#endif
