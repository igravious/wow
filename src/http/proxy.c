/*
 * src/http/proxy.c — HTTP proxy support with CONNECT tunnelling
 *
 * All wow traffic is HTTPS.  To proxy HTTPS through an HTTP proxy:
 *   1. Connect TCP to the proxy (not the target)
 *   2. Send CONNECT target:443 HTTP/1.1
 *   3. Read 200 Connection Established
 *   4. Do normal TLS handshake through the tunnel
 *
 * Env var precedence (matching curl/wget/uv):
 *   HTTPS target: HTTPS_PROXY -> https_proxy -> ALL_PROXY -> all_proxy
 *   HTTP target:  HTTP_PROXY  -> http_proxy  -> ALL_PROXY -> all_proxy
 *   Bypass:       NO_PROXY    -> no_proxy
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libc/calls/calls.h"
#include "libc/calls/struct/timeval.h"
#include "libc/sock/goodsocket.internal.h"
#include "libc/sock/sock.h"
#include "libc/sysv/consts/af.h"
#include "libc/sysv/consts/ipproto.h"
#include "libc/sysv/consts/sock.h"
#include "third_party/musl/netdb.h"

#include "wow/http/client.h"
#include "wow/http/proxy.h"

/*
 * Check whether target_host is bypassed by the NO_PROXY / no_proxy list.
 * Returns 1 if bypassed (should NOT use proxy), 0 otherwise.
 */
static int is_no_proxy(const char *target_host) {
    const char *np = getenv("NO_PROXY");
    if (!np) np = getenv("no_proxy");
    if (!np || !np[0]) return 0;

    size_t hlen = strlen(target_host);

    /* Walk comma-separated entries */
    const char *p = np;
    while (*p) {
        /* Skip leading whitespace and commas */
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;

        const char *end = p;
        while (*end && *end != ',') end++;

        /* Trim trailing whitespace */
        const char *etrim = end;
        while (etrim > p && etrim[-1] == ' ') etrim--;

        size_t elen = (size_t)(etrim - p);
        /* cppcheck-suppress knownConditionTrueFalse - elen can be 0 for whitespace-only entries */
        if (elen == 0) { p = end; continue; }

        /* "*" bypasses everything */
        if (elen == 1 && p[0] == '*') return 1;

        /* Leading dot: suffix match */
        if (p[0] == '.') {
            if (hlen >= elen &&
                strncasecmp(target_host + hlen - elen, p, elen) == 0)
                return 1;
        } else {
            /* Exact match */
            if (hlen == elen &&
                strncasecmp(target_host, p, elen) == 0)
                return 1;
            /* Suffix match: target ends with .entry */
            if (hlen > elen &&
                target_host[hlen - elen - 1] == '.' &&
                strncasecmp(target_host + hlen - elen, p, elen) == 0)
                return 1;
        }

        p = end;
    }

    return 0;
}

/*
 * Parse a proxy URL like "http://host:port" into host + port.
 * Only http:// scheme is supported for the proxy itself.
 * Returns 0 on success, -1 on error.
 */
static int parse_proxy_url(const char *url, struct wow_proxy *out) {
    /* Must start with http:// */
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "wow: proxy URL must use http:// scheme: %s\n", url);
        return -1;
    }
    const char *hp = url + 7;

    /* Strip trailing slash */
    size_t hplen = strlen(hp);
    while (hplen > 0 && hp[hplen - 1] == '/') hplen--;

    /* Find port separator */
    const char *colon = NULL;
    for (size_t i = 0; i < hplen; i++) {
        if (hp[i] == ':') colon = hp + i;
    }

    if (colon) {
        size_t hostlen = (size_t)(colon - hp);
        if (hostlen == 0 || hostlen >= sizeof(out->host)) return -1;
        memcpy(out->host, hp, hostlen);
        out->host[hostlen] = '\0';

        size_t portlen = hplen - hostlen - 1;
        if (portlen == 0 || portlen >= sizeof(out->port)) return -1;
        memcpy(out->port, colon + 1, portlen);
        out->port[portlen] = '\0';
    } else {
        if (hplen == 0 || hplen >= sizeof(out->host)) return -1;
        memcpy(out->host, hp, hplen);
        out->host[hplen] = '\0';
        /* Default proxy port */
        strcpy(out->port, "3128");
    }

    return 0;
}

