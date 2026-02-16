# Phase 2: HTTPS Client

> Talk to rubygems.org over HTTPS and parse JSON responses.

## 2a: HTTP GET

**Demo:** `./wow.com fetch http://example.com` prints raw HTTP response.

**Files:**
- `src/http.c`
- `include/wow/http.h`

**Implementation:**
- BSD socket connect
- Send HTTP/1.1 GET request with Host header
- Read response, parse status line + headers + body
- Handle `Content-Length` and `Transfer-Encoding: chunked`
- Print raw response to stdout

This is a temporary `fetch` subcommand for testing — removed later.

**Verify:** `./build/wow.com fetch http://example.com` → prints HTML

## 2b: HTTPS GET (mbedTLS)

**Demo:** `./wow.com fetch https://rubygems.org/api/v1/gems/sinatra.json` prints JSON.

**Changes to http.c:**
- Detect `https://` scheme
- mbedTLS TLS handshake over the socket
- Certificate validation (use Cosmo's bundled root CAs)
- TLS read/write wrappers
- Handle HTTP redirects (301, 302) — rubygems.org uses them

**Verify:** `./build/wow.com fetch https://rubygems.org/api/v1/gems/sinatra.json` → prints JSON blob

## 2c: JSON Parsing + Pretty Print

**Demo:** `./wow.com gem-info sinatra` fetches and pretty-prints gem metadata.

**Files:**
- `vendor/cjson/cJSON.c`
- `vendor/cjson/cJSON.h`
- `src/registry.c`
- `include/wow/registry.h`

**Implementation:**
- Vendor cJSON (download from https://github.com/DaveGamble/cJSON)
- `registry.c`: `registry_gem_info(name)` → hits `/api/v1/gems/{name}.json`
- Parse JSON with cJSON
- Pretty-print: name, version, authors, summary, dependencies

**Verify:**
```bash
./build/wow.com gem-info sinatra
# Output:
# sinatra 4.1.1
# Authors: Blake Mizerany, Ryan Tomayko, Simon Rozet, Kunpei Sakai
# Classy web-development dressed in a DSL
# Dependencies: mustermann (~> 3.0), rack (>= 3.0.0, < 4), ...
```
