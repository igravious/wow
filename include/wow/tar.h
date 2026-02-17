#ifndef WOW_TAR_H
#define WOW_TAR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Extract a gzipped tar archive to a destination directory.
 *
 * gz_path:          Path to the .tar.gz file on disc.
 * dest_dir:         Directory to extract into (must exist).
 * strip_components: Number of leading path components to strip (like
 *                   tar --strip-components). Use 1 to strip the x64/
 *                   prefix from ruby-builder tarballs.
 *
 * Security: rejects absolute paths, ".." traversal, hard links,
 * devices, FIFOs, and symlinks that escape dest_dir.
 *
 * Returns 0 on success, -1 on error (messages printed to stderr).
 */
int wow_tar_extract_gz(const char *gz_path, const char *dest_dir,
                       int strip_components);

/*
 * Extract an uncompressed tar archive to a destination directory.
 * Same interface and security guarantees as wow_tar_extract_gz().
 */
int wow_tar_extract(const char *tar_path, const char *dest_dir,
                    int strip_components);

/*
 * Callback for iterating tar entries.
 * name:     entry name (after stripping trailing slashes for dirs).
 * size:     entry size in bytes.
 * typeflag: tar type flag ('0'=file, '5'=dir, '2'=symlink, etc.).
 * ctx:      user-provided context pointer.
 *
 * Return 0 to continue iteration, non-zero to stop early.
 */
typedef int (*wow_tar_list_fn)(const char *name, size_t size,
                               char typeflag, void *ctx);

/*
 * Iterate entries in an uncompressed tar, calling fn for each.
 * Returns 0 on success, -1 on error.
 */
int wow_tar_list(const char *tar_path, wow_tar_list_fn fn, void *ctx);

/*
 * Extract a single named entry from an uncompressed tar to a buffer.
 *
 * entry_name: name of the entry to find (exact match).
 * out_data:   on success, *out_data is a malloc'd buffer (caller frees).
 * out_len:    on success, *out_len is the entry's size.
 * max_size:   reject entries larger than this (guards unbounded malloc).
 *
 * Returns 0 on success, -1 if entry not found or on error.
 */
int wow_tar_read_entry(const char *tar_path, const char *entry_name,
                       uint8_t **out_data, size_t *out_len, size_t max_size);

/*
 * Stream a single named entry from an uncompressed tar to a file descriptor.
 * Does not hold the entry in memory — streams in chunks.
 *
 * Returns 0 on success, -1 if entry not found or on error.
 */
int wow_tar_extract_entry_to_fd(const char *tar_path, const char *entry_name,
                                int fd);

#endif
