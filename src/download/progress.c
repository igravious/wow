/*
 * progress.c — reusable progress tracking for downloads and extractions
 *
 * Provides uv-style output: name + teal bar + bytes/total.
 * All TTY rendering is buffered (single write per frame) to avoid flicker.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wow/download/progress.h"
#include "wow/util/fmt.h"
#include "wow/util/buf.h"

#define WOW_ANSI_BOLD      "\033[1m"
#define WOW_ANSI_DIM       "\033[2m"
#define WOW_ANSI_CYAN      "\033[36m"
#define WOW_ANSI_RESET     "\033[0m"

#define BAR_WIDTH 30

/* Current time in seconds (monotonic) */
static double progress_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void wow_progress_init(wow_progress_state_t *state, const char *name,
                       size_t total, void *user_ctx)
{
    memset(state, 0, sizeof(*state));
    state->name = name;
    state->total = total;
    state->user_ctx = user_ctx;
    state->is_tty = isatty(STDERR_FILENO);
    state->announced = 0;
    state->start_time = progress_now();
    state->_last_reported = 0;
}

size_t wow_progress_update(wow_progress_state_t *state, size_t bytes)
{
    state->current += bytes;

    /* Non-TTY: print start message on first call if >1MiB */
    if (!state->is_tty) {
        if (!state->announced && state->total > 1024 * 1024) {
            char total_str[16];
            wow_fmt_bytes(state->total, total_str, sizeof(total_str));
            fprintf(stderr, "Downloading %s (%s)...\n",
                    state->name, total_str);
            state->announced = 1;
        }
        return state->current;
    }

    /*
     * TTY: compose the entire progress line in a buffer, then write
     * it in a single syscall to avoid flicker.
     *
     * Format:  \r\033[K  name  =====>---------  XX.XMiB/YY.YMiB
     *                    dim   teal  dim         plain
     */
    char line[512];
    int pos = 0;

    /* Clear line */
    WOW_BUF_APPEND(line, sizeof(line), pos, "\r\033[K");

    if (state->total > 0) {
        char cur_str[16], tot_str[16];
        wow_fmt_bytes(state->current, cur_str, sizeof(cur_str));
        wow_fmt_bytes(state->total, tot_str, sizeof(tot_str));

        int filled = (int)((state->current * BAR_WIDTH) / state->total);
        if (filled > BAR_WIDTH) filled = BAR_WIDTH;
        int empty = BAR_WIDTH - filled;

        /* Dimmed name */
        WOW_BUF_APPEND(line, sizeof(line), pos,
                   WOW_ANSI_DIM "%s" WOW_ANSI_RESET " ", state->name);

        /* Teal (cyan) filled portion */
        WOW_BUF_APPEND(line, sizeof(line), pos, WOW_ANSI_CYAN);
        wow_buf_fill(line, sizeof(line), &pos, '=', filled);

        /* Dim empty portion */
        if (empty > 0) {
            WOW_BUF_APPEND(line, sizeof(line), pos, WOW_ANSI_RESET WOW_ANSI_DIM ">");
            wow_buf_fill(line, sizeof(line), &pos, '-', empty - 1);
        }

        WOW_BUF_APPEND(line, sizeof(line), pos, WOW_ANSI_RESET);

        /* Bytes counter */
        WOW_BUF_APPEND(line, sizeof(line), pos, " %7s/%s", cur_str, tot_str);
    } else {
        /* Unknown size */
        char cur_str[16];
        wow_fmt_bytes(state->current, cur_str, sizeof(cur_str));
        WOW_BUF_APPEND(line, sizeof(line), pos,
                   WOW_ANSI_DIM "%s" WOW_ANSI_RESET " %s", state->name, cur_str);
    }

    /* Single atomic write — no flicker */
    (void)write(STDERR_FILENO, line, (size_t)pos);

    state->announced = 1;
    return state->current;
}

void wow_progress_finish(wow_progress_state_t *state, const char *status)
{
    if (!state->announced) return;

    if (state->is_tty) {
        char line[512];
        int pos = 0;

        WOW_BUF_APPEND(line, sizeof(line), pos, "\r\033[K");

        if (status) {
            WOW_BUF_APPEND(line, sizeof(line), pos,
                       " " WOW_ANSI_BOLD WOW_ANSI_CYAN "%s" WOW_ANSI_RESET " %s\n",
                       status, state->name);
        } else {
            char tot_str[16];
            wow_fmt_bytes(state->current, tot_str, sizeof(tot_str));
            WOW_BUF_APPEND(line, sizeof(line), pos,
                       " " WOW_ANSI_CYAN "\u2713" WOW_ANSI_RESET " %s (%s)\n",
                       state->name, tot_str);
        }

        (void)write(STDERR_FILENO, line, (size_t)pos);
    } else {
        if (status)
            fprintf(stderr, "%s %s\n", status, state->name);
        else
            fprintf(stderr, "Completed %s\n", state->name);
    }
}

void wow_progress_cancel(wow_progress_state_t *state)
{
    if (state->is_tty && state->announced)
        (void)write(STDERR_FILENO, "\r\033[K", 4);
}

/*
 * Adapter for wow_http_download_to_fd.
 * ctx must be a wow_progress_state_t* initialised with wow_progress_init().
 */
void wow_progress_http_callback(size_t received, size_t total, void *ctx)
{
    wow_progress_state_t *state = (wow_progress_state_t *)ctx;

    /* On first call, update total if we now know it */
    if (state->total == 0 && total > 0)
        state->total = total;

    /* Calculate delta from previous call */
    size_t delta = received - state->_last_reported;
    state->_last_reported = received;

    wow_progress_update(state, delta);
}
