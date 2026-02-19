#ifndef WOW_RESOLVER_H
#define WOW_RESOLVER_H

/*
 * Dependency resolver — PubGrub algorithm with RubyGems semantics.
 * This is a convenience header that includes all resolver submodules.
 */

#include "wow/resolver/gemver.h"
#include "wow/resolver/arena.h"
#include "wow/resolver/pubgrub.h"
#include "wow/resolver/provider.h"
#include "wow/resolver/lockfile.h"

/* Forward declarations for CLI handlers */
int cmd_resolve(int argc, char *argv[]);
int cmd_lock(int argc, char *argv[]);

#endif
