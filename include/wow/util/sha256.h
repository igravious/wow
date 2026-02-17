#ifndef WOW_UTIL_SHA256_H
#define WOW_UTIL_SHA256_H

#include <stddef.h>

/*
 * Compute SHA-256 of a file and return as hex string.
 *
 * path:     Path to file to hash.
 * out_hex:  Buffer to receive 64-character hex digest + null terminator.
 * hex_sz:   Size of out_hex buffer (must be >= 65).
 *
 * Returns 0 on success, -1 on error.
 */
int wow_sha256_file(const char *path, char *out_hex, size_t hex_sz);

#endif
