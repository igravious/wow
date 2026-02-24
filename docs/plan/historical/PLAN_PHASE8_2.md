# Phase 8.2: Production Hardening — Signals, Disk Checks, and Cleanup

> The polish that separates a working demo from a production-ready tool.
> Status: Planning — Not Started

This phase addresses critical gaps between "Phase 8 works in the happy path" and
"wow is ready for daily use by Rubyists." Focused on four tightly-scoped features:
graceful interruption, disk space pre-flight, stale temp cleanup, and a cache clean
command.

**Out of scope** (moved to other phases):
- Retry logic with exponential backoff → Phase 8.3 (HTTP hardening)
- HTTP Range resume for Ruby tarballs → Phase 8.5 (new)

---

## 8.2a: Signal Handling & Graceful Interruption

**Problem:** Pressing Ctrl+C during `wow sync` or `wow rubies install` may leave:
- Partially downloaded files in `~/.cache/wow/gems/`
- Corrupted tar extractions in `~/.local/share/wow/rubies/.temp/`
- Orphaned worker threads holding network connections

**Implementation:**

```c
/* src/signal.c — Signal handling infrastructure */

/* Global volatile flag for graceful shutdown */
volatile sig_atomic_t wow_shutdown_requested = 0;

/* Signal handler — sets flag, actual cleanup in main loops */
void wow_signal_handler(int sig);

/* Cleanup registration — push cleanup functions onto a stack */
typedef void (*wow_cleanup_fn_t)(void *arg);
void wow_cleanup_push(wow_cleanup_fn_t fn, void *arg);
void wow_cleanup_pop(void);

/* Called from download loops, install loops, etc. */
bool wow_should_shutdown(void);
```

**Thread safety for parallel downloads:**

`wow_parallel_download` uses `pthread` workers. The signal handler sets the flag in
the main thread; workers must cooperate:

- `pthread_cond_broadcast` wakes all idle workers when shutdown flag is set
- Workers check `wow_should_shutdown()` **between items** (not mid-transfer) —
  interrupting an in-flight SSL transfer risks corrupted state
- In-flight transfers complete normally, then the worker exits its loop
- No `pthread_cancel` — too messy with mbedTLS SSL state and open file descriptors
- Pool drain: main thread sets flag, broadcasts, then `pthread_join`s all workers

**Cleanup responsibilities:**

| Component | On SIGINT | Cleanup Action |
|-----------|-----------|----------------|
| `download/parallel.c` | Stop accepting new work | Complete in-flight downloads, discard queue |
| `gems/download.c` | Delete partial .gem file | `unlink(tmp_path)` on partial file |
| `rubies/install.c` | Abort extraction | `rm -rf ~/.local/share/wow/rubies/.temp/<id>/` |
| `http/pool.c` | Close connections | `shutdown(fd, SHUT_RDWR)` on all sockets |

**User experience:**
```bash
$ wow rubies install 4.0.1
Downloading ruby-4.0.1-linux-x86_64.tar.gz...
  45% [===========>                   ] 18.2MB/s  ^C
Interrupted by user.
Cleaning up partial download...
Partial installation removed from .temp/
Run the command again to retry.
```

---

## 8.2b: Disk Space Pre-Flight Checks

**Problem:** Downloads fail mysteriously or corrupt when disk is full. No early warning.

**Implementation:**

```c
/* src/resource.c — Resource availability checking */

/* Check available space via statvfs(3) */
int wow_check_disk_space(const char *path, size_t required_bytes,
                         size_t *available);

/* Friendly error messages */
#define WOW_ENOSPC_MSG \
    "Insufficient disk space.\n" \
    "Required: %s\n" \
    "Available: %s\n" \
    "Free up space or set $WOW_CACHE_DIR to a different location."
```

**Size estimation strategy:**

No hardcoded gem size heuristics. Instead, use real data:

| Operation | How we know the size | Fallback |
|-----------|---------------------|----------|
| `wow rubies install` | `Content-Length` from GitHub releases HTTP HEAD | Skip check, warn "unknown size" |
| `wow sync` | Sum of `Content-Length` from gem download URLs | Skip check per-gem if unavailable |
| `wowx <tool>` | Same as sync (Content-Length per gem) | Skip check |

For extraction size estimates, use a conservative 3x multiplier on download size
(compressed → extracted). Ruby tarballs expand ~2.5x; gems vary but 3x covers the
worst case.

**Pre-flight check locations:**

| Operation | Check Location |
|-----------|----------------|
| `wow rubies install` | `~/.local/share/wow/rubies/` |
| `wow sync` | `~/.cache/wow/gems/` + `vendor/bundle/` |
| `wowx <tool>` | `~/.cache/wow/wowx/` |

**User experience:**
```bash
$ wow rubies install 4.0.1
  Required: ~600 MB (download + extract)
  Available: 45.2 GB
Downloading ruby-4.0.1-linux-x86_64.tar.gz...
```

```bash
$ wow rubies install 4.0.1
error: Insufficient disk space.
  Required: ~600 MB (download + extract)
  Available: 312 MB
  Free up space or set $WOW_CACHE_DIR to a different location.
```

---

## 8.2c: Stale Temp Cleanup

**Problem:** `~/.local/share/wow/rubies/.temp/` may accumulate failed extractions
from crashes or interrupted installs. No automatic cleanup.

**Implementation:**

```c
/* src/cleanup.c — Stale temp detection and removal */

/* On startup: check for stale temp directories */
void wow_cleanup_stale_temps(const char *base_dir, int max_age_hours);

/* Mark extraction as complete (atomic rename pattern) */
int wow_install_commit(const char *temp_dir, const char *final_dir);
```

**Stale detection — using `flock()`, not file existence:**

