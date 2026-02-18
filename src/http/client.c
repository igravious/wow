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

#include "wow/http/client.h"
#include "wow/version.h"

/* Global verbose flag for HTTP debugging (set by --verbose or -v) */
int wow_http_debug = 0;

#define HasHeader(H)    (!!msg.headers[H].a)
#define HeaderData(H)   (raw + msg.headers[H].a)
#define HeaderLength(H) (msg.headers[H].b - msg.headers[H].a)
#define HeaderEqualCase(H, S) \
    SlicesEqualCase(S, strlen(S), HeaderData(H), (size_t)HeaderLength(H))

/*
 * TLS I/O callbacks — same pattern as cosmo tool/curl/curl.c
 */

static int tls_send(void *ctx, const unsigned char *buf, size_t len) {
    int rc = write(*(int *)ctx, buf, len);
    if (rc == -1) return MBEDTLS_ERR_NET_SEND_FAILED;
    return rc;
}

static int tls_recv(void *ctx, unsigned char *buf, size_t len, uint32_t timeout) {
    (void)timeout;
    int rc = read(*(int *)ctx, buf, len);
    if (rc == -1) return MBEDTLS_ERR_NET_RECV_FAILED;
    if (rc == 0)  return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return rc;
}

/*
 * Extract a header value as a heap-allocated string, or NULL if absent.
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
 * Perform a single HTTP/HTTPS GET request.
 * Populates resp (including resp->location on 3xx). Returns 0 on success.
 */
