#ifndef WOW_POOL_H
#define WOW_POOL_H

#include "wow/http.h"

#define WOW_POOL_MAX_CONNS 8

/*
 * Connection pool for HTTP/HTTPS Keep-Alive reuse.
 * Caches open sockets + TLS contexts keyed by host:port.
 */
struct wow_pool_entry {
    char  *host;
    char  *port;
    int    usessl;
    int    sock;
    void  *ssl_ctx;   /* mbedtls_ssl_context* if TLS, NULL otherwise */
    void  *ssl_conf;  /* mbedtls_ssl_config* */
    void  *ssl_drbg;  /* mbedtls_ctr_drbg_context* */
    int    in_use;
};

struct wow_http_pool {
    struct wow_pool_entry entries[WOW_POOL_MAX_CONNS];
    int max_conns;
    int reuse_count;
    int new_count;
};

/* Initialise a connection pool. max_conns capped to WOW_POOL_MAX_CONNS. */
int  wow_http_pool_init(struct wow_http_pool *p, int max_conns);

/* GET via pool — reuses connections when possible. Same semantics as wow_http_get. */
int  wow_http_pool_get(struct wow_http_pool *p, const char *url,
                       struct wow_response *resp);

/* Close all pooled connections and free resources. */
void wow_http_pool_cleanup(struct wow_http_pool *p);

#endif
