# Phase A: Production Hardening — Signals, Resources, and Cleanup

> The polish that separates a working demo from a production-ready tool.
> Status: Planning — Not Started
> MVP: High

**Depends on**: Phase 3 (download), Phase 4 (unpack), Phase 8 (sync)
**Related to**: Phase B (HTTP hardening)

---

## Aa: Signal Handling & Graceful Interruption

**Problem:** Ctrl+C during `wow sync` leaves partial downloads and corrupted extractions.

**Implementation:**

```c
/* src/signal.c */

volatile sig_atomic_t wow_shutdown_requested = 0;

void wow_signal_handler(int sig) {
    wow_shutdown_requested = 1;
    pthread_cond_broadcast(&work_available);  /* Wake all workers */
}

/* Workers check between items, not mid-transfer */
bool wow_should_shutdown(void) {
    return wow_shutdown_requested;
}
```

**Cleanup responsibilities:**

| Component | Action |
|-----------|--------|
| `download/parallel.c` | Complete in-flight, discard queue |
| `gems/download.c` | `unlink()` partial .gem file |
| `rubies/install.c` | `rm -rf` `.temp/` directory |
| `http/pool.c` | `shutdown(fd, SHUT_RDWR)` all sockets |

**User experience:**

```bash
$ wow rubies install 4.0.1
Downloading ruby-4.0.1-linux-x86_64.tar.gz...
  45% [===========>                   ] 18.2MB/s  ^C
Interrupted by user.
Cleaning up partial download...
Run the command again to retry.
```

---

## Ab: Disk Space Pre-Flight Checks

**Problem:** Downloads fail mysteriously when disk is full.

**Implementation:**

```c
/* src/resource.c */
int wow_check_disk_space(const char *path, size_t required_bytes);
```

**Size estimation:**

| Operation | Source | Multiplier |
|-----------|--------|------------|
| `wow rubies install` | HTTP HEAD `Content-Length` | 3x (compressed → extracted) |
| `wow sync` | Sum of gem `Content-Length` | 2x (extract) |
| `wowx <tool>` | Same as sync | 2x |

**User experience:**

```bash
$ wow rubies install 4.0.1
  Required: ~600 MB (download + extract)
  Available: 45.2 GB ✓
Downloading...

$ wow rubies install 4.0.1
error: Insufficient disk space.
  Required: ~600 MB
  Available: 312 MB
  Free up space or set $WOW_CACHE_DIR to a different location.
```

---

## Ac: Stale Temp Cleanup

**Problem:** `~/.local/share/wow/rubies/.temp/` accumulates from crashes/interrupts.

**Implementation:**

```c
/* src/cleanup.c */

void wow_cleanup_stale_temps(const char *base_dir, int max_age_hours);
```

**Stale detection using `flock()`:**

```c
int fd = open(temp_dir, O_RDONLY);
if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
    /* No process holds lock — safe to remove */
    flock(fd, LOCK_UN);
    wow_rmrf(temp_dir);
}
```

**Criteria:**
- Mtime > 24 hours ago
- No `flock()` held (auto-releases on process death)
- Name matches `.temp-*`

**Automatic:** Called on `wow sync` and `wow rubies install` startup.

---

## Ad: `wow cache clean` Command

**Problem:** Cache directories grow unbounded. No user-facing cleanup.

**Scope:**

| Directory | Contents |
|-----------|----------|
| `~/.cache/wow/gems/` | Downloaded `.gem` files |
| `~/.cache/wow/wowx/` | wowx ephemeral environments |
| `~/.local/share/wow/rubies/.temp/` | Stale extraction temps |

**Interface:**

```bash
wow cache clean              # Remove stale temps + orphaned downloads
wow cache clean --all        # Nuke all caches, start fresh
wow cache clean --dry-run    # Show what would be deleted
```

**Output:**

```bash
$ wow cache clean --dry-run
Would remove:
  ~/.cache/wow/gems/       (47 files, 23.4 MB)
  ~/.cache/wow/wowx/       (3 environments, 8.1 MB)
  ~/.local/share/wow/rubies/.temp-abc123/  (stale, 412 MB)
Total: 443.5 MB
```

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/signal.c` | Create | Signal handling + cleanup stack |
| `src/resource.c` | Create | Disk space checking via `statvfs(3)` |
| `src/cleanup.c` | Create | Stale temp detection + removal |
| `src/cache_cmd.c` | Create | `wow cache clean` subcommand |
| `src/download/parallel.c` | Modify | Integrate shutdown flag |
| `src/rubies/install.c` | Modify | Disk check, signal handling |
| `src/main.c` | Modify | Register signal handlers, dispatch `cache` |

---

## Verification

```bash
# 1. Signal handling
wow rubies install 4.0.1 &
PID=$!
sleep 2
kill -INT $PID
# Verify: .temp/ cleaned up

# 2. Disk space check
mkdir -p /tmp/fake-wow
mount -t tmpfs -o size=100M tmpfs /tmp/fake-wow
XDG_DATA_HOME=/tmp/fake-wow wow rubies install 4.0.1
# Verify: Clean "Insufficient disk space" error

# 3. Stale cleanup
mkdir -p ~/.local/share/wow/rubies/.temp-stale123
touch -d "2 days ago" ~/.local/share/wow/rubies/.temp-stale123
wow sync
# Verify: stale dir removed silently

# 4. Cache clean
wow cache clean --dry-run
wow cache clean --all --yes
```

---

*Last updated: 2026-02-21*
*Status: Draft*
