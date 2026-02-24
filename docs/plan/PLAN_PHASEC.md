# Phase C: Diagnostics — Debugging wow's HTTP Stack

> `wow curl`: The same HTTP code paths as `wow sync`, exposed for debugging.
> Status: Planning — Not Started
> MVP: Low (High for support/debugging)

**Depends on**: Phase B (HTTP hardening)
**Useful for**: Debugging corporate proxy issues, auth problems, SSL failures

---

## Ca: `wow curl` Command

**Purpose:** Expose wow's HTTP stack as a diagnostic tool. Same code paths as `wow sync`.

**Interface:**

```bash
wow curl [OPTIONS] <URL>

Options:
  -v, --verbose          Show request/response headers, TLS info
  -H, --header <header>  Add request header (repeatable)
  -X, --request <method> HTTP method: GET, POST, PUT, DELETE
  -d, --data <data>      Request body (for POST/PUT)
  -o, --output <file>    Write response to file (default: stdout)
  --proxy <url>          Override proxy URL
  --no-proxy             Disable proxy for this request
  --auth <user:pass>     Basic auth
  --bearer <token>       Bearer token auth
  --connect-timeout <s>  Connection timeout (default: 30)
  --max-time <s>         Total timeout (default: 0 = none)
  --retry <n>            Retry attempts (default: 3)
  -s, --silent           No progress meter
  -L, --location         Follow redirects (default: yes)
```

**Examples:**

```bash
# Basic GET
wow curl https://rubygems.org/api/v1/gems/rails.json

# Verbose (debugging SSL/proxy)
wow curl -v https://rubygems.org

# POST with body
wow curl -X POST -d '{"name":"test"}' https://api.example.com

# Through corporate proxy
wow curl --proxy http://proxy.corp:8080 https://gems.company.com

# Verify auth works before wow publish
wow curl -v --bearer $GEM_HOST_API_KEY https://rubygems.org/api/v1/gems/rails.json
```

---

## Cb: HTTP Request Body Support

**Needed for:** Debugging `wow publish` (POST/PUT requests).

**Implementation:**

```c
struct wow_http_options {
    const char *method;           /* "GET", "POST", "PUT" */
    const void *body;
    size_t body_len;
    /* ... other options ... */
};

int wow_http_request(const char *url, const struct wow_http_options *opts,
                     struct wow_response *resp);
```

- Add `Content-Length` header from body length
- Write body after request headers
- Support chunked encoding for large bodies

---

## Cc: Verbose Mode (-v)

**Output format** (curl-compatible):

```
* Connected to rubygems.org (151.101.65.227) port 443
* TLS: mbedTLS/2.28.0, TLSv1.3, cipher: TLS_AES_256_GCM_SHA384
* Certificate: CN=*.rubygems.org, O=Fastly, Inc.
> GET /api/v1/gems/rails.json HTTP/1.1
> Host: rubygems.org
> User-Agent: wow/1.0.0
> Accept: */*
> Accept-Encoding: gzip, deflate
>
< HTTP/1.1 200 OK
< Content-Type: application/json
< Content-Encoding: gzip
< Content-Length: 1234
<
{ [1234 bytes data]
```

**Implementation:** Debug callback in HTTP client.

```c
typedef void (*wow_http_debug_fn)(const char *prefix, const char *data, void *ctx);
void wow_http_set_debug(wow_http_debug_fn fn, void *ctx);
```

---

## Cd: Exit Code Compatibility

Match curl's exit codes where practical:

| Exit | Meaning |
|------|---------|
| 0 | Success |
| 1 | Generic error |
| 6 | Couldn't resolve host |
| 7 | Failed to connect |
| 22 | HTTP error (>= 400) |
| 28 | Operation timeout |
| 35 | SSL/TLS handshake failed |
| 52 | Server returned nothing |

**Usage:** Scripts can check `wow curl` exit codes programmatically.

---

## Ce: CLI Argument Parsing

**Requirements:**
- `-H` repeatable for multiple headers
- `-X` default to GET
- `-o -` means stdout (even with `-o` flag)
- `--no-location` to disable redirects

**Header handling precedence:**
- `-H "Authorization: Bearer token"` overrides `--bearer`
- `-H "Host: override.com"` overrides automatic Host header

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/cmd/curl.c` | Create | `wow curl` implementation |
| `src/http/client.c` | Modify | Add `wow_http_request()`, debug hooks |
| `src/main.c` | Modify | Add "curl" subcommand dispatch |
| `include/wow/http/client.h` | Modify | New options struct, debug callback |

---

## Relationship to Real curl

| Aspect | curl | wow curl |
|--------|------|----------|
| Code base | cURL project | wow's HTTP stack |
| Features | 200+ options, 20+ protocols | HTTP/HTTPS only, ~10 options |
| Goal | Swiss Army Knife | Debug wow's HTTP behavior |
| Speed | Highly optimized | Adequate for debugging |

**When to use curl:** General HTTP work, APIs, scripts.
**When to use wow curl:** Debugging wow issues, verifying wow's network config.

---

## Verification

```bash
# Basic GET
wow curl http://httpbin.org/get

# Verbose shows TLS
wow curl -v https://rubygems.org 2>&1 | grep "TLS:"

# POST body
wow curl -X POST -d '{"x":1}' http://httpbin.org/post

# Binary output
wow curl -o file.gem https://rubygems.org/gems/rails-7.0.0.gem

# Exit codes
wow curl https://invalid.host; echo $?   # Should be 6
wow curl https://httpbin.org/status/500; echo $?  # Should be 22
```

---

*Last updated: 2026-02-21*
*Status: Draft*
