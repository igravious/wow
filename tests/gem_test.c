/*
 * tests/gem_test.c — Gem infrastructure tests
 *
 * Offline tests: plain tar read/extract, tar_read_entry, tar_list,
 * tar_extract_entry_to_fd, SHA-256 verification, gunzip round-trip,
 * and gemspec YAML parsing.
 *
 * All fixtures are embedded — no network access or curl required.
 *
 * Run via: make test-gem
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <third_party/zlib/zlib.h>
#include <third_party/mbedtls/sha256.h>

#include "wow/tar.h"
#include "wow/gems/meta.h"

/* Composite path buffer */
#define TPATH (PATH_MAX + 256)

static int n_pass, n_fail;

static void check(const char *name, int condition) {
    if (condition) {
        printf("  PASS: %s\n", name);
        n_pass++;
    } else {
        printf("  FAIL: %s\n", name);
        n_fail++;
    }
}

/* ── Helpers for crafting tar test fixtures ───────────────────── */

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} test_tar_hdr_t;

_Static_assert(sizeof(test_tar_hdr_t) == 512, "tar header must be 512 bytes");

static void set_checksum(test_tar_hdr_t *h) {
    memset(h->checksum, ' ', 8);
    unsigned int sum = 0;
    const unsigned char *p = (const unsigned char *)h;
    for (int i = 0; i < 512; i++)
        sum += p[i];
    snprintf(h->checksum, sizeof(h->checksum), "%06o", sum);
}

static void init_hdr(test_tar_hdr_t *h, const char *name, char type,
                     size_t size, const char *linkname) {
    memset(h, 0, sizeof(*h));
    strncpy(h->name, name, sizeof(h->name) - 1);
    snprintf(h->mode, sizeof(h->mode), "%07o", type == '5' ? 0755 : 0644);
    snprintf(h->uid, sizeof(h->uid), "%07o", 1000);
    snprintf(h->gid, sizeof(h->gid), "%07o", 1000);
    snprintf(h->size, sizeof(h->size), "%011lo", (unsigned long)size);
    snprintf(h->mtime, sizeof(h->mtime), "%011o", 0);
    h->typeflag = type;
    if (linkname)
        strncpy(h->linkname, linkname, sizeof(h->linkname) - 1);
    memcpy(h->magic, "ustar", 6);
    memcpy(h->version, "00", 2);
    set_checksum(h);
}

/*
 * Write a plain (uncompressed) tar containing multiple file entries.
 * entries[i] = { name, data, datalen }.  Returns 0 on success.
 */
struct tar_entry {
    const char *name;
    const void *data;
    size_t      len;
};

static int make_plain_tar(const char *path, const struct tar_entry *entries,
                          int n_entries) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    for (int i = 0; i < n_entries; i++) {
        test_tar_hdr_t h;
        init_hdr(&h, entries[i].name, '0', entries[i].len, NULL);
        fwrite(&h, 1, 512, f);

        if (entries[i].len > 0) {
            fwrite(entries[i].data, 1, entries[i].len, f);
            size_t pad = (512 - (entries[i].len % 512)) % 512;
            if (pad > 0) {
                char zeros[512];
                memset(zeros, 0, sizeof(zeros));
                fwrite(zeros, 1, pad, f);
            }
        }
    }

    /* End-of-archive: two zero blocks */
    char end[1024];
    memset(end, 0, sizeof(end));
    fwrite(end, 1, 1024, f);
    fclose(f);
    return 0;
}

/* Write a tar.gz containing one regular file */
static int make_tar_gz_file(const char *gz_path, const char *entry_name,
                            const void *data, size_t datalen) {
    gzFile gz = gzopen(gz_path, "wb");
    if (!gz) return -1;

    test_tar_hdr_t h;
    init_hdr(&h, entry_name, '0', datalen, NULL);
    gzwrite(gz, &h, 512);

    if (datalen > 0) {
        gzwrite(gz, data, (unsigned)datalen);
        size_t pad = (512 - (datalen % 512)) % 512;
        if (pad > 0) {
            char zeros[512];
            memset(zeros, 0, sizeof(zeros));
            gzwrite(gz, zeros, (unsigned)pad);
        }
    }

    char end_blk[1024];
    memset(end_blk, 0, sizeof(end_blk));
    gzwrite(gz, end_blk, 1024);
    gzclose(gz);
    return 0;
}

