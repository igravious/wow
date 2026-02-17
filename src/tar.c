/*
 * tar.c — streaming tar+gzip extraction
 *
 * Reads a .tar.gz file from disc, decompresses with zlib, and extracts
 * entries to a destination directory.  Reusable across Phase 3 (Ruby
 * download) and Phase 4 (.gem unpack).
 *
 * Reference: demos/phase0/demo_tar.c (Phase 0a proof-of-concept).
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <third_party/zlib/zlib.h>

#include "wow/tar.h"

/* Per-file extraction limit (100 MiB) */
#define TAR_MAX_FILE_SIZE (100ULL * 1024 * 1024)

/* I/O buffer sizes */
#define ZBUF_SIZE  65536   /* compressed input buffer */
#define TBUF_SIZE  65536   /* decompressed tar data buffer */

/* ── ustar tar header — 512 bytes ────────────────────────────────── */

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
    char magic[6];      /* 257 — "ustar\0" or "ustar " */
    char version[2];    /* 263 — "00" */
    char uname[32];     /* 265 */
    char gname[32];     /* 297 */
    char devmajor[8];   /* 329 */
    char devminor[8];   /* 337 */
    char prefix[155];   /* 345 */
    char padding[12];   /* 500 */
} tar_header_t;

_Static_assert(sizeof(tar_header_t) == 512, "tar header must be 512 bytes");

/* ── Streaming gzip+tar reader ───────────────────────────────────── */

struct tar_reader {
    z_stream   zstrm;
    FILE      *input;
    uint8_t    zbuf[ZBUF_SIZE];     /* compressed input */
    uint8_t    tbuf[TBUF_SIZE];     /* decompressed tar data */
    size_t     tpos;                /* read position in tbuf */
    size_t     tlen;                /* valid bytes in tbuf */
    int        zeof;                /* zlib hit Z_STREAM_END */
};

/* ── Helpers ─────────────────────────────────────────────────────── */

static size_t parse_octal(const char *s, int len)
{
    size_t val = 0;
    for (int i = 0; i < len && s[i] != '\0' && s[i] != ' '; i++) {
        if (s[i] < '0' || s[i] > '7') return 0;
        val = val * 8 + (size_t)(s[i] - '0');
    }
    return val;
}

static int is_zero_block(const void *block)
{
    const unsigned char *p = block;
    for (int i = 0; i < 512; i++)
        if (p[i] != 0) return 0;
    return 1;
}

/*
 * Recursive mkdir -p.  Modifies path in place temporarily.
 */