`rubies/install.c` already holds an `flock()` on the temp directory during extraction.
`flock()` auto-releases on process death (kernel handles it), so we never get orphaned
lock files from crashes.

A temp directory is stale if:
1. Modification time > 24 hours ago **AND**
2. No `flock()` is held on it (try `flock(fd, LOCK_EX | LOCK_NB)` — if it succeeds,
   no process owns it)
3. Name matches pattern `.temp-*`

```c
/* Stale detection pseudocode */
int fd = open(temp_dir, O_RDONLY);
if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
    /* No process holds the lock — safe to remove */
    flock(fd, LOCK_UN);
    close(fd);
    wow_rmrf(temp_dir);
} else {
    /* Another process is actively using this — leave it alone */
    close(fd);
}
```

**Automatic cleanup on startup:** `wow sync` and `wow rubies install` call
`wow_cleanup_stale_temps()` at the start, before doing their own work. Silent
unless something is actually cleaned up.

---

## 8.2d: `wow cache clean` Command

**Problem:** Cache directories grow unbounded. No user-facing way to reclaim space.

**Scope — all wow cache locations:**

| Directory | Contents |
|-----------|----------|
| `~/.cache/wow/gems/` | Downloaded `.gem` files |
| `~/.cache/wow/wowx/` | wowx ephemeral environments |
| `~/.local/share/wow/rubies/.temp/` | Stale extraction temps |

**Subcommand interface:**

```bash
wow cache clean              # Remove stale temps + orphaned downloads
wow cache clean --all        # Nuke all caches, start fresh
wow cache clean --dry-run    # Show what would be deleted + space saved
```

**Implementation:**

```c
/* src/cache_cmd.c — wow cache clean subcommand */

int cmd_cache_clean(int argc, char *argv[]);
```

**`--dry-run` output:**
```bash
$ wow cache clean --dry-run
Would remove:
  ~/.cache/wow/gems/       (47 files, 23.4 MB)
  ~/.cache/wow/wowx/       (3 environments, 8.1 MB)
  ~/.local/share/wow/rubies/.temp-abc123/  (stale, 412 MB)
Total: 443.5 MB
```

**`--all` behaviour:**
- Removes everything in `~/.cache/wow/` (gems + wowx)
- Removes stale `.temp-*` dirs in rubies (but NOT installed rubies)
- Confirms before proceeding (unless `--yes` flag)

---

## Implementation Order

1. **Signal handling** (8.2a) — Most user-visible. Foundation for everything else.
2. **Disk space checks** (8.2b) — Simple, prevents the worst failure mode.
3. **Stale temp cleanup** (8.2c) — Quick win, hooks into existing flock pattern.
4. **Cache clean command** (8.2d) — User-facing, builds on cleanup infrastructure.

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/signal.c` | Create | Signal handling + cleanup stack |
| `include/wow/signal.h` | Create | Public signal API |
| `src/resource.c` | Create | Disk space checking via `statvfs(3)` |
| `include/wow/resource.h` | Create | Resource checking API |
| `src/cleanup.c` | Create | Stale temp detection + removal |
| `include/wow/cleanup.h` | Create | Cleanup API |
| `src/cache_cmd.c` | Create | `wow cache clean` subcommand |
| `src/download/parallel.c` | Modify | Integrate shutdown flag + `pthread_cond_broadcast` |
| `src/rubies/install.c` | Modify | Disk check before extract, signal handling |
| `src/main.c` | Modify | Register signal handlers at startup, dispatch `cache` subcommand |
| `src/sync.c` | Modify | Pre-flight disk check, startup stale cleanup |

---

## Verification

```bash
# 1. Signal handling
wow rubies install 4.0.1 &
PID=$!
sleep 2
kill -INT $PID
# Verify: .temp/ cleaned up, no partial ruby-* dir left

# 2. Disk space check (simulate full disk)
mkdir -p /tmp/fake-wow-home
mount -t tmpfs -o size=100M tmpfs /tmp/fake-wow-home
XDG_DATA_HOME=/tmp/fake-wow-home wow rubies install 4.0.1
# Verify: Clean error "Insufficient disk space"

# 3. Stale cleanup
mkdir -p ~/.local/share/wow/rubies/.temp-stale123
touch -d "2 days ago" ~/.local/share/wow/rubies/.temp-stale123
wow sync
# Verify: stale dir removed silently on startup

# 4. Cache clean
wow cache clean --dry-run
# Verify: Lists gems + wowx + stale temps with sizes
wow cache clean --all --yes
# Verify: All cache dirs emptied
```

---

## Dependencies

- **Blocks:** Nothing (can be done in parallel with other Phase 8.x work)
- **Blocked by:** Phase 3 (download), Phase 4 (unpack), Phase 8 (sync)
- **Related:**
  - Phase 8.3 (HTTP hardening — retry logic lives there)
  - Phase 8.5 (HTTP Range resume for Ruby tarballs — new phase)

---

## Post-MVP Enhancements

| Feature | Description | Priority |
|---------|-------------|----------|
| Cache quotas | "Keep only 10GB of gems, LRU eviction" | Low |
| Progress persistence | Save/restore multibar state across interrupts | Nice |
| `wow cache info` | Show cache usage breakdown by category | Low |

---

## Review Credits

Reviewed by Claude + Kimi via agent-relay. Key changes from original draft:
- Removed retry logic (→ Phase 8.3), resume support (→ Phase 8.5)
- Killed gem size heuristics in favour of `Content-Length`
- Added pthread synchronisation details for signal handling
- Clarified `flock()` vs file-existence for stale detection
- Added wowx cache to cleanup scope

---

*Last updated: 2026-02-20*
*Status: Ready for implementation*