/* Simple cleanup helper — use system() with full path to GNU rm */
static void rm_rf(const char *path) {
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "/bin/rm -rf '%s'", path);
    (void)system(cmd);
}

/* ── Plain tar extraction tests ──────────────────────────────── */

static void test_plain_tar_extract(void) {
    printf("\n[Test] Plain tar extraction...\n");

    char tmpdir[] = "/tmp/wow-test-plain-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char tar_path[PATH_MAX], dest[PATH_MAX];
    snprintf(tar_path, sizeof(tar_path), "%s/test.tar", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    const char *content_a = "alpha";
    const char *content_b = "bravo charlie";
    struct tar_entry entries[] = {
        { "a.txt", content_a, strlen(content_a) },
        { "b.txt", content_b, strlen(content_b) },
    };
    int rc = make_plain_tar(tar_path, entries, 2);
    check("create plain tar succeeds", rc == 0);

    rc = wow_tar_extract(tar_path, dest, 0);
    check("extract succeeds", rc == 0);

    /* Verify file a.txt */
    char outfile[TPATH];
    snprintf(outfile, sizeof(outfile), "%s/a.txt", dest);
    FILE *f = fopen(outfile, "r");
    check("a.txt exists", f != NULL);
    if (f) {
        char buf[64];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        check("a.txt content matches", strcmp(buf, content_a) == 0);
    }

    /* Verify file b.txt */
    snprintf(outfile, sizeof(outfile), "%s/b.txt", dest);
    f = fopen(outfile, "r");
    check("b.txt exists", f != NULL);
    if (f) {
        char buf[64];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        check("b.txt content matches", strcmp(buf, content_b) == 0);
    }

    rm_rf(tmpdir);
}

/* ── tar_list tests ──────────────────────────────────────────── */

struct list_ctx {
    char names[8][128];
    size_t sizes[8];
    int count;
};

static int list_cb(const char *name, size_t size, char typeflag, void *ctx) {
    (void)typeflag;
    struct list_ctx *lc = ctx;
    if (lc->count < 8) {
        snprintf(lc->names[lc->count], 128, "%s", name);
        lc->sizes[lc->count] = size;
        lc->count++;
    }
    return 0;
}

static void test_tar_list(void) {
    printf("\n[Test] tar_list callback...\n");

    char tmpdir[] = "/tmp/wow-test-list-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char tar_path[PATH_MAX];
    snprintf(tar_path, sizeof(tar_path), "%s/test.tar", tmpdir);

    const char *data = "hello";
    struct tar_entry entries[] = {
        { "metadata.gz", data, 5 },
        { "data.tar.gz", data, 5 },
        { "checksums.yaml.gz", data, 5 },
    };
    int rc = make_plain_tar(tar_path, entries, 3);
    check("create tar succeeds", rc == 0);

    struct list_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    rc = wow_tar_list(tar_path, list_cb, &ctx);
    check("tar_list succeeds", rc == 0);
    check("found 3 entries", ctx.count == 3);
    check("entry 0 is metadata.gz",
          strcmp(ctx.names[0], "metadata.gz") == 0);
    check("entry 1 is data.tar.gz",
          strcmp(ctx.names[1], "data.tar.gz") == 0);
    check("entry 2 is checksums.yaml.gz",
          strcmp(ctx.names[2], "checksums.yaml.gz") == 0);

    rm_rf(tmpdir);
}

/* ── tar_read_entry tests ────────────────────────────────────── */

static void test_tar_read_entry(void) {
    printf("\n[Test] tar_read_entry...\n");

    char tmpdir[] = "/tmp/wow-test-readent-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char tar_path[PATH_MAX];
    snprintf(tar_path, sizeof(tar_path), "%s/test.tar", tmpdir);

    const char *meta = "gemspec yaml here";
    const char *data = "big tarball data";
    struct tar_entry entries[] = {
        { "metadata.gz", meta, strlen(meta) },
        { "data.tar.gz", data, strlen(data) },
    };
    int rc = make_plain_tar(tar_path, entries, 2);
    check("create tar succeeds", rc == 0);

    /* Read first entry */
    uint8_t *out = NULL;
    size_t out_len = 0;
    rc = wow_tar_read_entry(tar_path, "metadata.gz", &out, &out_len,
                            1024 * 1024);
    check("read_entry succeeds", rc == 0);
    check("correct length", out_len == strlen(meta));
    check("correct content", out && memcmp(out, meta, out_len) == 0);
    free(out);

    /* Read second entry */
    out = NULL;
    out_len = 0;
    rc = wow_tar_read_entry(tar_path, "data.tar.gz", &out, &out_len,
                            1024 * 1024);
    check("read_entry (2nd) succeeds", rc == 0);
    check("correct content (2nd)", out && memcmp(out, data, out_len) == 0);
    free(out);

    /* max_size guard */
    out = NULL;
    out_len = 0;
    rc = wow_tar_read_entry(tar_path, "data.tar.gz", &out, &out_len, 5);
    check("max_size rejects oversized entry", rc != 0);
    check("no data returned", out == NULL);

    /* Missing entry */
    rc = wow_tar_read_entry(tar_path, "nosuchfile", &out, &out_len,
                            1024 * 1024);
    check("missing entry returns error", rc != 0);

    rm_rf(tmpdir);
}

/* ── tar_extract_entry_to_fd tests ───────────────────────────── */

static void test_tar_extract_entry_to_fd(void) {
    printf("\n[Test] tar_extract_entry_to_fd...\n");

    char tmpdir[] = "/tmp/wow-test-tofd-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char tar_path[PATH_MAX], out_path[PATH_MAX];
    snprintf(tar_path, sizeof(tar_path), "%s/test.tar", tmpdir);
    snprintf(out_path, sizeof(out_path), "%s/extracted", tmpdir);

    const char *data = "streamed data content here";
    struct tar_entry entries[] = {
        { "data.tar.gz", data, strlen(data) },
    };
    int rc = make_plain_tar(tar_path, entries, 1);
    check("create tar succeeds", rc == 0);

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check("open output fd", fd >= 0);

    rc = wow_tar_extract_entry_to_fd(tar_path, "data.tar.gz", fd);
    close(fd);
    check("extract_entry_to_fd succeeds", rc == 0);

    /* Verify content */
    FILE *f = fopen(out_path, "r");
    check("output file exists", f != NULL);
    if (f) {
        char buf[128];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        check("streamed content matches", strcmp(buf, data) == 0);
    }

    rm_rf(tmpdir);
}

/* ── SHA-256 tests ───────────────────────────────────────────── */

static void test_sha256(void) {
    printf("\n[Test] SHA-256 verification...\n");

    char tmpdir[] = "/tmp/wow-test-sha-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/test.bin", tmpdir);

    /* Write known content */
    const char *content = "hello world\n";
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(content, 1, strlen(content), f);
        fclose(f);
    }

    /* Compute SHA-256 and check against known hash */
    /* sha256("hello world\n") = 7509e5bda... */
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, (const uint8_t *)content,
                               strlen(content));
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);

    check("sha256 hex is 64 chars", strlen(hex) == 64);
    check("sha256 matches known digest",
          strcmp(hex, "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb8"
                      "5d299a192a447") == 0);

    /* Verify round-trip: hash a file and compare */
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);

    f = fopen(path, "rb");
    check("reopen for file hash", f != NULL);
    if (f) {
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            mbedtls_sha256_update_ret(&ctx, buf, n);
        fclose(f);
    }

    uint8_t file_digest[32];
    mbedtls_sha256_finish_ret(&ctx, file_digest);
    mbedtls_sha256_free(&ctx);

    check("file hash matches memory hash",
          memcmp(digest, file_digest, 32) == 0);

    rm_rf(tmpdir);
}

