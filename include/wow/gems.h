#ifndef WOW_GEMS_H
#define WOW_GEMS_H

/*
 * Gem management — download, inspect, and unpack .gem files.
 * This is a convenience header that includes all gems submodules.
 */

#include "wow/gems/download.h"
#include "wow/gems/list.h"
#include "wow/gems/meta.h"
#include "wow/gems/unpack.h"

/* Forward declarations for CLI handlers */
int cmd_gem_download(int argc, char *argv[]);
int cmd_gem_list(int argc, char *argv[]);
int cmd_gem_meta(int argc, char *argv[]);
int cmd_gem_unpack(int argc, char *argv[]);

#endif
