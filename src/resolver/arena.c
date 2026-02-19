/*
 * arena.c -- Simple bump allocator for PubGrub data
 *
 * Starts at 64 KiB, doubles on growth. All allocations are 8-byte aligned.
 */

#include "wow/resolver/arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_INITIAL_CAP (64 * 1024)
#define ARENA_ALIGN       8

void wow_arena_init(wow_arena *a)
{
    a->buf  = NULL;
    a->used = 0;
    a->cap  = 0;
}

static int arena_grow(wow_arena *a, size_t needed)
{
    size_t new_cap = a->cap ? a->cap : ARENA_INITIAL_CAP;
    while (new_cap < a->used + needed)
        new_cap *= 2;

    char *nb = realloc(a->buf, new_cap);
    if (!nb) return -1;

    a->buf = nb;
    a->cap = new_cap;
    return 0;
}

void *wow_arena_alloc(wow_arena *a, size_t n)
{
    /* Align up */
    size_t aligned = (n + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);

    if (a->used + aligned > a->cap) {
        if (arena_grow(a, aligned) != 0)
            return NULL;
    }

    void *ptr = a->buf + a->used;
    a->used += aligned;
    return ptr;
}

wow_aoff wow_arena_alloc_off(wow_arena *a, size_t n)
{
    size_t off = a->used;
    if (!wow_arena_alloc(a, n))
        return WOW_AOFF_NULL;
    return off;
}

char *wow_arena_strdup(wow_arena *a, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = wow_arena_alloc(a, len);
    if (p) memcpy(p, s, len);
    return p;
}

wow_aoff wow_arena_strdup_off(wow_arena *a, const char *s)
{
    if (!s) return WOW_AOFF_NULL;
    size_t len = strlen(s) + 1;
    size_t off = a->used;
    char *p = wow_arena_alloc(a, len);
    if (!p) return WOW_AOFF_NULL;
    memcpy(p, s, len);
    return off;
}

void wow_arena_reset(wow_arena *a)
{
    a->used = 0;
}

void wow_arena_destroy(wow_arena *a)
{
    free(a->buf);
    a->buf  = NULL;
    a->used = 0;
    a->cap  = 0;
}
