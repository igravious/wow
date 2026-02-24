# Phase B: HTTP Enterprise — Corporate Networks & Reliability

> Production HTTP stack: compression, proxies, auth, retry, resume.
> Status: Planning — Not Started
> MVP: Medium (High for corporate users)

**Depends on**: Phase 2 (basic HTTPS), Phase A (signal handling)
**Enables**: Phase 9 (`wow publish` — uploads need auth/proxy/retry)

---

## Ba: Response Decompression (gzip, deflate)

**Implementation:**

```c
/* src/http/client.c */

/* Add to request headers */
"Accept-Encoding: gzip, deflate"

/* Detect response header */
if (strstr(headers, "Content-Encoding: gzip")) {
    /* Wrap body read with inflate() */
}
```

- Stream decompression (no buffering entire response)
- zlib already linked for tar extraction

**Verify:** `wow curl -v https://rubygems.org` shows compressed transfer, decompressed output.

---

## Bb: HTTP Proxy Support (HTTP CONNECT)

**Environment variables:**
- `HTTP_PROXY` / `http_proxy`
- `HTTPS_PROXY` / `https_proxy`
- `NO_PROXY` — comma-separated hostnames to bypass

**Implementation:**

```c
/* src/http/proxy.c */

struct wow_proxy {
    char *url;        /* http://proxy.company.com:8080 */
    char *host;
    char *port;
    char *username;   /* NULL if no auth */
    char *password;
};

/* HTTPS via CONNECT tunnel */
CONNECT rubygems.org:443 HTTP/1.1
Host: rubygems.org:443

/* HTTP via proxy */
GET http://rubygems.org/gems/rails.json HTTP/1.1
Host: rubygems.org
```

---

## Bc: Authentication (Basic + Bearer + netrc)

**Mechanisms:**

| Mechanism | Source |
|-----------|--------|
| Basic auth | `user:pass@` in proxy URL, or `--auth user:pass` |
| Bearer token | `--bearer <token>` or `WOW_AUTH_TOKEN_<HOST>` |
| netrc | `~/.netrc` lookup for host |

**Precedence:**
1. Explicit env var (`WOW_AUTH_TOKEN_RUBYGEMS_ORG`)
2. netrc entry
3. Prompt (interactive only)

**Security:** Redact credentials in `wow curl -v` output.

---

## Bd: Retry with Exponential Backoff

**Retryable conditions:**
- HTTP 429 (respect `Retry-After` header)
- HTTP 503, 504
- Network timeouts, connection reset

**Non-retryable:**
- HTTP 4xx (except 429)
- TLS certificate errors
- DNS failures

**Policy:**
- 3 retries
- Exponential: 1s → 2s → 4s
- Max 10s total

---

## Be: Connection Pool Hardening

**Current:** Simple LRU, idle timeout.

**Additions:**
- `MSG_PEEK` before reuse — detect closed connections
- Max connection age: 5 minutes
- Per-host limits (avoid overwhelming small servers)

```c
/* src/http/pool.c */
int wow_http_pool_check_health(struct wow_connection *conn);
void wow_http_pool_set_max_idle_per_host(int max);
```

---

## Bf: Proxy Authentication

**HTTP 407 response handling:**

```http
HTTP/1.1 407 Proxy Authentication Required
Proxy-Authenticate: Basic realm="Corporate Proxy"

Proxy-Authorization: Basic base64(user:pass)
```

---

## Bg: Download Resume for Ruby Tarballs

**Scope:** `wow rubies install` only (200MB+ tarballs). Gems are <500KB — full retry is fine.

**HTTP Range requests:**

```bash
# Check support
HEAD /ruby-4.0.1.tar.gz
Accept-Ranges: bytes
Content-Length: 198700000

# Resume partial download
GET /ruby-4.0.1.tar.gz
Range: bytes=89400000-

# Server responds:
HTTP/1.1 206 Partial Content
Content-Range: bytes 89400000-198699999/198700000
```

**Partial file convention:**

```
~/.cache/wow/rubies/ruby-4.0.1-ubuntu-22.04-x64.tar.gz.partial
```

**Flow:**
1. User interrupts download → preserve `.partial` file
2. User re-runs → detect `.partial`, stat for size
3. HEAD request → verify `Accept-Ranges: bytes`
4. GET with `Range:` header → append to partial
5. On completion: rename `.partial` → final path

**Edge cases:**
- Server doesn't support Range → delete partial, full re-download
- Partial file larger than Content-Length → delete, restart
- SHA256 validation post-resume → catch corruption

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/http/proxy.c` | Create | HTTP CONNECT proxy support |
| `src/http/auth.c` | Create | Basic/Bearer/netrc auth |
| `src/http/range.c` | Create | HTTP Range request support |
| `src/http/client.c` | Modify | Decompression, retry, proxy integration |
| `src/http/pool.c` | Modify | MSG_PEEK health check, age limits |
| `src/rubies/install.c` | Modify | Resume-capable download, preserve partial on interrupt |

---

## Verification

```bash
# Compression
wow curl -v https://rubygems.org/api/v1/gems/rails.json | grep Content-Encoding

# Proxy
HTTP_PROXY=http://proxy:8080 wow curl https://rubygems.org

# Auth
wow curl --auth user:pass https://private.gems/gems/rails.gem

# Retry
# Mock server returning 429 with Retry-After: 2

# Resume
wow rubies install 4.0.1 &
kill -INT $!
wow rubies install 4.0.1  # Should resume from partial
```

---

*Last updated: 2026-02-21*
*Status: Draft*
