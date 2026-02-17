/*
 * demo_https.c — Phase 2a/2b demo: native HTTPS client
 *
 * Fetches a URL using wow's HTTP/HTTPS client (mbedTLS + cosmo)
 * and prints the response status, headers, and body.
 *
 * Build:  make -C demos demo_https.com   (after main 'make')
 * Usage:  ./demos/demo_https.com http://example.com
 *         ./demos/demo_https.com https://rubygems.org/api/v1/gems/sinatra.json
 */

#include <stdio.h>
#include <string.h>
#include "wow/http.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <url>\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s http://example.com\n", argv[0]);
        fprintf(stderr, "  %s https://rubygems.org/api/v1/gems/sinatra.json\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    struct wow_response resp = {0};

    printf("=== wow HTTPS Client Demo ===\n\n");
    printf("Fetching: %s\n\n", url);

    int rc = wow_http_get(url, &resp);
    if (rc != 0) {
        fprintf(stderr, "error: wow_http_get() failed\n");
        return 1;
    }

    printf("Status:       %d\n", resp.status);
    if (resp.content_type)
        printf("Content-Type: %s\n", resp.content_type);
    if (resp.etag)
        printf("ETag:         %s\n", resp.etag);
    printf("Body length:  %zu bytes\n", resp.body_len);

    printf("\n--- Body ---\n");
    if (resp.body_len > 2048) {
        /* Truncate large responses for demo output */
        fwrite(resp.body, 1, 2048, stdout);
        printf("\n... (truncated, %zu bytes total)\n", resp.body_len);
    } else {
        fwrite(resp.body, 1, resp.body_len, stdout);
        if (resp.body_len > 0 && resp.body[resp.body_len - 1] != '\n')
            putchar('\n');
    }

    wow_response_free(&resp);
    return 0;
}
