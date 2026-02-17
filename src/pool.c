/*
 * src/pool.c — HTTP connection pool with Keep-Alive reuse
 *
 * Phase 2 implementation: pools connections by host:port, reuses
 * TLS sessions for consecutive requests to the same origin.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libc/calls/calls.h"
#include "libc/calls/struct/timeval.h"
#include "libc/mem/mem.h"
#include "libc/sock/goodsocket.internal.h"
#include "libc/sock/sock.h"
#include "libc/stdio/append.h"
#include "libc/str/slice.h"
#include "libc/str/str.h"
#include "libc/sysv/consts/af.h"
#include "libc/sysv/consts/ipproto.h"
#include "libc/sysv/consts/msg.h"
#include "libc/sysv/consts/sock.h"
#include "net/http/http.h"
#include "net/http/url.h"
#include "net/https/https.h"
#include "third_party/mbedtls/ctr_drbg.h"
#include "third_party/mbedtls/error.h"
#include "third_party/mbedtls/iana.h"
#include "third_party/mbedtls/net_sockets.h"
#include "third_party/mbedtls/ssl.h"
#include "third_party/musl/netdb.h"

#include "wow/http.h"
#include "wow/pool.h"

#define HasHeader(H)    (!!msg.headers[H].a)
#define HeaderData(H)   (raw + msg.headers[H].a)
#define HeaderLength(H) (msg.headers[H].b - msg.headers[H].a)
#define HeaderEqualCase(H, S) \
    SlicesEqualCase(S, strlen(S), HeaderData(H), (size_t)HeaderLength(H))

/* TLS callbacks — same as http.c */
static int pool_tls_send(void *ctx, const unsigned char *buf, size_t len) {
    int rc = write(*(int *)ctx, buf, len);
    if (rc == -1) return MBEDTLS_ERR_NET_SEND_FAILED;
    return rc;
}

static int pool_tls_recv(void *ctx, unsigned char *buf, size_t len,
                         uint32_t timeout) {
    (void)timeout;
    int rc = read(*(int *)ctx, buf, len);
    if (rc == -1) return MBEDTLS_ERR_NET_RECV_FAILED;
    if (rc == 0)  return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return rc;
}

static void pool_entry_close(struct wow_pool_entry *e) {
    if (e->ssl_ctx) {
        mbedtls_ssl_free(e->ssl_ctx);
        free(e->ssl_ctx);
        e->ssl_ctx = NULL;
    }
    if (e->ssl_conf) {
        mbedtls_ssl_config_free(e->ssl_conf);
        free(e->ssl_conf);
        e->ssl_conf = NULL;
    }
    if (e->ssl_drbg) {
        mbedtls_ctr_drbg_free(e->ssl_drbg);
        free(e->ssl_drbg);
        e->ssl_drbg = NULL;
    }
    if (e->sock >= 0) {
        close(e->sock);
        e->sock = -1;
    }
    free(e->host);
    free(e->port);
    e->host = NULL;
    e->port = NULL;
    e->in_use = 0;
    e->usessl = 0;
}

int wow_http_pool_init(struct wow_http_pool *p, int max_conns) {
    memset(p, 0, sizeof(*p));
    p->max_conns = max_conns > WOW_POOL_MAX_CONNS ? WOW_POOL_MAX_CONNS
                                                   : max_conns;
    for (int i = 0; i < WOW_POOL_MAX_CONNS; i++)
        p->entries[i].sock = -1;
    return 0;
}

/*
 * Find or create a connection to host:port.
 * Returns the pool entry index, or -1 on failure.
 */