static int mkdirs(char *path, mode_t mode)
{
    char *p = path;
    if (*p == '/') p++;

    for (; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, mode) != 0 && errno != EEXIST) {
            fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    /* Final component (if path doesn't end with /) */
    if (p > path && *(p - 1) != '/') {
        if (mkdir(path, mode) != 0 && errno != EEXIST) {
            fprintf(stderr, "wow: mkdir %s: %s\n", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/*
 * Make parent directories for a file path.
 */
static int mkdirs_for_file(const char *filepath, mode_t mode)
{
    char *tmp = strdup(filepath);
    if (!tmp) return -1;

    char *slash = strrchr(tmp, '/');
    if (slash && slash != tmp) {
        *slash = '\0';
        int rc = mkdirs(tmp, mode);
        free(tmp);
        return rc;
    }
    free(tmp);
    return 0;
}

/*
 * Strip leading path components.
 * Returns pointer into name (or NULL if entirely stripped away).
 * E.g. strip_path("x64/bin/ruby", 1) → "bin/ruby"
 */
static const char *strip_path(const char *name, int strip)
{
    const char *p = name;
    for (int i = 0; i < strip && *p; i++) {
        const char *slash = strchr(p, '/');
        if (!slash) return NULL;  /* Fewer components than strip count */
        p = slash + 1;
    }
    return *p ? p : NULL;
}

/*
 * Security: check a path for traversal attacks.
 * Rejects absolute paths and any component that is "..".
 */
static int path_is_safe(const char *path)
{
    if (!path || !*path) return 0;

    /* Absolute path */
    if (path[0] == '/') return 0;

    /* Check each component */
    const char *p = path;
    while (*p) {
        /* Check for ".." component */
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
            return 0;

        /* Skip to next component */
        const char *slash = strchr(p, '/');
        if (!slash) break;
        p = slash + 1;
    }
    return 1;
}

/*
 * Security: check that a symlink target doesn't escape dest_dir.
 * For absolute targets: always reject.
 * For relative targets: resolve against the symlink's parent dir
 * and ensure it stays within dest_dir.
 */
static int symlink_target_is_safe(const char *target, const char *entry_path)
{
    if (!target || !*target) return 0;
    if (target[0] == '/') return 0;

    /*
     * Walk the combined path (entry's parent dir + target) and count
     * how many levels above the entry's dir we go.  If we ever go
     * above the extraction root, it's unsafe.
     */
    /* Count depth of entry's parent */
    int depth = 0;
    for (const char *p = entry_path; *p; p++)
        if (*p == '/') depth++;

    /* Walk the target */
    const char *p = target;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
            depth--;
            if (depth < 0) return 0;
            p += (p[2] == '/') ? 3 : 2;
        } else {
            const char *slash = strchr(p, '/');
            if (!slash) break;
            depth++;
            p = slash + 1;
        }
    }
    return 1;
}

/* ── tar_reader I/O ──────────────────────────────────────────────── */

static int tar_reader_init(struct tar_reader *r, const char *gz_path)
{
    memset(r, 0, sizeof(*r));

    r->input = fopen(gz_path, "rb");
    if (!r->input) {
        fprintf(stderr, "wow: cannot open %s: %s\n", gz_path, strerror(errno));
        return -1;
    }

    /* 16 + MAX_WBITS tells zlib to handle gzip header */
    if (inflateInit2(&r->zstrm, 16 + MAX_WBITS) != Z_OK) {
        fprintf(stderr, "wow: zlib inflateInit2 failed\n");
        fclose(r->input);
        return -1;
    }

    return 0;
}

static void tar_reader_close(struct tar_reader *r)
{
    inflateEnd(&r->zstrm);
    if (r->input) fclose(r->input);
}

/*
 * Read exactly n bytes of decompressed tar data.
 * Returns 0 on success, -1 on error/premature EOF.
 */
static int tar_reader_read(struct tar_reader *r, void *buf, size_t n)
{
    uint8_t *dst = buf;
    size_t remaining = n;

    while (remaining > 0) {
        /* Consume from decompressed buffer first */
        if (r->tpos < r->tlen) {
            size_t avail = r->tlen - r->tpos;
            size_t take = avail < remaining ? avail : remaining;
            memcpy(dst, r->tbuf + r->tpos, take);
            r->tpos += take;
            dst += take;
            remaining -= take;
            continue;
        }

        /* Need more decompressed data */
        if (r->zeof)
            return -1;  /* No more data */

        /* Feed compressed data to zlib if its input is exhausted */
        if (r->zstrm.avail_in == 0) {
            size_t got = fread(r->zbuf, 1, ZBUF_SIZE, r->input);
            if (got == 0) {
                if (ferror(r->input)) {
                    fprintf(stderr, "wow: read error on tar.gz\n");
                    return -1;
                }
                /* EOF on compressed stream but zlib hasn't finished */
                r->zeof = 1;
                if (remaining > 0) return -1;
                return 0;
            }
            r->zstrm.next_in = r->zbuf;
            r->zstrm.avail_in = (uInt)got;
        }

        /* Decompress into tbuf */
        r->zstrm.next_out = r->tbuf;
        r->zstrm.avail_out = TBUF_SIZE;

        int zrc = inflate(&r->zstrm, Z_NO_FLUSH);
        if (zrc == Z_STREAM_END) {
            r->zeof = 1;
        } else if (zrc != Z_OK) {
            fprintf(stderr, "wow: zlib inflate error: %d\n", zrc);
            return -1;
        }

        r->tpos = 0;
        r->tlen = TBUF_SIZE - r->zstrm.avail_out;
    }
    return 0;
}

/*
 * Skip n bytes of decompressed tar data.
 */
static int tar_reader_skip(struct tar_reader *r, size_t n)
{
    while (n > 0) {
        if (r->tpos < r->tlen) {
            size_t avail = r->tlen - r->tpos;
            size_t skip = avail < n ? avail : n;
            r->tpos += skip;
            n -= skip;
            continue;
        }
        /* Read into tbuf just to discard */
        uint8_t discard[512];
        size_t chunk = n < sizeof(discard) ? n : sizeof(discard);
        if (tar_reader_read(r, discard, chunk) != 0)
            return -1;
        n -= chunk;
    }
    return 0;
}

/* ── Main extraction logic ───────────────────────────────────────── */

int wow_tar_extract_gz(const char *gz_path, const char *dest_dir,
                       int strip_components)
{
    struct tar_reader reader;
    if (tar_reader_init(&reader, gz_path) != 0)
        return -1;

    int ret = -1;
    int zero_blocks = 0;
    char long_name[PATH_MAX];
    int have_long_name = 0;

    for (;;) {
        tar_header_t hdr;
        if (tar_reader_read(&reader, &hdr, 512) != 0) {
            /* Premature EOF — could be truncated archive */
            if (zero_blocks > 0) {
                ret = 0;  /* Had at least one zero block, likely just end */
                break;
            }
            fprintf(stderr, "wow: tar: unexpected end of archive\n");
            break;
        }

        if (is_zero_block(&hdr)) {
            zero_blocks++;
            if (zero_blocks >= 2) {
                ret = 0;  /* Normal end of archive */
                break;
            }
            continue;
        }
        zero_blocks = 0;

        /* Verify ustar magic (accept both "ustar\0" and "ustar ") */
        if (memcmp(hdr.magic, "ustar", 5) != 0) {
            fprintf(stderr, "wow: tar: bad magic '%.6s' (not ustar)\n",
                    hdr.magic);
            break;
        }

        size_t size = parse_octal(hdr.size, 12);
        size_t blocks = (size + 511) / 512;
        char typeflag = hdr.typeflag ? hdr.typeflag : '0';

        /* GNU @LongLink extension: next header's name is in this entry's data */
        if (typeflag == 'L') {
            if (size >= PATH_MAX) {
                fprintf(stderr, "wow: tar: @LongLink too long (%zu)\n", size);
                break;
            }
            if (tar_reader_read(&reader, long_name, size) != 0) break;
            long_name[size] = '\0';
            /* Skip padding to 512-byte boundary */
            size_t pad = blocks * 512 - size;
            if (pad > 0 && tar_reader_skip(&reader, pad) != 0) break;
            have_long_name = 1;
            continue;
        }

        /* Build full entry name */
        char entry_name[PATH_MAX];
        if (have_long_name) {
            snprintf(entry_name, sizeof(entry_name), "%s", long_name);
            have_long_name = 0;
        } else if (hdr.prefix[0]) {
            /* ustar prefix + name */
            snprintf(entry_name, sizeof(entry_name), "%.155s/%.100s",
                     hdr.prefix, hdr.name);
        } else {
            snprintf(entry_name, sizeof(entry_name), "%.100s", hdr.name);
        }

        /* Strip leading path components */
        const char *stripped = strip_path(entry_name, strip_components);
        if (!stripped || !*stripped) {
            /* Entry stripped away entirely — skip data */
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;
            continue;
        }

        /* ── Security checks ──────────────────────────────────────── */

        if (!path_is_safe(stripped)) {
            fprintf(stderr, "wow: tar: rejecting unsafe path: %s\n", stripped);
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;
            continue;
        }

        /* Reject dangerous entry types */
        if (typeflag == '1') {
            fprintf(stderr, "wow: tar: rejecting hard link: %s\n", stripped);
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;
            continue;
        }
        if (typeflag == '3' || typeflag == '4' || typeflag == '6') {
            fprintf(stderr, "wow: tar: rejecting device/FIFO: %s\n", stripped);
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;
            continue;
        }

        if (size > TAR_MAX_FILE_SIZE) {
            fprintf(stderr, "wow: tar: file too large (%zu bytes): %s\n",
                    size, stripped);
            break;
        }

        /* Build output path */
        char outpath[PATH_MAX];
        int n = snprintf(outpath, sizeof(outpath), "%s/%s", dest_dir, stripped);
        if (n < 0 || (size_t)n >= sizeof(outpath)) {
            fprintf(stderr, "wow: tar: path too long: %s/%s\n",
                    dest_dir, stripped);
            break;
        }

        /* ── Extract by type ──────────────────────────────────────── */

        if (typeflag == '5') {
            /* Directory */
            if (mkdirs(outpath, 0755) != 0) break;
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;

        } else if (typeflag == '2') {
            /* Symlink */
            char link_target[PATH_MAX];
            snprintf(link_target, sizeof(link_target), "%.100s", hdr.linkname);

            if (!symlink_target_is_safe(link_target, stripped)) {
                fprintf(stderr, "wow: tar: rejecting symlink escape: "
                        "%s -> %s\n", stripped, link_target);
                if (blocks > 0 &&
                    tar_reader_skip(&reader, blocks * 512) != 0)
                    break;
                continue;
            }

            if (mkdirs_for_file(outpath, 0755) != 0) break;

            /* Remove existing file/symlink if present */
            unlink(outpath);

            if (symlink(link_target, outpath) != 0) {
                fprintf(stderr, "wow: tar: symlink %s -> %s: %s\n",
                        outpath, link_target, strerror(errno));
                break;
            }
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;

        } else if (typeflag == '0' || typeflag == '\0') {
            /* Regular file */
            if (mkdirs_for_file(outpath, 0755) != 0) break;

            mode_t mode = (mode_t)parse_octal(hdr.mode, 8);
            if (mode == 0) mode = 0644;
            /* Ensure owner can read+write, preserve execute bit */
            mode |= 0600;

            FILE *out = fopen(outpath, "wb");
            if (!out) {
                fprintf(stderr, "wow: tar: cannot create %s: %s\n",
                        outpath, strerror(errno));
                break;
            }

            /* Write file data */
            size_t remaining = size;
            uint8_t filebuf[8192];
            while (remaining > 0) {
                size_t chunk = remaining < sizeof(filebuf)
                             ? remaining : sizeof(filebuf);
                if (tar_reader_read(&reader, filebuf, chunk) != 0) {
                    fclose(out);
                    goto done;
                }
                if (fwrite(filebuf, 1, chunk, out) != chunk) {
                    fprintf(stderr, "wow: tar: write error: %s\n",
                            outpath);
                    fclose(out);
                    goto done;
                }
                remaining -= chunk;
            }
            fclose(out);

            /* Set file permissions */
            chmod(outpath, mode);

            /* Skip tar block padding */
            size_t pad = blocks * 512 - size;
            if (pad > 0 && tar_reader_skip(&reader, pad) != 0) break;

        } else {
            /* Unknown type — skip */
            fprintf(stderr, "wow: tar: skipping unknown type '%c': %s\n",
                    typeflag, stripped);
            if (blocks > 0 && tar_reader_skip(&reader, blocks * 512) != 0)
                break;
        }
    }

done:
    tar_reader_close(&reader);
    return ret;
}
