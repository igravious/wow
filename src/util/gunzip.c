/*
 * util/gunzip.c — In-memory gzip decompression
 *
 * Uses zlib for decompression with size limits to prevent unbounded
 * memory allocation on malformed input.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <third_party/zlib/zlib.h>

#include "wow/util/gunzip.h"

int wow_gunzip(const uint8_t *gz_data, size_t gz_len,
               uint8_t **out_data, size_t *out_len,
               size_t max_output)
{
    if (gz_len > (size_t)UINT_MAX) {
        fprintf(stderr, "wow: compressed data too large\n");
        return -1;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    /* 16 + MAX_WBITS = gzip header handling */
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        fprintf(stderr, "wow: zlib inflateInit2 failed\n");
        return -1;
    }

    strm.next_in = (uint8_t *)gz_data;
    strm.avail_in = (uInt)gz_len;

    size_t alloc = gz_len * 4;
    if (alloc < 4096) alloc = 4096;
    if (alloc > max_output) alloc = max_output;

    uint8_t *buf = malloc(alloc);
    if (!buf) {
        inflateEnd(&strm);
        return -1;
    }

    size_t total = 0;
    int zrc;
    do {
        if (total >= alloc) {
            size_t newalloc = alloc * 2;
            if (newalloc > max_output) {
                fprintf(stderr, "wow: decompressed data exceeds max size\n");
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            uint8_t *tmp = realloc(buf, newalloc);
            if (!tmp) {
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            buf = tmp;
            alloc = newalloc;
        }

        strm.next_out = buf + total;
        strm.avail_out = (uInt)(alloc - total);
        zrc = inflate(&strm, Z_NO_FLUSH);

        if (zrc == Z_STREAM_ERROR || zrc == Z_DATA_ERROR || zrc == Z_MEM_ERROR) {
            fprintf(stderr, "wow: zlib inflate error: %d\n", zrc);
            free(buf);
            inflateEnd(&strm);
            return -1;
        }

        total = alloc - strm.avail_out;
    } while (zrc != Z_STREAM_END);

    inflateEnd(&strm);
    *out_data = buf;
    *out_len = total;
    return 0;
}
