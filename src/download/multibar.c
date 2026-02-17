/*
 * multibar.c — concurrent multi-bar progress display
 *
 * Renders N progress bars simultaneously on the terminal, one per row.
 * A pthread mutex serialises all stderr writes to prevent interleaving.
 * ANSI cursor positioning moves to the correct row for each bar.
 *
 * Two modes:
 *   Fixed mode  (n_bars == n_total): one row per download.
 *   Worker mode (n_bars < n_total):  one row per worker thread,
 *     bars are reused via wow_multibar_reset(), status line shows
 *     [completed/total] progress.
 *
 * Reuses the teal =====>------ bar style from progress.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wow/download/multibar.h"

#define WOW_ANSI_BOLD      "\033[1m"
#define WOW_ANSI_DIM       "\033[2m"
#define WOW_ANSI_CYAN      "\033[36m"
#define WOW_ANSI_GREEN     "\033[32m"
#define WOW_ANSI_RED       "\033[31m"
#define ANSI_WHITE     "\033[37m"
#define WOW_ANSI_RESET     "\033[0m"

#define BAR_WIDTH 30

/* ── Helpers ─────────────────────────────────────────────────────── */

static double mb_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void format_bytes(size_t bytes, char *buf, size_t bufsz)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, bufsz, "%.1fGiB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1fMiB",
                 (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1fKiB", (double)bytes / 1024.0);
    else
        snprintf(buf, bufsz, "%zuB", bytes);
}

static void buf_fill(char *buf, size_t bufsz, int *pos, char c, int n)
{
    for (int i = 0; i < n && (size_t)*pos < bufsz - 1; i++)
        buf[(*pos)++] = c;
}

#define BUF_APPEND(buf, bufsz, pos, ...) \
    do { \
        int _n = snprintf((buf) + *(pos), (bufsz) - *(pos), __VA_ARGS__); \
        if (_n > 0) *(pos) += _n; \
    } while (0)

/* ── Rendering (internal, called with mutex held) ────────────────── */

/*
 * Return the high-water name width (only grows, never shrinks — matching uv).
 */
static int max_name_width(const wow_multibar_t *mb)
{
    return mb->max_nw;
}

/*
 * Render a single bar into a buffer.  Does not include cursor
 * positioning — that is handled by the caller.
 *
 * Layout matches uv exactly:
 *   {name:nw.dim} {bar:30.cyan/dim} {bytes:>7}/{total:7}
 *
 * Every state (in-progress, finished, failed, waiting) is padded
 * to the same total visible width so all rows align.
 */
