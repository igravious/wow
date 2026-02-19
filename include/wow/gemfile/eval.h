#ifndef WOW_GEMFILE_EVAL_H
#define WOW_GEMFILE_EVAL_H

/*
 * eval.h -- Gemfile evaluator for simple Ruby conditionals
 *
 * The evaluator wraps the re2c lexer and filters its token stream,
 * handling if/unless/elsif/else/end blocks, trailing conditionals,
 * variable assignments, ENV[] lookups, and eval_gemfile includes.
 * The parser only ever sees clean static Gemfile tokens.
 *
 * Supported Ruby subset:
 *   - if/unless/elsif/else/end blocks
 *   - trailing if/unless on gem/source/etc. lines
 *   - ENV["KEY"], ENV.fetch("KEY", "default"), ENV.key?("KEY")
 *   - RUBY_VERSION, RUBY_ENGINE, RUBY_PLATFORM constants
 *   - Gem::Version.new(str) comparison
 *   - Variable assignment: var = expr
 *   - Boolean: &&, ||, !
 *   - Comparison: ==, !=, >=, >, <=, <
 *   - Method calls: .to_f, .to_i, .to_s, .empty?, .include?(str)
 *   - eval_gemfile "path" (static string paths)
 *   - String interpolation: "#{var}" (simple variable only)
 *
 * Unsupported constructs cause immediate abort with a diagnostic.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wow/gemfile/lexer.h"

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

#define WOW_EVAL_MAX_DEPTH   16   /* max nesting of if/do blocks      */
#define WOW_EVAL_MAX_VARS    64   /* max variable store entries        */
#define WOW_EVAL_MAX_LINE   128   /* max tokens per logical line       */
#define WOW_EVAL_MAX_ALLOCS 256   /* max interpolated string allocs    */
#define WOW_EVAL_MAX_RECURSE  8   /* max eval_gemfile recursion depth  */

/* ------------------------------------------------------------------ */
/* Expression value type                                               */
/* ------------------------------------------------------------------ */

enum wow_eval_type {
    VAL_NIL,
    VAL_BOOL,
    VAL_STRING,
    VAL_INT,
    VAL_FLOAT,
    VAL_UNEVALUATABLE  /* expression we cannot evaluate — must bail */
};

struct wow_eval_val {
    enum wow_eval_type type;
    bool is_version;    /* true if from RUBY_VERSION or Gem::Version.new() */
    union {
        bool    b;
        char   *s;      /* heap-allocated (owned by evaluator alloc pool) */
        int64_t i;
        double  f;
    };
};

/* ------------------------------------------------------------------ */
/* Block stack                                                         */
/* ------------------------------------------------------------------ */

enum wow_block_type { BLOCK_IF, BLOCK_DO };

struct wow_eval_block {
    enum wow_block_type type;
    bool active;        /* is this specific branch executing?           */
    bool any_taken;     /* has any branch in this if/elsif chain taken? */
};

/* ------------------------------------------------------------------ */
/* Variable store                                                      */
/* ------------------------------------------------------------------ */

struct wow_eval_var {
    char *name;         /* heap-allocated variable name                 */
    char *value;        /* heap-allocated string value (NULL = nil)     */
    bool  is_version;   /* true if value originates from RUBY_VERSION   */
};

/* ------------------------------------------------------------------ */
/* Token + ID pair (for line buffer and output queue)                   */
/* ------------------------------------------------------------------ */

struct wow_eval_tok {
    int              id;
    struct wow_token tok;
};

/* ------------------------------------------------------------------ */
/* Evaluator context                                                   */
/* ------------------------------------------------------------------ */

struct wow_eval_ctx {
    /* Lexer we pull tokens from */
    struct wow_lexer *lex;

    /* Block stack */
    struct wow_eval_block stack[WOW_EVAL_MAX_DEPTH];
    int depth;

    /* Variable store */
    struct wow_eval_var vars[WOW_EVAL_MAX_VARS];
    int n_vars;

    /* Environment constants */
    const char *ruby_version;    /* e.g. "3.3.0", from .ruby-version  */
    const char *ruby_engine;     /* "ruby" (default)                   */
    const char *ruby_platform;   /* "x86_64-linux" (default)           */

    /* Line buffer (accumulates tokens until NEWLINE) */
    struct wow_eval_tok line[WOW_EVAL_MAX_LINE];
    int line_len;

    /* Output queue (tokens emitted to parser) */
    struct wow_eval_tok out[WOW_EVAL_MAX_LINE];
    int out_len;
    int out_pos;

    /* Heap-allocated strings (interpolated strings, expression results) */
    char **allocs;
    int    n_allocs;
    int    allocs_cap;

    /* File context (for eval_gemfile path resolution) */
    const char *file_path;

    /* Source buffer (for error messages) */
    const char *buf;
    int         buf_len;

    /* Recursion depth (for eval_gemfile) */
    int recurse_depth;

    /* Error state: non-zero means evaluation failed */
    int error;
    int error_line;
    char error_msg[256];
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Initialise evaluator context.
 *
 * ruby_version: target Ruby version string, or NULL to auto-detect
 *               (reads RUBY_VERSION env var, then falls back to "3.3.0")
 * file_path:    path of the Gemfile being parsed (for eval_gemfile and
 *               __FILE__), or NULL
 * buf/buf_len:  source buffer (for error message context lines)
 */
void wow_eval_init(struct wow_eval_ctx *ctx, struct wow_lexer *lex,
                   const char *ruby_version, const char *file_path,
                   const char *buf, int buf_len);

/* Free all resources owned by the evaluator context. */
void wow_eval_free(struct wow_eval_ctx *ctx);

/*
 * Get the next token for the parser.
 *
 * This is the main entry point — replaces wow_lexer_scan() in the
 * glue loop. Returns:
 *   > 0 : valid token ID (from parser.h)
 *     0 : end of input
 *    -1 : error (check ctx->error_msg for diagnostic)
 */
int wow_eval_next(struct wow_eval_ctx *ctx, struct wow_token *out_tok);

/*
 * Compare two dot-separated version strings numerically.
 * Returns <0, 0, or >0 (like strcmp but with numeric segment ordering).
 * E.g. "3.10.0" > "3.9.1", "2.0" < "10.0".
 */
int wow_version_compare(const char *a, const char *b);

#endif