static int pool_acquire(struct wow_http_pool *p, const char *host,
                        const char *port, int usessl) {
    /* Look for existing idle connection */
    for (int i = 0; i < p->max_conns; i++) {
        struct wow_pool_entry *e = &p->entries[i];
        if (e->host && !e->in_use &&
            e->usessl == usessl &&
            strcmp(e->host, host) == 0 &&
            strcmp(e->port, port) == 0) {
            /* Check if still alive with a zero-byte peek */
            char probe;
            int flags = MSG_PEEK | MSG_DONTWAIT;
            ssize_t rc = recv(e->sock, &probe, 1, flags);
            if (rc == 0) {
                /* Peer closed — recycle */
                pool_entry_close(e);
                break;
            }
            if (rc == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                /* Real error (ECONNRESET, EPIPE, etc.) — recycle */
                pool_entry_close(e);
                break;
            }
            /* rc == -1 + EAGAIN means still alive, rc > 0 means data waiting */
            e->in_use = 1;
            p->reuse_count++;
            return i;
        }
    }

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < p->max_conns; i++) {
        if (!p->entries[i].host) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        /* Evict oldest idle entry */
        for (int i = 0; i < p->max_conns; i++) {
            if (!p->entries[i].in_use) {
                pool_entry_close(&p->entries[i]);
                slot = i;
                break;
            }
        }
    }
    if (slot == -1) return -1;

    struct wow_pool_entry *e = &p->entries[slot];

    /* DNS + connect */
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };
    struct addrinfo *addr;
    if (getaddrinfo(host, port, &hints, &addr) != 0) {
        fprintf(stderr, "wow: pool: could not resolve %s\n", host);
        return -1;
    }
    e->sock = GoodSocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol,
                         false, &(struct timeval){-WOW_HTTP_TIMEOUT_SECS, 0});
    if (e->sock == -1 || connect(e->sock, addr->ai_addr, addr->ai_addrlen) != 0) {
        fprintf(stderr, "wow: pool: connect to %s:%s failed\n", host, port);
        freeaddrinfo(addr);
        if (e->sock >= 0) { close(e->sock); e->sock = -1; }
        return -1;
    }
    freeaddrinfo(addr);

    /* TLS */
    if (usessl) {
        e->ssl_ctx  = calloc(1, sizeof(mbedtls_ssl_context));
        e->ssl_conf = calloc(1, sizeof(mbedtls_ssl_config));
        e->ssl_drbg = calloc(1, sizeof(mbedtls_ctr_drbg_context));
        if (!e->ssl_ctx || !e->ssl_conf || !e->ssl_drbg) goto fail;

        mbedtls_ssl_init(e->ssl_ctx);
        mbedtls_ctr_drbg_init(e->ssl_drbg);
        mbedtls_ssl_config_init(e->ssl_conf);

        if (mbedtls_ctr_drbg_seed(e->ssl_drbg, GetEntropy, NULL, "wowp", 4))
            goto fail;
        if (mbedtls_ssl_config_defaults(e->ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_SUITEC))
            goto fail;
        mbedtls_ssl_conf_authmode(e->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(e->ssl_conf, GetSslRoots(), NULL);
        mbedtls_ssl_conf_rng(e->ssl_conf, mbedtls_ctr_drbg_random, e->ssl_drbg);
        if (mbedtls_ssl_setup(e->ssl_ctx, e->ssl_conf)) goto fail;
        if (mbedtls_ssl_set_hostname(e->ssl_ctx, host)) goto fail;
        mbedtls_ssl_set_bio(e->ssl_ctx, &e->sock,
                            pool_tls_send, NULL, pool_tls_recv);

        int hrc = mbedtls_ssl_handshake(e->ssl_ctx);
        if (hrc != 0) {
            fprintf(stderr, "wow: pool: TLS handshake with %s failed: %s\n",
                    host, DescribeSslClientHandshakeError(e->ssl_ctx, hrc));
            goto fail;
        }
    }

    e->host   = strdup(host);
    e->port   = strdup(port);
    e->usessl = usessl;
    e->in_use = 1;
    p->new_count++;
    return slot;

fail:
    pool_entry_close(e);
    return -1;
}

static void pool_release(struct wow_http_pool *p, int slot, int keep) {
    struct wow_pool_entry *e = &p->entries[slot];
    e->in_use = 0;
    if (!keep)
        pool_entry_close(e);
}

/*
 * Extract header helper (same as http.c)
 */
static char *extract_header(const struct HttpMessage *m, const char *raw,
                            int hdr) {
    if (!m->headers[hdr].a) return NULL;
    size_t len = m->headers[hdr].b - m->headers[hdr].a;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, raw + m->headers[hdr].a, len);
    s[len] = '\0';
    return s;
}

/*
 * Perform GET on a pooled connection. Like do_get in http.c but uses
 * Connection: keep-alive and returns whether to keep the connection.
 */
