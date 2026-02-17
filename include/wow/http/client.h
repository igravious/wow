#ifndef WOW_HTTP_CLIENT_H
#define WOW_HTTP_CLIENT_H

#include <stddef.h>
#include "wow/download/progress.h"  /* Progress state module for downloads */

/* Global verbose flag for HTTP debugging (set by --verbose or -v) */
extern int wow_http_debug;

#define WOW_HTTP_MAX_BODY     (10 * 1024 * 1024)  /* 10 MiB default limit */
#define WOW_HTTP_MAX_REDIRECTS 10
#define WOW_HTTP_TIMEOUT_SECS  30

struct wow_response {
    int    status;
    char  *body;
    size_t body_len;
    char  *etag;
    char  *content_type;
    char  *location;       /* populated on 3xx redirects (internal use) */
};

/* GET url, follow redirects (up to 10). Returns 0 on success, -1 on error.
 * Errors are printed to stderr. Caller must wow_response_free(). */
int  wow_http_get(const char *url, struct wow_response *resp);
void wow_response_free(struct wow_response *resp);

/*
 * Progress callback for streaming downloads.
 * received: bytes downloaded so far.
 * total:    content-length (0 if unknown).
 * ctx:      user-provided context pointer.
 */
typedef void (*wow_progress_fn)(size_t received, size_t total, void *ctx);

/*
 * Download URL to a file descriptor, streaming.  Follows redirects.
 * Does NOT enforce WOW_HTTP_MAX_BODY (streams to disc).
 * Returns 0 on success, -1 on error.  Errors printed to stderr.
 */
int wow_http_download_to_fd(const char *url, int fd,
                            wow_progress_fn progress, void *progress_ctx);

#endif