int wow_proxy_from_env(const char *target_host, int usessl,
                       struct wow_proxy *out) {
    /* Check bypass first */
    if (is_no_proxy(target_host)) return -1;

    const char *val = NULL;
    if (usessl) {
        val = getenv("HTTPS_PROXY");
        if (!val) val = getenv("https_proxy");
    } else {
        val = getenv("HTTP_PROXY");
        if (!val) val = getenv("http_proxy");
    }
    if (!val) val = getenv("ALL_PROXY");
    if (!val) val = getenv("all_proxy");

    if (!val || !val[0]) return -1;

    return parse_proxy_url(val, out);
}

/*
 * Send CONNECT request and read the 200 response.
 * Returns 0 on success, -1 on error.
 */
static int proxy_tunnel(int sock, const char *host, const char *port) {
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "CONNECT %s:%s HTTP/1.1\r\n"
                     "Host: %s:%s\r\n"
                     "\r\n",
                     host, port, host, port);
    if (n < 0 || (size_t)n >= sizeof(req)) return -1;

    /* Send CONNECT */
    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t w = write(sock, req + sent, (size_t)n - sent);
        if (w <= 0) {
            fprintf(stderr, "wow: proxy CONNECT send failed\n");
            return -1;
        }
        sent += (size_t)w;
    }

    /* Read response — look for "HTTP/1.x 200" and blank line */
    char buf[1024];
    size_t got = 0;
    for (;;) {
        if (got >= sizeof(buf) - 1) {
            fprintf(stderr, "wow: proxy CONNECT response too large\n");
            return -1;
        }
        ssize_t r = read(sock, buf + got, sizeof(buf) - 1 - got);
        if (r <= 0) {
            fprintf(stderr, "wow: proxy closed connection during CONNECT\n");
            return -1;
        }
        got += (size_t)r;
        buf[got] = '\0';

        /* Check for end of headers */
        if (strstr(buf, "\r\n\r\n")) break;
    }

    /* Verify status is 200 */
    if (strncmp(buf, "HTTP/1.", 7) != 0) {
        fprintf(stderr, "wow: proxy returned invalid response\n");
        return -1;
    }
    /* Status code starts at position 9 (after "HTTP/1.x ") */
    if (got < 12 || buf[9] != '2' || buf[10] != '0' || buf[11] != '0') {
        /* Extract status line for error message */
        char *eol = strstr(buf, "\r\n");
        if (eol) *eol = '\0';
        fprintf(stderr, "wow: proxy CONNECT failed: %s\n", buf);
        return -1;
    }

    if (wow_http_debug) {
        fprintf(stderr, "wow: proxy CONNECT tunnel established to %s:%s\n",
                host, port);
    }

    return 0;
}

int wow_http_connect(const char *host, const char *port, int usessl) {
    struct wow_proxy proxy;
    int use_proxy = (wow_proxy_from_env(host, usessl, &proxy) == 0);

    const char *conn_host = use_proxy ? proxy.host : host;
    const char *conn_port = use_proxy ? proxy.port : port;

    /* DNS resolution */
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };
    struct addrinfo *addr;
    if (getaddrinfo(conn_host, conn_port, &hints, &addr) != 0) {
        if (use_proxy)
            fprintf(stderr, "wow: could not resolve proxy host: %s\n",
                    conn_host);
        else
            fprintf(stderr, "wow: could not resolve host: %s\n", host);
        return -1;
    }

    /* Connect with timeout */
    int sock = GoodSocket(addr->ai_family, addr->ai_socktype,
                          addr->ai_protocol, false,
                          &(struct timeval){-WOW_HTTP_TIMEOUT_SECS, 0});
    if (sock == -1) {
        fprintf(stderr, "wow: socket creation failed\n");
        freeaddrinfo(addr);
        return -1;
    }
    if (connect(sock, addr->ai_addr, addr->ai_addrlen) != 0) {
        if (use_proxy)
            fprintf(stderr, "wow: failed to connect to proxy %s:%s\n",
                    conn_host, conn_port);
        else
            fprintf(stderr, "wow: failed to connect to %s:%s\n", host, port);
        freeaddrinfo(addr);
        close(sock);
        return -1;
    }
    freeaddrinfo(addr);

    /* CONNECT tunnel for proxied HTTPS */
    if (use_proxy && usessl) {
        if (proxy_tunnel(sock, host, port) != 0) {
            close(sock);
            return -1;
        }
    }

    return sock;
}