static int pool_do_get(struct wow_pool_entry *e, const char *path,
                       struct wow_response *resp, int *keep_alive) {
    char *request = NULL;
    char *raw = NULL;
    struct HttpMessage msg;
    int msg_inited = 0;
    *keep_alive = 0;

    /* Build request with keep-alive */
    appendf(&request,
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%s\r\n"
            "User-Agent: wow/0.1.0\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            path, e->host, e->port);

    /* Send */
    size_t reqlen = appendz(request).i;
    for (size_t i = 0; i < reqlen; ) {
        ssize_t w;
        if (e->usessl) {
            w = mbedtls_ssl_write(e->ssl_ctx, (unsigned char *)request + i,
                                  reqlen - i);
            if (w <= 0) { free(request); return -1; }
        } else {
            w = write(e->sock, request + i, reqlen - i);
            if (w <= 0) { free(request); return -1; }
        }
        i += (size_t)w;
    }
    free(request);
    request = NULL;

    /* Receive + parse */
    InitHttpMessage(&msg, kHttpResponse);
    msg_inited = 1;

    size_t rawi = 0, rawn = 0;
    size_t hdrlen = 0, paylen = 0;
    int state = kHttpClientStateHeaders;
    char *body = NULL;
    size_t bodylen = 0;
    struct HttpUnchunker unchk;

    for (;;) {
        if (rawi == rawn) {
            rawn += 4096;
            rawn += rawn >> 1;
            char *tmp = realloc(raw, rawn);
            if (!tmp) goto fail;
            raw = tmp;
        }

        ssize_t rc;
        if (e->usessl) {
            rc = mbedtls_ssl_read(e->ssl_ctx, (unsigned char *)raw + rawi,
                                  rawn - rawi);
            if (rc < 0) {
                if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) rc = 0;
                else goto fail;
            }
        } else {
            rc = read(e->sock, raw + rawi, rawn - rawi);
            if (rc == -1) goto fail;
        }

        size_t got = (size_t)rc;
        rawi += got;

        switch (state) {
        case kHttpClientStateHeaders:
            if (!got) goto fail;
            rc = ParseHttpMessage(&msg, raw, rawi, rawn);
            if (rc == -1) goto fail;
            if (!rc) break;
            hdrlen = (size_t)rc;

            if (msg.status >= 100 && msg.status <= 199) {
                DestroyHttpMessage(&msg);
                InitHttpMessage(&msg, kHttpResponse);
                memmove(raw, raw + hdrlen, rawi - hdrlen);
                rawi -= hdrlen;
                break;
            }

            /* Check Connection header — explicit close overrides default */
            if (HasHeader(kHttpConnection)) {
                if (HeaderEqualCase(kHttpConnection, "close"))
                    *keep_alive = 0;
                else if (HeaderEqualCase(kHttpConnection, "keep-alive"))
                    *keep_alive = 1;
            }

            if (HasHeader(kHttpTransferEncoding) &&
                !HeaderEqualCase(kHttpTransferEncoding, "identity")) {
                if (!HeaderEqualCase(kHttpTransferEncoding, "chunked"))
                    goto fail;
                state = kHttpClientStateBodyChunked;
                memset(&unchk, 0, sizeof(unchk));
                goto do_chunked;
            } else if (HasHeader(kHttpContentLength)) {
                rc = ParseContentLength(HeaderData(kHttpContentLength),
                                        HeaderLength(kHttpContentLength));
                if (rc == -1) goto fail;
                paylen = (size_t)rc;
                if (paylen > WOW_HTTP_MAX_BODY) goto fail;
                state = kHttpClientStateBodyLengthed;
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    size_t take = have < paylen ? have : paylen;
                    body = malloc(paylen);
                    if (!body) goto fail;
                    memcpy(body, raw + hdrlen, take);
                    bodylen = take;
                    if (bodylen >= paylen) goto done;
                }
            } else {
                /* No content-length + no chunked = read to close */
                *keep_alive = 0;
                state = kHttpClientStateBody;
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    body = malloc(have);
                    if (!body) goto fail;
                    memcpy(body, raw + hdrlen, have);
                    bodylen = have;
                }
            }
            break;

        case kHttpClientStateBody:
            if (!got) goto done;
            if (bodylen + got > WOW_HTTP_MAX_BODY) goto fail;
            {
                char *tmp = realloc(body, bodylen + got);
                if (!tmp) goto fail;
                body = tmp;
                memcpy(body + bodylen, raw + rawi - got, got);
                bodylen += got;
            }
            break;

        case kHttpClientStateBodyLengthed:
            if (!got) goto fail;
            {
                size_t need = paylen - bodylen;
                size_t take = got < need ? got : need;
                if (!body) { body = malloc(paylen); if (!body) goto fail; }
                memcpy(body + bodylen, raw + rawi - got, take);
                bodylen += take;
                if (bodylen >= paylen) goto done;
            }
            break;

        case kHttpClientStateBodyChunked:
        do_chunked:
            rc = Unchunk(&unchk, raw + hdrlen, rawi - hdrlen, &paylen);
            if (rc == -1) goto fail;
            if (rc) {
                if (paylen > WOW_HTTP_MAX_BODY) goto fail;
                body = malloc(paylen);
                if (!body) goto fail;
                memcpy(body, raw + hdrlen, paylen);
                bodylen = paylen;
                goto done;
            }
            break;
        }
    }

