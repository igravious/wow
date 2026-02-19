#ifndef WOW_RESOLVER_ARENA_H
#define WOW_RESOLVER_ARENA_H

/*
 * arena.h -- Simple bump allocator for PubGrub data
 *
 * All PubGrub data has the same lifetime (one resolution run),
 * so a bump allocator avoids per-object free() and leak worries
 * during backtracking.
 *
 * IMPORTANT: The arena uses realloc() to grow, which may move the
 * buffer.  Never store raw pointers into the arena — use wow_aoff
 * offsets instead, and dereference via the WOW_ARENA_* macros.
 */

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Arena offset type                                                   */
/* ------------------------------------------------------------------ */

/*
 * wow_aoff — byte offset into an arena buffer.
 *
 * All persistent references to arena data must be stored as wow_aoff,
 * never as raw pointers.  Dereference with the macros below.
 */
typedef size_t wow_aoff;

/* Sentinel: "no object" / null offset.  SIZE_MAX can never be a valid
 * arena offset (the allocation would have to span the entire address
 * space).  Use this wherever a pointer field would have been NULL. */
#define WOW_AOFF_NULL  ((wow_aoff)-1)

/* ------------------------------------------------------------------ */
/* Dereference macros                                                  */
/* ------------------------------------------------------------------ */

/* Cast arena offset to a typed pointer.  Use for structs / arrays. */
#define WOW_ARENA_PTR(a, off, T)  ((T *)((a)->buf + (off)))

/* Cast arena offset to const char *.  Use for strings. */
#define WOW_ARENA_STR(a, off)     ((const char *)((a)->buf + (off)))

/* ------------------------------------------------------------------ */
/* Arena struct                                                        */
/* ------------------------------------------------------------------ */

typedef struct wow_arena {
    char  *buf;
    size_t used;
    size_t cap;
} wow_arena;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Initialise an arena. Does not allocate — first alloc triggers growth. */
void wow_arena_init(wow_arena *a);

/* Allocate n bytes (8-byte aligned). Returns NULL on OOM. */
void *wow_arena_alloc(wow_arena *a, size_t n);

/* Allocate n bytes and return the offset (not a pointer). */
wow_aoff wow_arena_alloc_off(wow_arena *a, size_t n);

/* Duplicate a string into the arena. Returns raw pointer (use only
 * when the result is consumed immediately, NOT stored persistently). */
char *wow_arena_strdup(wow_arena *a, const char *s);

/* Duplicate a string into the arena. Returns the offset — safe to
 * store in structs that outlive subsequent arena allocations. */
wow_aoff wow_arena_strdup_off(wow_arena *a, const char *s);

/* Reset the arena (reuse buffer, used → 0). */
void wow_arena_reset(wow_arena *a);

/* Free all arena memory. */
void wow_arena_destroy(wow_arena *a);

#endif
