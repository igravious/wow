/*
 * tests/ruby_mgr_test.c — Ruby manager + tar extraction tests
 *
 * Offline tests: platform detection, directory helpers, .ruby-version
 * parsing, and tar security hardening.
 *
 * Run via: make test-ruby-mgr
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <third_party/zlib/zlib.h>

#include "wow/rubies.h"
#include "wow/tar.h"

/* Composite path buffer — PATH_MAX alone is too tight for dest + entry */
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

/* ── Helpers for crafting tar.gz test fixtures ───────────────── */

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

    /* End-of-archive: two zero blocks */
    char end[1024];
    memset(end, 0, sizeof(end));
    gzwrite(gz, end, 1024);

    gzclose(gz);
    return 0;
}

/* Write a tar.gz containing one symlink */
static int make_tar_gz_symlink(const char *gz_path, const char *entry_name,
                               const char *target) {
    gzFile gz = gzopen(gz_path, "wb");
    if (!gz) return -1;

    test_tar_hdr_t h;
    init_hdr(&h, entry_name, '2', 0, target);
    gzwrite(gz, &h, 512);

    char end[1024];
    memset(end, 0, sizeof(end));
    gzwrite(gz, end, 1024);

    gzclose(gz);
    return 0;
}

/* Simple cleanup helper for temp directories */
static void rm_rf(const char *path) {
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
}

/* ── Platform detection tests ────────────────────────────────── */

static void test_platform_detection(void) {
    printf("\n[Test] Platform detection...\n");

    wow_platform_t p;
    wow_detect_platform(&p);

    check("os is non-empty", strlen(p.os) > 0);
    check("arch is non-empty", strlen(p.arch) > 0);
    check("wow_id is non-empty", strlen(p.wow_id) > 0);
    check("wow_id contains os", strstr(p.wow_id, p.os) != NULL);
    check("wow_id contains arch", strstr(p.wow_id, p.arch) != NULL);
}

static void test_builder_platform(void) {
    printf("\n[Test] Builder platform mapping...\n");

    wow_platform_t p;
    wow_detect_platform(&p);

    const char *rb = wow_ruby_builder_platform(&p);
    check("ruby_builder_platform returns non-NULL", rb != NULL);
    if (rb) {
        check("contains 'ubuntu' or 'macos'",
              strstr(rb, "ubuntu") != NULL || strstr(rb, "macos") != NULL);
    }
}

/* ── Directory helper tests ──────────────────────────────────── */

static void test_base_dir(void) {
    printf("\n[Test] Base directory...\n");

    char buf[PATH_MAX];
    int rc = wow_ruby_base_dir(buf, sizeof(buf));
    check("wow_ruby_base_dir succeeds", rc == 0);
    if (rc == 0) {
        check("path contains 'wow'", strstr(buf, "wow") != NULL);
        check("path contains 'ruby'", strstr(buf, "ruby") != NULL);
    }
}

static void test_shims_dir(void) {
    printf("\n[Test] Shims directory...\n");

    char buf[PATH_MAX];
    int rc = wow_shims_dir(buf, sizeof(buf));
    check("wow_shims_dir succeeds", rc == 0);
    if (rc == 0) {
        check("path contains 'shims'", strstr(buf, "shims") != NULL);
    }
}

/* ── .ruby-version tests ─────────────────────────────────────── */

static void test_find_ruby_version(void) {
    printf("\n[Test] Find .ruby-version...\n");

    char orig_cwd[PATH_MAX];
    if (!getcwd(orig_cwd, sizeof(orig_cwd))) {
        check("getcwd", 0);
        return;
    }

    /* Create temp dir with .ruby-version */
    char tmpdir[] = "/tmp/wow-test-rv-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        check("mkdtemp", 0);
        return;
    }

    char rvpath[PATH_MAX];
    snprintf(rvpath, sizeof(rvpath), "%s/.ruby-version", tmpdir);
    FILE *f = fopen(rvpath, "w");
    if (f) {
        fputs("3.3.6\n", f);
        fclose(f);
    }

    /* Direct lookup */
    if (chdir(tmpdir) == 0) {
        char ver[32];
        int rc = wow_find_ruby_version(ver, sizeof(ver));
        check("find_ruby_version succeeds", rc == 0);
        if (rc == 0)
            check("version is 3.3.6", strcmp(ver, "3.3.6") == 0);
    }

    /* Walk-up from subdirectory */
    char adir[PATH_MAX], bdir[PATH_MAX], subdir[PATH_MAX];
    snprintf(adir, sizeof(adir), "%s/a", tmpdir);
    snprintf(bdir, sizeof(bdir), "%s/a/b", tmpdir);
    snprintf(subdir, sizeof(subdir), "%s/a/b/c", tmpdir);
    mkdir(adir, 0755);
    mkdir(bdir, 0755);
    mkdir(subdir, 0755);

    if (chdir(subdir) == 0) {
        char ver[32];
        int rc = wow_find_ruby_version(ver, sizeof(ver));
        check("find_ruby_version walks up directories", rc == 0);
        if (rc == 0)
            check("walked-up version is 3.3.6", strcmp(ver, "3.3.6") == 0);
    }

    chdir(orig_cwd);
    rm_rf(tmpdir);
}

static void test_find_ruby_version_missing(void) {
    printf("\n[Test] Find .ruby-version (missing)...\n");

    char orig_cwd[PATH_MAX];
    if (!getcwd(orig_cwd, sizeof(orig_cwd))) {
        check("getcwd", 0);
        return;
    }

    char tmpdir[] = "/tmp/wow-test-norv-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        check("mkdtemp", 0);
        return;
    }

    if (chdir(tmpdir) == 0) {
        char ver[32];
        int rc = wow_find_ruby_version(ver, sizeof(ver));
        check("returns -1 when no .ruby-version", rc == -1);
    }

    chdir(orig_cwd);
    rm_rf(tmpdir);
}

