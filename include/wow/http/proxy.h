#ifndef WOW_HTTP_PROXY_H
#define WOW_HTTP_PROXY_H

/*
 * HTTP proxy support — CONNECT tunnelling for HTTPS through HTTP proxies.
 *
 * Reads HTTPS_PROXY / HTTP_PROXY / ALL_PROXY from the environment,
 * honours NO_PROXY for bypass.  Provides a unified connection function
 * that all HTTP code paths call instead of raw getaddrinfo+connect.
 */

struct wow_proxy {
    char host[256];
    char port[8];       /* "3128" etc. */
};

/*
 * Read HTTPS_PROXY / HTTP_PROXY / ALL_PROXY from environment.
 * Checks NO_PROXY against target_host.
 * Returns 0 + fills *out if proxy found, -1 if no proxy / bypassed.
 */
int wow_proxy_from_env(const char *target_host, int usessl,
                       struct wow_proxy *out);

/*
 * Open a TCP socket to target_host:target_port.
 * If a proxy is configured (env vars), connects through the proxy
 * and performs CONNECT tunnelling for HTTPS targets.
 * Returns socket FD on success, -1 on error.
 */
int wow_http_connect(const char *host, const char *port, int usessl);

#endif
