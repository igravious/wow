#ifndef WOW_RUBIES_INTERNAL_H
#define WOW_RUBIES_INTERNAL_H

/*
 * Internal helpers for Ruby version management.
 * Not part of the public API.
 */

/* File locking for atomic installation */
int wow_rubies_acquire_lock(const char *base_dir);
void wow_rubies_release_lock(int fd);

/* Recursively remove directory */
int wow_rubies_rmdir_recursive(const char *path);

#endif