static int do_get(const char *host, const char *port, int usessl,
                  const char *path, struct wow_response *resp) {
    int ret = -1;
    int sock = -1;
    char *request = NULL;
    char *raw = NULL;
    struct addrinfo *addr = NULL;
    struct HttpMessage msg;
    int msg_inited = 0;

    mbedtls_ssl_config       conf;
    mbedtls_ssl_context      ssl;
    mbedtls_ctr_drbg_context drbg;
    int tls_inited = 0;

    /* DNS resolution */
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };
    if (getaddrinfo(host, port, &hints, &addr) != 0) {
        fprintf(stderr, "wow: could not resolve host: %s\n", host);
        goto out;
    }

    /* Connect with timeout */
    sock = GoodSocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol,
                      false, &(struct timeval){-WOW_HTTP_TIMEOUT_SECS, 0});
    if (sock == -1) {
        fprintf(stderr, "wow: socket creation failed\n");
        goto out;
    }
    if (connect(sock, addr->ai_addr, addr->ai_addrlen) != 0) {
        fprintf(stderr, "wow: failed to connect to %s:%s\n", host, port);
        goto out;
    }
    freeaddrinfo(addr);
    addr = NULL;

    /* TLS handshake */
    if (usessl) {
        mbedtls_ssl_init(&ssl);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_ssl_config_init(&conf);
        tls_inited = 1;

        if (mbedtls_ctr_drbg_seed(&drbg, GetEntropy, NULL, "wow", 3) != 0) {
            fprintf(stderr, "wow: TLS entropy seed failed\n");
            goto out;
        }
        if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_SUITEC) != 0) {
            fprintf(stderr, "wow: TLS config defaults failed\n");
            goto out;
        }
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, GetSslRoots(), NULL);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        
        /* Force HTTP/1.1 via ALPN — reject HTTP/2 which has massive headers
         * that overflow our parser buffers */
        static const char *alpn_protos[] = {"http/1.1", NULL};
        mbedtls_ssl_conf_alpn_protocols(&conf, alpn_protos);
        if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
            fprintf(stderr, "wow: TLS setup failed\n");
            goto out;
        }
        if (mbedtls_ssl_set_hostname(&ssl, host) != 0) {
            fprintf(stderr, "wow: TLS SNI failed for %s\n", host);
            goto out;
        }
        mbedtls_ssl_set_bio(&ssl, &sock, tls_send, NULL, tls_recv);

        int hrc = mbedtls_ssl_handshake(&ssl);
        if (hrc != 0) {
            fprintf(stderr, "wow: TLS handshake with %s failed: %s\n",
                    host, DescribeSslClientHandshakeError(&ssl, hrc));
            goto out;
        }
    }

    /* Build HTTP request */
    appendf(&request,
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%s\r\n"
            "User-Agent: wow/" WOW_VERSION "\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, port);

    /* Send */
    {
        size_t reqlen = appendz(request).i;
        for (size_t i = 0; i < reqlen; ) {
            ssize_t w;
            if (usessl) {
                w = mbedtls_ssl_write(&ssl, (unsigned char *)request + i,
                                      reqlen - i);
                if (w <= 0) {
                    fprintf(stderr, "wow: TLS send failed: %s\n",
                            DescribeMbedtlsErrorCode((int)w));
                    goto out;
                }
            } else {
                w = write(sock, request + i, reqlen - i);
                if (w <= 0) {
                    fprintf(stderr, "wow: send to %s failed\n", host);
                    goto out;
                }
            }
            i += (size_t)w;
        }
    }

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
            if (!tmp) { fprintf(stderr, "wow: out of memory\n"); goto fail_body; }
            raw = tmp;
        }

        ssize_t rc;
        if (usessl) {
            rc = mbedtls_ssl_read(&ssl, (unsigned char *)raw + rawi, rawn - rawi);
            if (rc < 0) {
                if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) rc = 0;
                else {
                    fprintf(stderr, "wow: TLS recv failed: %s\n",
                            DescribeMbedtlsErrorCode((int)rc));
                    goto fail_body;
                }
            }
        } else {
            rc = read(sock, raw + rawi, rawn - rawi);
            if (rc == -1) { fprintf(stderr, "wow: recv failed\n"); goto fail_body; }
        }

        size_t got = (size_t)rc;
        rawi += got;

        switch (state) {
        case kHttpClientStateHeaders:
            if (!got) {
                fprintf(stderr, "wow: connection closed before headers\n");
                if (wow_http_debug && rawi > 0 && rawi < 200) {
                    /* Show what we got for debugging short responses */
                    fprintf(stderr, "wow: partial response: ");
                    for (size_t i = 0; i < rawi; i++) {
                        char c = raw[i];
                        if (c >= 32 && c < 127) fputc(c, stderr);
                        else fputc('?', stderr);
                    }
                    fprintf(stderr, "\n");
                }
                goto fail_body;
            }
            rc = ParseHttpMessage(&msg, raw, rawi, rawn);
            if (rc == -1) {
                fprintf(stderr, "wow: bad HTTP response from %s", host);
                if (msg.status > 0) {
                    fprintf(stderr, " (HTTP %d)", msg.status);
                }
                fprintf(stderr, "\n");
                /* Show response headers for debugging if verbose mode enabled */
                if (wow_http_debug && rawi > 0) {
                    fprintf(stderr, "wow: response headers:\n");
                    for (size_t i = 0; i < rawi && i < 800; i++) {
                        char c = raw[i];
                        /* Stop at end of headers (double CRLF) */
                        if (i > 4 && c == '\n' && 
                            raw[i-1] == '\r' && raw[i-2] == '\n') break;
                        /* Printable ASCII or newline */
                        if (c >= 32 && c < 127) fputc(c, stderr);
                        else if (c == '\n') fputc('\n', stderr);
                        else if (c == '\r') { /* skip */ }
                        else fputc('?', stderr);
                    }
                    fprintf(stderr, "\n");
                }
                goto fail_body;
            }
            if (!rc) break;
            hdrlen = (size_t)rc;

            /* Skip 1xx informational */
            if (msg.status >= 100 && msg.status <= 199) {
                DestroyHttpMessage(&msg);
                InitHttpMessage(&msg, kHttpResponse);
                memmove(raw, raw + hdrlen, rawi - hdrlen);
                rawi -= hdrlen;
                break;
            }

            /* Determine body mode */
            if (HasHeader(kHttpTransferEncoding) &&
                !HeaderEqualCase(kHttpTransferEncoding, "identity")) {
                if (!HeaderEqualCase(kHttpTransferEncoding, "chunked")) {
                    fprintf(stderr, "wow: unsupported transfer encoding\n");
                    goto fail_body;
                }
                state = kHttpClientStateBodyChunked;
                memset(&unchk, 0, sizeof(unchk));
                goto do_chunked;
            } else if (HasHeader(kHttpContentLength)) {
                rc = ParseContentLength(HeaderData(kHttpContentLength),
                                        HeaderLength(kHttpContentLength));
                if (rc == -1) {
                    fprintf(stderr, "wow: bad content-length from %s\n", host);
                    goto fail_body;
                }
                paylen = (size_t)rc;
                if (paylen > WOW_HTTP_MAX_BODY) {
                    fprintf(stderr, "wow: response too large (%zu bytes)\n",
                            paylen);
                    goto fail_body;
                }
                state = kHttpClientStateBodyLengthed;
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    size_t take = have < paylen ? have : paylen;
                    body = malloc(paylen);
                    if (!body) goto fail_body;
                    memcpy(body, raw + hdrlen, take);
                    bodylen = take;
                    if (bodylen >= paylen) goto done;
                }
            } else {
                state = kHttpClientStateBody;
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    body = malloc(have);
                    if (!body) goto fail_body;
                    memcpy(body, raw + hdrlen, have);
                    bodylen = have;
                }
            }
            break;

        case kHttpClientStateBody:
            if (!got) goto done;
            if (bodylen + got > WOW_HTTP_MAX_BODY) {
                fprintf(stderr, "wow: response too large\n");
                goto fail_body;
            }
            {
                char *tmp = realloc(body, bodylen + got);
                if (!tmp) goto fail_body;
                body = tmp;
                memcpy(body + bodylen, raw + rawi - got, got);
                bodylen += got;
            }
            break;

        case kHttpClientStateBodyLengthed:
            if (!got) {
                fprintf(stderr, "wow: connection closed before body complete\n");
                goto fail_body;
            }
            {
                size_t need = paylen - bodylen;
                size_t take = got < need ? got : need;
                if (!body) {
                    body = malloc(paylen);
                    if (!body) goto fail_body;
                }
                memcpy(body + bodylen, raw + rawi - got, take);
                bodylen += take;
                if (bodylen >= paylen) goto done;
            }
            break;

        case kHttpClientStateBodyChunked:
        do_chunked:
            rc = Unchunk(&unchk, raw + hdrlen, rawi - hdrlen, &paylen);
            if (rc == -1) {
                fprintf(stderr, "wow: bad chunked encoding from %s\n", host);
                goto fail_body;
            }
            if (rc) {
                if (paylen > WOW_HTTP_MAX_BODY) {
                    fprintf(stderr, "wow: response too large (%zu bytes)\n",
                            paylen);
                    goto fail_body;
                }
                body = malloc(paylen);
                if (!body) goto fail_body;
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
    ret = 0;
    goto cleanup;

fail_body:
    free(body);
cleanup:
    if (msg_inited) DestroyHttpMessage(&msg);
    free(raw);
    free(request);
    if (addr) freeaddrinfo(addr);
    if (tls_inited) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_ssl_config_free(&conf);
    }
    if (sock != -1) close(sock);
    return ret;

