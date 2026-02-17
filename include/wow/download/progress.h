#ifndef WOW_PROGRESS_H
#define WOW_PROGRESS_H

#include <stddef.h>

/*
 * Progress State Module — reusable progress tracking for downloads/extractions
 *
 * Example usage:
 *   wow_progress_state_t prog;
 *   wow_progress_init(&prog, "ruby-3.3.6", 45*1024*1024, NULL);
 *   wow_http_download_to_fd(url, fd, wow_progress_http_callback, &prog);
 *   wow_progress_finish(&prog, "complete");
 */

/* Progress state for tracking long-running operations */
typedef struct {
    const char *name;       /* Operation name shown to user (e.g., "ruby-3.3.6") */
    size_t current;         /* Bytes transferred so far */
    size_t total;           /* Expected total bytes (0 if unknown) */
    void *user_ctx;         /* Optional user-provided context */
    
    /* Internal state */
    int is_tty;             /* Whether stderr is a TTY */
    int announced;          /* Whether we've shown initial message */
    double start_time;      /* For ETA calculation (optional) */
    size_t _last_reported;  /* Internal: for http callback delta tracking */
} wow_progress_state_t;

/* Initialize progress state. Call before starting operation. */
void wow_progress_init(wow_progress_state_t *state, const char *name,
                       size_t total, void *user_ctx);

/* 
 * Update progress with bytes just transferred.
 * Call periodically during operation. Returns new total.
 */
size_t wow_progress_update(wow_progress_state_t *state, size_t bytes);

/* 
 * Finish progress — clears line and prints completion.
 * status: optional status string (e.g., "complete", "extracted"), or NULL
 */
void wow_progress_finish(wow_progress_state_t *state, const char *status);

/* Cancel progress — clears line without completion message. */
void wow_progress_cancel(wow_progress_state_t *state);

/*
 * Adapter for wow_http_download_to_fd progress callback.
 * Pass this as the progress_fn with a wow_progress_state_t* as ctx.
 * 
 * Example:
 *   wow_progress_state_t prog;
 *   wow_progress_init(&prog, "ruby-3.3.6", 0, NULL);
 *   wow_http_download_to_fd(url, fd, wow_progress_http_callback, &prog);
 */
void wow_progress_http_callback(size_t received, size_t total, void *ctx);

#endif
