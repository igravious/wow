# Phase 8.5: Download Resume for Ruby Tarballs

> Resume interrupted Ruby downloads instead of starting over.
> Status: Planning — Not Started

**Depends on**: Phase 8.2 (signal handling), Phase 8.3 (HTTP hardening — retry logic)
**Scope**: `wow rubies install` only. Gem downloads are small (<500KB typical) and don't need resume.

## Why This Phase Exists

Ruby tarballs from GitHub releases are 200MB+. On slow or unreliable connections,
Ctrl+C or a network blip means re-downloading from scratch. Phase 8.2 handles
graceful cleanup of partial files; this phase preserves them for resumption instead.

## How It Works

### HTTP Range Requests

GitHub releases (the source for ruby-builder tarballs) support `Range: bytes=N-`
via their CDN. We use this to resume partial downloads.

```c
/* src/http/range.c — Partial content support */

/* Issue HEAD to check Accept-Ranges + Content-Length */
int wow_http_check_range(const char *url, size_t *total_size);

/* Download with resume: reads existing file size, sends Range header */
int wow_http_download_resume(const char *url, const char *dest_path,
                             wow_progress_cb_t progress_cb, void *cb_arg);
```

### Resume Flow

```
1. User runs: wow rubies install 4.0.1
2. Download starts: ruby-4.0.1-ubuntu-22.04-x64.tar.gz → .temp file
3. Ctrl+C at 45% → Phase 8.2 signal handler fires
4. Instead of deleting partial file, keep it with .partial suffix
5. User re-runs: wow rubies install 4.0.1
6. Detect .partial file, stat() for size → 89.4 MB
7. HEAD request → Content-Length: 198.7 MB, Accept-Ranges: bytes
8. GET with Range: bytes=89400000- → server returns 206 Partial Content
9. Append to .partial file, rename on completion
```

### Partial File Convention

```
~/.cache/wow/rubies/ruby-4.0.1-ubuntu-22.04-x64.tar.gz.partial
```

- `.partial` suffix = incomplete download, safe to resume or delete
- On successful completion: rename `.partial` → final path
- On `wow cache clean`: `.partial` files are removed

### Validation

After resume completes, verify integrity:
- **Content-Length match**: final file size == expected total
- **If available**: SHA256 checksum from ruby-builder definition file

If validation fails, delete the partial and retry from scratch (once).

## Edge Cases

| Situation | Behaviour |
|-----------|-----------|
| Server doesn't support Range | Delete partial, full re-download |
| Partial file larger than Content-Length | Delete partial, full re-download |
| Partial file is 0 bytes | Same as no partial — full download |
| Server returns different Content-Length than before | Delete partial, full re-download |
| `.partial` file is corrupt (bit rot) | Caught by SHA256 validation post-download |

## User Experience

```bash
$ wow rubies install 4.0.1
Downloading ruby-4.0.1-ubuntu-22.04-x64.tar.gz...
  45% [===========>                   ] 18.2MB/s  ^C
Interrupted by user. Partial download saved.

$ wow rubies install 4.0.1
Downloading ruby-4.0.1-ubuntu-22.04-x64.tar.gz...
  Resuming from 89.4 MB / 198.7 MB
  67% [========================>      ] 21.3MB/s
```

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/http/range.c` | Create | HTTP Range request support |
| `include/wow/http/range.h` | Create | Range download API |
| `src/rubies/install.c` | Modify | Use resume-capable download, keep `.partial` on interrupt |
| `src/signal.c` | Modify | Rubies cleanup handler preserves `.partial` instead of deleting |
| `src/cleanup.c` | Modify | `wow cache clean` removes `.partial` files |

## Verification

```bash
# 1. Normal resume
wow rubies install 4.0.1 &
PID=$!
sleep 3
kill -INT $PID
ls ~/.cache/wow/rubies/*.partial  # should exist
wow rubies install 4.0.1          # should resume, not restart

# 2. Server without Range support (simulated)
# Verify: falls back to full download gracefully

# 3. Corrupt partial
truncate -s 1000 ~/.cache/wow/rubies/ruby-4.0.1-*.partial
wow rubies install 4.0.1
# Verify: SHA256 mismatch detected, full re-download
```

## Dependencies

- **Blocked by**: Phase 8.2 (signal handling — partial file preservation), Phase 8.3 (retry logic)
- **Blocks**: Nothing
- **Related**: Phase 8.6 (`wow curl` can test Range support: `wow curl -v -H "Range: bytes=0-100" <url>`)

---

*Last updated: 2026-02-21*
*Status: Draft*