out:
    /* Early failure before msg/body are live */
    free(request);
    free(raw);
    if (addr) freeaddrinfo(addr);
    if (tls_inited) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_ssl_config_free(&conf);
    }
    if (sock != -1) close(sock);
    return -1;
}

/*
 * Parsed URL components for a single redirect hop.
 */
struct parsed_request {
    char *host;
    char *port;
    char *path;
    int   usessl;
};

/*
 * Parse a URL into host/port/path/usessl.
 * Returns 0 on success, -1 on error.
 * Caller must free host, port, path.
 */
static int parse_request_url(const char *url, struct parsed_request *req) {
    struct Url parsed;
    char *urlmem = ParseUrl(url, -1, &parsed, kUrlPlus);

    /* Determine scheme */
    req->usessl = 0;
    if (parsed.scheme.n == 5 &&
        !memcasecmp(parsed.scheme.p, "https", 5)) {
        req->usessl = 1;
    } else if (parsed.scheme.n == 4 &&
               !memcasecmp(parsed.scheme.p, "http", 4)) {
        req->usessl = 0;
    } else {
        fprintf(stderr, "wow: unsupported URL scheme: %.*s\n",
                (int)parsed.scheme.n, parsed.scheme.p);
        free(urlmem);
        free(parsed.params.p);
        return -1;
    }

    /* Extract host + port */
    if (parsed.host.n) {
        req->host = strndup(parsed.host.p, parsed.host.n);
        req->port = parsed.port.n
                  ? strndup(parsed.port.p, parsed.port.n)
                  : strdup(req->usessl ? "443" : "80");
    } else {
        req->host = strdup("127.0.0.1");
        req->port = strdup(req->usessl ? "443" : "80");
    }

    /* Build request path (path + query) */
    parsed.fragment.p = NULL; parsed.fragment.n = 0;
    parsed.scheme.p   = NULL; parsed.scheme.n   = 0;
    parsed.user.p     = NULL; parsed.user.n     = 0;
    parsed.pass.p     = NULL; parsed.pass.n     = 0;
    parsed.host.p     = NULL; parsed.host.n     = 0;
    parsed.port.p     = NULL; parsed.port.n     = 0;
    char *fixpath = NULL;
    if (!parsed.path.n || parsed.path.p[0] != '/') {
        fixpath = malloc(1 + parsed.path.n);
        if (fixpath) {
            fixpath[0] = '/';
            if (parsed.path.n)
                memcpy(fixpath + 1, parsed.path.p, parsed.path.n);
            parsed.path.p = fixpath;
            parsed.path.n += 1;
        }
    }
    req->path = EncodeUrl(&parsed, NULL);
    if (!req->path || !req->path[0]) {
        free(req->path);
        req->path = strdup("/");
    }

    free(fixpath);
    free(urlmem);
    free(parsed.params.p);
    return 0;
}