done:
    resp->status       = msg.status;
    resp->body         = body;
    resp->body_len     = bodylen;
    resp->etag         = extract_header(&msg, raw, kHttpEtag);
    resp->content_type = extract_header(&msg, raw, kHttpContentType);
    resp->location     = extract_header(&msg, raw, kHttpLocation);
    DestroyHttpMessage(&msg);
    free(raw);
    return 0;

fail:
    free(body);
    if (msg_inited) DestroyHttpMessage(&msg);
    free(raw);
    free(request);
    *keep_alive = 0;
    return -1;
}

int wow_http_pool_get(struct wow_http_pool *p, const char *url,
                      struct wow_response *resp) {
    memset(resp, 0, sizeof(*resp));
    char *current_url = strdup(url);
    if (!current_url) return -1;

    int was_https = 0;

    for (int redir = 0; redir <= WOW_HTTP_MAX_REDIRECTS; redir++) {
        struct Url parsed;
        char *urlmem = ParseUrl(current_url, -1, &parsed, kUrlPlus);

        int usessl = 0;
        if (parsed.scheme.n == 5 && !memcasecmp(parsed.scheme.p, "https", 5))
            usessl = 1;
        else if (!(parsed.scheme.n == 4 && !memcasecmp(parsed.scheme.p, "http", 4))) {
            free(urlmem);
            free(parsed.params.p);
            free(current_url);
            return -1;
        }

        if (redir > 0 && was_https && !usessl) {
            fprintf(stderr, "wow: pool: refusing HTTPS to HTTP downgrade\n");
            free(urlmem);
            free(parsed.params.p);
            free(current_url);
            return -1;
        }
        was_https = usessl;

        char *host = parsed.host.n ? strndup(parsed.host.p, parsed.host.n)
                                   : strdup("127.0.0.1");
        char *port = parsed.port.n ? strndup(parsed.port.p, parsed.port.n)
                                   : strdup(usessl ? "443" : "80");

        /* Build path */
        parsed.fragment.p = NULL; parsed.fragment.n = 0;
        parsed.scheme.p = NULL;   parsed.scheme.n = 0;
        parsed.user.p = NULL;     parsed.user.n = 0;
        parsed.pass.p = NULL;     parsed.pass.n = 0;
        parsed.host.p = NULL;     parsed.host.n = 0;
        parsed.port.p = NULL;     parsed.port.n = 0;
        char *fixpath = NULL;
        if (!parsed.path.n || parsed.path.p[0] != '/') {
            fixpath = malloc(1 + parsed.path.n);
            if (fixpath) {
                fixpath[0] = '/';
                if (parsed.path.n) memcpy(fixpath + 1, parsed.path.p, parsed.path.n);
                parsed.path.p = fixpath;
                parsed.path.n += 1;
            }
        }
        char *pathstr = EncodeUrl(&parsed, NULL);
        if (!pathstr || !pathstr[0]) { free(pathstr); pathstr = strdup("/"); }

        free(fixpath);
        free(urlmem);
        free(parsed.params.p);

        int slot = pool_acquire(p, host, port, usessl);
        if (slot == -1) {
            free(host);
            free(port);
            free(pathstr);
            free(current_url);
            return -1;
        }

        struct wow_response single;
        memset(&single, 0, sizeof(single));
        int keep_alive = 0;
        int rc = pool_do_get(&p->entries[slot], pathstr, &single, &keep_alive);
        pool_release(p, slot, keep_alive && rc == 0);

        free(host);
        free(port);
        free(pathstr);

        if (rc != 0) {
            free(current_url);
            return -1;
        }

        if (single.status == 301 || single.status == 302 ||
            single.status == 307 || single.status == 308) {
            if (!single.location || !single.location[0]) {
                wow_response_free(&single);
                free(current_url);
                return -1;
            }
            free(current_url);
            current_url = single.location;
            single.location = NULL;
            wow_response_free(&single);
            continue;
        }

        *resp = single;
        free(current_url);
        return 0;
    }

    free(current_url);
    return -1;
}

void wow_http_pool_cleanup(struct wow_http_pool *p) {
    for (int i = 0; i < WOW_POOL_MAX_CONNS; i++)
        pool_entry_close(&p->entries[i]);
}
