# Wow Code Review — Security & Quality Issues

**Date:** 2026-02-18  
**Reviewer:** Kimi  
**Scope:** Full codebase (C + bash)

---

## 🔴 CRITICAL

### 1. SQL Injection (lode/enrich.sh)

**Files:** `lode/enrich.sh`, `lode/lib/db.sh`

**Problem:** String interpolation into SQL queries without proper parameterization:

```bash
# Line 48
status=$(db_exec "SELECT fetch_status FROM gems WHERE name='$(db_quote "$name")';")

# Line 106 — $http_code from curl output
 db_exec "UPDATE gems SET ... error_msg='HTTP $http_code', ..."

# Line 130-136 — Multiple interpolated variables
db_exec "UPDATE gems SET
    downloads=$downloads,           -- from jq
    source_uri='${source_escaped}',
    homepage_uri='${homepage_escaped}',
    fetch_status='${status}',
    fetch_time=$(date +%s)
WHERE name='${escaped}';"
```

**Risk:** `db_quote()` only escapes single quotes. Malicious gem names or unexpected curl/jq output could inject SQL.

**Fix:** Use SQLite parameterized queries:
```bash
# Instead of string interpolation
sqlite3 "$DB_PATH" "UPDATE gems SET error_msg=? WHERE name=?" "HTTP $http_code" "$name"
```

**Also fix:** `db_exec()` in `lib/db.sh` should support args:
```bash
db_exec() {
    local query="$1"
    shift
    sqlite3 "$DB_PATH" "$query" "$@"
}
```

---

## 🟡 HIGH

### 2. Integer Underflow (src/gemfile/parser.y:29)

**File:** `src/gemfile/parser.y`

```c
static char *tok_strdup(struct wow_token t, int strip_left, int strip_right)
{
    int len = t.length - strip_left - strip_right;  // UNDERFLOW!
    if (len <= 0) return strdup("");
    return strndup(t.start + strip_left, (size_t)len);
}
```

**Problem:** If `strip_left + strip_right > t.length`, signed integer underflow occurs before the `len <= 0` check.

**Fix:**
```c
if ((size_t)strip_left + (size_t)strip_right > (size_t)t.length)
    return strdup("");
size_t len = (size_t)t.length - strip_left - strip_right;
```

---

### 3. TOCTOU Race Condition (src/rubies/install.c:102-123)

**File:** `src/rubies/install.c`

```c
/* Check if already installed */
if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {  // Check
    return 0;
}

/* Acquire lock */
int lockfd = wow_rubies_acquire_lock(base);  // Then lock

/* Double-check after lock */
if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {  // Double-check
```

**Problem:** Window between first `stat` and `acquire_lock` allows symlink attack.

**Fix:** Lock first, then check:
```c
int lockfd = wow_rubies_acquire_lock(base);
if (lockfd < 0) return -1;

if (stat(install_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
    wow_rubies_release_lock(lockfd);
    return 0;
}
```

---

## 🟡 MEDIUM

### 4. Buffer Growth Overflow (src/http/client.c:205-206)

**File:** `src/http/client.c`

```c
rawn += 4096;
rawn += rawn >> 1;  // 1.5x growth — could overflow size_t
```

**Problem:** When downloading massive responses, this could wrap around.

**Fix:**
```c
if (rawn > SIZE_MAX / 2) goto fail_body;  // Prevent overflow
rawn += 4096;
rawn += rawn >> 1;
```

---

### 5. Symlink Depth Logic Gap (src/tar.c:194-211)

**File:** `src/tar.c`

```c
while (*p) {
    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
        depth--;
        if (depth < 0) return 0;
        p += (p[2] == '/') ? 3 : 2;
    } else {
        const char *slash = strchr(p, '/');
        if (!slash) break;  // Stops at last component!
        depth++;            // Only increments on '/'
        p = slash + 1;
    }
}
```

**Problem:** Depth only increments when `/` is found. Path `foo/bar/..` works, but `foo/bar/baz/..` — the final component isn't counted.

**Fix:** Count components properly, including the final one.

---

### 6. Missing Error Check on ParseUrl (src/http/pool.c:446)

**File:** `src/http/pool.c`

```c
char *urlmem = ParseUrl(current_url, -1, &parsed, kUrlPlus);
// No NULL check for urlmem before use
```

---

### 7. Integer Overflow in Version Parsing (src/rubies/definitions.c:44,78)

**File:** `src/rubies/definitions.c`

```c
long val = strtol(s, &end, 10);
out[n++] = (int)val;  // Cast without bounds check
```

**Problem:** `strtol` can return values > INT_MAX, causing UB when cast.

**Fix:** Check `val <= INT_MAX` before casting.

---

## 🟢 LOW

### 8. Uninitialized Variable Risk (src/http/client.c:199-200)

```c
char *body = NULL;
size_t bodylen = 0;
```

Control flow to `goto done` may use these before assignment in some edge cases. Worth auditing all `goto` paths.

---

### 9. Missing fclose on Error Path (src/gemfile/eval.c)

If `fopen` succeeds but subsequent operations fail (OOM, file too large), the FILE* is leaked.

---

### 10. realloc Leak on Failure (src/gemfile/parser.y)

```c
a->constraints = realloc(a->constraints, ...);  // Old ptr lost on failure
```

Use temporary pointer pattern:
```c
void *tmp = realloc(a->constraints, ...);
if (!tmp) { /* handle error, original still valid */ }
a->constraints = tmp;
```

---

## ✅ Positive Security Findings

1. **Tar extraction** correctly rejects absolute paths and `..` components
2. **Symlink targets** are validated (though depth logic needs fix)
3. **HTTP client** uses `MBEDTLS_SSL_VERIFY_REQUIRED`
4. **HTTPS downgrade** explicitly blocked
5. **SHA-256 verification** on Ruby binaries
6. **File locking** during Ruby installation
7. **eval_gemfile** has recursion depth limits

---

## Priority Summary

| Priority | Issue | File |
|----------|-------|------|
| 🔴 CRITICAL | SQL injection | `lode/enrich.sh` |
| 🟡 HIGH | Integer underflow | `src/gemfile/parser.y:29` |
| 🟡 HIGH | TOCTOU race | `src/rubies/install.c:102` |
| 🟡 MEDIUM | Buffer overflow | `src/http/client.c:205` |
| 🟡 MEDIUM | Symlink logic | `src/tar.c:194` |
| 🟢 LOW | ParseUrl NULL check | `src/http/pool.c:446` |
| 🟢 LOW | strtol overflow | `src/rubies/definitions.c:44` |

---

## Fix Recommendations

1. **Immediate:** Add SQLite parameterized query support to `db_exec()`
2. **Short-term:** Fix integer underflow in `tok_strdup()`
3. **Short-term:** Reorder lock/check in install.c
4. **Medium-term:** Add bounds checks to all buffer growth
5. **Medium-term:** Audit all `strtol`/`strtoll` casts