static void free_parsed_request(struct parsed_request *req) {
    free(req->host);
    free(req->port);
    free(req->path);
}

/*
 * Perform a single HTTP/HTTPS GET, streaming body to an fd.
 * Populates resp headers (status, location) but NOT body.
 * Body data is written directly to out_fd.
 */
static int do_get_to_fd(const char *host, const char *port, int usessl,
                        const char *path, struct wow_response *resp,
                        int out_fd, wow_progress_fn progress,
                        void *progress_ctx) {
    int ret = -1;
    int sock = -1;
    char *request = NULL;
    char *raw = NULL;
    struct addrinfo *addr = NULL;
    struct HttpMessage msg;
    int msg_inited = 0;

    mbedtls_ssl_config       conf;
    mbedtls_ssl_context      ssl;
    mbedtls_ctr_drbg_context drbg;
    int tls_inited = 0;

    /* DNS resolution */
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };
    if (getaddrinfo(host, port, &hints, &addr) != 0) {
        fprintf(stderr, "wow: could not resolve host: %s\n", host);
        goto out;
    }

    /* Connect with timeout */
    sock = GoodSocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol,
                      false, &(struct timeval){-WOW_HTTP_TIMEOUT_SECS, 0});
    if (sock == -1) {
        fprintf(stderr, "wow: socket creation failed\n");
        goto out;
    }
    if (connect(sock, addr->ai_addr, addr->ai_addrlen) != 0) {
        fprintf(stderr, "wow: failed to connect to %s:%s\n", host, port);
        goto out;
    }
    freeaddrinfo(addr);
    addr = NULL;

    /* TLS handshake */
    if (usessl) {
        mbedtls_ssl_init(&ssl);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_ssl_config_init(&conf);
        tls_inited = 1;

        if (mbedtls_ctr_drbg_seed(&drbg, GetEntropy, NULL, "wow", 3) != 0) {
            fprintf(stderr, "wow: TLS entropy seed failed\n");
            goto out;
        }
        if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_SUITEC) != 0) {
            fprintf(stderr, "wow: TLS config defaults failed\n");
            goto out;
        }
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, GetSslRoots(), NULL);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        
        /* Force HTTP/1.1 via ALPN — reject HTTP/2 which has massive headers */
        static const char *alpn_protos_fd[] = {"http/1.1", NULL};
        mbedtls_ssl_conf_alpn_protocols(&conf, alpn_protos_fd);
        
        if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
            fprintf(stderr, "wow: TLS setup failed\n");
            goto out;
        }
        if (mbedtls_ssl_set_hostname(&ssl, host) != 0) {
            fprintf(stderr, "wow: TLS SNI failed for %s\n", host);
            goto out;
        }
        mbedtls_ssl_set_bio(&ssl, &sock, tls_send, NULL, tls_recv);

        int hrc = mbedtls_ssl_handshake(&ssl);
        if (hrc != 0) {
            fprintf(stderr, "wow: TLS handshake with %s failed: %s\n",
                    host, DescribeSslClientHandshakeError(&ssl, hrc));
            goto out;
        }
    }

    /* Build HTTP request */
    appendf(&request,
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%s\r\n"
            "User-Agent: wow/" WOW_VERSION "\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, port);

    /* Send */
    {
        size_t reqlen = appendz(request).i;
        for (size_t i = 0; i < reqlen; ) {
            ssize_t w;
            if (usessl) {
                w = mbedtls_ssl_write(&ssl, (unsigned char *)request + i,
                                      reqlen - i);
                if (w <= 0) {
                    fprintf(stderr, "wow: TLS send failed: %s\n",
                            DescribeMbedtlsErrorCode((int)w));
                    goto out;
                }
            } else {
                w = write(sock, request + i, reqlen - i);
                if (w <= 0) {
                    fprintf(stderr, "wow: send to %s failed\n", host);
                    goto out;
                }
            }
            i += (size_t)w;
        }
    }

    /* Receive + parse headers, stream body to fd */
    InitHttpMessage(&msg, kHttpResponse);
    msg_inited = 1;

    size_t rawi = 0, rawn = 0;
    size_t hdrlen = 0, paylen = 0;
    size_t written = 0;
    int state = kHttpClientStateHeaders;
    struct HttpUnchunker unchk;

    for (;;) {
        if (rawi == rawn) {
            rawn += 4096;
            rawn += rawn >> 1;
            char *tmp = realloc(raw, rawn);
            if (!tmp) { fprintf(stderr, "wow: out of memory\n"); goto fail; }
            raw = tmp;
        }

        ssize_t rc;
        if (usessl) {
            rc = mbedtls_ssl_read(&ssl, (unsigned char *)raw + rawi,
                                  rawn - rawi);
            if (rc < 0) {
                if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) rc = 0;
                else {
                    fprintf(stderr, "wow: TLS recv failed: %s\n",
                            DescribeMbedtlsErrorCode((int)rc));
                    goto fail;
                }
            }
        } else {
            rc = read(sock, raw + rawi, rawn - rawi);
            if (rc == -1) { fprintf(stderr, "wow: recv failed\n"); goto fail; }
        }

        size_t got = (size_t)rc;
        rawi += got;

        switch (state) {
        case kHttpClientStateHeaders:
            if (!got) {
                fprintf(stderr, "wow: connection closed before headers\n");
                if (wow_http_debug && rawi > 0 && rawi < 200) {
                    /* Show what we got for debugging short responses */
                    fprintf(stderr, "wow: partial response: ");
                    for (size_t i = 0; i < rawi; i++) {
                        char c = raw[i];
                        if (c >= 32 && c < 127) fputc(c, stderr);
                        else fputc('?', stderr);
                    }
                    fprintf(stderr, "\n");
                }
                goto fail;
            }
            rc = ParseHttpMessage(&msg, raw, rawi, rawn);
            if (rc == -1) {
                fprintf(stderr, "wow: bad HTTP response from %s", host);
                if (msg.status > 0) {
                    fprintf(stderr, " (HTTP %d)", msg.status);
                }
                fprintf(stderr, "\n");
                /* Show response headers for debugging if verbose mode enabled */
                if (wow_http_debug && rawi > 0) {
                    fprintf(stderr, "wow: response headers:\n");
                    for (size_t i = 0; i < rawi && i < 800; i++) {
                        char c = raw[i];
                        /* Stop at end of headers (double CRLF) */
                        if (i > 4 && c == '\n' && 
                            raw[i-1] == '\r' && raw[i-2] == '\n') break;
                        /* Printable ASCII or newline */
                        if (c >= 32 && c < 127) fputc(c, stderr);
                        else if (c == '\n') fputc('\n', stderr);
                        else if (c == '\r') { /* skip */ }
                        else fputc('?', stderr);
                    }
                    fprintf(stderr, "\n");
                }
                goto fail;
            }
            if (!rc) break;
            hdrlen = (size_t)rc;

            /* Skip 1xx informational */
            if (msg.status >= 100 && msg.status <= 199) {
                DestroyHttpMessage(&msg);
                InitHttpMessage(&msg, kHttpResponse);
                memmove(raw, raw + hdrlen, rawi - hdrlen);
                rawi -= hdrlen;
                break;
            }

            /* Extract Content-Length for progress */
            if (HasHeader(kHttpContentLength)) {
                ssize_t cl = ParseContentLength(
                    HeaderData(kHttpContentLength),
                    HeaderLength(kHttpContentLength));
                if (cl >= 0) paylen = (size_t)cl;
            }

            /* Determine body mode */
            if (HasHeader(kHttpTransferEncoding) &&
                !HeaderEqualCase(kHttpTransferEncoding, "identity")) {
                if (!HeaderEqualCase(kHttpTransferEncoding, "chunked")) {
                    fprintf(stderr, "wow: unsupported transfer encoding\n");
                    goto fail;
                }
                state = kHttpClientStateBodyChunked;
                memset(&unchk, 0, sizeof(unchk));
                goto do_chunked_fd;
            } else if (paylen > 0) {
                state = kHttpClientStateBodyLengthed;
                /* Write any body data already read with headers */
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    size_t take = have < paylen ? have : paylen;
                    if (write(out_fd, raw + hdrlen, take) != (ssize_t)take) {
                        fprintf(stderr, "wow: write to file failed\n");
                        goto fail;
                    }
                    written = take;
                    if (progress) progress(written, paylen, progress_ctx);
                    if (written >= paylen) goto done_fd;
                }
            } else {
                state = kHttpClientStateBody;
                /* Write any leftover body data */
                size_t have = rawi - hdrlen;
                if (have > 0) {
                    if (write(out_fd, raw + hdrlen, have) != (ssize_t)have) {
                        fprintf(stderr, "wow: write to file failed\n");
                        goto fail;
                    }
                    written = have;
                    if (progress) progress(written, 0, progress_ctx);
                }
            }
            break;

        case kHttpClientStateBody:
            if (!got) goto done_fd;
            {
                const char *data = raw + rawi - got;
                if (write(out_fd, data, got) != (ssize_t)got) {
                    fprintf(stderr, "wow: write to file failed\n");
                    goto fail;
                }
                written += got;
                if (progress) progress(written, 0, progress_ctx);
            }
            break;

        case kHttpClientStateBodyLengthed:
            if (!got) {
                fprintf(stderr, "wow: connection closed before body complete\n");
                goto fail;
            }
            {
                size_t need = paylen - written;
                size_t take = got < need ? got : need;
                const char *data = raw + rawi - got;
                if (write(out_fd, data, take) != (ssize_t)take) {
                    fprintf(stderr, "wow: write to file failed\n");
                    goto fail;
                }
                written += take;
                if (progress) progress(written, paylen, progress_ctx);
                if (written >= paylen) goto done_fd;
            }
            break;

        case kHttpClientStateBodyChunked:
        do_chunked_fd: {
            size_t decoded_len;
            rc = Unchunk(&unchk, raw + hdrlen, rawi - hdrlen, &decoded_len);
            if (rc == -1) {
                fprintf(stderr, "wow: bad chunked encoding from %s\n", host);
                goto fail;
            }
            if (rc) {
                /* All chunks decoded — write to fd */
                if (decoded_len > 0) {
                    if (write(out_fd, raw + hdrlen, decoded_len) !=
                        (ssize_t)decoded_len) {
                        fprintf(stderr, "wow: write to file failed\n");
                        goto fail;
                    }
                    written = decoded_len;
                    if (progress) progress(written, 0, progress_ctx);
                }
                goto done_fd;
            }
            break;
        }
        }
    }

