#ifndef WOW_PARALLEL_H
#define WOW_PARALLEL_H

#include <stddef.h>

/*
 * Parallel download API — bounded-concurrency worker pool.
 *
 * Spawns min(n, max_concurrent) worker threads that pull download
 * specs from a shared queue.  Matches uv's semaphore-gated pattern
 * (default 50 concurrent downloads).
 *
 * Usage:
 *   wow_download_spec_t specs[] = {
 *       { "https://...", "/tmp/a.tar.gz", "ruby-3.3.6" },
 *       { "https://...", "/tmp/b.tar.gz", "ruby-3.4.2" },
 *   };
 *   wow_download_result_t results[2];
 *   int ok = wow_parallel_download(specs, results, 2, 0);
 *   // ok == number of successful downloads
 */

/* Default max concurrent downloads (matches uv's default of 50) */
#define WOW_DOWNLOAD_CONCURRENCY 50

/* One download specification */
typedef struct {
    const char *url;        /* Source URL (HTTPS) */
    const char *dest_path;  /* Destination file path (created/truncated) */
    const char *label;      /* Human-readable label for progress bar */
} wow_download_spec_t;

/* Result for one download */
typedef struct {
    int    ok;              /* 1 on success, 0 on failure */
    size_t bytes;           /* Bytes downloaded */
} wow_download_result_t;

/*
 * Download n files in parallel using a bounded worker pool.
 *
 * specs:          array of n download specifications
 * results:        array of n result slots (caller-allocated)
 * n:              number of downloads
 * max_concurrent: max worker threads (0 = WOW_DOWNLOAD_CONCURRENCY)
 * throttle_us:    if >0, sleep this many µs per chunk (rate limiting)
 *
 * Returns the number of successful downloads (0..n).
 * All downloads are attempted even if some fail.
 */
int wow_parallel_download(const wow_download_spec_t *specs,
                          wow_download_result_t *results,
                          int n, int max_concurrent,
                          unsigned throttle_us);

#endif