/* ── Gunzip round-trip tests ─────────────────────────────────── */

static void test_gunzip_roundtrip(void) {
    printf("\n[Test] Gzip compress + decompress round-trip...\n");

    char tmpdir[] = "/tmp/wow-test-gz-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/test.gz", tmpdir);

    /* Compress some data with gzopen — long enough that gzip wins */
    const char *original =
        "This is test data for gzip round-trip verification. "
        "Repeating text compresses well with deflate: "
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccc"
        "dddddddddddddddddddddddddddddddddddddddddddddddddd";
    size_t orig_len = strlen(original);

    gzFile gz = gzopen(gz_path, "wb");
    check("gzopen for write", gz != NULL);
    if (gz) {
        gzwrite(gz, original, (unsigned)orig_len);
        gzclose(gz);
    }

    /* Read back compressed data */
    FILE *f = fopen(gz_path, "rb");
    check("open compressed file", f != NULL);
    if (!f) { rm_rf(tmpdir); return; }

    fseek(f, 0, SEEK_END);
    long gz_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *gz_data = malloc((size_t)gz_len);
    fread(gz_data, 1, (size_t)gz_len, f);
    fclose(f);

    check("compressed is smaller", (size_t)gz_len < orig_len);

    /* Decompress with inflateInit2 (same as gems/meta.c) */
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    int zrc = inflateInit2(&strm, 16 + MAX_WBITS);
    check("inflateInit2 succeeds", zrc == Z_OK);

    strm.next_in = gz_data;
    strm.avail_in = (uInt)gz_len;

    uint8_t out[512];
    strm.next_out = out;
    strm.avail_out = sizeof(out);

    zrc = inflate(&strm, Z_FINISH);
    check("inflate returns Z_STREAM_END", zrc == Z_STREAM_END);

    size_t decompressed_len = sizeof(out) - strm.avail_out;
    inflateEnd(&strm);
    free(gz_data);

    check("decompressed length matches", decompressed_len == orig_len);
    check("decompressed content matches",
          memcmp(out, original, orig_len) == 0);

    rm_rf(tmpdir);
}

