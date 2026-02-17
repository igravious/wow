/*
 * demo_tar.c — Phase 0a demo: parse a .gem file (ustar tar)
 *
 * Reads a .gem file, lists its entries, extracts metadata.gz,
 * decompresses it, and prints the YAML gemspec.
 *
 * Build:  cosmocc -o demo_tar.com demo_tar.c -lz
 * Usage:  ./demo_tar.com sinatra-4.1.1.gem
 *         curl -sO https://rubygems.org/downloads/sinatra-4.1.1.gem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <third_party/zlib/zlib.h>

/* ustar tar header — 512 bytes */
typedef struct {
    char name[100];     /* 0   */
    char mode[8];       /* 100 */
    char uid[8];        /* 108 */
    char gid[8];        /* 116 */
    char size[12];      /* 124 — octal ASCII */
    char mtime[12];     /* 136 */
    char checksum[8];   /* 148 */
    char typeflag;      /* 156 */
    char linkname[100]; /* 157 */
    char magic[6];      /* 257 — "ustar\0" */
    char version[2];    /* 263 — "00" */
    char uname[32];     /* 265 */
    char gname[32];     /* 297 */
    char devmajor[8];   /* 329 */
    char devminor[8];   /* 337 */
    char prefix[155];   /* 345 */
    char padding[12];   /* 500 */
} tar_header_t;

/* Parse octal ASCII size field */
static size_t parse_octal(const char *s, int len)
{
    size_t val = 0;
    for (int i = 0; i < len && s[i] != '\0' && s[i] != ' '; i++)
        val = val * 8 + (s[i] - '0');
    return val;
}

/* Check if a tar block is all zeros (end-of-archive marker) */
static int is_zero_block(const tar_header_t *hdr)
{
    const unsigned char *p = (const unsigned char *)hdr;
    for (int i = 0; i < 512; i++)
        if (p[i] != 0) return 0;
    return 1;
}

/* Decompress gzip data and print it */
static int decompress_and_print(const unsigned char *gz_data, size_t gz_len)
{
    z_stream strm = {0};
    /* 16 + MAX_WBITS tells zlib to handle gzip header */
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        fprintf(stderr, "inflateInit2 failed\n");
        return -1;
    }

    strm.next_in = (unsigned char *)gz_data;
    strm.avail_in = gz_len;

    unsigned char buf[4096];
    int ret;
    do {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            fprintf(stderr, "inflate error: %d\n", ret);
            inflateEnd(&strm);
            return -1;
        }
        size_t have = sizeof(buf) - strm.avail_out;
        fwrite(buf, 1, have, stdout);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.gem>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror(argv[1]);
        return 1;
    }

    printf("=== .gem contents: %s ===\n\n", argv[1]);

    tar_header_t hdr;
    int entry_num = 0;
    unsigned char *metadata_gz = NULL;
    size_t metadata_gz_len = 0;

    while (fread(&hdr, 1, 512, fp) == 512) {
        if (is_zero_block(&hdr))
            break;

        /* Verify ustar magic */
        if (memcmp(hdr.magic, "ustar", 5) != 0) {
            fprintf(stderr, "Not a ustar tar (magic: '%.6s')\n", hdr.magic);
            break;
        }

        size_t size = parse_octal(hdr.size, 12);
        /* Round up to next 512-byte block */
        size_t blocks = (size + 511) / 512;

        entry_num++;
        printf("Entry %d: %-24s  size=%zu  type=%c  mode=%s\n",
               entry_num, hdr.name, size,
               hdr.typeflag ? hdr.typeflag : '0', hdr.mode);

        /* Read entry data */
        unsigned char *data = malloc(blocks * 512);
        if (!data || fread(data, 1, blocks * 512, fp) != blocks * 512) {
            fprintf(stderr, "Failed to read entry data\n");
            free(data);
            break;
        }

        /* Stash metadata.gz for later decompression */
        if (strcmp(hdr.name, "metadata.gz") == 0) {
            metadata_gz = malloc(size);
            if (metadata_gz) {
                memcpy(metadata_gz, data, size);
                metadata_gz_len = size;
            }
        }

        free(data);
    }

    fclose(fp);

    printf("\n--- %d entries found ---\n", entry_num);

    /* Decompress and print metadata */
    if (metadata_gz) {
        printf("\n=== Decompressed metadata.gz (YAML gemspec) ===\n\n");
        decompress_and_print(metadata_gz, metadata_gz_len);
        free(metadata_gz);
    } else {
        printf("\nNo metadata.gz found in archive.\n");
    }

    return 0;
}
