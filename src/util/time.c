/*
 * util/time.c — Monotonic time utilities
 */

#include <time.h>

#include "wow/util/time.h"

double wow_now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
