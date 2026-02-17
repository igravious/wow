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

#include "wow/http.h"

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
            "User-Agent: wow/0.1.0\r\n"
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
                goto fail_body;
            }
            rc = ParseHttpMessage(&msg, raw, rawi, rawn);
            if (rc == -1) {
                fprintf(stderr, "wow: bad HTTP response from %s\n", host);
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
 * Public API: GET with automatic redirect following.
 */
int wow_http_get(const char *url, struct wow_response *resp) {
    memset(resp, 0, sizeof(*resp));
    char *current_url = strdup(url);
    if (!current_url) return -1;

    int was_https = 0;

    for (int redir = 0; redir <= WOW_HTTP_MAX_REDIRECTS; redir++) {
        struct Url parsed;
        char *urlmem = ParseUrl(current_url, -1, &parsed, kUrlPlus);

        /* Determine scheme */
        int usessl = 0;
        if (parsed.scheme.n == 5 &&
            !memcasecmp(parsed.scheme.p, "https", 5)) {
            usessl = 1;
        } else if (parsed.scheme.n == 4 &&
                   !memcasecmp(parsed.scheme.p, "http", 4)) {
            usessl = 0;
        } else {
            fprintf(stderr, "wow: unsupported URL scheme: %.*s\n",
                    (int)parsed.scheme.n, parsed.scheme.p);
            free(urlmem);
            free(parsed.params.p);
            free(current_url);
            return -1;
        }

        /* Block HTTPS -> HTTP downgrade */
        if (redir > 0 && was_https && !usessl) {
            fprintf(stderr, "wow: refusing HTTPS to HTTP downgrade\n");
            free(urlmem);
            free(parsed.params.p);
            free(current_url);
            return -1;
        }
        was_https = usessl;

        /* Extract host + port */
        char *host, *port;
        if (parsed.host.n) {
            host = strndup(parsed.host.p, parsed.host.n);
            port = parsed.port.n ? strndup(parsed.port.p, parsed.port.n)
                                 : strdup(usessl ? "443" : "80");
        } else {
            host = strdup("127.0.0.1");
            port = strdup(usessl ? "443" : "80");
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
                if (parsed.path.n) memcpy(fixpath + 1, parsed.path.p, parsed.path.n);
                parsed.path.p = fixpath;
                parsed.path.n += 1;
            }
        }
        char *pathstr = EncodeUrl(&parsed, NULL);
        if (!pathstr || !pathstr[0]) {
            free(pathstr);
            pathstr = strdup("/");
        }

        free(fixpath);
        free(urlmem);
        free(parsed.params.p);

        struct wow_response single;
        memset(&single, 0, sizeof(single));
        int rc = do_get(host, port, usessl, pathstr, &single);
        free(host);
        free(port);
        free(pathstr);

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

void wow_response_free(struct wow_response *resp) {
    free(resp->body);
    free(resp->etag);
    free(resp->content_type);
    free(resp->location);
    memset(resp, 0, sizeof(*resp));
}
