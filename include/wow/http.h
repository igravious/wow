#ifndef WOW_HTTP_H
#define WOW_HTTP_H

#include <stddef.h>

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

#endif
