/*
 * parallel.c — bounded-concurrency parallel download worker pool
 *
 * Spawns W = min(n, max_concurrent) worker threads.  Each worker
 * owns a bar slot (indexed by worker_id) and loops: dequeue a
 * download spec, reset the bar, download, mark finish, repeat.
 *
 * In worker mode (n > W), the multibar shows W rows + a status
 * line with [completed/total].  In fixed mode (n <= W), each
 * download gets its own bar row.
 *
 * Thread safety: wow_http_download_to_fd() is safe for concurrent
 * use — all TLS state is stack-local, GetSslRoots() uses cosmo_once,
 * GetEntropy() wraps getentropy(2).  The only shared mutable state
 * is the work queue (mutex-protected) and the multibar (has its own
 * internal mutex).
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wow/http.h"
#include "wow/download/multibar.h"
#include "wow/download/parallel.h"

/* ── Work queue ──────────────────────────────────────────────────── */

typedef struct {
    const wow_download_spec_t *specs;
    wow_download_result_t     *results;
    int                        n;           /* Total specs */
    int                        next;        /* Next spec to dequeue */
    pthread_mutex_t            mu;
    wow_multibar_t            *mb;
    int                        n_workers;   /* For fixed vs worker mode */
    unsigned                   throttle_us; /* Per-chunk sleep for rate limiting */
} download_queue_t;

/* Dequeue the next work item.  Returns the spec index, or -1 if done. */
static int queue_next(download_queue_t *q)
{
    pthread_mutex_lock(&q->mu);
    int idx = -1;
    if (q->next < q->n)
        idx = q->next++;
    pthread_mutex_unlock(&q->mu);
    return idx;
}

/* ── Worker thread ───────────────────────────────────────────────── */

typedef struct {
    download_queue_t *queue;
    int               worker_id;   /* This worker's bar slot index */
} worker_arg_t;

static void *download_worker(void *arg)
{
    worker_arg_t *wa = (worker_arg_t *)arg;
    download_queue_t *q = wa->queue;
    int bar = wa->worker_id;

    for (;;) {
        int idx = queue_next(q);
        if (idx < 0)
            break;  /* No more work */

        const wow_download_spec_t *spec = &q->specs[idx];
        wow_download_result_t *res = &q->results[idx];

        /*
         * In fixed mode (n_workers == n), bar index == spec index.
         * In worker mode, reset our bar slot for the new download.
         */
        if (q->n_workers < q->n)
            wow_multibar_reset(q->mb, bar, spec->label);

        /* Open destination file */
        int fd = open(spec->dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fprintf(stderr, "wow: cannot create %s: %s\n",
                    spec->dest_path, strerror(errno));
            res->ok = 0;
            wow_multibar_fail(q->mb, bar);
            continue;
        }

        /* Download with multibar progress */
        wow_multibar_ctx_t ctx = { .mb = q->mb, .index = bar,
                                   .throttle_us = q->throttle_us };
        int rc = wow_http_download_to_fd(spec->url, fd,
                                          wow_multibar_http_callback, &ctx);
        close(fd);

        if (rc == 0) {
            res->ok = 1;
            res->bytes = q->mb->slots[bar].current;
            wow_multibar_finish(q->mb, bar);
        } else {
            res->ok = 0;
            wow_multibar_fail(q->mb, bar);
            unlink(spec->dest_path);  /* Clean up partial download */
        }
    }

    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────── */

int wow_parallel_download(const wow_download_spec_t *specs,
                          wow_download_result_t *results,
                          int n, int max_concurrent,
                          unsigned throttle_us)
{
    if (n <= 0) return 0;

    if (max_concurrent <= 0)
        max_concurrent = WOW_DOWNLOAD_CONCURRENCY;

    int n_workers = n < max_concurrent ? n : max_concurrent;

    /* Initialise multi-bar display */
    wow_multibar_t mb;
    wow_multibar_init(&mb, n_workers, n);

    /*
     * In fixed mode, pre-set names.  In worker mode, names are
     * set dynamically via wow_multibar_reset() as workers pick up jobs.
     */
    if (n_workers == n) {
        for (int i = 0; i < n; i++)
            wow_multibar_set_name(&mb, i, specs[i].label);
    }
    wow_multibar_start(&mb);

    /* Initialise work queue */
    download_queue_t queue = {
        .specs       = specs,
        .results     = results,
        .n           = n,
        .next        = 0,
        .mb          = &mb,
        .n_workers   = n_workers,
        .throttle_us = throttle_us,
    };
    pthread_mutex_init(&queue.mu, NULL);

    /* Clear results */
    memset(results, 0, (size_t)n * sizeof(results[0]));

    /* Spawn worker threads */
    pthread_t *threads = calloc((size_t)n_workers, sizeof(pthread_t));
    worker_arg_t *args = calloc((size_t)n_workers, sizeof(worker_arg_t));
    if (!threads || !args) {
        fprintf(stderr, "wow: out of memory for %d threads\n", n_workers);
        free(threads);
        free(args);
        wow_multibar_destroy(&mb);
        pthread_mutex_destroy(&queue.mu);
        return 0;
    }

    for (int i = 0; i < n_workers; i++) {
        args[i].queue     = &queue;
        args[i].worker_id = i;
        pthread_create(&threads[i], NULL, download_worker, &args[i]);
    }

    /* Wait for all workers to finish */
    for (int i = 0; i < n_workers; i++)
        pthread_join(threads[i], NULL);

    free(threads);
    free(args);
    pthread_mutex_destroy(&queue.mu);
    wow_multibar_destroy(&mb);

    /* Count successes */
    int ok = 0;
    for (int i = 0; i < n; i++)
        if (results[i].ok) ok++;

    return ok;
}