static int render_bar(const wow_bar_slot_t *slot, char *line, size_t linesz,
                      int nw)
{
    int pos = 0;
    /*
     * Target visible width (the in-progress line is the reference):
     *   nw + ' ' + BAR_WIDTH + ' ' + 7 + '/' + 7 = nw + 47
     */
    int target_vw = nw + 1 + BAR_WIDTH + 1 + 7 + 1 + 7;

    /* Clear line */
    BUF_APPEND(line, linesz, &pos, "\r\033[K");

    if (!slot->name) {
        /* Empty slot (worker hasn't started yet) — visible 11 chars */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "  (waiting)" WOW_ANSI_RESET);
        buf_fill(line, linesz, &pos, ' ', target_vw - 11);
        return pos;
    }

    if (slot->finished) {
        char sz[16];
        format_bytes(slot->current, sz, sizeof(sz));
        /*
         * Name at col 0, bold green ✓ in bar area,
         * (size) aligned with byte counter column at nw+32.
         *
         * name(nw) + ' ✓' = nw + 2
         * pad 30 to reach nw + 32, then '(' + sz(7) + ')' = 9
         * total = nw + 41, pad 6 to reach target_vw (nw + 47)
         */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_BOLD WOW_ANSI_GREEN "%-*.*s"
                   " \xe2\x9c\x93" WOW_ANSI_RESET,
                   nw, nw, slot->name);
        buf_fill(line, linesz, &pos, ' ', BAR_WIDTH);  /* 30 */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "(%7s)" WOW_ANSI_RESET, sz);
        buf_fill(line, linesz, &pos, ' ', target_vw - (nw + 41));
        return pos;
    }

    if (slot->failed) {
        /*
         * Same layout: name at col 0, ✗ in bar area,
         * (failed) aligned with byte counter column.
         *
         * name(nw) + ' ✗' = nw + 2
         * pad 30 to reach nw + 32, then '(failed)' = 8
         * total = nw + 40, pad 7 to reach target_vw (nw + 47)
         */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "%-*.*s" WOW_ANSI_RESET
                   " " WOW_ANSI_BOLD WOW_ANSI_RED "\xe2\x9c\x97" WOW_ANSI_RESET,
                   nw, nw, slot->name);
        buf_fill(line, linesz, &pos, ' ', BAR_WIDTH);  /* 30 */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "(failed)" WOW_ANSI_RESET);
        buf_fill(line, linesz, &pos, ' ', target_vw - (nw + 40));
        return pos;
    }

    if (slot->total > 0) {
        char cur_str[16], tot_str[16];
        format_bytes(slot->current, cur_str, sizeof(cur_str));
        format_bytes(slot->total, tot_str, sizeof(tot_str));

        int filled = (int)((slot->current * BAR_WIDTH) / slot->total);
        if (filled > BAR_WIDTH) filled = BAR_WIDTH;
        int empty = BAR_WIDTH - filled;

        /* Dimmed name, padded to nw */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "%-*.*s" WOW_ANSI_RESET " ", nw, nw, slot->name);

        /* Cyan filled portion */
        BUF_APPEND(line, linesz, &pos, WOW_ANSI_CYAN);
        buf_fill(line, linesz, &pos, '=', filled);

        /* Dim empty portion with '>' tip */
        if (empty > 0) {
            BUF_APPEND(line, linesz, &pos, WOW_ANSI_RESET WOW_ANSI_DIM ">");
            buf_fill(line, linesz, &pos, '-', empty - 1);
        }

        BUF_APPEND(line, linesz, &pos, WOW_ANSI_RESET);

        /* Bytes: right-aligned 7 / left-aligned 7 (matches uv) */
        BUF_APPEND(line, linesz, &pos, " %7s/%-7s", cur_str, tot_str);
    } else {
        /* Unknown size — name + bytes so far, padded to target */
        char cur_str[16];
        format_bytes(slot->current, cur_str, sizeof(cur_str));
        /* name(nw) + ' ' + cur(7) = nw + 8 */
        BUF_APPEND(line, linesz, &pos,
                   WOW_ANSI_DIM "%-*.*s" WOW_ANSI_RESET " %7s", nw, nw,
                   slot->name, cur_str);
        buf_fill(line, linesz, &pos, ' ', target_vw - (nw + 8));
    }

    return pos;
}

/*
 * Render the status line (worker mode only).
 * Shows: [completed/total] total_bytes downloaded
 * Written at the cursor position (below all bars).
 * Must be called with mb->mu held.
 */
/* Braille spinner frames (matching uv's tick_strings) */
static const char *spinner[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
    "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f",
};
#define N_SPINNER (sizeof(spinner) / sizeof(spinner[0]))

static void render_status_line(wow_multibar_t *mb)
{
    if (!mb->is_tty || !mb->has_status) return;

    char out[256];
    int pos = 0;
    char bytes_str[16];
    format_bytes(mb->total_bytes, bytes_str, sizeof(bytes_str));

    double elapsed = mb_now() - mb->start_time;
    int done = mb->n_completed + mb->n_failed;

    /* Braille spinner (5 fps) — stops when all done */
    int frame = (int)(elapsed * 5.0) % (int)N_SPINNER;

    BUF_APPEND(out, sizeof(out), &pos, "\r\033[K");

    if (done < mb->n_total)
        BUF_APPEND(out, sizeof(out), &pos,
                   ANSI_WHITE "%s" WOW_ANSI_RESET " ", spinner[frame]);

    BUF_APPEND(out, sizeof(out), &pos,
               WOW_ANSI_DIM "[%d/%d]" WOW_ANSI_RESET " %s downloaded",
               done, mb->n_total, bytes_str);

    if (mb->n_failed > 0)
        BUF_APPEND(out, sizeof(out), &pos,
                   " " WOW_ANSI_RED "(%d failed)" WOW_ANSI_RESET, mb->n_failed);

    if (elapsed > 0.5 && mb->total_bytes > 0) {
        char rate_str[16];
        format_bytes((size_t)((double)mb->total_bytes / elapsed),
                     rate_str, sizeof(rate_str));
        /* ⚡ = U+26A1 = \xe2\x9a\xa1 */
        BUF_APPEND(out, sizeof(out), &pos,
                   " \xe2\x9a\xa1 " WOW_ANSI_BOLD "%s/s" WOW_ANSI_RESET,
                   rate_str);
    }

    (void)write(STDERR_FILENO, out, (size_t)pos);
}

/*
 * Render bar i at its fixed terminal row.
 * Must be called with mb->mu held.  Uses save/restore cursor.
 *
 * Layout: the n_bars rows (+ optional status line) were reserved above
 * the current cursor line.  Bar 0 is the topmost, bar n_bars-1 is just
 * above the cursor (or above the status line in worker mode).
 * To reach bar i, move up (n_rows - i) lines where n_rows includes
 * the status line if present.
 */
