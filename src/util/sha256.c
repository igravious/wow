/*
 * util/sha256.c — SHA-256 hashing utilities
 *
 * Uses mbedTLS for SHA-256 computation.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <third_party/mbedtls/sha256.h>

#include "wow/util/sha256.h"

int wow_sha256_file(const char *path, char *out_hex, size_t hex_sz)
{
    if (hex_sz < 65) {
        fprintf(stderr, "wow: output buffer too small for SHA-256 hex\n");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "wow: cannot open %s for hashing: %s\n",
                path, strerror(errno));
        return -1;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);  /* 0 = SHA-256, not SHA-224 */

    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        mbedtls_sha256_update_ret(&ctx, buf, n);

    int read_err = ferror(f);
    fclose(f);

    if (read_err) {
        fprintf(stderr, "wow: read error hashing %s\n", path);
        mbedtls_sha256_free(&ctx);
        return -1;
    }

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    for (int i = 0; i < 32; i++)
        snprintf(out_hex + i * 2, 3, "%02x", digest[i]);
    out_hex[64] = '\0';

    return 0;
}