/* ── Gemspec YAML parsing tests ──────────────────────────────── */

/*
 * Build a minimal .gem (plain tar) with a gzip-compressed metadata.gz
 * containing embedded YAML, then parse it with wow_gemspec_parse.
 */
static void test_gemspec_parse(void) {
    printf("\n[Test] Gemspec YAML parsing...\n");

    char tmpdir[] = "/tmp/wow-test-gemspec-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    /* Sample gemspec YAML (simplified from a real sinatra gemspec) */
    const char *yaml =
        "--- !ruby/object:Gem::Specification\n"
        "name: test-gem\n"
        "version: !ruby/object:Gem::Version\n"
        "  version: '1.2.3'\n"
        "summary: A test gem for unit tests\n"
        "authors:\n"
        "- Alice\n"
        "- Bob\n"
        "required_ruby_version: !ruby/object:Gem::Requirement\n"
        "  requirements:\n"
        "  - - \">=\"\n"
        "    - !ruby/object:Gem::Version\n"
        "      version: '2.7.0'\n"
        "dependencies:\n"
        "- !ruby/object:Gem::Dependency\n"
        "  name: rack\n"
        "  requirement: !ruby/object:Gem::Requirement\n"
        "    requirements:\n"
        "    - - \"~>\"\n"
        "      - !ruby/object:Gem::Version\n"
        "        version: '3.0'\n"
        "  type: :runtime\n"
        "- !ruby/object:Gem::Dependency\n"
        "  name: minitest\n"
        "  requirement: !ruby/object:Gem::Requirement\n"
        "    requirements:\n"
        "    - - \">=\"\n"
        "      - !ruby/object:Gem::Version\n"
        "        version: '0'\n"
        "  type: :development\n";

    /* Gzip the YAML into metadata.gz content */
    char gz_path[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/metadata.gz", tmpdir);
    gzFile gz = gzopen(gz_path, "wb");
    check("gzopen metadata.gz", gz != NULL);
    if (!gz) { rm_rf(tmpdir); return; }
    gzwrite(gz, yaml, (unsigned)strlen(yaml));
    gzclose(gz);

    /* Read back the compressed bytes */
    FILE *f = fopen(gz_path, "rb");
    fseek(f, 0, SEEK_END);
    long gz_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *gz_data = malloc((size_t)gz_len);
    fread(gz_data, 1, (size_t)gz_len, f);
    fclose(f);

    /* Build a .gem (plain tar) containing metadata.gz */
    char gem_path[PATH_MAX];
    snprintf(gem_path, sizeof(gem_path), "%s/test-gem-1.2.3.gem", tmpdir);

    struct tar_entry entries[] = {
        { "metadata.gz", gz_data, (size_t)gz_len },
    };
    int rc = make_plain_tar(gem_path, entries, 1);
    free(gz_data);
    check("create fake .gem succeeds", rc == 0);

    /* Parse with wow_gemspec_parse */
    struct wow_gemspec spec;
    rc = wow_gemspec_parse(gem_path, &spec);
    check("gemspec_parse succeeds", rc == 0);

    if (rc == 0) {
        check("name is test-gem",
              spec.name && strcmp(spec.name, "test-gem") == 0);
        check("version is 1.2.3",
              spec.version && strcmp(spec.version, "1.2.3") == 0);
        check("summary parsed",
              spec.summary && strstr(spec.summary, "test gem") != NULL);
        check("authors contains Alice",
              spec.authors && strstr(spec.authors, "Alice") != NULL);
        check("authors contains Bob",
              spec.authors && strstr(spec.authors, "Bob") != NULL);
        check("required_ruby_version parsed",
              spec.required_ruby_version &&
              strstr(spec.required_ruby_version, "2.7.0") != NULL);
        check("1 runtime dep (minitest excluded)",
              spec.n_deps == 1);
        if (spec.n_deps >= 1) {
            check("dep is rack",
                  spec.deps[0].name &&
                  strcmp(spec.deps[0].name, "rack") == 0);
            check("rack constraint is ~> 3.0",
                  spec.deps[0].constraint &&
                  strstr(spec.deps[0].constraint, "~> 3.0") != NULL);
        }

        wow_gemspec_free(&spec);
    }

    rm_rf(tmpdir);
}

/* ── Gem cache dir tests ─────────────────────────────────────── */

static void test_gem_cache_dir(void) {
    printf("\n[Test] Gem cache directory...\n");

    /* wow_gem_cache_dir is declared in gems/download.h */
    extern int wow_gem_cache_dir(char *buf, size_t bufsz);

    char buf[PATH_MAX];
    int rc = wow_gem_cache_dir(buf, sizeof(buf));
    check("wow_gem_cache_dir succeeds", rc == 0);
    if (rc == 0) {
        check("path contains 'wow'", strstr(buf, "wow") != NULL);
        check("path contains 'gems'", strstr(buf, "gems") != NULL);
    }
}

/* ── main ────────────────────────────────────────────────────── */

int main(void) {
    printf("=== wow gem infrastructure tests ===\n");

    /* Plain tar tests */
    test_plain_tar_extract();
    test_tar_list();
    test_tar_read_entry();
    test_tar_extract_entry_to_fd();

    /* SHA-256 */
    test_sha256();

    /* Gzip round-trip */
    test_gunzip_roundtrip();

    /* Gemspec YAML parsing */
    test_gemspec_parse();

    /* Cache directory */
    test_gem_cache_dir();

    printf("\n=== Results: %d passed, %d failed ===\n", n_pass, n_fail);
    return n_fail > 0 ? 1 : 0;
}