static void render_bar_at_row(wow_multibar_t *mb, int i)
{
    if (!mb->is_tty) return;

    int nw = max_name_width(mb);
    int n_rows = mb->n_bars + (mb->has_status ? 1 : 0);

    char out[768];
    int pos = 0;

    /* Save cursor + move up to bar's row */
    int up = n_rows - i;
    BUF_APPEND(out, sizeof(out), &pos, "\033[s\033[%dA", up);

    /* Render the bar content */
    char bar_buf[512];
    int bar_len = render_bar(&mb->slots[i], bar_buf, sizeof(bar_buf), nw);
    if ((size_t)(pos + bar_len) < sizeof(out) - 8) {
        memcpy(out + pos, bar_buf, (size_t)bar_len);
        pos += bar_len;
    }

    /* Restore cursor */
    BUF_APPEND(out, sizeof(out), &pos, "\033[u");

    /* Single atomic write */
    (void)write(STDERR_FILENO, out, (size_t)pos);
}

/*
 * Sort key for a bar slot.  Higher = rendered closer to the top.
 * Finished (101%) > in-progress by % > unknown-size > waiting > failed.
 */
static int slot_progress_key(const wow_bar_slot_t *s)
{
    if (!s->name)    return -200;   /* waiting — no work assigned yet */
    if (s->failed)   return -100;   /* failed — sink to bottom */
    if (s->finished) return 10100;  /* 101% — above all in-progress */
    if (s->total > 0)
        return (int)((s->current * 10000) / s->total);  /* 0..10000 */
    return 0;  /* unknown size, just started */
}

/*
 * Re-render all bars sorted by progress (highest % at top).
 * Batches all bar writes into a single write() to avoid flicker.
 * Must be called with mb->mu held.
 */
static void render_all_sorted(wow_multibar_t *mb)
{
    if (!mb->is_tty) return;

    int nw = max_name_width(mb);

    /* Build sort order: highest progress at row 0 (top) */
    int order[WOW_MULTIBAR_MAX];
    for (int i = 0; i < mb->n_bars; i++)
        order[i] = i;

    /* Insertion sort — n_bars is small (typically <= 16) */
    for (int i = 1; i < mb->n_bars; i++) {
        int tmp = order[i];
        int tmp_key = slot_progress_key(&mb->slots[tmp]);
        int j = i - 1;
        while (j >= 0 && slot_progress_key(&mb->slots[order[j]]) < tmp_key) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = tmp;
    }

    /* Render all bars into one buffer for a single atomic write */
    int n_rows = mb->n_bars + (mb->has_status ? 1 : 0);
    char out[8192];
    int pos = 0;

    for (int row = 0; row < mb->n_bars; row++) {
        int slot = order[row];
        int up = n_rows - row;
        BUF_APPEND(out, sizeof(out), &pos, "\033[s\033[%dA", up);

        char bar_buf[512];
        int bar_len = render_bar(&mb->slots[slot], bar_buf, sizeof(bar_buf),
                                 nw);
        if ((size_t)(pos + bar_len) < sizeof(out) - 16) {
            memcpy(out + pos, bar_buf, (size_t)bar_len);
            pos += bar_len;
        }

        BUF_APPEND(out, sizeof(out), &pos, "\033[u");
    }

    (void)write(STDERR_FILENO, out, (size_t)pos);
}

/* ── Public API ──────────────────────────────────────────────────── */

void wow_multibar_init(wow_multibar_t *mb, int n_bars, int n_total)
{
    memset(mb, 0, sizeof(*mb));
    if (n_bars > WOW_MULTIBAR_MAX) n_bars = WOW_MULTIBAR_MAX;
    mb->n_bars = n_bars;
    mb->n_total = n_total;
    mb->has_status = (n_bars < n_total);
    mb->is_tty = isatty(STDERR_FILENO);
    mb->max_nw = 20;  /* Minimum name column width (matches uv) */
    mb->start_time = mb_now();
    pthread_mutex_init(&mb->mu, NULL);
}

static void update_max_nw(wow_multibar_t *mb, const char *name)
{
    if (name) {
        int len = (int)strlen(name);
        if (len > mb->max_nw)
            mb->max_nw = len;
    }
}

void wow_multibar_set_name(wow_multibar_t *mb, int i, const char *name)
{
    if (i >= 0 && i < mb->n_bars) {
        mb->slots[i].name = name;
        update_max_nw(mb, name);
    }
}

