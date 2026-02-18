# Phase 8.6: `wow curl`

## Goal

Expose wow's HTTP stack as a diagnostic tool. `wow curl` uses the exact same code paths as `wow sync`, making it invaluable for debugging network issues in enterprise environments.

**Depends on**: Phase 8.3 (HTTP hardening — proxy, auth, compression)
**Enables**: Debugging Phase 9 (`wow publish`) failures; support tickets with reproducible HTTP traces

## Why This Exists

Users report "wow sync fails behind corporate proxy." You ask them to run `curl -v`. It works. The bug is in wow's HTTP handling. `wow curl` eliminates the guesswork:

```bash
# User runs this, pastes output:
wow curl -v https://rubygems.org/api/v1/gems/rails.json

# Developer sees: proxy detection, auth flow, TLS handshake, headers, body
# Same code path as wow sync — if curl works, sync works
```

## Design Philosophy

**Not a curl replacement.** A **diagnostic tool** for wow's HTTP stack.

- Same flags as curl where possible (`-v`, `-H`, `-X`, `-d`, `-o`)
- Skip flags that don't matter for wow's use cases (cookies, FTP, etc.)
- Output format similar to curl for familiarity

## Command Interface

```
wow curl [OPTIONS] <URL>

Options:
  -v, --verbose          Show request headers, response headers, TLS info
  -H, --header <header>  Add request header (can be repeated)
  -X, --request <method> HTTP method: GET, POST, PUT, DELETE (default: GET)
  -d, --data <data>      Request body (for POST/PUT)
  -o, --output <file>    Write response body to file (default: stdout)
  --proxy <url>          Override proxy URL (else from env)
  --no-proxy             Disable proxy for this request
  --auth <user:pass>     Basic auth (else from netrc/env)
  --bearer <token>       Bearer token auth
  --connect-timeout <s>  Connection timeout (default: 30)
  --max-time <s>         Total operation timeout (default: 0 = none)
  --retry <n>            Retry attempts (default: 3)
  -s, --silent           No progress meter or error messages
  -L, --location         Follow redirects (default: yes, use --no-location)

Examples:
  wow curl https://rubygems.org/api/v1/gems/rails.json
  wow curl -v https://my-company.gemfury.io/api/v1/gems.json
  wow curl -H "Accept: application/json" https://api.rubygems.org/v2
  wow curl --proxy http://proxy.corp:8080 https://rubygems.org
  wow curl --auth user:pass https://private.gems/gems/rails.gem
  wow curl -X POST -d '{"name":"test"}' https://api.example.com
```

## Implementation Steps

### Step 1: Refactor HTTP Client for Reusability

**Files**: `src/http/client.c`, `include/wow/http/client.h`

Current API is high-level: `wow_http_get(url, &resp)`. Need more control for curl.

**New low-level API**:
```c
struct wow_http_options {
    const char *method;           // "GET", "POST", etc.
    struct wow_header *headers;   // NULL-terminated array
    const void *body;
    size_t body_len;
    int follow_redirects;
    int max_redirects;
    int connect_timeout_secs;
    int max_time_secs;
    int retry_count;
    struct wow_proxy *proxy;      // NULL = auto-detect
    struct wow_credentials *auth; // NULL = auto-detect
};

int wow_http_request(const char *url, const struct wow_http_options *opts,
                     struct wow_response *resp);
```

Refactor `wow_http_get()` and `wow_http_download_to_fd()` to use this internally.

**Verify**: Existing tests pass; no behavior change.

### Step 2: Add Request Body Support

**Files**: `src/http/client.c`

For POST/PUT (needed for debugging `wow publish`):

- Add `Content-Length` header from body length
- Write body after request headers
- Support for chunked encoding (streaming large bodies)

**Verify**: `wow curl -X POST -d 'test' httpbin.org/post` works.

### Step 3: Verbose Output (-v)

**Files**: `src/cmd/curl.c` (new), `src/http/client.c` (debug hooks)

