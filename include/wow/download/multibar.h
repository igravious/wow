#ifndef WOW_MULTIBAR_H
#define WOW_MULTIBAR_H

#include <pthread.h>
#include <stddef.h>

/*
 * Multi-bar progress display for concurrent downloads.
 *
 * Supports two modes:
 *
 * 1. Fixed mode (n_bars == n_total): one row per download.
 *    Good for small batches (2-8 files).
 *
 * 2. Worker mode (n_bars < n_total): one row per worker thread,
 *    plus a status line showing [completed/total].  Workers call
 *    wow_multibar_reset() to reuse their bar slot for the next
 *    download.  Good for large batches (100+ files).
 *
 * Thread-safe: all update/finish/fail/reset acquire the internal mutex.
 *
 * Fixed mode usage:
 *   wow_multibar_t mb;
 *   wow_multibar_init(&mb, 3, 3);
 *   wow_multibar_set_name(&mb, 0, "ruby-3.3.6");
 *   ...
 *
 * Worker mode usage:
 *   wow_multibar_t mb;
 *   wow_multibar_init(&mb, 8, 100);   // 8 workers, 100 total downloads
 *   wow_multibar_start(&mb);
 *   // worker 0 starts first download:
 *   wow_multibar_reset(&mb, 0, "sinatra-4.2.1.gem");
 *   // ... download ...
 *   wow_multibar_finish(&mb, 0);       // increments completed counter
 *   // worker 0 starts next download:
 *   wow_multibar_reset(&mb, 0, "rack-3.1.12.gem");
 */

#define WOW_MULTIBAR_MAX 64

/* Per-bar slot state */
typedef struct {
    const char *name;           /* Label shown to user */
    size_t      current;        /* Bytes received so far */
    size_t      total;          /* Expected total (0 if unknown) */
    int         finished;       /* 1 if complete */
    int         failed;         /* 1 if errored */
    size_t      _last_reported; /* Internal: for HTTP callback delta tracking */
} wow_bar_slot_t;

/* Shared multi-bar state */
typedef struct {
    wow_bar_slot_t  slots[WOW_MULTIBAR_MAX];
    int             n_bars;       /* Number of bar rows (== workers in worker mode) */
    int             n_total;      /* Total downloads in the batch */
    int             n_completed;  /* Completed downloads so far */
    int             n_failed;     /* Failed downloads so far */
    size_t          total_bytes;  /* Total bytes downloaded across all items */
    int             is_tty;       /* Whether stderr is a terminal */
    int             started;      /* Whether rows have been reserved */
    int             has_status;   /* Whether we have a status line (worker mode) */
    double          start_time;   /* For elapsed time */
    int             max_nw;       /* High-water name width (only grows, like uv) */
    pthread_mutex_t mu;           /* Serialises all rendering */
} wow_multibar_t;

/*
 * Initialise the multi-bar display.
 * n_bars:  number of bar rows (one per worker in worker mode).
 * n_total: total number of downloads in the batch.
 * If n_bars == n_total, fixed mode (no status line).
 * If n_bars < n_total, worker mode (status line shown).
 */
void wow_multibar_init(wow_multibar_t *mb, int n_bars, int n_total);

/* Set the name for bar at index i.  Call before wow_multibar_start(). */
void wow_multibar_set_name(wow_multibar_t *mb, int i, const char *name);

/* Reserve terminal rows.  Call once from the main thread before spawning. */
void wow_multibar_start(wow_multibar_t *mb);

/*
 * Reset bar i for a new download (worker mode).  Thread-safe.
 * Clears progress, sets new name, re-renders the bar.
 */
void wow_multibar_reset(wow_multibar_t *mb, int i, const char *name);

/*
 * Update bar i with a byte delta.  Thread-safe.
 * If total was unknown (0) and is now known, pass it via total.
 */
void wow_multibar_update(wow_multibar_t *mb, int i, size_t delta, size_t total);

/* Mark bar i as finished (tick mark).  Thread-safe.  Increments completed count. */
void wow_multibar_finish(wow_multibar_t *mb, int i);

/* Mark bar i as failed (cross mark).  Thread-safe.  Increments failed count. */
void wow_multibar_fail(wow_multibar_t *mb, int i);

/* Destroy mutex.  Call after all threads have joined. */
void wow_multibar_destroy(wow_multibar_t *mb);

/*
 * HTTP progress callback adapter.
 * ctx must point to a wow_multibar_ctx_t.
 */
typedef struct {
    wow_multibar_t *mb;
    int             index;
    unsigned        throttle_us;  /* If >0, sleep this many µs per chunk (rate limiting) */
} wow_multibar_ctx_t;

void wow_multibar_http_callback(size_t received, size_t total, void *ctx);

#endif
