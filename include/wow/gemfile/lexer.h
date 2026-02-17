#ifndef WOW_GEMFILE_LEXER_H
#define WOW_GEMFILE_LEXER_H

/*
 * lexer.h -- re2c-generated Gemfile lexer interface
 *
 * The lexer scans an in-memory buffer and returns one token at a time.
 * Token IDs come from the lemon-generated parser.h.
 */

/* Token value passed from lexer to parser */
struct wow_token {
    const char *start;    /* pointer into the source buffer   */
    int         length;   /* length of the token text         */
    int         line;     /* 1-based line number              */
};

/* Lexer state */
struct wow_lexer {
    const char *cursor;   /* YYCURSOR -- current scan position */
    const char *limit;    /* YYLIMIT  -- end of input          */
    const char *marker;   /* YYMARKER -- backtrack position    */
    const char *tok;      /* start of current token            */
    const char *start;    /* start of buffer (for column-0)    */
    int         line;     /* current line number (1-based)     */
};

/* Initialise lexer state for a null-terminated buffer */
void wow_lexer_init(struct wow_lexer *lex, const char *buf, int len);

/*
 * Scan the next token.
 * Returns token ID (from parser.h), or 0 for EOF.
 * Fills in *token with the token text and line number.
 */
int wow_lexer_scan(struct wow_lexer *lex, struct wow_token *token);

#endif
