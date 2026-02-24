/*
 * demo_platform.c — Phase 0d demo: host platform detection
 *
 * Detects the current platform using uname() and maps it to a
 * wow platform identifier, then constructs ruby-builder download URLs.
 *
 * Build:  cosmocc -o demo_platform.com demo_platform.c
 * Usage:  ./demo_platform.com
 *         ./demo_platform.com 3.3.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

/* Safe string copy */
static void scopy(char *dst, size_t dstsz, const char *src, size_t n)
{
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

typedef struct {
    char os[32];
    char arch[32];
    char libc[16];
    char wow_id[80];
} platform_t;

/*
 * Detect libc on Linux.
 *
 * Strategy: check for the existence of musl's dynamic linker.
 * This avoids popen/pclose (which are POSIX but not in strict C11)
 * and works in Cosmopolitan's APE environment.
 */
static void detect_libc(platform_t *p)
{
    if (strcmp(p->os, "linux") != 0) {
        p->libc[0] = '\0';
        return;
    }

    /* musl uses /lib/ld-musl-*.so.1 as its dynamic linker */
    FILE *fp = fopen("/lib/ld-musl-x86_64.so.1", "r");
    if (!fp) fp = fopen("/lib/ld-musl-aarch64.so.1", "r");
    if (fp) {
        fclose(fp);
        scopy(p->libc, sizeof(p->libc), "musl", 4);
    } else {
        scopy(p->libc, sizeof(p->libc), "gnu", 3);
    }
}

static void detect_platform(platform_t *p)
{
    memset(p, 0, sizeof(*p));

    struct utsname u;
    if (uname(&u) != 0) {
        fprintf(stderr, "uname() failed\n");
        scopy(p->os, sizeof(p->os), "unknown", 7);
        scopy(p->arch, sizeof(p->arch), "unknown", 7);
        return;
    }

    /* OS */
    if (strcmp(u.sysname, "Linux") == 0)
        scopy(p->os, sizeof(p->os), "linux", 5);
    else if (strcmp(u.sysname, "Darwin") == 0)
        scopy(p->os, sizeof(p->os), "darwin", 6);
    else
        scopy(p->os, sizeof(p->os), u.sysname, strlen(u.sysname));

    /* Architecture — normalise aarch64 → arm64 */
    if (strcmp(u.machine, "aarch64") == 0)
        scopy(p->arch, sizeof(p->arch), "arm64", 5);
    else
        scopy(p->arch, sizeof(p->arch), u.machine, strlen(u.machine));

    /* libc (Linux only) */
    detect_libc(p);

    /* Compose wow platform identifier */
    if (p->libc[0])
        snprintf(p->wow_id, sizeof(p->wow_id), "%s-%s-%s", p->os, p->arch, p->libc);
    else
        snprintf(p->wow_id, sizeof(p->wow_id), "%s-%s", p->os, p->arch);
}

/*
 * Map wow platform to ruby-builder asset name.
 * Returns NULL if no pre-built binary is available.
 */
static const char *ruby_builder_platform(const platform_t *p)
{
    if (strcmp(p->os, "linux") == 0 && strcmp(p->arch, "x86_64") == 0
        && strcmp(p->libc, "gnu") == 0)
        return "ubuntu-22.04";

    if (strcmp(p->os, "linux") == 0 && strcmp(p->arch, "arm64") == 0
        && strcmp(p->libc, "gnu") == 0)
        return "ubuntu-22.04-arm64";

    if (strcmp(p->os, "darwin") == 0 && strcmp(p->arch, "arm64") == 0)
        return "macos-13-arm64";

    if (strcmp(p->os, "darwin") == 0 && strcmp(p->arch, "x86_64") == 0)
        return "macos-latest";

    return NULL;
}

int main(int argc, char **argv)
{
    const char *ruby_version = argc >= 2 ? argv[1] : "3.4.2";

    platform_t p;
    detect_platform(&p);

    printf("=== Platform Detection ===\n\n");
    printf("OS:            %s\n", p.os);
    printf("Architecture:  %s\n", p.arch);
    if (p.libc[0])
        printf("libc:          %s\n", p.libc);
    printf("wow platform:  %s\n", p.wow_id);

    printf("\n=== Ruby %s Download URLs ===\n\n", ruby_version);

    /* Source tarball (always available) */
    char major_minor[8] = {0};
    /* Extract "X.Y" from "X.Y.Z" */
    const char *dot2 = strchr(ruby_version, '.');
    if (dot2) dot2 = strchr(dot2 + 1, '.');
    if (dot2) {
        size_t mm_len = (size_t)(dot2 - ruby_version);
        scopy(major_minor, sizeof(major_minor), ruby_version, mm_len);
    } else {
        scopy(major_minor, sizeof(major_minor), ruby_version, strlen(ruby_version));
    }

    printf("Source:    https://cache.ruby-lang.org/pub/ruby/%s/ruby-%s.tar.gz\n",
           major_minor, ruby_version);

    /* Pre-built binary */
    const char *rb_plat = ruby_builder_platform(&p);
    if (rb_plat) {
        printf("Binary:    https://github.com/ruby/ruby-builder/releases/"
               "download/toolcache/ruby-%s-%s.tar.gz\n",
               ruby_version, rb_plat);
        printf("\n  Note: binary tarballs have an x64/ prefix to strip:\n");
        printf("  tar -xzf ruby-%s-%s.tar.gz --strip-components=1 "
               "-C ~/.local/share/wow/rubies/%s/\n",
               ruby_version, rb_plat, ruby_version);
    } else {
        printf("Binary:    (no pre-built binary available for %s)\n", p.wow_id);
        printf("           Source compilation required (post-MVP).\n");
    }

    return 0;
}