done_fd:
    resp->status       = msg.status;
    resp->body         = NULL;
    resp->body_len     = written;
    resp->etag         = extract_header(&msg, raw, kHttpEtag);
    resp->content_type = extract_header(&msg, raw, kHttpContentType);
    resp->location     = extract_header(&msg, raw, kHttpLocation);
    ret = 0;
    goto cleanup_fd;

fail:
cleanup_fd:
    if (msg_inited) DestroyHttpMessage(&msg);
    free(raw);
    free(request);
    if (addr) freeaddrinfo(addr);
    if (tls_inited) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_ssl_config_free(&conf);
    }
    if (sock != -1) close(sock);
    return ret;

out:
    free(request);
    free(raw);
    if (addr) freeaddrinfo(addr);
    if (tls_inited) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_ssl_config_free(&conf);
    }
    if (sock != -1) close(sock);
    return -1;
}

/*
 * Public API: GET with automatic redirect following.
 */
int wow_http_get(const char *url, struct wow_response *resp) {
    memset(resp, 0, sizeof(*resp));
    char *current_url = strdup(url);
    if (!current_url) return -1;

    int was_https = 0;

    for (int redir = 0; redir <= WOW_HTTP_MAX_REDIRECTS; redir++) {
        struct parsed_request req;
        if (parse_request_url(current_url, &req) != 0) {
            free(current_url);
            return -1;
        }

        /* Block HTTPS -> HTTP downgrade */
        if (redir > 0 && was_https && !req.usessl) {
            fprintf(stderr, "wow: refusing HTTPS to HTTP downgrade\n");
            free_parsed_request(&req);
            free(current_url);
            return -1;
        }
        was_https = req.usessl;

        struct wow_response single;
        memset(&single, 0, sizeof(single));
        int rc = do_get(req.host, req.port, req.usessl, req.path, &single);
        free_parsed_request(&req);

        if (rc != 0) {
            free(current_url);
            return -1;
        }

        /* Follow redirects */
        if (single.status == 301 || single.status == 302 ||
            single.status == 307 || single.status == 308) {
            if (!single.location || !single.location[0]) {
                fprintf(stderr, "wow: redirect without Location header\n");
                wow_response_free(&single);
                free(current_url);
                return -1;
            }
            free(current_url);
            current_url = single.location;
            single.location = NULL;  /* ownership transferred */
            wow_response_free(&single);
            continue;
        }

        /* Final response */
        *resp = single;
        free(current_url);
        return 0;
    }

    fprintf(stderr, "wow: too many redirects\n");
    free(current_url);
    return -1;
}