When `-v` flag set, print:

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

**Implementation**: Add debug callback to HTTP client:
```c
typedef void (*wow_http_debug_fn)(const char *prefix, const char *data, void *ctx);
void wow_http_set_debug(wow_http_debug_fn fn, void *ctx);
```

**Verify**: Output matches curl format closely enough for familiarity.

### Step 4: CLI Argument Parsing

**Files**: `src/cmd/curl.c` (new), `src/main.c` (dispatch)

Parse curl-style arguments. Handle `-H` multiple times.

**Header handling**:
```c
// -H "Accept: application/json" → add to headers array
// -H "Authorization: Bearer token" → overrides --bearer
// -H "Host: override.com" → overrides automatic Host header
```

**Verify**: All documented options parse correctly.

### Step 5: Output Handling

**Files**: `src/cmd/curl.c`

- Default: response body to stdout
- `-o file`: write to file (binary safe)
- `-`: write to stdout even with `-o -`
- Progress: show % for large downloads (unless `-s`)

**Verify**: Binary files (e.g., `.gem`) download correctly with `-o`.

### Step 6: Error Reporting

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

**Verify**: `wow curl https://invalid.hostname` returns 6.

## Files Created/Modified

```
src/
├── cmd/
│   └── curl.c             # NEW — wow curl command implementation
├── http/
│   └── client.c           # MOD — add wow_http_request(), debug hooks
├── main.c                 # MOD — add "curl" subcommand dispatch

include/wow/
├── cmd/curl.h             # NEW
└── http/client.h          # MOD — new options struct, debug callback
```

## Testing Strategy

| Test | Approach |
|------|----------|
| Basic GET | `wow curl http://httpbin.org/get` matches curl output |
| Verbose mode | `-v` shows TLS info, headers, body |
| POST body | `-X POST -d '{"x":1}'` sends correct body |
| Proxy | Through tinyproxy, `-v` shows CONNECT tunnel |
| Auth | `--auth user:pass` sends Basic header |
| Binary output | `-o file.gem` produces identical file to curl |
| Exit codes | Each error condition returns correct code |

## Usage Examples for Debugging

```bash
# "wow sync fails with SSL error"
wow curl -v https://rubygems.org
# Shows: TLS version, cipher, certificate chain

# "can't reach private gem server"
wow curl -v --proxy http://proxy.corp:8080 https://gems.company.com
# Shows: proxy CONNECT, auth flow, response

# "is this a wow bug or server bug?"
wow curl -v https://problematic.gem.server
# Paste output in issue; same code path as sync

# "verify auth works before wow publish"
wow curl -v --bearer $GEM_HOST_API_KEY https://rubygems.org/api/v1/gems/rails.json
```

## Relationship to curl

| Aspect | curl | wow curl |
|--------|------|----------|
| Code base | cURL project | wow's HTTP stack |
| Features | 200+ options, 20+ protocols | HTTP/HTTPS only, ~10 options |
| Goal | Swiss Army Knife for transfers | Debug wow's HTTP behavior |
| Proxy auth | All mechanisms | Basic, Bearer, env vars |
| Output format | Very configurable | Fixed (similar to curl -v) |
| Speed | Highly optimized | Adequate for debugging |

**When to use curl**: General HTTP work, APIs, scripts.
**When to use wow curl**: Debugging wow issues, verifying wow's network config, support tickets.

## Verification Checklist

- [ ] `wow curl https://rubygems.org` outputs JSON
- [ ] `wow curl -v` shows TLS handshake, headers, body
- [ ] `wow curl --proxy http://proxy:8080` routes through proxy
- [ ] `wow curl -o file.gem` writes binary-identical to curl
- [ ] `wow curl -X POST -d 'test'` sends POST body
- [ ] `wow curl --auth user:pass` sends Basic auth
- [ ] `wow curl https://invalid.host` exits 6
- [ ] `wow curl https://httpbin.org/status/500` exits 22
