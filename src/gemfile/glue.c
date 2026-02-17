/*
 * glue.c -- wire the re2c lexer to the lemon parser
 *
 * Provides:
 *   wow_gemfile_parse_file()  -- parse a Gemfile from a path
 *   wow_gemfile_parse_buf()   -- parse from an in-memory buffer
 *   wow_gemfile_lex_file()    -- debug: print token stream
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile/lexer.h"
#include "wow/gemfile/types.h"
#include "wow/gemfile/parse.h"
#include "parser.h"

/* lemon-generated parser functions */
void *ParseAlloc(void *(*mallocProc)(size_t));
void  ParseFree(void *p, void (*freeProc)(void *));
void  Parse(void *yyp, int yymajor, struct wow_token yyminor,
            struct wow_gemfile *gf);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
 * Read file into a null-terminated malloc'd buffer.
 * Returns NULL on error (diagnostic on stderr).
 */
static char *read_file(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "wow: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 1024 * 1024) {
        fprintf(stderr, "wow: Gemfile too large (%ld bytes)\n", sz);
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';  /* null sentinel for re2c */
    fclose(f);

    *out_len = (int)sz;
    return buf;
}

/*
 * Find the full text of a given line number in buf (for error messages).
 */
static const char *find_line(const char *buf, int line_num, int *line_len)
{
    const char *p = buf;
    int cur = 1;
    while (*p && cur < line_num) {
        if (*p == '\n') cur++;
        p++;
    }
    const char *eol = strchr(p, '\n');
    *line_len = eol ? (int)(eol - p) : (int)strlen(p);
    return p;
}

/* Token name table for lex debug output */
static const char *token_name(int id)
{
    switch (id) {
    case SOURCE:        return "SOURCE";
    case GEM:           return "GEM";
    case GROUP:         return "GROUP";
    case DO:            return "DO";
    case END:           return "END";
    case RUBY:          return "RUBY";
    case GEMSPEC:       return "GEMSPEC";
    case LIT_TRUE:      return "TRUE";
    case LIT_FALSE:     return "FALSE";
    case LIT_NIL:       return "NIL";
    case STRING:        return "STRING";
    case SYMBOL:        return "SYMBOL";
    case KEY:           return "KEY";
    case COMMA:         return "COMMA";
    case HASHROCKET:    return "HASHROCKET";
    case NEWLINE:       return "NEWLINE";
    case IDENT:         return "IDENT";
    case UNSUPPORTED:   return "UNSUPPORTED";
    case ERROR:         return "ERROR";
    case GIT_SOURCE:    return "GIT_SOURCE";
    case PLUGIN:        return "PLUGIN";
    case PLATFORMS:     return "PLATFORMS";
    case LBRACKET:      return "LBRACKET";
    case RBRACKET:      return "RBRACKET";
    case LPAREN:        return "LPAREN";
    case RPAREN:        return "RPAREN";
    case PERCENT_ARRAY: return "PERCENT_ARRAY";
    default:            return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int wow_gemfile_lex_file(const char *path)
{
    int len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return -1;

    struct wow_lexer lex;
    wow_lexer_init(&lex, buf, len);

    struct wow_token tok;
    int id;
    while ((id = wow_lexer_scan(&lex, &tok)) != 0) {
        if (id == NEWLINE) {
            printf("%3d: %-12s\n", tok.line, "NEWLINE");
        } else {
            printf("%3d: %-12s %.*s\n", tok.line, token_name(id),
                   tok.length, tok.start);
        }
    }

    free(buf);
    return 0;
}

int wow_gemfile_parse_buf(const char *buf, int len, struct wow_gemfile *gf)
{
    wow_gemfile_init(gf);

    struct wow_lexer lex;
    wow_lexer_init(&lex, buf, len);

    void *parser = ParseAlloc(malloc);
    if (!parser) return -1;

    int rc = 0;
    struct wow_token tok;
    int id;

    while ((id = wow_lexer_scan(&lex, &tok)) != 0) {
        if (id == UNSUPPORTED) {
            int line_len;
            const char *line_text = find_line(buf, tok.line, &line_len);
            fprintf(stderr,
                "wow: unsupported syntax at line %d:\n"
                "  %d | %.*s\n"
                "    wow does not evaluate Ruby code in Gemfiles.\n"
                "    Supported directives: source, gem, group, ruby, gemspec\n",
                tok.line, tok.line, line_len, line_text);
            rc = -1;
            break;
        }
        if (id == ERROR) {
            int line_len;
            const char *line_text = find_line(buf, tok.line, &line_len);
            fprintf(stderr,
                "wow: unexpected character at line %d:\n"
                "  %d | %.*s\n",
                tok.line, tok.line, line_len, line_text);
            rc = -1;
            break;
        }

        Parse(parser, id, tok, gf);

        /* Check if parser signalled an error via our sentinel */
        if (gf->_deps_cap == (size_t)-1) {
            int line_len;
            const char *line_text = find_line(buf, tok.line, &line_len);
            fprintf(stderr, "  %d | %.*s\n",
                    tok.line, line_len, line_text);
            rc = -1;
            break;
        }
    }

    if (rc == 0) {
        /* Signal end of input */
        struct wow_token eof_tok = { .start = NULL, .length = 0,
                                     .line = lex.line };
        Parse(parser, 0, eof_tok, gf);
    }

    ParseFree(parser, free);

    if (gf->_deps_cap == (size_t)-1) {
        wow_gemfile_free(gf);
        return -1;
    }

    return rc;
}

int wow_gemfile_parse_file(const char *path, struct wow_gemfile *gf)
{
    int len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return -1;

    int rc = wow_gemfile_parse_buf(buf, len, gf);
    free(buf);
    return rc;
}