/*
 * Public API: streaming download to file descriptor with redirect following.
 */
int wow_http_download_to_fd(const char *url, int fd,
                            wow_progress_fn progress, void *progress_ctx) {
    char *current_url = strdup(url);
    if (!current_url) return -1;

    int was_https = 0;

    for (int redir = 0; redir <= WOW_HTTP_MAX_REDIRECTS; redir++) {
        struct parsed_request req;
        if (parse_request_url(current_url, &req) != 0) {
            free(current_url);
            return -1;
        }

        /* Block HTTPS -> HTTP downgrade */
        if (redir > 0 && was_https && !req.usessl) {
            fprintf(stderr, "wow: refusing HTTPS to HTTP downgrade\n");
            free_parsed_request(&req);
            free(current_url);
            return -1;
        }
        was_https = req.usessl;

        struct wow_response resp;
        memset(&resp, 0, sizeof(resp));
        int rc = do_get_to_fd(req.host, req.port, req.usessl, req.path,
                              &resp, fd, progress, progress_ctx);
        free_parsed_request(&req);

        if (rc != 0) {
            free(current_url);
            return -1;
        }

        /* Follow redirects */
        if (resp.status == 301 || resp.status == 302 ||
            resp.status == 307 || resp.status == 308) {
            if (!resp.location || !resp.location[0]) {
                fprintf(stderr, "wow: redirect without Location header\n");
                wow_response_free(&resp);
                free(current_url);
                return -1;
            }
            free(current_url);
            current_url = resp.location;
            resp.location = NULL;
            wow_response_free(&resp);
            continue;
        }

        /* Check for HTTP errors */
        if (resp.status != 200) {
            const char *status_text = "";
            switch (resp.status) {
                case 301: status_text = "Moved Permanently"; break;
                case 302: status_text = "Found (redirect)"; break;
                case 307: status_text = "Temporary Redirect"; break;
                case 308: status_text = "Permanent Redirect"; break;
                case 400: status_text = "Bad Request"; break;
                case 401: status_text = "Unauthorized"; break;
                case 403: status_text = "Forbidden"; break;
                case 404: status_text = "Not Found"; break;
                case 429: status_text = "Too Many Requests (rate limited)"; break;
                case 500: status_text = "Internal Server Error"; break;
                case 502: status_text = "Bad Gateway"; break;
                case 503: status_text = "Service Unavailable"; break;
                case 504: status_text = "Gateway Timeout"; break;
            }
            if (status_text[0]) {
                fprintf(stderr, "wow: HTTP %d %s for %s\n",
                        resp.status, status_text, current_url);
            } else {
                fprintf(stderr, "wow: HTTP %d for %s\n", resp.status, current_url);
            }
            wow_response_free(&resp);
            free(current_url);
            return -1;
        }

        wow_response_free(&resp);
        free(current_url);
        return 0;
    }

    fprintf(stderr, "wow: too many redirects\n");
    free(current_url);
    return -1;
}

void wow_response_free(struct wow_response *resp) {
    free(resp->body);
    free(resp->etag);
    free(resp->content_type);
    free(resp->location);
    memset(resp, 0, sizeof(*resp));
}
