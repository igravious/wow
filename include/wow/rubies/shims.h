#ifndef WOW_RUBIES_SHIMS_H
#define WOW_RUBIES_SHIMS_H

/*
 * Shim creation for argv[0] dispatch.
 * Creates hardlinks/copies of wow binary for ruby, irb, gem, etc.
 */

/* Create shims in the shims directory */
int wow_create_shims(const char *wow_binary_path);

#endif