void wow_multibar_start(wow_multibar_t *mb)
{
    if (mb->started) return;
    mb->started = 1;

    if (!mb->is_tty) return;

    /*
     * Reserve n_bars + (has_status ? 1 : 0) lines by printing newlines.
     * The cursor stays at the bottom of the reserved area.
     * render_bar_at_row() moves UP from here into the reserved rows;
     * render_status_line() writes at the cursor position (bottom row).
     */
    int n_rows = mb->n_bars + (mb->has_status ? 1 : 0);

    char reserve[256];
    int pos = 0;
    for (int i = 0; i < n_rows && pos < (int)sizeof(reserve) - 2; i++)
        reserve[pos++] = '\n';

    (void)write(STDERR_FILENO, reserve, (size_t)pos);

    /* Render initial state for each bar + status line */
    pthread_mutex_lock(&mb->mu);
    for (int i = 0; i < mb->n_bars; i++)
        render_bar_at_row(mb, i);
    render_status_line(mb);
    pthread_mutex_unlock(&mb->mu);
}

void wow_multibar_reset(wow_multibar_t *mb, int i, const char *name)
{
    if (i < 0 || i >= mb->n_bars) return;

    pthread_mutex_lock(&mb->mu);

    wow_bar_slot_t *s = &mb->slots[i];
    s->name = name;
    s->current = 0;
    s->total = 0;
    s->finished = 0;
    s->failed = 0;
    s->_last_reported = 0;
    update_max_nw(mb, name);

    if (mb->is_tty)
        render_all_sorted(mb);

    pthread_mutex_unlock(&mb->mu);
}

void wow_multibar_update(wow_multibar_t *mb, int i, size_t delta, size_t total)
{
    if (i < 0 || i >= mb->n_bars) return;

    pthread_mutex_lock(&mb->mu);

    wow_bar_slot_t *s = &mb->slots[i];
    s->current += delta;
    if (s->total == 0 && total > 0)
        s->total = total;

    if (mb->is_tty) {
        render_all_sorted(mb);
        render_status_line(mb);
    } else if (!s->finished && !s->failed && s->total > 0 &&
               s->current == delta) {
        /* Non-TTY: announce on first update when we know the size */
        char tot_str[16];
        format_bytes(s->total, tot_str, sizeof(tot_str));
        fprintf(stderr, "Downloading %s (%s)...\n",
                s->name ? s->name : "?", tot_str);
    }

    pthread_mutex_unlock(&mb->mu);
}

void wow_multibar_finish(wow_multibar_t *mb, int i)
{
    if (i < 0 || i >= mb->n_bars) return;

    pthread_mutex_lock(&mb->mu);

    wow_bar_slot_t *s = &mb->slots[i];
    s->finished = 1;
    mb->n_completed++;
    mb->total_bytes += s->current;

    if (mb->is_tty) {
        render_all_sorted(mb);
        render_status_line(mb);
    } else {
        char tot_str[16];
        format_bytes(s->current, tot_str, sizeof(tot_str));
        fprintf(stderr, "[%d/%d] Downloaded %s (%s)\n",
                mb->n_completed + mb->n_failed, mb->n_total,
                s->name ? s->name : "?", tot_str);
    }

    pthread_mutex_unlock(&mb->mu);
}

void wow_multibar_fail(wow_multibar_t *mb, int i)
{
    if (i < 0 || i >= mb->n_bars) return;

    pthread_mutex_lock(&mb->mu);

    mb->slots[i].failed = 1;
    mb->n_failed++;

    if (mb->is_tty) {
        render_all_sorted(mb);
        render_status_line(mb);
    } else {
        fprintf(stderr, "[%d/%d] Failed: %s\n",
                mb->n_completed + mb->n_failed, mb->n_total,
                mb->slots[i].name ? mb->slots[i].name : "?");
    }

    pthread_mutex_unlock(&mb->mu);
}

void wow_multibar_destroy(wow_multibar_t *mb)
{
    if (mb->is_tty && mb->started) {
        /*
         * Move cursor below all bars + status line so subsequent
         * output doesn't overwrite the final state.
         */
        (void)write(STDERR_FILENO, "\n", 1);
    }
    pthread_mutex_destroy(&mb->mu);
}

/* ── HTTP callback adapter ───────────────────────────────────────── */

void wow_multibar_http_callback(size_t received, size_t total, void *ctx)
{
    wow_multibar_ctx_t *mc = (wow_multibar_ctx_t *)ctx;

    /* Calculate delta from previous call */
    size_t prev = mc->mb->slots[mc->index]._last_reported;
    size_t delta = received - prev;
    mc->mb->slots[mc->index]._last_reported = received;

    wow_multibar_update(mc->mb, mc->index, delta, total);

    /* Rate limiting: sleep per chunk to back-pressure TCP recv */
    if (mc->throttle_us > 0)
        usleep(mc->throttle_us);
}
