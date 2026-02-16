# Phase 7: Parallel Downloads

> The thing that sold us on uv. Multiple gems downloading simultaneously with connection pooling.

## 7a: Download 2 Gems in Parallel

**Demo:** Download 2 gems with pthreads, print timing vs serial.

**Files:**
- `src/pool.c`
- `include/wow/pool.h`

**Implementation:**
- Thread pool with N worker threads (default: 4, match CPU cores)
- Work queue: list of (gem_name, version, url) to download
- Each worker: take item from queue → HTTP GET → write to `~/.cache/wow/gems/`
- Main thread waits for all workers to finish
- Compare timing: serial vs parallel

**Verify:**
```bash
./build/wow.com download-test sinatra rack tilt mustermann
# Serial:   1.8s (4 gems)
# Parallel: 0.6s (4 gems, 4 threads)
```

## 7b: Connection Pooling — Reuse TLS Sessions

**Demo:** Same download, but faster because TLS handshakes are reused.

**mbedTLS thread safety (Kimi review):**

mbedTLS is thread-safe, but with caveats. Each worker thread must own its entire TLS stack — no sharing:

| Object | Thread safety | Approach |
|--------|--------------|----------|
| `mbedtls_ssl_context` | **NOT** thread-safe | One per thread |
| `mbedtls_ssl_config` | Read-only after setup → safe to share | Shared, set up once on main thread |
| Entropy / CTR-DRBG | Not safe to share | One per thread (simpler than mutex) |
| Socket fd | One per connection | One per thread |

Each worker thread owns:
```
- own mbedtls_ssl_context
- own mbedtls_ctr_drbg_context + mbedtls_entropy_context
- own socket
- own persistent connection to rubygems.org
```

The `mbedtls_ssl_config` (CA certs, ciphersuites, protocol version) is set up once on the main thread before spawning workers, then shared read-only. No sharing of mutable state across threads = no thread-safety issues.

**Changes to http.c + pool.c:**
- Connection pool: keep TLS sessions open between requests to same host
- All gem downloads go to `rubygems.org` → one TLS handshake, multiple requests
- HTTP keep-alive (`Connection: keep-alive`)
- Per-thread connection (no sharing TLS state across threads)
- mbedTLS session resumption if supported

**Verify:**
```bash
./build/wow.com download-test sinatra rack tilt mustermann rack-session
# Without pooling: 0.8s (5 gems, 5 TLS handshakes)
# With pooling:    0.4s (5 gems, 1 TLS handshake per thread)
```

## 7c: Stream N Gems in Parallel

**Demo:** Download a real dependency tree (sinatra + all deps) with streaming progress.

**Implementation:**
- Streaming: write to disk as data arrives (don't buffer entire gem in memory)
- Progress display: uv-style, shows each download concurrently
  ```
  ⠼ Preparing packages...
    sinatra-4.1.1.gem         52.1 KiB/52.1 KiB  ✓
    rack-3.1.12.gem           12.3 KiB/234.5 KiB
    mustermann-3.0.3.gem      78.2 KiB/78.2 KiB   ✓
    tilt-2.6.0.gem            0.0 KiB/45.0 KiB
  ```
- Respect global cache: skip download if gem already in `~/.cache/wow/gems/`
- SHA-256 verification after download completes

**Verify:**
```bash
./build/wow.com download-all sinatra   # downloads sinatra + all resolved deps
# Prepared 5 packages in 0.4s
#  ✓ sinatra-4.1.1.gem
#  ✓ rack-3.1.12.gem
#  ✓ mustermann-3.0.3.gem
#  ✓ rack-session-2.1.0.gem
#  ✓ tilt-2.6.0.gem
```
