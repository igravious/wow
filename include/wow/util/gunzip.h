#ifndef WOW_UTIL_GUNZIP_H
#define WOW_UTIL_GUNZIP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Decompress gzip data in memory.
 *
 * gz_data:     Compressed input data.
 * gz_len:      Length of compressed data.
 * out_data:    On success, receives malloc'd buffer (caller frees).
 * out_len:     On success, receives decompressed length.
 * max_output:  Maximum allowed decompressed size (guards unbounded malloc).
 *
 * Returns 0 on success, -1 on error.
 */
int wow_gunzip(const uint8_t *gz_data, size_t gz_len,
               uint8_t **out_data, size_t *out_len,
               size_t max_output);

#endif
