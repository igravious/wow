# Phase 8.3: HTTP Stack Hardening

## Goal

Make wow's HTTP stack production-ready for enterprise/corporate environments. This phase adds compression, proxy support, authentication, retry logic, and improved connection pooling — all prerequisites for reliable operation behind corporate firewalls and for private gem servers.

**Depends on**: Phase 8 (basic `wow sync` working)
**Enables**: Phase 9 (`wow publish` — uploads need auth/proxy/retry)

## Why This Phase Exists

Phase 8's HTTP stack is minimal: GET requests, TLS, redirects, basic connection pooling. It works for rubygems.org in open networks. It fails for:

- Corporate environments with HTTP proxies
- Private gem servers requiring authentication
- Intermittent network conditions (needs retry)
- Bandwidth-constrained connections (needs compression)

Without Phase 8.3, Phase 9 (`gem push`) would be unreliable — uploads can't be retried as easily as downloads, and corporate users couldn't publish at all.

## Implementation Steps

### Step 1: Response Decompression (gzip, deflate)

**Files**: `src/http/client.c`, `include/wow/http/client.h`

zlib is already linked for tar extraction. Add response body decompression:

- Add `Content-Encoding` header detection in response parser
- Wrap response body read with `inflate()` for `gzip` and `deflate`
- Stream decompression (don't buffer entire response)
- Default: request `Accept-Encoding: gzip, deflate`

**API changes**:
```c
// Transparent — existing wow_http_get() callers get decompressed body
// Internal: decompression state in response handling
```

**Verify**: `wow curl -v https://rubygems.org/api/v1/gems/rails.json` shows compressed transfer, decompressed output.

### Step 2: Proxy Support (HTTP CONNECT)

**Files**: `src/http/proxy.c`, `include/wow/http/proxy.h`, `src/http/client.c`

Read `HTTP_PROXY`, `HTTPS_PROXY`, `NO_PROXY` environment variables (standard curl format).

**Implementation**:
- Parse proxy URL: `http://proxy.company.com:8080` or `http://user:pass@proxy:8080`
- For HTTPS destinations: `CONNECT host:port` tunnel through proxy
- For HTTP destinations: full URL in request line, proxy forwards
- `NO_PROXY`: comma-separated hostnames/patterns to bypass proxy

**New types**:
```c
struct wow_proxy {
    char *url;           // http://proxy:8080
    char *host;
    char *port;
    char *username;      // NULL if no auth
    char *password;
};

int wow_proxy_from_env(const char *target_url, struct wow_proxy **out);
void wow_proxy_free(struct wow_proxy *p);
```

**Verify**: Behind corporate proxy, `wow curl https://rubygems.org` works when `curl` works.

### Step 3: Authentication (Bearer + Basic)

**Files**: `src/http/auth.c`, `include/wow/http/auth.h`, `src/http/client.c`

Support for private gem indexes and authenticated proxies.

**Mechanisms**:
- **Basic auth**: `Authorization: Basic base64(user:pass)`
- **Bearer tokens**: `Authorization: Bearer <token>`
- **netrc**: `~/.netrc` lookup for host credentials (standard format)
- **Environment**: `WOW_AUTH_TOKEN_<HOST>` (uppercased, dots → underscores)

**Auth precedence** (matching uv):
1. Explicit env var for host
2. netrc file entry
3. Prompt (interactive only, hidden input)

**New API**:
```c
struct wow_credentials {
    char *username;
    char *password;  // or token for Bearer
    enum { WOW_AUTH_BASIC, WOW_AUTH_BEARER } type;
};

int wow_auth_for_host(const char *host, struct wow_credentials **out);
void wow_credentials_free(struct wow_credentials *c);
```

**Security**: Redact credentials in debug output (`wow curl -v`).

**Verify**: `wow curl https://private.gem.server/gems/rails.gem` with netrc auth succeeds.

### Step 4: Retry with Exponential Backoff

**Files**: `src/http/client.c` (retry logic), `src/http/pool.c` (connection health)

Retry transient failures automatically.

**Retryable conditions**:
- HTTP 429 (Too Many Requests) — respect `Retry-After` header
- HTTP 503 (Service Unavailable)
- Network timeouts (connect, read)
- Connection reset / broken pipe

**Non-retryable** (fail fast):
- HTTP 4xx (client errors, except 429)
- TLS certificate errors
- DNS resolution failures

**Policy**: 3 retries, exponential backoff (1s, 2s, 4s), max 10s total.

**Implementation**: Wrap `do_get()` with retry loop; recheck connection health between attempts.

**Verify**: Mock server returning 429 with Retry-After; wow retries at correct interval.

### Step 5: Connection Pool Improvements

**Files**: `src/http/pool.c`, `include/wow/http/pool.h`

Harder problem: detect and handle stale connections.

**Current**: Simple LRU, idle timeout.

**Add**:
- `MSG_PEEK` before reuse: check if peer closed connection
- Connection age limit (max 5 minutes)
- Per-host connection limits (avoid overwhelming small servers)
- HTTP/1.1 keep-alive with `Connection: keep-alive` request header

**New API**:
```c
int wow_http_pool_check_health(struct wow_connection *conn);
void wow_http_pool_set_max_idle_per_host(int max);
```

**Verify**: Long-running `wow sync` doesn't fail with "connection reset by peer" mid-download.

### Step 6: Proxy Authentication

**Files**: `src/http/proxy.c`, `src/http/auth.c`

Proxies often require their own auth (different from target server).

**Mechanisms**:
- HTTP 407 (Proxy Authentication Required) response
- `Proxy-Authorization` header (Basic, Bearer)
- Credentials from `HTTP_PROXY` URL or separate env vars

**Verify**: `wow curl` through authenticated corporate proxy succeeds.

## Testing Strategy

| Component | Test Approach |
|-----------|---------------|
| Compression | Mock server returning gzip'd body, verify decompressed |
| Proxy | Local tinyproxy, verify CONNECT tunneling |
| Auth | Mock server requiring Basic/Bearer, verify headers |
| Retry | Mock server with 429/503/close, verify retry count |
| Pool | Simulate server-side close, verify MSG_PEEK detection |

**Integration test**: Docker Compose stack with proxy, authenticated registry, flaky network simulation.

## Files Created/Modified

```
src/
├── http/
│   ├── proxy.c          # NEW — HTTP CONNECT proxy support
│   ├── auth.c           # NEW — Basic/Bearer/netrc auth
│   └── client.c         # MOD — decompression, retry, proxy integration

include/wow/http/
├── proxy.h              # NEW
├── auth.h               # NEW
└── client.h             # MOD — retry options
```

## Verification Checklist

- [ ] `wow curl https://rubygems.org` uses gzip (smaller transfer)
- [ ] Behind `HTTP_PROXY`, requests route through proxy
- [ ] `NO_PROXY=rubygems.org` bypasses proxy for that host
- [ ] `~/.netrc` credentials work for private gem server
- [ ] 429 response with `Retry-After: 2` waits 2 seconds then retries
- [ ] Long-idle connection detected as stale before reuse
- [ ] Credentials never appear in `wow curl -v` output

## Dependencies

- zlib (already linked for tar) — decompression
- mbedTLS (already linked) — TLS through proxy tunnels
- Existing `src/http/pool.c` — connection reuse foundation