/* ── Tar extraction tests ────────────────────────────────────── */

static void test_tar_basic_extract(void) {
    printf("\n[Test] Tar basic extraction...\n");

    char tmpdir[] = "/tmp/wow-test-tar-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX], dest[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/test.tar.gz", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    const char *content = "Hello, wow!";
    int rc = make_tar_gz_file(gz_path, "hello.txt", content, strlen(content));
    check("create tar.gz succeeds", rc == 0);

    rc = wow_tar_extract_gz(gz_path, dest, 0);
    check("extract succeeds", rc == 0);

    char outfile[TPATH];
    snprintf(outfile, sizeof(outfile), "%s/hello.txt", dest);
    FILE *f = fopen(outfile, "r");
    check("extracted file exists", f != NULL);
    if (f) {
        char buf[64];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        check("file content matches", strcmp(buf, content) == 0);
    }

    rm_rf(tmpdir);
}

static void test_tar_strip_components(void) {
    printf("\n[Test] Tar strip components...\n");

    char tmpdir[] = "/tmp/wow-test-strip-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX], dest[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/test.tar.gz", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    const char *content = "stripped!";
    int rc = make_tar_gz_file(gz_path, "x64/bin/hello.txt",
                              content, strlen(content));
    check("create tar.gz succeeds", rc == 0);

    rc = wow_tar_extract_gz(gz_path, dest, 1);
    check("extract with strip=1 succeeds", rc == 0);

    /* Should be at bin/hello.txt, not x64/bin/hello.txt */
    char outfile[TPATH];
    snprintf(outfile, sizeof(outfile), "%s/bin/hello.txt", dest);
    FILE *f = fopen(outfile, "r");
    check("stripped file at bin/hello.txt", f != NULL);
    if (f) fclose(f);

    char orig[TPATH];
    snprintf(orig, sizeof(orig), "%s/x64/bin/hello.txt", dest);
    check("original path does not exist", access(orig, F_OK) != 0);

    rm_rf(tmpdir);
}

static void test_tar_path_traversal(void) {
    printf("\n[Test] Tar path traversal rejection...\n");

    char tmpdir[] = "/tmp/wow-test-trav-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX], dest[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/evil.tar.gz", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    const char *evil = "pwned";
    int rc = make_tar_gz_file(gz_path, "../../etc/evil", evil, strlen(evil));
    check("create evil tar.gz succeeds", rc == 0);

    rc = wow_tar_extract_gz(gz_path, dest, 0);
    check("extract completes (skips unsafe entry)", rc == 0);

    /* Verify evil file was NOT created anywhere outside dest */
    char evil_path[TPATH];
    snprintf(evil_path, sizeof(evil_path), "%s/etc/evil", tmpdir);
    check("traversal file not created", access(evil_path, F_OK) != 0);

    rm_rf(tmpdir);
}

static void test_tar_symlink_escape(void) {
    printf("\n[Test] Tar symlink escape rejection...\n");

    char tmpdir[] = "/tmp/wow-test-symesc-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX], dest[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/evil-sym.tar.gz", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    int rc = make_tar_gz_symlink(gz_path, "escape",
                                 "../../../../etc/passwd");
    check("create symlink tar.gz succeeds", rc == 0);

    rc = wow_tar_extract_gz(gz_path, dest, 0);
    check("extract completes (skips unsafe symlink)", rc == 0);

    char sym_path[TPATH];
    snprintf(sym_path, sizeof(sym_path), "%s/escape", dest);
    struct stat st;
    check("escape symlink not created", lstat(sym_path, &st) != 0);

    rm_rf(tmpdir);
}

static void test_tar_corrupted(void) {
    printf("\n[Test] Tar corrupted archive...\n");

    char tmpdir[] = "/tmp/wow-test-corrupt-XXXXXX";
    if (!mkdtemp(tmpdir)) { check("mkdtemp", 0); return; }

    char gz_path[PATH_MAX], dest[PATH_MAX];
    snprintf(gz_path, sizeof(gz_path), "%s/corrupt.tar.gz", tmpdir);
    snprintf(dest, sizeof(dest), "%s/out", tmpdir);
    mkdir(dest, 0755);

    /* Write garbage bytes */
    FILE *f = fopen(gz_path, "wb");
    if (f) {
        const char garbage[] = "this is definitely not a tar.gz file!!!";
        fwrite(garbage, 1, sizeof(garbage), f);
        fclose(f);
    }

    int rc = wow_tar_extract_gz(gz_path, dest, 0);
    check("corrupted archive returns error", rc != 0);

    rm_rf(tmpdir);
}

/* ── main ────────────────────────────────────────────────────── */

int main(void) {
    printf("=== wow ruby manager tests ===\n");

    /* Platform + directory tests */
    test_platform_detection();
    test_builder_platform();
    test_base_dir();
    test_shims_dir();

    /* .ruby-version tests */
    test_find_ruby_version();
    test_find_ruby_version_missing();

    /* Tar extraction tests */
    test_tar_basic_extract();
    test_tar_strip_components();
    test_tar_path_traversal();
    test_tar_symlink_escape();
    test_tar_corrupted();

    printf("\n=== Results: %d passed, %d failed ===\n", n_pass, n_fail);
    return n_fail > 0 ? 1 : 0;
}
