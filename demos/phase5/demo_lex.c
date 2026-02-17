/*
 * demo_lex.c — Phase 5a demo: Gemfile lexer with colourised token stream
 *
 * Tokenises a Gemfile using wow's re2c-generated lexer and prints
 * each token with ANSI colours: keywords in bold cyan, strings in
 * green, symbols in magenta, punctuation dim.
 *
 * Build:  make -C demos/phase5              (after main 'make')
 * Usage:  ./demos/phase5/demo_lex.com [Gemfile]
 *         echo 'gem "sinatra", "~> 4.0"' | ./demos/phase5/demo_lex.com -
 *
 * If no argument is given, uses a built-in sample Gemfile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile/lexer.h"
#include "parser.h"

/* ANSI escapes */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_CYAN    "\033[36m"
#define C_GREEN   "\033[32m"
#define C_MAGENTA "\033[35m"
#define C_YELLOW  "\033[33m"
#define C_RED     "\033[31m"

static const char SAMPLE[] =
    "# frozen_string_literal: true\n"
    "\n"
    "source \"https://rubygems.org\"\n"
    "\n"
    "ruby \"3.3.0\"\n"
    "\n"
    "gem \"sinatra\", \"~> 4.0\"\n"
    "gem \"rack\", \">= 3.0.0\", \"< 4\"\n"
    "gem \"pry\", require: false\n"
    "\n"
    "group :development do\n"
    "  gem \"rspec\", \"~> 3.0\"\n"
    "  gem \"rubocop\"\n"
    "end\n"
    "\n"
    "gem \"debug\", require: false, group: :test\n"
    "gemspec\n";

static const char *token_colour(int id)
{
    switch (id) {
    case SOURCE: case GEM: case GROUP: case DO:
    case END: case RUBY: case GEMSPEC:
        return C_BOLD C_CYAN;
    case LIT_TRUE: case LIT_FALSE:
        return C_YELLOW;
    case STRING:
        return C_GREEN;
    case SYMBOL:
        return C_MAGENTA;
    case KEY:
        return C_BOLD C_YELLOW;
    case COMMA: case HASHROCKET:
        return C_DIM;
    case UNSUPPORTED: case ERROR:
        return C_RED;
    default:
        return C_RESET;
    }
}

static const char *token_label(int id)
{
    switch (id) {
    case SOURCE:      return "SOURCE";
    case GEM:         return "GEM";
    case GROUP:       return "GROUP";
    case DO:          return "DO";
    case END:         return "END";
    case RUBY:        return "RUBY";
    case GEMSPEC:     return "GEMSPEC";
    case LIT_TRUE:    return "TRUE";
    case LIT_FALSE:   return "FALSE";
    case STRING:      return "STRING";
    case SYMBOL:      return "SYMBOL";
    case KEY:         return "KEY";
    case COMMA:       return "COMMA";
    case HASHROCKET:  return "HASHROCKET";
    case NEWLINE:     return "NEWLINE";
    case IDENT:       return "IDENT";
    case UNSUPPORTED: return "UNSUPPORTED";
    case ERROR:       return "ERROR";
    default:          return "?";
    }
}

static char *read_stdin(int *out_len)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *p = realloc(buf, cap);
            if (!p) { free(buf); return NULL; }
            buf = p;
        }
    }
    buf[len] = '\0';
    *out_len = (int)len;
    return buf;
}

static char *read_file(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    *out_len = (int)sz;
    return buf;
}

int main(int argc, char **argv)
{
    printf(C_BOLD "=== wow Gemfile Lexer Demo ===" C_RESET "\n\n");

    char *buf;
    int len;
    int heap = 0;

    if (argc >= 2 && strcmp(argv[1], "-") == 0) {
        printf(C_DIM "Reading from stdin..." C_RESET "\n\n");
        buf = read_stdin(&len);
        heap = 1;
    } else if (argc >= 2) {
        printf("Lexing: %s\n\n", argv[1]);
        buf = read_file(argv[1], &len);
        heap = 1;
    } else {
        printf(C_DIM "Using built-in sample Gemfile" C_RESET "\n\n");
        buf = (char *)SAMPLE;
        len = (int)strlen(SAMPLE);
    }

    if (!buf) return 1;

    /* Print the source */
    printf(C_DIM "─── Source ───────────────────────────────────────" C_RESET "\n");
    printf("%s", buf);
    if (len > 0 && buf[len - 1] != '\n') printf("\n");
    printf(C_DIM "─── Tokens ──────────────────────────────────────" C_RESET "\n");

    struct wow_lexer lex;
    wow_lexer_init(&lex, buf, len);

    struct wow_token tok;
    int id;
    int count = 0;

    while ((id = wow_lexer_scan(&lex, &tok)) != 0) {
        count++;
        const char *colour = token_colour(id);
        const char *label = token_label(id);

        if (id == NEWLINE) {
            printf(C_DIM "%3d: %-12s" C_RESET "\n", tok.line, "NEWLINE");
        } else {
            printf(C_DIM "%3d:" C_RESET " %s%-12s" C_RESET " %s%.*s" C_RESET "\n",
                   tok.line, colour, label, colour, tok.length, tok.start);
        }
    }

    printf(C_DIM "─────────────────────────────────────────────────" C_RESET "\n");
    printf("\n" C_BOLD "%d" C_RESET " tokens scanned\n", count);

    if (heap) free(buf);
    return 0;
}
