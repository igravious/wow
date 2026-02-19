/*
 * eval.c -- Gemfile evaluator for simple Ruby conditionals
 *
 * Wraps the re2c lexer and filters its token stream, handling
 * if/unless/elsif/else/end blocks, trailing conditionals, variable
 * assignments, ENV[] lookups, and eval_gemfile includes.
 *
 * The parser only ever sees clean static Gemfile tokens.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile/eval.h"
#include "wow/gemfile/lexer.h"
#include "parser.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void process_line(struct wow_eval_ctx *ctx);
static struct wow_eval_val eval_expr(struct wow_eval_ctx *ctx,
                                     int *pos, int end);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Extract null-terminated text from a token (caller frees) */
static char *tok_text(struct wow_token t)
{
    return strndup(t.start, (size_t)t.length);
}

/* Extract text, stripping strip_l from left and strip_r from right */
static char *tok_strip(struct wow_token t, int strip_l, int strip_r)
{
    int len = t.length - strip_l - strip_r;
    if (len <= 0) return strdup("");
    return strndup(t.start + strip_l, (size_t)len);
}

/* Track a heap allocation for later cleanup */
static char *eval_alloc(struct wow_eval_ctx *ctx, char *s)
{
    if (!s) return NULL;
    if (ctx->n_allocs >= ctx->allocs_cap) {
        ctx->allocs_cap = ctx->allocs_cap ? ctx->allocs_cap * 2 : 32;
        ctx->allocs = realloc(ctx->allocs,
                              sizeof(char *) * (size_t)ctx->allocs_cap);
    }
    ctx->allocs[ctx->n_allocs++] = s;
    return s;
}

/* Set error state with a formatted message */
static void eval_error(struct wow_eval_ctx *ctx, int line,
                       const char *fmt, ...)
{
    if (ctx->error) return;  /* keep first error */
    ctx->error = -1;
    ctx->error_line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error_msg, sizeof(ctx->error_msg), fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* Block stack                                                         */
/* ------------------------------------------------------------------ */

static bool is_suppressed(struct wow_eval_ctx *ctx)
{
    for (int i = 0; i < ctx->depth; i++) {
        if (!ctx->stack[i].active) return true;
    }
    return false;
}

static bool enclosing_active(struct wow_eval_ctx *ctx)
{
    for (int i = 0; i < ctx->depth - 1; i++) {
        if (!ctx->stack[i].active) return false;
    }
    return true;
}

static void push_block(struct wow_eval_ctx *ctx, enum wow_block_type type,
                        bool active, bool any_taken)
{
    if (ctx->depth >= WOW_EVAL_MAX_DEPTH) {
        eval_error(ctx, ctx->line_len > 0 ? ctx->line[0].tok.line : 0,
                   "too many nested blocks (max %d)", WOW_EVAL_MAX_DEPTH);
        return;
    }
    ctx->stack[ctx->depth].type = type;
    ctx->stack[ctx->depth].active = active;
    ctx->stack[ctx->depth].any_taken = any_taken;
    ctx->depth++;
}

/* ------------------------------------------------------------------ */
/* Variable store                                                      */
/* ------------------------------------------------------------------ */

static const char *lookup_var(struct wow_eval_ctx *ctx, const char *name,
                              bool *out_is_version)
{
    for (int i = ctx->n_vars - 1; i >= 0; i--) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            if (out_is_version) *out_is_version = ctx->vars[i].is_version;
            return ctx->vars[i].value;
        }
    }
    if (out_is_version) *out_is_version = false;
    return NULL;
}

static void store_var(struct wow_eval_ctx *ctx, const char *name,
                      const char *value, bool is_version)
{
    /* Update existing */
    for (int i = 0; i < ctx->n_vars; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            free(ctx->vars[i].value);
            ctx->vars[i].value = value ? strdup(value) : NULL;
            ctx->vars[i].is_version = is_version;
            return;
        }
    }
    /* Add new */
    if (ctx->n_vars >= WOW_EVAL_MAX_VARS) {
        eval_error(ctx, 0, "too many variables (max %d)", WOW_EVAL_MAX_VARS);
        return;
    }
    ctx->vars[ctx->n_vars].name = strdup(name);
    ctx->vars[ctx->n_vars].value = value ? strdup(value) : NULL;
    ctx->vars[ctx->n_vars].is_version = is_version;
    ctx->n_vars++;
}

/* ------------------------------------------------------------------ */
/* Output queue                                                        */
/* ------------------------------------------------------------------ */

static void emit(struct wow_eval_ctx *ctx, int id, struct wow_token tok)
{
    if (ctx->out_len >= WOW_EVAL_MAX_LINE) return;
    ctx->out[ctx->out_len].id = id;
    ctx->out[ctx->out_len].tok = tok;
    ctx->out_len++;
}

static void emit_newline(struct wow_eval_ctx *ctx, int line)
{
    struct wow_token nl = { .start = "\n", .length = 1, .line = line };
    emit(ctx, NEWLINE, nl);
}

/* ------------------------------------------------------------------ */
/* Version comparison                                                  */
/* ------------------------------------------------------------------ */

int wow_version_compare(const char *a, const char *b)
{
    while (*a || *b) {
        /* Parse next numeric segment */
        long sa = 0, sb = 0;
        if (*a) {
            sa = strtol(a, (char **)&a, 10);
            if (*a == '.') a++;
        }
        if (*b) {
            sb = strtol(b, (char **)&b, 10);
            if (*b == '.') b++;
        }
        if (sa != sb) return (sa < sb) ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Expression evaluator (recursive descent)                            */
/* ------------------------------------------------------------------ */

/*
 * Expression grammar (Ruby subset):
 *
 *   expr       → or_expr
 *   or_expr    → and_expr ('||' and_expr)*
 *   and_expr   → not_expr ('&&' not_expr)*
 *   not_expr   → '!' not_expr | cmp_expr
 *   cmp_expr   → primary (cmp_op primary)?
 *   primary    → atom ('.' method)*
 *   atom       → STRING | SYMBOL | INTEGER | FLOAT_LIT
 *              | LIT_TRUE | LIT_FALSE | LIT_NIL
 *              | IDENT
 *              | IDENT '[' STRING ']'
 *              | IDENT '.' IDENT '(' args ')'
 *              | IDENT '::' IDENT '.' IDENT '(' expr ')'
 *              | '(' expr ')'
 */

/* Peek at token at position pos (returns 0 for out of bounds) */
static int peek(struct wow_eval_ctx *ctx, int pos, int end)
{
    if (pos >= end) return 0;
    return ctx->line[pos].id;
}

/* Truthiness: only nil and false are falsy (Ruby semantics) */
static bool is_truthy(struct wow_eval_val v)
{
    if (v.type == VAL_NIL) return false;
    if (v.type == VAL_BOOL) return v.b;
    return true;  /* strings, ints, floats are all truthy */
}

/* Make value helpers */
static struct wow_eval_val val_nil(void)
{
    return (struct wow_eval_val){ .type = VAL_NIL };
}

static struct wow_eval_val val_bool(bool b)
{
    return (struct wow_eval_val){ .type = VAL_BOOL, .b = b };
}

static struct wow_eval_val val_string(struct wow_eval_ctx *ctx,
                                      const char *s, bool is_version)
{
    struct wow_eval_val v = { .type = VAL_STRING, .is_version = is_version };
    v.s = eval_alloc(ctx, s ? strdup(s) : strdup(""));
    return v;
}

static struct wow_eval_val val_int(int64_t i)
{
    return (struct wow_eval_val){ .type = VAL_INT, .i = i };
}

static struct wow_eval_val val_float(double f)
{
    return (struct wow_eval_val){ .type = VAL_FLOAT, .f = f };
}

/* Convert value to string for comparison */
static const char *val_to_str(struct wow_eval_ctx *ctx, struct wow_eval_val v)
{
    char buf[64];
    switch (v.type) {
    case VAL_NIL:    return "";
    case VAL_BOOL:   return v.b ? "true" : "false";
    case VAL_STRING: return v.s ? v.s : "";
    case VAL_INT:
        snprintf(buf, sizeof(buf), "%lld", (long long)v.i);
        return eval_alloc(ctx, strdup(buf));
    case VAL_FLOAT:
        snprintf(buf, sizeof(buf), "%g", v.f);
        return eval_alloc(ctx, strdup(buf));
    }
    return "";
}

/* Convert value to double for numeric comparison */
static double val_to_double(struct wow_eval_val v)
{
    switch (v.type) {
    case VAL_INT:    return (double)v.i;
    case VAL_FLOAT:  return v.f;
    case VAL_STRING: return v.s ? strtod(v.s, NULL) : 0.0;
    default:         return 0.0;
    }
}

/* Check if value is numeric */
static bool val_is_numeric(struct wow_eval_val v)
{
    return v.type == VAL_INT || v.type == VAL_FLOAT;
}

/* Forward declaration for recursive call */
static struct wow_eval_val eval_or_expr(struct wow_eval_ctx *ctx,
                                        int *pos, int end);

/* Parse a primary expression: atom with optional method calls */
static struct wow_eval_val eval_atom(struct wow_eval_ctx *ctx,
                                     int *pos, int end)
{
    int id = peek(ctx, *pos, end);
    struct wow_token tok = ctx->line[*pos].tok;

    switch (id) {
    case STRING: {
        (*pos)++;
        return val_string(ctx, tok_strip(tok, 1, 1), false);
    }

    case SYMBOL: {
        (*pos)++;
        return val_string(ctx, tok_strip(tok, 1, 0), false);
    }

    case INTEGER: {
        (*pos)++;
        char *s = tok_text(tok);
        int64_t v = strtoll(s, NULL, 10);
        free(s);
        return val_int(v);
    }

    case FLOAT_LIT: {
        (*pos)++;
        char *s = tok_text(tok);
        double v = strtod(s, NULL);
        free(s);
        return val_float(v);
    }

    case LIT_TRUE:  (*pos)++; return val_bool(true);
    case LIT_FALSE: (*pos)++; return val_bool(false);
    case LIT_NIL:   (*pos)++; return val_nil();

    case LPAREN: {
        (*pos)++;  /* skip ( */
        struct wow_eval_val v = eval_or_expr(ctx, pos, end);
        if (peek(ctx, *pos, end) == RPAREN) (*pos)++;
        return v;
    }

    case IDENT: {
        char *name = tok_text(tok);
        (*pos)++;

        /* ENV["KEY"] or ENV.fetch("KEY", default) or ENV.key?("KEY") */
        if (strcmp(name, "ENV") == 0) {
            if (peek(ctx, *pos, end) == LBRACKET) {
                /* ENV["KEY"] */
                (*pos)++;  /* skip [ */
                if (peek(ctx, *pos, end) == STRING) {
                    char *key = tok_strip(ctx->line[*pos].tok, 1, 1);
                    (*pos)++;  /* skip key string */
                    if (peek(ctx, *pos, end) == RBRACKET) (*pos)++;
                    const char *val = getenv(key);
                    free(key);
                    free(name);
                    if (val) return val_string(ctx, val, false);
                    return val_nil();
                }
            } else if (peek(ctx, *pos, end) == DOT) {
                (*pos)++;  /* skip . */
                if (peek(ctx, *pos, end) == IDENT) {
                    char *method = tok_text(ctx->line[*pos].tok);
                    (*pos)++;
                    if (strcmp(method, "fetch") == 0 &&
                        peek(ctx, *pos, end) == LPAREN) {
                        /* ENV.fetch("KEY", "default") */
                        (*pos)++;  /* skip ( */
                        char *key = NULL;
                        if (peek(ctx, *pos, end) == STRING) {
                            key = tok_strip(ctx->line[*pos].tok, 1, 1);
                            (*pos)++;
                        }
                        char *def = NULL;
                        if (peek(ctx, *pos, end) == COMMA) {
                            (*pos)++;  /* skip , */
                            if (peek(ctx, *pos, end) == STRING) {
                                def = tok_strip(ctx->line[*pos].tok, 1, 1);
                                (*pos)++;
                            }
                        }
                        if (peek(ctx, *pos, end) == RPAREN) (*pos)++;
                        const char *val = key ? getenv(key) : NULL;
                        free(key);
                        free(method);
                        free(name);
                        if (val) {
                            free(def);
                            return val_string(ctx, val, false);
                        }
                        if (def) {
                            struct wow_eval_val r = val_string(ctx, def, false);
                            free(def);
                            return r;
                        }
                        return val_nil();
                    } else if (strcmp(method, "key") == 0 &&
                               peek(ctx, *pos, end) == QUESTION) {
                        /* ENV.key?("KEY") */
                        (*pos)++;  /* skip ? */
                        if (peek(ctx, *pos, end) == LPAREN) (*pos)++;
                        bool found = false;
                        if (peek(ctx, *pos, end) == STRING) {
                            char *key = tok_strip(ctx->line[*pos].tok, 1, 1);
                            (*pos)++;
                            found = getenv(key) != NULL;
                            free(key);
                        }
                        if (peek(ctx, *pos, end) == RPAREN) (*pos)++;
                        free(method);
                        free(name);
                        return val_bool(found);
                    }
                    free(method);
                }
            }
            free(name);
            return val_nil();
        }

        /* RUBY_VERSION */
        if (strcmp(name, "RUBY_VERSION") == 0) {
            free(name);
            return val_string(ctx, ctx->ruby_version, true);
        }

        /* RUBY_ENGINE */
        if (strcmp(name, "RUBY_ENGINE") == 0) {
            free(name);
            return val_string(ctx, ctx->ruby_engine, false);
        }

        /* RUBY_PLATFORM */
        if (strcmp(name, "RUBY_PLATFORM") == 0) {
            free(name);
            return val_string(ctx, ctx->ruby_platform, false);
        }

        /* __FILE__ */
        if (strcmp(name, "__FILE__") == 0) {
            free(name);
            return val_string(ctx, ctx->file_path ? ctx->file_path : "", false);
        }

        /* __dir__ */
        if (strcmp(name, "__dir__") == 0) {
            free(name);
            if (ctx->file_path) {
                char *dir = eval_alloc(ctx, strdup(ctx->file_path));
                char *slash = strrchr(dir, '/');
                if (slash) *slash = '\0';
                return (struct wow_eval_val){
                    .type = VAL_STRING, .s = dir
                };
            }
            return val_string(ctx, ".", false);
        }

        /* Gem::Version.new(expr) */
        if (strcmp(name, "Gem") == 0 &&
            peek(ctx, *pos, end) == COLON_COLON) {
            (*pos)++;  /* skip :: */
            if (peek(ctx, *pos, end) == IDENT) {
                char *cls = tok_text(ctx->line[*pos].tok);
                (*pos)++;
                if (strcmp(cls, "Version") == 0 &&
                    peek(ctx, *pos, end) == DOT) {
                    (*pos)++;  /* skip . */
                    if (peek(ctx, *pos, end) == IDENT) {
                        char *m = tok_text(ctx->line[*pos].tok);
                        (*pos)++;
                        if (strcmp(m, "new") == 0 &&
                            peek(ctx, *pos, end) == LPAREN) {
                            (*pos)++;  /* skip ( */
                            struct wow_eval_val inner =
                                eval_or_expr(ctx, pos, end);
                            if (peek(ctx, *pos, end) == RPAREN) (*pos)++;
                            free(m);
                            free(cls);
                            free(name);
                            /* Wrap as version-tagged string */
                            const char *s = val_to_str(ctx, inner);
                            return val_string(ctx, s, true);
                        }
                        free(m);
                    }
                }
                free(cls);
            }
            free(name);
            return val_nil();
        }

        /* Variable lookup */
        bool var_is_ver = false;
        const char *val = lookup_var(ctx, name, &var_is_ver);
        free(name);
        if (val) return val_string(ctx, val, var_is_ver);
        return val_nil();
    }

    default:
        /* Unexpected token — return nil rather than crash */
        if (*pos < end) (*pos)++;
        return val_nil();
    }
}

/* Parse method calls: primary ('.' IDENT)* */
static struct wow_eval_val eval_primary(struct wow_eval_ctx *ctx,
                                        int *pos, int end)
{
    struct wow_eval_val v = eval_atom(ctx, pos, end);

    while (peek(ctx, *pos, end) == DOT) {
        (*pos)++;  /* skip . */
        if (peek(ctx, *pos, end) != IDENT) break;
        char *method = tok_text(ctx->line[*pos].tok);
        (*pos)++;

        /* Check for ? suffix: empty? key? include? */
        bool has_q = (peek(ctx, *pos, end) == QUESTION);
        if (has_q) (*pos)++;

        if (strcmp(method, "to_f") == 0) {
            v = val_float(val_to_double(v));
        } else if (strcmp(method, "to_i") == 0) {
            const char *s = val_to_str(ctx, v);
            v = val_int(strtoll(s, NULL, 10));
        } else if (strcmp(method, "to_s") == 0) {
            const char *s = val_to_str(ctx, v);
            v = val_string(ctx, s, v.is_version);
        } else if (strcmp(method, "empty") == 0 && has_q) {
            const char *s = val_to_str(ctx, v);
            v = val_bool(s[0] == '\0');
        } else if (strcmp(method, "include") == 0 && has_q) {
            /* .include?("str") */
            if (peek(ctx, *pos, end) == LPAREN) (*pos)++;
            const char *needle = "";
            if (peek(ctx, *pos, end) == STRING) {
                char *tmp = tok_strip(ctx->line[*pos].tok, 1, 1);
                needle = eval_alloc(ctx, tmp);
                (*pos)++;
            }
            if (peek(ctx, *pos, end) == RPAREN) (*pos)++;
            const char *s = val_to_str(ctx, v);
            v = val_bool(strstr(s, needle) != NULL);
        } else if (strcmp(method, "freeze") == 0) {
            /* .freeze is a no-op for our purposes */
        } else {
            /* Unknown method — return nil */
            if (peek(ctx, *pos, end) == LPAREN) {
                /* Skip args: consume until matching RPAREN */
                (*pos)++;
                int depth = 1;
                while (*pos < end && depth > 0) {
                    int t = ctx->line[*pos].id;
                    if (t == LPAREN) depth++;
                    else if (t == RPAREN) depth--;
                    (*pos)++;
                }
            }
            v = val_nil();
        }
        free(method);
    }

    return v;
}

/* Compare two values */
static struct wow_eval_val eval_compare(struct wow_eval_ctx *ctx,
                                        struct wow_eval_val l, int op,
                                        struct wow_eval_val r)
{
    int cmp;

    /* Version comparison if either side is tagged */
    if (l.is_version || r.is_version) {
        const char *ls = val_to_str(ctx, l);
        const char *rs = val_to_str(ctx, r);
        cmp = wow_version_compare(ls, rs);
    } else if (val_is_numeric(l) && val_is_numeric(r)) {
        /* Numeric comparison */
        double ld = val_to_double(l);
        double rd = val_to_double(r);
        cmp = (ld < rd) ? -1 : (ld > rd) ? 1 : 0;
    } else if (val_is_numeric(l) || val_is_numeric(r)) {
        /* Mixed: try numeric */
        double ld = val_to_double(l);
        double rd = val_to_double(r);
        cmp = (ld < rd) ? -1 : (ld > rd) ? 1 : 0;
    } else {
        /* String comparison */
        const char *ls = val_to_str(ctx, l);
        const char *rs = val_to_str(ctx, r);
        cmp = strcmp(ls, rs);
    }

    switch (op) {
    case EQ:  return val_bool(cmp == 0);
    case NEQ: return val_bool(cmp != 0);
    case GTE: return val_bool(cmp >= 0);
    case GT:  return val_bool(cmp > 0);
    case LTE: return val_bool(cmp <= 0);
    case LT:  return val_bool(cmp < 0);
    default:  return val_bool(false);
    }
}

/* cmp_expr → primary (cmp_op primary)? */
static struct wow_eval_val eval_cmp_expr(struct wow_eval_ctx *ctx,
                                         int *pos, int end)
{
    struct wow_eval_val l = eval_primary(ctx, pos, end);
    int op = peek(ctx, *pos, end);
    if (op == EQ || op == NEQ || op == GTE || op == GT ||
        op == LTE || op == LT) {
        (*pos)++;
        struct wow_eval_val r = eval_primary(ctx, pos, end);
        return eval_compare(ctx, l, op, r);
    }
    if (op == MATCH) {
        /* =~ regex — unsupported, treat as truthy */
        (*pos)++;
        /* Skip the regex (usually a string-like /.../) — consume rest */
        while (*pos < end) {
            int t = peek(ctx, *pos, end);
            if (t == AND || t == OR || t == RPAREN || t == NEWLINE || t == 0)
                break;
            (*pos)++;
        }
        return val_bool(true);
    }
    return l;
}

/* not_expr → '!' not_expr | cmp_expr */
static struct wow_eval_val eval_not_expr(struct wow_eval_ctx *ctx,
                                         int *pos, int end)
{
    if (peek(ctx, *pos, end) == BANG) {
        (*pos)++;
        struct wow_eval_val v = eval_not_expr(ctx, pos, end);
        return val_bool(!is_truthy(v));
    }
    return eval_cmp_expr(ctx, pos, end);
}

/* and_expr → not_expr ('&&' not_expr)* */
static struct wow_eval_val eval_and_expr(struct wow_eval_ctx *ctx,
                                         int *pos, int end)
{
    struct wow_eval_val v = eval_not_expr(ctx, pos, end);
    while (peek(ctx, *pos, end) == AND) {
        (*pos)++;
        if (!is_truthy(v)) {
            /* Short-circuit: skip RHS but still advance pos */
            eval_not_expr(ctx, pos, end);
            /* v stays falsy */
        } else {
            v = eval_not_expr(ctx, pos, end);
        }
    }
    return v;
}

/* or_expr → and_expr ('||' and_expr)* */
static struct wow_eval_val eval_or_expr(struct wow_eval_ctx *ctx,
                                        int *pos, int end)
{
    struct wow_eval_val v = eval_and_expr(ctx, pos, end);
    while (peek(ctx, *pos, end) == OR) {
        (*pos)++;
        if (is_truthy(v)) {
            /* Short-circuit: skip RHS */
            eval_and_expr(ctx, pos, end);
        } else {
            v = eval_and_expr(ctx, pos, end);
        }
    }
    return v;
}

/* Top-level expression evaluation */
static struct wow_eval_val eval_expr(struct wow_eval_ctx *ctx,
                                     int *pos, int end)
{
    return eval_or_expr(ctx, pos, end);
}

/* ------------------------------------------------------------------ */
/* String interpolation                                                */
/* ------------------------------------------------------------------ */

/*
 * Process string interpolation: scan for #{var} and substitute.
 * Returns the original string if no interpolation found,
 * or a new allocated string with substitutions.
 */
static const char *interpolate_string(struct wow_eval_ctx *ctx,
                                       const char *s, int len)
{
    /* Quick check: any #{ present? */
    const char *p = s;
    const char *end = s + len;
    bool has_interp = false;
    while (p + 1 < end) {
        if (p[0] == '#' && p[1] == '{') { has_interp = true; break; }
        p++;
    }
    if (!has_interp) return NULL;

    /* Build interpolated string with growable buffer */
    int cap = len * 2 + 64;
    char *result = malloc((size_t)cap);
    if (!result) return NULL;
    int rpos = 0;

    p = s;
    while (p < end) {
        if (p + 1 < end && p[0] == '#' && p[1] == '{') {
            p += 2;  /* skip #{ */
            /* Extract variable name until } */
            const char *var_start = p;
            while (p < end && *p != '}') p++;
            int var_len = (int)(p - var_start);
            if (p < end) p++;  /* skip } */

            /* Check for simple identifier (no operators/calls) */
            bool simple = true;
            for (int i = 0; i < var_len; i++) {
                char c = var_start[i];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_')) {
                    simple = false;
                    break;
                }
            }

            const char *subst = NULL;
            int subst_len = 0;

            if (simple && var_len > 0) {
                char *vname = strndup(var_start, (size_t)var_len);
                subst = lookup_var(ctx, vname, NULL);
                if (!subst) {
                    /* Try built-in constants */
                    if (strcmp(vname, "RUBY_VERSION") == 0)
                        subst = ctx->ruby_version;
                }
                free(vname);
                if (subst) subst_len = (int)strlen(subst);
            }

            /* Calculate needed space */
            int needed = subst ? subst_len : (var_len + 3);  /* +3 for #{} */
            if (rpos + needed + 1 > cap) {
                cap = (rpos + needed + 1) * 2;
                result = realloc(result, (size_t)cap);
                if (!result) return NULL;
            }

            if (subst) {
                memcpy(result + rpos, subst, (size_t)subst_len);
                rpos += subst_len;
            } else if (simple) {
                /* Variable not found — substitute empty */
            } else {
                /* Complex interpolation — unsupported, leave #{...} as-is */
                result[rpos++] = '#';
                result[rpos++] = '{';
                memcpy(result + rpos, var_start, (size_t)var_len);
                rpos += var_len;
                result[rpos++] = '}';
            }
        } else {
            if (rpos + 2 > cap) {
                cap *= 2;
                result = realloc(result, (size_t)cap);
                if (!result) return NULL;
            }
            result[rpos++] = *p++;
        }
    }
    result[rpos] = '\0';
    return eval_alloc(ctx, result);
}

/* ------------------------------------------------------------------ */
/* Line processing                                                     */
/* ------------------------------------------------------------------ */

/* Find trailing IF/UNLESS in line buffer (returns position or -1) */
static int find_trailing_if(struct wow_eval_ctx *ctx)
{
    /* Walk backwards past NEWLINE */
    int end = ctx->line_len;
    while (end > 0 && ctx->line[end - 1].id == NEWLINE) end--;

    for (int i = 1; i < end; i++) {
        int id = ctx->line[i].id;
        if (id == IF || id == UNLESS) return i;
    }
    return -1;
}

/* Check if line is a variable assignment (IDENT ASSIGN ...) */
static bool is_assignment(struct wow_eval_ctx *ctx)
{
    if (ctx->line_len < 3) return false;
    return ctx->line[0].id == IDENT && ctx->line[1].id == ASSIGN;
}

/* Scan for DO tokens and push BLOCK_DO entries */
static void track_do_tokens(struct wow_eval_ctx *ctx, bool active)
{
    for (int i = 0; i < ctx->line_len; i++) {
        if (ctx->line[i].id == DO) {
            push_block(ctx, BLOCK_DO, active, false);
        }
    }
}

/* Flush line buffer to output, tracking DO tokens, with interpolation */
static void flush_line(struct wow_eval_ctx *ctx, int start, int end)
{
    int last_line = 0;
    for (int i = start; i < end; i++) {
        int id = ctx->line[i].id;
        struct wow_token tok = ctx->line[i].tok;
        last_line = tok.line;

        /* Track DO for block stack */
        if (id == DO) push_block(ctx, BLOCK_DO, true, false);

        /* String interpolation on STRING tokens */
        if (id == STRING && tok.length > 2) {
            /* Only double-quoted strings can have interpolation */
            if (tok.start[0] == '"') {
                const char *interp = interpolate_string(
                    ctx, tok.start + 1, tok.length - 2);
                if (interp) {
                    /* Create new token pointing to interpolated string */
                    int ilen = (int)strlen(interp);
                    char *quoted = eval_alloc(ctx,
                        malloc((size_t)ilen + 3));
                    quoted[0] = '"';
                    memcpy(quoted + 1, interp, (size_t)ilen);
                    quoted[ilen + 1] = '"';
                    quoted[ilen + 2] = '\0';
                    tok.start = quoted;
                    tok.length = ilen + 2;
                }
            }
        }

        /* Variable substitution on IDENT tokens: replace known
         * variables and built-in constants with STRING tokens so
         * the parser can handle e.g. gem "rack", rack_version */
        if (id == IDENT) {
            const char *val = NULL;
            char *vname = strndup(tok.start, (size_t)tok.length);
            if (vname) {
                val = lookup_var(ctx, vname, NULL);
                if (!val) {
                    if (strcmp(vname, "RUBY_VERSION") == 0)
                        val = ctx->ruby_version;
                    else if (strcmp(vname, "RUBY_ENGINE") == 0)
                        val = ctx->ruby_engine;
                    else if (strcmp(vname, "RUBY_PLATFORM") == 0)
                        val = ctx->ruby_platform;
                }
                free(vname);
            }
            if (val) {
                int vlen = (int)strlen(val);
                char *quoted = eval_alloc(ctx,
                    malloc((size_t)vlen + 3));
                quoted[0] = '"';
                memcpy(quoted + 1, val, (size_t)vlen);
                quoted[vlen + 1] = '"';
                quoted[vlen + 2] = '\0';
                tok.start = quoted;
                tok.length = vlen + 2;
                id = STRING;
            }
        }

        emit(ctx, id, tok);
    }

    /* Emit trailing NEWLINE if the line had one */
    if (ctx->line_len > 0 && ctx->line[ctx->line_len - 1].id == NEWLINE) {
        /* Already included in the flush range — no need */
    } else if (last_line > 0) {
        emit_newline(ctx, last_line);
    }
}

/* Process a buffered line */
static void process_line(struct wow_eval_ctx *ctx)
{
    if (ctx->error) return;
    if (ctx->line_len == 0) return;

    /* Find first non-NEWLINE token */
    int first_idx = 0;
    while (first_idx < ctx->line_len &&
           ctx->line[first_idx].id == NEWLINE) first_idx++;

    if (first_idx >= ctx->line_len) {
        /* Line is all NEWLINEs */
        emit_newline(ctx, ctx->line[0].tok.line);
        ctx->line_len = 0;
        return;
    }

    int first_id = ctx->line[first_idx].id;
    int first_line = ctx->line[first_idx].tok.line;

    /* ---- Block control keywords (handled regardless of suppression) ---- */

    if (first_id == IF || first_id == UNLESS) {
        bool negate = (first_id == UNLESS);
        if (is_suppressed(ctx)) {
            /* Inside false branch — push dummy, don't evaluate */
            push_block(ctx, BLOCK_IF, false, false);
        } else {
            /* Evaluate condition from tokens after IF/UNLESS */
            int pos = first_idx + 1;
            int end = ctx->line_len;
            /* Strip trailing NEWLINE from expression range */
            while (end > pos && ctx->line[end - 1].id == NEWLINE) end--;
            struct wow_eval_val cond = eval_expr(ctx, &pos, end);
            bool truthy = is_truthy(cond) ^ negate;
            push_block(ctx, BLOCK_IF, truthy, truthy);
        }
        ctx->line_len = 0;
        return;
    }

    if (first_id == ELSIF) {
        if (ctx->depth <= 0 || ctx->stack[ctx->depth - 1].type != BLOCK_IF) {
            eval_error(ctx, first_line, "elsif without matching if");
            ctx->line_len = 0;
            return;
        }
        if (!enclosing_active(ctx)) {
            /* Parent suppressed — no change */
        } else if (ctx->stack[ctx->depth - 1].any_taken) {
            ctx->stack[ctx->depth - 1].active = false;
        } else {
            int pos = first_idx + 1;
            int end = ctx->line_len;
            while (end > pos && ctx->line[end - 1].id == NEWLINE) end--;
            struct wow_eval_val cond = eval_expr(ctx, &pos, end);
            bool truthy = is_truthy(cond);
            ctx->stack[ctx->depth - 1].active = truthy;
            ctx->stack[ctx->depth - 1].any_taken |= truthy;
        }
        ctx->line_len = 0;
        return;
    }

    if (first_id == ELSE) {
        if (ctx->depth <= 0 || ctx->stack[ctx->depth - 1].type != BLOCK_IF) {
            eval_error(ctx, first_line, "else without matching if");
            ctx->line_len = 0;
            return;
        }
        if (!enclosing_active(ctx)) {
            /* Parent suppressed — no change */
        } else {
            struct wow_eval_block *top = &ctx->stack[ctx->depth - 1];
            top->active = !top->any_taken;
            top->any_taken = true;
        }
        ctx->line_len = 0;
        return;
    }

    if (first_id == END) {
        if (ctx->depth <= 0) {
            eval_error(ctx, first_line, "end without matching block");
            ctx->line_len = 0;
            return;
        }
        struct wow_eval_block block = ctx->stack[--ctx->depth];
        if (block.type == BLOCK_IF) {
            /* Evaluator consumed it — don't emit END */
        } else if (block.type == BLOCK_DO) {
            if (block.active) {
                /* We emitted DO to parser — emit matching END */
                emit(ctx, END, ctx->line[first_idx].tok);
                emit_newline(ctx, first_line);
            }
            /* If !active, we consumed DO earlier, consume END too */
        }
        ctx->line_len = 0;
        return;
    }

    /* ---- Suppressed scope: discard line, but track DO for depth ---- */

    if (is_suppressed(ctx)) {
        /* Track DO tokens for block depth */
        for (int i = first_idx; i < ctx->line_len; i++) {
            if (ctx->line[i].id == DO)
                push_block(ctx, BLOCK_DO, false, false);
        }
        ctx->line_len = 0;
        return;
    }

    /* ---- Active scope: classify line content ---- */

    /* Check for trailing if/unless */
    int trail_pos = find_trailing_if(ctx);
    if (trail_pos > 0) {
        bool negate = (ctx->line[trail_pos].id == UNLESS);
        int cond_start = trail_pos + 1;
        int cond_end = ctx->line_len;
        while (cond_end > cond_start &&
               ctx->line[cond_end - 1].id == NEWLINE) cond_end--;
        struct wow_eval_val cond = eval_expr(ctx, &cond_start, cond_end);
        bool truthy = is_truthy(cond) ^ negate;
        if (truthy) {
            /* Emit tokens before the if/unless */
            flush_line(ctx, first_idx, trail_pos);
        }
        /* If false: discard entire line */
        ctx->line_len = 0;
        return;
    }

    /* Check for variable assignment */
    if (is_assignment(ctx)) {
        char *name = tok_text(ctx->line[0].tok);
        int pos = 2;  /* skip IDENT ASSIGN */
        int end = ctx->line_len;
        while (end > pos && ctx->line[end - 1].id == NEWLINE) end--;
        struct wow_eval_val val = eval_expr(ctx, &pos, end);
        const char *sval = (val.type == VAL_NIL) ? NULL : val_to_str(ctx, val);
        store_var(ctx, name, sval, val.is_version);
        free(name);
        ctx->line_len = 0;
        return;
    }

    /* Check for eval_gemfile */
    if (first_id == EVAL_GEMFILE) {
        if (ctx->line_len >= 2 && ctx->line[first_idx + 1].id == STRING) {
            char *path = tok_strip(ctx->line[first_idx + 1].tok, 1, 1);
            /* Resolve relative to current file */
            char resolved[1024];
            if (strlen(path) >= sizeof(resolved)) {
                eval_error(ctx, first_line, "eval_gemfile path too long");
                free(path);
                ctx->line_len = 0;
                return;
            }
            if (path[0] != '/' && ctx->file_path &&
                strlen(ctx->file_path) < sizeof(resolved)) {
                char dir[1024];
                strncpy(dir, ctx->file_path, sizeof(dir) - 1);
                dir[sizeof(dir) - 1] = '\0';
                char *slash = strrchr(dir, '/');
                if (slash) {
                    slash[1] = '\0';
                    size_t dlen = strlen(dir);
                    size_t plen = strlen(path);
                    if (dlen + plen >= sizeof(resolved)) {
                        eval_error(ctx, first_line,
                                   "eval_gemfile path too long");
                        free(path);
                        ctx->line_len = 0;
                        return;
                    }
                    memcpy(resolved, dir, dlen);
                    memcpy(resolved + dlen, path, plen + 1);
                } else {
                    strncpy(resolved, path, sizeof(resolved) - 1);
                    resolved[sizeof(resolved) - 1] = '\0';
                }
            } else {
                strncpy(resolved, path, sizeof(resolved) - 1);
                resolved[sizeof(resolved) - 1] = '\0';
            }
            free(path);

            if (ctx->recurse_depth >= WOW_EVAL_MAX_RECURSE) {
                eval_error(ctx, first_line,
                           "eval_gemfile recursion too deep (max %d)",
                           WOW_EVAL_MAX_RECURSE);
            } else {
                /* Read and parse the included file */
                FILE *f = fopen(resolved, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (sz >= 0 && sz < 1024 * 1024) {
                        char *buf = malloc((size_t)sz + 1);
                        if (buf) {
                            fread(buf, 1, (size_t)sz, f);
                            buf[sz] = '\0';

                            /* Sub-lexer + sub-evaluator sharing our state */
                            struct wow_lexer sub_lex;
                            wow_lexer_init(&sub_lex, buf, (int)sz);

                            struct wow_eval_ctx sub;
                            wow_eval_init(&sub, &sub_lex,
                                          ctx->ruby_version, resolved,
                                          buf, (int)sz);
                            sub.recurse_depth = ctx->recurse_depth + 1;

                            /* Copy variables from parent */
                            for (int i = 0; i < ctx->n_vars; i++) {
                                store_var(&sub, ctx->vars[i].name,
                                          ctx->vars[i].value,
                                          ctx->vars[i].is_version);
                            }

                            /* Feed sub-eval tokens to our output */
                            struct wow_token st;
                            int sid;
                            while ((sid = wow_eval_next(&sub, &st)) > 0) {
                                emit(ctx, sid, st);
                            }
                            if (sid < 0) {
                                eval_error(ctx, first_line,
                                    "error in eval_gemfile \"%s\": %s",
                                    resolved, sub.error_msg);
                            }

                            wow_eval_free(&sub);
                            free(buf);
                        }
                    }
                    fclose(f);
                }
                /* If file doesn't exist, silently skip (common pattern:
                 * eval_gemfile "Gemfile.local" when file may not exist) */
            }
        } else {
            /* Unsupported eval_gemfile form (e.g. with File.expand_path) */
            eval_error(ctx, first_line,
                "unsupported eval_gemfile form (only static string paths)");
        }
        ctx->line_len = 0;
        return;
    }

    /* Check for UNSUPPORTED token */
    if (first_id == UNSUPPORTED) {
        eval_error(ctx, first_line,
            "unsupported Ruby construct: %.*s",
            ctx->line[first_idx].tok.length,
            ctx->line[first_idx].tok.start);
        ctx->line_len = 0;
        return;
    }

    /* Regular line — flush all tokens to output */
    flush_line(ctx, first_idx, ctx->line_len);
    ctx->line_len = 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void wow_eval_init(struct wow_eval_ctx *ctx, struct wow_lexer *lex,
                   const char *ruby_version, const char *file_path,
                   const char *buf, int buf_len)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->lex = lex;
    ctx->file_path = file_path;
    ctx->buf = buf;
    ctx->buf_len = buf_len;

    /* Determine Ruby version */
    if (ruby_version) {
        ctx->ruby_version = ruby_version;
    } else {
        const char *env = getenv("RUBY_VERSION");
        ctx->ruby_version = env ? env : "3.3.0";
    }

    ctx->ruby_engine = "ruby";

    /* RUBY_PLATFORM is determined at compile time. For APE binaries this
     * reflects the build host, not necessarily the runtime platform.
     * In practice this is fine — Gemfile platform checks almost always
     * test for "ruby" engine, not specific architecture strings. */
#if defined(__x86_64__) || defined(_M_X64)
# if defined(__linux__) || defined(__COSMOPOLITAN__)
    ctx->ruby_platform = "x86_64-linux";
# elif defined(__APPLE__)
    ctx->ruby_platform = "x86_64-darwin";
# else
    ctx->ruby_platform = "x86_64";
# endif
#elif defined(__aarch64__)
# if defined(__linux__) || defined(__COSMOPOLITAN__)
    ctx->ruby_platform = "aarch64-linux";
# elif defined(__APPLE__)
    ctx->ruby_platform = "arm64-darwin";
# else
    ctx->ruby_platform = "aarch64";
# endif
#else
    ctx->ruby_platform = "unknown";
#endif
}

void wow_eval_free(struct wow_eval_ctx *ctx)
{
    /* Free variable store */
    for (int i = 0; i < ctx->n_vars; i++) {
        free(ctx->vars[i].name);
        free(ctx->vars[i].value);
    }
    /* Free tracked allocations */
    for (int i = 0; i < ctx->n_allocs; i++) {
        free(ctx->allocs[i]);
    }
    free(ctx->allocs);
}

int wow_eval_next(struct wow_eval_ctx *ctx, struct wow_token *out_tok)
{
    /* Return from output queue if available */
    if (ctx->out_pos < ctx->out_len) {
        *out_tok = ctx->out[ctx->out_pos].tok;
        return ctx->out[ctx->out_pos++].id;
    }

    /* Reset output queue */
    ctx->out_len = 0;
    ctx->out_pos = 0;

    /* Process input tokens until we have output or EOF */
    for (;;) {
        if (ctx->error) return -1;

        struct wow_token tok;
        int id = wow_lexer_scan(ctx->lex, &tok);

        if (id == 0) {
            /* EOF — process any remaining buffered line */
            if (ctx->line_len > 0) {
                process_line(ctx);
                ctx->line_len = 0;
                if (ctx->error) return -1;
                if (ctx->out_pos < ctx->out_len) {
                    *out_tok = ctx->out[ctx->out_pos].tok;
                    return ctx->out[ctx->out_pos++].id;
                }
            }
            return 0;
        }

        if (id == ERROR) {
            /* Propagate lexer error */
            *out_tok = tok;
            return ERROR;
        }

        if (id == NEWLINE) {
            /* Add NEWLINE to buffer, then process the whole line */
            if (ctx->line_len < WOW_EVAL_MAX_LINE) {
                ctx->line[ctx->line_len].id = NEWLINE;
                ctx->line[ctx->line_len].tok = tok;
                ctx->line_len++;
            }
            process_line(ctx);
            ctx->line_len = 0;
            if (ctx->error) return -1;
            if (ctx->out_pos < ctx->out_len) {
                *out_tok = ctx->out[ctx->out_pos].tok;
                return ctx->out[ctx->out_pos++].id;
            }
            continue;
        }

        /* Buffer non-NEWLINE tokens */
        if (ctx->line_len >= WOW_EVAL_MAX_LINE) {
            eval_error(ctx, tok.line,
                       "line too long (max %d tokens)", WOW_EVAL_MAX_LINE);
            return -1;
        }
        ctx->line[ctx->line_len].id = id;
        ctx->line[ctx->line_len].tok = tok;
        ctx->line_len++;
    }
}
